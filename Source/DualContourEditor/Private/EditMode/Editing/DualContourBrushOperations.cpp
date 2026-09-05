#include "EditMode/Editing/DualContourBrushOperations.h"
#include "DualContour.h"
#include "DualContourMeshActor.h"
#include "DualContourMaterialBrushVolume.h"
#include "DualContourUtils.h"
#include "VolumeSampledDualContour.h"
#include "Async/ParallelFor.h"

namespace
{
void GetSampleBounds(const UDualContour& DualContour, const FVector& BoundsMin, const FVector& BoundsMax,
	FIntVector& SampleMin, FIntVector& SampleMax)
{
	const FIntVector Dims = DualContour.GetSampleDimensions();
	SampleMin = FIntVector(
		FMath::Clamp(FMath::FloorToInt(BoundsMin.X / DualContour.CellSize), 0, Dims.X - 1),
		FMath::Clamp(FMath::FloorToInt(BoundsMin.Y / DualContour.CellSize), 0, Dims.Y - 1),
		FMath::Clamp(FMath::FloorToInt(BoundsMin.Z / DualContour.CellSize), 0, Dims.Z - 1));
	SampleMax = FIntVector(
		FMath::Clamp(FMath::CeilToInt(BoundsMax.X / DualContour.CellSize), 0, Dims.X - 1),
		FMath::Clamp(FMath::CeilToInt(BoundsMax.Y / DualContour.CellSize), 0, Dims.Y - 1),
		FMath::Clamp(FMath::CeilToInt(BoundsMax.Z / DualContour.CellSize), 0, Dims.Z - 1));
}
}

float DualContourBrushOperations::EvaluateFalloff(float NormalizedDistance, float Falloff, EDualContourBrushFalloff FalloffType)
{
	if (NormalizedDistance >= 1.0f)
		return 0.0f;
	const float Inner = 1.0f - FMath::Clamp(Falloff, 0.0f, 1.0f);
	if (NormalizedDistance <= Inner || Falloff <= UE_SMALL_NUMBER)
		return 1.0f;
	const float T = FMath::Clamp((NormalizedDistance - Inner) / FMath::Max(Falloff, UE_SMALL_NUMBER), 0.0f, 1.0f);
	switch (FalloffType)
	{
		case EDualContourBrushFalloff::Linear:
			return 1.0f - T;
		case EDualContourBrushFalloff::Spherical:
			return FMath::Sqrt(FMath::Max(0.0f, 1.0f - T * T));
		case EDualContourBrushFalloff::Tip:
			return FMath::Square(1.0f - T);
		default:
			return 1.0f - FMath::SmoothStep(0.0f, 1.0f, T);
	}
}

bool DualContourBrushOperations::SetMaterialSample(const UDualContour& DualContour, FDualContourPendingMaterialBatch& Batch,
	int32 X, int32 Y, int32 Z, uint8 PaintId)
{
	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(X, Y, Z);
	TMap<uint16, FDualContourPendingMaterialSample>& Chunk = Batch.ChunkSamples.FindOrAdd(ChunkCoord);
	const uint16 LocalIndex = DualContourUtils::ChunkLocalIndex(X, Y, Z);
	FDualContourPendingMaterialSample* Pending = Chunk.Find(LocalIndex);
	if (!Pending)
	{
		FDualContourPendingMaterialSample Initial;
		Initial.Before = DualContour.GetMaterialId(X, Y, Z);
		Initial.WorkingId = Initial.Before;
		Pending = &Chunk.Add(LocalIndex, Initial);
}
Pending->WorkingId = PaintId;
return Pending->Before != Pending->WorkingId;
}

