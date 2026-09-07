#include "DualContourEditContext.h"
#include "DualContour.h"
#include "DualContourSampledRegionUtils.h"
#include "DualContourUtils.h"
#include "Async/ParallelFor.h"

float FDualContourShapeSampler::EvaluateFalloff(float Distance, float Falloff, EDualContourEditFalloff Type)
{
	if (Distance >= 1.0f)
		return 0.0f;
	const float Inner = 1.0f - FMath::Clamp(Falloff, 0.0f, 1.0f);
	if (Distance <= Inner || Falloff <= UE_SMALL_NUMBER)
		return 1.0f;
	const float T = FMath::Clamp((Distance - Inner) / FMath::Max(Falloff, UE_SMALL_NUMBER), 0.0f, 1.0f);
	switch (Type)
	{
	case EDualContourEditFalloff::Linear:
		return 1.0f - T;
	case EDualContourEditFalloff::Spherical:
		return FMath::Sqrt(FMath::Max(0.0f, 1.0f - T * T));
	case EDualContourEditFalloff::Tip:
		return FMath::Square(1.0f - T);
	default:
		return 1.0f - FMath::SmoothStep(0.0f, 1.0f, T);
	}
}

FBox FDualContourShapeSampler::GetBounds() const
{
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER || Center.ContainsNaN())
		return FBox(ForceInit);
	// A rotated directional box/cylinder can extend beyond Radius along a target axis.
	const FVector Extent(Radius * (bDirectional ? FMath::Sqrt(3.0f) : 1.0f));
	return FBox(Center - Extent, Center + Extent);
}

bool FDualContourShapeSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER)
		return false;
	const FVector Offset = Position - Center;
	float Distance;
	if (bDirectional)
	{
		const FVector Axis = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const float Axial = FVector::DotProduct(Offset, Axis);
		const FVector Planar = Offset - Axis * Axial;
		FVector X, Y;
		Axis.FindBestAxisVectors(X, Y);
		Distance =
		    bBox ? FMath::Max(FMath::Abs(FVector::DotProduct(Planar, X)), FMath::Abs(FVector::DotProduct(Planar, Y))) : Planar.Length();
		Weight = EvaluateFalloff(Distance / Radius, Falloff, FalloffType);
		if (FMath::Abs(Axial) > Radius * Weight)
			return false;
	}
	else
	{
		Distance = bBox ? FMath::Max3(FMath::Abs(Offset.X), FMath::Abs(Offset.Y), FMath::Abs(Offset.Z)) : Offset.Length();
		Weight = EvaluateFalloff(Distance / Radius, Falloff, FalloffType);
	}
	Value = GDualContourMaxLinearDensity * (1.0f - Distance / Radius);
	return Weight > 0.0f;
}

bool FDualContourPlaneSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	if (!Mask.Sample(Position, Value, Weight))
		return false;
	Value = -FVector::DotProduct(Position - Origin, Normal) * Scale;
	return true;
}

FDualContourVolumeSampler::FDualContourVolumeSampler(const UDualContour& InSource, const FTransform& InSourceToTarget)
    : Source(&InSource), SourceToTarget(InSourceToTarget)
{
}

FBox FDualContourVolumeSampler::GetBounds() const
{
	const UDualContour* Grid = Source.Get();
	if (!Grid || !Grid->HasCurrentGeneratedData() || SourceToTarget.ContainsNaN() ||
	    SourceToTarget.GetScale3D().GetAbs().GetMin() <= UE_SMALL_NUMBER)
		return FBox(ForceInit);
	return FBox(FVector::ZeroVector, FVector(Grid->CellCount) * Grid->CellSize).TransformBy(SourceToTarget);
}

bool FDualContourVolumeSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	const UDualContour* Grid = Source.Get();
	if (!Grid || !Grid->HasCurrentGeneratedData())
		return false;
	const FVector P = SourceToTarget.InverseTransformPosition(Position) / Grid->CellSize;
	if (P.X < 0 || P.Y < 0 || P.Z < 0 || P.X > Grid->CellCount.X || P.Y > Grid->CellCount.Y || P.Z > Grid->CellCount.Z)
		return false;
	Value = Grid->TrilinearDensity(P);
	Weight = 1.0f;
	return true;
}

FDualContourEditContext::FDualContourEditContext(UDualContour& InTarget) : Target(&InTarget)
{
	check(IsInGameThread());
	bOpen = InTarget.HasCurrentGeneratedData();
	DensityBatch.Owner = MaterialBatch.Owner = &InTarget;
	DensityBatch.bOpen = MaterialBatch.bOpen = bOpen;
}

bool FDualContourEditContext::IsOpen() const
{
	return bOpen && Target.IsValid() && Target->HasCurrentGeneratedData();
}
UDualContour* FDualContourEditContext::GetTarget() const
{
	return Target.Get();
}