bool DualContourBrushOperations::ApplyMaterialStamp(UDualContour* DualContour, FDualContourPendingMaterialBatch& Batch,
	const FDualContourBrushStamp& Stamp, uint8 PaintId, float Threshold, bool bSolidSamplesOnly)
{
	if (!IsValid(DualContour) || !Batch.bOpen || Batch.Owner != DualContour
		|| !DualContour->HasCurrentGeneratedData() || Stamp.Radius <= UE_SMALL_NUMBER)
		return false;

	const FVector BoundsMin = Stamp.LocalCenter - FVector(Stamp.Radius);
	const FVector BoundsMax = Stamp.LocalCenter + FVector(Stamp.Radius);
	FIntVector SampleMin;
	FIntVector SampleMax;
	GetSampleBounds(*DualContour, BoundsMin, BoundsMax, SampleMin, SampleMax);
	Threshold = FMath::Clamp(Threshold, 0.0f, 1.0f);
	bool bChanged = false;
	for (int32 Z = SampleMin.Z; Z <= SampleMax.Z; ++Z)
		for (int32 Y = SampleMin.Y; Y <= SampleMax.Y; ++Y)
			for (int32 X = SampleMin.X; X <= SampleMax.X; ++X)
			{
				const FVector Offset = DualContour->GetSampleLocalPosition(X, Y, Z) - Stamp.LocalCenter;
				const float Distance = Stamp.Shape == EDualContourBrushShape::Box
					? FMath::Max3(FMath::Abs(Offset.X), FMath::Abs(Offset.Y), FMath::Abs(Offset.Z)) : Offset.Length();
				if (EvaluateFalloff(Distance / Stamp.Radius, Stamp.Falloff, Stamp.FalloffType) < Threshold)
					continue;
				if (bSolidSamplesOnly && DualContour->GetDensity(X, Y, Z) < GDualContourIsoValue)
					continue;
				bChanged |= SetMaterialSample(*DualContour, Batch, X, Y, Z, PaintId);
			}
	return bChanged;
}

bool DualContourBrushOperations::ApplyDensityStamp(UDualContour* DualContour, const UDualContour* RestoreSource,
	FDualContourPendingBatch& Batch, const FDualContourBrushStamp& Stamp)
{
	if (!IsValid(DualContour) || !Batch.bOpen || Batch.Owner != DualContour || !DualContour->HasCurrentGeneratedData()
	    || !FMath::IsFinite(Stamp.Radius) || Stamp.Radius <= UE_SMALL_NUMBER || !FMath::IsFinite(Stamp.Strength))
	{
		return false;
	}

	const FIntVector SampleDims = DualContour->GetSampleDimensions();
	if (Stamp.Operation == EDualContourDensityEditOperation::Erase
	    && (!IsValid(RestoreSource) || !RestoreSource->HasCurrentGeneratedData()
	        || RestoreSource->GetSampleDimensions() != SampleDims
	        || !FMath::IsNearlyEqual(RestoreSource->CellSize, DualContour->CellSize)))
	{
		return false;
	}

	auto GetWorkingDensity = [DualContour, &Batch](int32 X, int32 Y, int32 Z) -> float
	{
		const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(X, Y, Z);
		if (const TMap<uint16, FDualContourPendingSample>* Chunk = Batch.ChunkSamples.Find(ChunkCoord))
			if (const FDualContourPendingSample* Pending = Chunk->Find(DualContourUtils::ChunkLocalIndex(X, Y, Z)))
				return Pending->WorkingValue;
		return DualContour->GetLinearDensity(X, Y, Z);
	};
	auto SetWorkingDensity = [DualContour, &Batch, &GetWorkingDensity](int32 X, int32 Y, int32 Z, float Value)
	{
		const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(X, Y, Z);
		TMap<uint16, FDualContourPendingSample>& Chunk = Batch.ChunkSamples.FindOrAdd(ChunkCoord);
		const uint16 LocalIndex = DualContourUtils::ChunkLocalIndex(X, Y, Z);
		FDualContourPendingSample* Existing = Chunk.Find(LocalIndex);
		if (!Existing)
		{
			FDualContourPendingSample Pending;
			Pending.Before = DualContour->GetDensity(X, Y, Z);
			Pending.WorkingValue = GetWorkingDensity(X, Y, Z);
			Existing = &Chunk.Add(LocalIndex, Pending);
		}
		Existing->WorkingValue = FMath::Clamp(Value, GDualContourMinLinearDensity, GDualContourMaxLinearDensity);
	};

	FVector BoundsMin = Stamp.LocalCenter - FVector(Stamp.Radius);
	FVector BoundsMax = Stamp.LocalCenter + FVector(Stamp.Radius);
	const bool bVolumeStamp = Stamp.Operation == EDualContourDensityEditOperation::StampUnion
	                          || Stamp.Operation == EDualContourDensityEditOperation::StampDifference;
	if (bVolumeStamp)
	{
		if (!IsValid(Stamp.VolumeBrush) || !Stamp.VolumeBrush->HasCurrentGeneratedData())
			return false;
		const FVector SourceMax(
			Stamp.VolumeBrush->CellCount.X * Stamp.VolumeBrush->CellSize,
			Stamp.VolumeBrush->CellCount.Y * Stamp.VolumeBrush->CellSize,
			Stamp.VolumeBrush->CellCount.Z * Stamp.VolumeBrush->CellSize);
		FBox TargetBounds(ForceInit);
		for (int32 Z = 0; Z <= 1; ++Z)
			for (int32 Y = 0; Y <= 1; ++Y)
				for (int32 X = 0; X <= 1; ++X)
					TargetBounds += Stamp.VolumeToTarget.TransformPosition(FVector(X * SourceMax.X, Y * SourceMax.Y, Z * SourceMax.Z));
		BoundsMin = TargetBounds.Min;
		BoundsMax = TargetBounds.Max;
	}

	FIntVector SampleMin;
	FIntVector SampleMax;
	GetSampleBounds(*DualContour, BoundsMin, BoundsMax, SampleMin, SampleMax);
	const float OperationWeight = bVolumeStamp
		                              ? 1.0f
		                              : FMath::Clamp(Stamp.Strength * Stamp.TimeScale, 0.0f, 1.0f);
	if (OperationWeight <= 0.0f)
		return false;

	TArray<float> SmoothedValues;
	FIntVector SmoothDims = FIntVector::ZeroValue;
	if (Stamp.Operation == EDualContourDensityEditOperation::Smooth)
	{
		const FIntVector HaloMin(FMath::Max(0, SampleMin.X - 1), FMath::Max(0, SampleMin.Y - 1), FMath::Max(0, SampleMin.Z - 1));
		const FIntVector HaloMax(FMath::Min(SampleDims.X - 1, SampleMax.X + 1), FMath::Min(SampleDims.Y - 1, SampleMax.Y + 1),
			FMath::Min(SampleDims.Z - 1, SampleMax.Z + 1));
		SmoothDims = FIntVector(HaloMax.X - HaloMin.X + 1, HaloMax.Y - HaloMin.Y + 1, HaloMax.Z - HaloMin.Z + 1);
		TArray<float> Source;
		Source.SetNumUninitialized(DualContourUtils::Volume(SmoothDims));
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					Source[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] = GetWorkingDensity(HaloMin.X + X, HaloMin.Y + Y, HaloMin.Z + Z);

		TArray<float> PassX;
		TArray<float> PassY;
		PassX.SetNumUninitialized(Source.Num());
		PassY.SetNumUninitialized(Source.Num());
		SmoothedValues.SetNumUninitialized(Source.Num());
		auto AtClamped = [&SmoothDims](const TArray<float>& Values, int32 X, int32 Y, int32 Z)
		{
			return Values[DualContourUtils::LinearIndex(SmoothDims, FMath::Clamp(X, 0, SmoothDims.X - 1), FMath::Clamp(Y, 0, SmoothDims.Y - 1),
				FMath::Clamp(Z, 0, SmoothDims.Z - 1))];
		};
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					PassX[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] =
						(AtClamped(Source, X - 1, Y, Z) + 2.0f * AtClamped(Source, X, Y, Z) + AtClamped(Source, X + 1, Y, Z)) * 0.25f;
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					PassY[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] =
						(AtClamped(PassX, X, Y - 1, Z) + 2.0f * AtClamped(PassX, X, Y, Z) + AtClamped(PassX, X, Y + 1, Z)) * 0.25f;
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					SmoothedValues[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] =
						(AtClamped(PassY, X, Y, Z - 1) + 2.0f * AtClamped(PassY, X, Y, Z) + AtClamped(PassY, X, Y, Z + 1)) * 0.25f;

		// Store the snapshot origin in BoundsMin for indexing below; bounds are no longer needed.
		BoundsMin = FVector(HaloMin.X, HaloMin.Y, HaloMin.Z);
	}

	bool bChanged = false;
	if (bVolumeStamp)
	{
		check(IsInGameThread());
		const FIntVector VolumeSampleDims(
			SampleMax.X - SampleMin.X + 1,
			SampleMax.Y - SampleMin.Y + 1,
			SampleMax.Z - SampleMin.Z + 1);
		const int64 PlaneSampleCount64 = static_cast<int64>(VolumeSampleDims.X) * VolumeSampleDims.Y;
		const int64 VolumeSampleCount64 = PlaneSampleCount64 * VolumeSampleDims.Z;
		if (PlaneSampleCount64 <= 0 || PlaneSampleCount64 > MAX_int32
		    || VolumeSampleCount64 <= 0 || VolumeSampleCount64 > MAX_int32)
		{
			return false;
		}

		const int32 PlaneSampleCount = static_cast<int32>(PlaneSampleCount64);
		const int32 VolumeSampleCount = static_cast<int32>(VolumeSampleCount64);
		TArray<float> PendingDensities;
		TArray<uint8> bPendingDensityChanges;
		PendingDensities.SetNumUninitialized(VolumeSampleCount);
		bPendingDensityChanges.SetNumUninitialized(VolumeSampleCount);

		// Batch and both DualContours remain unchanged while the game thread blocks in
		// ParallelFor, so workers may read their TMaps concurrently. Each worker writes
		// only its own PendingDensities element; nested Batch TMaps are staged below.
		const TMap<FIntVector, TMap<uint16, FDualContourPendingSample>>& ReadOnlyChunkSamples = Batch.ChunkSamples;
		const auto GetSnapshotDensity = [DualContour, &ReadOnlyChunkSamples](int32 X, int32 Y, int32 Z) -> float
		{
			const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(X, Y, Z);
			if (const TMap<uint16, FDualContourPendingSample>* Chunk = ReadOnlyChunkSamples.Find(ChunkCoord))
				if (const FDualContourPendingSample* Pending = Chunk->Find(DualContourUtils::ChunkLocalIndex(X, Y, Z)))
					return Pending->WorkingValue;
			return DualContour->GetLinearDensity(X, Y, Z);
		};

		ParallelFor(TEXT("DualContourBrush.VolumeStampSamples"), VolumeSampleCount, 256,
			[DualContour, &Stamp, SampleMin, VolumeSampleDims, PlaneSampleCount, OperationWeight,
				&GetSnapshotDensity, &PendingDensities, &bPendingDensityChanges](int32 SampleIndex)
			{
				const int32 LocalZ = SampleIndex / PlaneSampleCount;
				const int32 PlaneIndex = SampleIndex - LocalZ * PlaneSampleCount;
				const int32 LocalY = PlaneIndex / VolumeSampleDims.X;
				const int32 LocalX = PlaneIndex - LocalY * VolumeSampleDims.X;
				const int32 X = SampleMin.X + LocalX;
				const int32 Y = SampleMin.Y + LocalY;
				const int32 Z = SampleMin.Z + LocalZ;

				const FVector LocalPosition = DualContour->GetSampleLocalPosition(X, Y, Z);
				const FVector SourcePosition = Stamp.VolumeToTarget.InverseTransformPosition(LocalPosition);
				const FVector SourceGrid = SourcePosition / Stamp.VolumeBrush->CellSize;
				if (SourceGrid.X < 0.0 || SourceGrid.Y < 0.0 || SourceGrid.Z < 0.0
				    || SourceGrid.X > Stamp.VolumeBrush->CellCount.X
				    || SourceGrid.Y > Stamp.VolumeBrush->CellCount.Y
				    || SourceGrid.Z > Stamp.VolumeBrush->CellCount.Z)
				{
					bPendingDensityChanges[SampleIndex] = false;
					return;
				}

				const float TargetDensity = Stamp.VolumeBrush->TrilinearDensity(SourceGrid);
				const float OldDensity = GetSnapshotDensity(X, Y, Z);
				float CombinedDensity = TargetDensity;
				if (Stamp.Operation == EDualContourDensityEditOperation::StampDifference)
				{
					const float DifferenceLinearDensity = TargetDensity >= GDualContourLinearIsoValue ? -TargetDensity : GDualContourMaxLinearDensity;
					CombinedDensity = FMath::Min(OldDensity, DifferenceLinearDensity);
				}
				else
				{
					CombinedDensity = FMath::Max(OldDensity, TargetDensity);
				}

				const float NewDensity = FMath::Lerp(OldDensity, CombinedDensity, OperationWeight);
				PendingDensities[SampleIndex] = NewDensity;
				bPendingDensityChanges[SampleIndex] = !FMath::IsNearlyEqual(NewDensity, OldDensity, KINDA_SMALL_NUMBER);
			}, EParallelForFlags::Unbalanced);

		for (int32 SampleIndex = 0; SampleIndex < VolumeSampleCount; ++SampleIndex)
		{
			if (!bPendingDensityChanges[SampleIndex])
				continue;

			const int32 LocalZ = SampleIndex / PlaneSampleCount;
			const int32 PlaneIndex = SampleIndex - LocalZ * PlaneSampleCount;
			const int32 LocalY = PlaneIndex / VolumeSampleDims.X;
			const int32 LocalX = PlaneIndex - LocalY * VolumeSampleDims.X;
			SetWorkingDensity(SampleMin.X + LocalX, SampleMin.Y + LocalY, SampleMin.Z + LocalZ,
				PendingDensities[SampleIndex]);
			bChanged = true;
		}
		return bChanged;
	}

	for (int32 Z = SampleMin.Z; Z <= SampleMax.Z; ++Z)
		for (int32 Y = SampleMin.Y; Y <= SampleMax.Y; ++Y)
			for (int32 X = SampleMin.X; X <= SampleMax.X; ++X)
			{
				const FVector LocalPosition = DualContour->GetSampleLocalPosition(X, Y, Z);
				float TargetDensity = 0.0f;
				float Weight = 1.0f;
				const FVector Offset = LocalPosition - Stamp.LocalCenter;
				float Distance = 0.0f;
				if (Stamp.bUseDirectionalFalloff
				    && (Stamp.Operation == EDualContourDensityEditOperation::Sculpt
				        || Stamp.Operation == EDualContourDensityEditOperation::SculptSubtract))
				{
					const FVector Axis = Stamp.LocalNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
					const float AxialDistance = FVector::DotProduct(Offset, Axis);
					const FVector PlanarOffset = Offset - Axis * AxialDistance;
					if (Stamp.Shape == EDualContourBrushShape::Box)
					{
						FVector AxisX;
						FVector AxisY;
						Axis.FindBestAxisVectors(AxisX, AxisY);
						Distance = FMath::Max(
							FMath::Abs(FVector::DotProduct(PlanarOffset, AxisX)),
							FMath::Abs(FVector::DotProduct(PlanarOffset, AxisY)));
					}
					else
					{
						Distance = PlanarOffset.Length();
					}

					Weight = EvaluateFalloff(Distance / Stamp.Radius, Stamp.Falloff, Stamp.FalloffType);
					// Falloff is the axial profile, rather than merely a density
					// multiplier inside a sphere. Linear therefore has a flat core
					// and straight tapered rim; Spherical produces the rounded cap.
					if (FMath::Abs(AxialDistance) > Stamp.Radius * Weight)
						continue;
				}
				else
				{
					Distance = Stamp.Shape == EDualContourBrushShape::Box
						           ? FMath::Max3(FMath::Abs(Offset.X), FMath::Abs(Offset.Y), FMath::Abs(Offset.Z))
						           : Offset.Length();
					Weight = EvaluateFalloff(Distance / Stamp.Radius, Stamp.Falloff, Stamp.FalloffType);
				}
				if (Weight <= 0.0f)
					continue;
				if (Stamp.Operation == EDualContourDensityEditOperation::Smooth)
				{
					const int32 SX = X - static_cast<int32>(BoundsMin.X);
					const int32 SY = Y - static_cast<int32>(BoundsMin.Y);
					const int32 SZ = Z - static_cast<int32>(BoundsMin.Z);
					TargetDensity = SmoothedValues[DualContourUtils::LinearIndex(SmoothDims, SX, SY, SZ)];
				}
				else if (Stamp.Operation == EDualContourDensityEditOperation::Erase)
				{
					TargetDensity = RestoreSource->GetLinearDensity(X, Y, Z);
				}
				else if (Stamp.Operation == EDualContourDensityEditOperation::Flatten)
				{
					const float SignedDistance = FVector::DotProduct(
						LocalPosition - Stamp.FlattenPlaneOrigin,
						Stamp.FlattenPlaneNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
					TargetDensity = -SignedDistance * GDualContourLinearDensityFixedPointScale;
				}
				else if (Stamp.bUseClayBrush)
				{
					const float SignedDistance = FVector::DotProduct(LocalPosition - Stamp.ClayPlaneOrigin,
						Stamp.LocalNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
					TargetDensity = -SignedDistance * (GDualContourMaxLinearDensity / FMath::Max(DualContour->CellSize, UE_SMALL_NUMBER));
				}
				else
				{
					TargetDensity = GDualContourMaxLinearDensity * (1.0f - Distance / Stamp.Radius);
				}

				const float OldDensity = GetWorkingDensity(X, Y, Z);
				if (!Stamp.bUseClayBrush
				    && (Stamp.Operation == EDualContourDensityEditOperation::Sculpt
				        || Stamp.Operation == EDualContourDensityEditOperation::SculptSubtract))
				{
					const float Direction = Stamp.Operation == EDualContourDensityEditOperation::Sculpt ? 1.0f : -1.0f;
					const float NewDensity = OldDensity
					                         + Direction * GDualContourMaxLinearDensity * OperationWeight * Weight;
					if (FMath::IsNearlyEqual(NewDensity, OldDensity, KINDA_SMALL_NUMBER))
						continue;
					SetWorkingDensity(X, Y, Z, NewDensity);
					bChanged = true;
					continue;
				}

				float CombinedDensity = TargetDensity;
				if (Stamp.Operation == EDualContourDensityEditOperation::SculptSubtract
				    || Stamp.Operation == EDualContourDensityEditOperation::StampDifference)
				{
					const float DifferenceDensity = TargetDensity >= GDualContourLinearIsoValue ? -TargetDensity : GDualContourMaxLinearDensity;
					CombinedDensity = FMath::Min(OldDensity, DifferenceDensity);
				}
				else if (Stamp.Operation == EDualContourDensityEditOperation::Sculpt
				         || Stamp.Operation == EDualContourDensityEditOperation::StampUnion)
				{
					CombinedDensity = FMath::Max(OldDensity, TargetDensity);
				}
				const float NewDensity = FMath::Lerp(OldDensity, CombinedDensity, OperationWeight * Weight);
				if (FMath::IsNearlyEqual(NewDensity, OldDensity, KINDA_SMALL_NUMBER))
					continue;
				SetWorkingDensity(X, Y, Z, NewDensity);
				bChanged = true;
			}
	return bChanged;
}

void DualContourBrushOperations::ApplyMaterialVolumes(ADualContourMeshActor* TargetActor,
	FDualContourPendingMaterialBatch& Batch, TConstArrayView<ADualContourMaterialBrushVolume*> BrushVolumes, uint8 PaintId)
{
	UDualContour* DualContour = TargetActor ? TargetActor->DualContour.Get() : nullptr;
	if (!IsValid(DualContour) || !Batch.bOpen || Batch.Owner != DualContour || !DualContour->HasCurrentGeneratedData())
		return;

	const FTransform TargetTransform = TargetActor->GetActorTransform();
	const FTransform WorldToTarget = TargetTransform.Inverse();

	for (ADualContourMaterialBrushVolume* BrushVolume : BrushVolumes)
	{
		if (!IsValid(BrushVolume) || BrushVolume->TargetActor != TargetActor)
			continue;

		BrushVolume->CacheBrushGeometry();
		const FBox WorldBounds = BrushVolume->GetBrushWorldBounds();
		if (!WorldBounds.IsValid)
			continue;

		FBox TargetBounds(ForceInit);
		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector Corner(
				(CornerIndex & 1) ? WorldBounds.Max.X : WorldBounds.Min.X,
				(CornerIndex & 2) ? WorldBounds.Max.Y : WorldBounds.Min.Y,
				(CornerIndex & 4) ? WorldBounds.Max.Z : WorldBounds.Min.Z);
			TargetBounds += WorldToTarget.TransformPosition(Corner);
		}

		FIntVector SampleMin;
		FIntVector SampleMax;
		GetSampleBounds(*DualContour, TargetBounds.Min, TargetBounds.Max, SampleMin, SampleMax);

		for (int32 Z = SampleMin.Z; Z <= SampleMax.Z; ++Z)
			for (int32 Y = SampleMin.Y; Y <= SampleMax.Y; ++Y)
				for (int32 X = SampleMin.X; X <= SampleMax.X; ++X)
				{
					// Region painting is deliberately restricted to solid samples.
					if (DualContour->GetDensity(X, Y, Z) < GDualContourIsoValue)
						continue;
					const FVector TargetLocalPosition = DualContour->GetSampleLocalPosition(X, Y, Z);
					if (!BrushVolume->EncompassesWorldPosition(TargetTransform.TransformPosition(TargetLocalPosition)))
						continue;

					SetMaterialSample(*DualContour, Batch, X, Y, Z, PaintId);
				}
	}

}