float FDualContourEditContext::GetDensity(FIntVector Coord) const
{
	if (const auto* Chunk = DensityBatch.ChunkSamples.Find(DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z)))
		if (const auto* Sample = Chunk->Find(DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z)))
			return Sample->WorkingValue;
	return Target.IsValid() ? Target->GetLinearDensity(Coord.X, Coord.Y, Coord.Z) : GDualContourMinLinearDensity;
}

uint8 FDualContourEditContext::GetMaterial(FIntVector Coord) const
{
	if (const auto* Chunk = MaterialBatch.ChunkSamples.Find(DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z)))
		if (const auto* Sample = Chunk->Find(DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z)))
			return Sample->WorkingId;
	return Target.IsValid() ? Target->GetMaterialId(Coord.X, Coord.Y, Coord.Z) : 0;
}

bool FDualContourEditContext::SetDensity(FIntVector Coord, float Value)
{
	check(IsInGameThread());
	if (!IsOpen() || !FMath::IsFinite(Value) ||
	    !DualContourUtils::IsValidCoordinate(Target->GetSampleDimensions(), Coord.X, Coord.Y, Coord.Z))
		return false;
	Value = FMath::Clamp(Value, GDualContourMinLinearDensity, GDualContourMaxLinearDensity);
	if (GetDensity(Coord) == Value)
		return false;
	auto& Chunk = DensityBatch.ChunkSamples.FindOrAdd(DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z));
	const uint16 Index = DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z);
	if (!Chunk.Contains(Index))
		Chunk.Add(Index, {Target->GetDensity(Coord.X, Coord.Y, Coord.Z), Value});
	else
		Chunk[Index].WorkingValue = Value;
	return true;
}

bool FDualContourEditContext::SetMaterial(FIntVector Coord, uint8 Value)
{
	check(IsInGameThread());
	if (!IsOpen() || !DualContourUtils::IsValidCoordinate(Target->GetSampleDimensions(), Coord.X, Coord.Y, Coord.Z) ||
	    GetMaterial(Coord) == Value)
		return false;
	auto& Chunk = MaterialBatch.ChunkSamples.FindOrAdd(DualContourUtils::ChunkCoord(Coord.X, Coord.Y, Coord.Z));
	const uint16 Index = DualContourUtils::ChunkLocalIndex(Coord.X, Coord.Y, Coord.Z);
	if (!Chunk.Contains(Index))
		Chunk.Add(Index, {Target->GetMaterialId(Coord.X, Coord.Y, Coord.Z), Value});
	else
		Chunk[Index].WorkingId = Value;
	return true;
}

bool FDualContourEditContext::GetSampleBounds(const FDualContourEditSampler& Sampler, FIntVector& Min, FIntVector& Max) const
{
	if (!IsOpen())
		return false;
	const FBox Bounds = Sampler.GetBounds();
	if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN())
		return false;
	const FVector GridMax = FVector(Target->CellCount) * Target->CellSize;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Bounds.Max[Axis] < 0 || Bounds.Min[Axis] > GridMax[Axis])
			return false;
		Min[Axis] = FMath::FloorToInt(FMath::Clamp(Bounds.Min[Axis] / Target->CellSize, 0.0, double(Target->CellCount[Axis])));
		Max[Axis] = FMath::CeilToInt(FMath::Clamp(Bounds.Max[Axis] / Target->CellSize, 0.0, double(Target->CellCount[Axis])));
	}
	return true;
}

bool FDualContourEditContext::ApplyDensity(const FDualContourEditSampler& Sampler, EDualContourEditOperation Operation, float Strength)
{
	check(IsInGameThread());
	FIntVector Min, Max;
	if (!FMath::IsFinite(Strength) || Strength <= 0 || !GetSampleBounds(Sampler, Min, Max))
		return false;
	Strength = FMath::Min(Strength, 1.0f);
	const FIntVector Dims = Max - Min + FIntVector(1);
	const int64 Count = int64(Dims.X) * Dims.Y * Dims.Z;
	if (Count <= 0 || Count > MAX_int32)
		return false;
	TArray<float> Smoothed;
	FIntVector HaloMin, HaloDims;
	if (Operation == EDualContourEditOperation::Smooth)
	{
		FIntVector HaloMax;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			HaloMin[Axis] = FMath::Max(0, Min[Axis] - 1);
			HaloMax[Axis] = FMath::Min(Target->CellCount[Axis], Max[Axis] + 1);
		}
		HaloDims = HaloMax - HaloMin + FIntVector(1);
		const int64 HaloCount = int64(HaloDims.X) * HaloDims.Y * HaloDims.Z;
		if (HaloCount > MAX_int32)
			return false;
		Smoothed.SetNumUninitialized(static_cast<int32>(HaloCount));
		for (int32 Z = 0; Z < HaloDims.Z; ++Z)
			for (int32 Y = 0; Y < HaloDims.Y; ++Y)
				for (int32 X = 0; X < HaloDims.X; ++X)
					Smoothed[DualContourUtils::LinearIndex(HaloDims, X, Y, Z)] = GetDensity(HaloMin + FIntVector(X, Y, Z));
		TArray<float> Pass;
		Pass.SetNumUninitialized(Smoothed.Num());
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (int32 Z = 0; Z < HaloDims.Z; ++Z)
				for (int32 Y = 0; Y < HaloDims.Y; ++Y)
					for (int32 X = 0; X < HaloDims.X; ++X)
					{
						FIntVector Left(X, Y, Z), Right(X, Y, Z);
						Left[Axis] = FMath::Max(0, Left[Axis] - 1);
						Right[Axis] = FMath::Min(HaloDims[Axis] - 1, Right[Axis] + 1);
						const int32 Index = DualContourUtils::LinearIndex(HaloDims, X, Y, Z);
						Pass[Index] = (Smoothed[DualContourUtils::LinearIndex(HaloDims, Left.X, Left.Y, Left.Z)] + 2.0f * Smoothed[Index] +
						               Smoothed[DualContourUtils::LinearIndex(HaloDims, Right.X, Right.Y, Right.Z)]) *
						              0.25f;
					}
			Swap(Smoothed, Pass);
		}
	}
	// Workers write only their own slot. The target and staged maps stay read-only until all sampling completes.
	struct FWrite
	{
		FIntVector Coord;
		float Value;
		bool bChanged = false;
	};
	TArray<FWrite> Writes;
	Writes.SetNum(static_cast<int32>(Count));
	const auto Compute = [&](int32 Index)
	{
		const int32 X = Min.X + Index % Dims.X;
		const int32 Y = Min.Y + (Index / Dims.X) % Dims.Y;
		const int32 Z = Min.Z + Index / (Dims.X * Dims.Y);
		const FIntVector Coord(X, Y, Z);
		float Value = 0, Weight = 0;
		if (!Sampler.Sample(Target->GetSampleLocalPosition(X, Y, Z), Value, Weight) || !FMath::IsFinite(Value) ||
		    !FMath::IsFinite(Weight) || Weight <= 0)
			return;
		const float Before = GetDensity(Coord);
		if (Operation == EDualContourEditOperation::Smooth)
		{
			const FIntVector C = Coord - HaloMin;
			Value = Smoothed[DualContourUtils::LinearIndex(HaloDims, C.X, C.Y, C.Z)];
		}
		switch (Operation)
		{
		case EDualContourEditOperation::Add:
			Value = Before + GDualContourMaxLinearDensity;
			break;
		case EDualContourEditOperation::Subtract:
			Value = Before - GDualContourMaxLinearDensity;
			break;
		case EDualContourEditOperation::Union:
			Value = FMath::Max(Before, Value);
			break;
		case EDualContourEditOperation::Difference:
			Value = FMath::Min(Before, Value >= GDualContourLinearIsoValue ? -Value : GDualContourMaxLinearDensity);
			break;
		default:
			break;
		}
		Value = FMath::Clamp(FMath::Lerp(Before, Value, Strength * FMath::Clamp(Weight, 0.0f, 1.0f)), GDualContourMinLinearDensity,
		                     GDualContourMaxLinearDensity);
		if (!FMath::IsNearlyEqual(Value, Before, KINDA_SMALL_NUMBER))
			Writes[Index] = {Coord, Value, true};
	};
	if (Sampler.SupportsParallelSampling())
		ParallelFor(TEXT("DualContour.EditSamples"), Writes.Num(), 256, Compute, EParallelForFlags::Unbalanced);
	else
		for (int32 Index = 0; Index < Writes.Num(); ++Index)
			Compute(Index);
	bool bChanged = false;
	for (const FWrite& Write : Writes)
		if (Write.bChanged)
			bChanged |= SetDensity(Write.Coord, Write.Value);
	return bChanged;
}

bool FDualContourEditContext::ApplyMaterial(const FDualContourEditSampler& Sampler, uint8 MaterialId, float Threshold, bool bSolidOnly)
{
	check(IsInGameThread());
	FIntVector Min, Max;
	if (!FMath::IsFinite(Threshold) || !GetSampleBounds(Sampler, Min, Max))
		return false;
	Threshold = FMath::Clamp(Threshold, 0.0f, 1.0f);
	bool bChanged = false;
	for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				float Value = 0, Weight = 0;
				if (!Sampler.Sample(Target->GetSampleLocalPosition(X, Y, Z), Value, Weight) || !FMath::IsFinite(Weight) || Weight <= 0 ||
				    Weight < Threshold)
					continue;
				const FIntVector Coord(X, Y, Z);
				if (bSolidOnly && FDensityChunk::EncodeDensity(GetDensity(Coord)) < GDualContourIsoValue)
					continue;
				bChanged |= SetMaterial(Coord, MaterialId);
			}
	return bChanged;
}

bool FDualContourEditContext::Commit(FDualContourMaterialEditResult& MaterialResult,
                                     TFunctionRef<void(const FIntVector&, uint16, uint16)> OnDensityChanged)
{
	check(IsInGameThread());
	MaterialResult = FDualContourMaterialEditResult();
	if (!IsOpen())
		return false;
	bOpen = false;
	return Target->ApplyPendingEdit(DensityBatch, MaterialBatch, MaterialResult, OnDensityChanged);
}

bool FDualContourEditContext::Commit()
{
	FDualContourMaterialEditResult Result;
	return Commit(Result);
}

bool FDualContourEditContext::ApplySampledRegion(const FDualContourSampledRegion& SampledRegion, bool bExcavate)
{
	check(IsInGameThread());
	if (!IsOpen() || !DualContourSampling::IsValidSampledRegion(GetTarget()->GetSampleDimensions(), SampledRegion))
		return false;
	bool bChanged = false;
	const FIntVector SampleMax = SampledRegion.SampleMin + SampledRegion.SampleDimensions;
	for (const FDualContourSampledChunk& SampledChunk : SampledRegion.Chunks)
	{
		if (SampledChunk.Density.IsUniform() &&
		    (SampledChunk.Density.UniformValue == 0 || (bExcavate && SampledChunk.Density.UniformValue < GDualContourIsoValue)))
		{
			continue;
		}

		const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(SampledChunk.ChunkCoord);
		const FIntVector BuildMin(FMath::Max(SampledRegion.SampleMin.X, ChunkOrigin.X),
		                          FMath::Max(SampledRegion.SampleMin.Y, ChunkOrigin.Y),
		                          FMath::Max(SampledRegion.SampleMin.Z, ChunkOrigin.Z));
		const FIntVector BuildMax(FMath::Min(SampleMax.X, ChunkOrigin.X + GDualContourChunkSize),
		                          FMath::Min(SampleMax.Y, ChunkOrigin.Y + GDualContourChunkSize),
		                          FMath::Min(SampleMax.Z, ChunkOrigin.Z + GDualContourChunkSize));

		for (int32 SampleZ = BuildMin.Z; SampleZ < BuildMax.Z; ++SampleZ)
			for (int32 SampleY = BuildMin.Y; SampleY < BuildMax.Y; ++SampleY)
				for (int32 SampleX = BuildMin.X; SampleX < BuildMax.X; ++SampleX)
				{
					const uint16 LocalIndex = DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ);
					const uint16 SamplerDensity = SampledChunk.Density.IsUniform() ? SampledChunk.Density.UniformValue
					                                                               : SampledChunk.Density.DensitySamples[LocalIndex];
					if (SamplerDensity == 0 || (bExcavate && SamplerDensity < GDualContourIsoValue))
						continue;

					const uint16 OldDensity = FDensityChunk::EncodeDensity(GetDensity(FIntVector(SampleX, SampleY, SampleZ)));
					uint16 NewDensity = FMath::Max(OldDensity, SamplerDensity);
					if (bExcavate)
					{
						const uint16 DifferenceDensity =
						    static_cast<uint16>(2u * static_cast<uint32>(GDualContourIsoValue) - SamplerDensity);
						NewDensity = FMath::Min(OldDensity, DifferenceDensity);
					}
					if (NewDensity == OldDensity)
						continue;

					bChanged |= SetDensity(FIntVector(SampleX, SampleY, SampleZ), FDensityChunk::DecodeLinearDensity(NewDensity));
				}
	}
	return bChanged;
}

FDualContourRestoreSampler::FDualContourRestoreSampler(const FDualContourEditSampler& InMask, const UDualContour& InSource)
    : Mask(InMask), Source(&InSource)
{
}

FBox FDualContourRestoreSampler::GetBounds() const
{
	return Source.IsValid() && Source->HasCurrentGeneratedData() ? Mask.GetBounds() : FBox(ForceInit);
}

bool FDualContourRestoreSampler::Sample(const FVector& Position, float& Value, float& Weight) const
{
	if (!Source.IsValid() || !Source->HasCurrentGeneratedData() || !Mask.Sample(Position, Value, Weight))
		return false;
	const FVector Grid = Position / Source->CellSize;
	Value = Source->GetLinearDensity(FMath::RoundToInt(Grid.X), FMath::RoundToInt(Grid.Y), FMath::RoundToInt(Grid.Z));
	return true;
}
