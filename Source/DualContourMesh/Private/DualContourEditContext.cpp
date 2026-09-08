#include "DualContourEditContext.h"
#include "DualContour.h"
#include "VolumeSampler/VolumeSampler.h"
#include "Misc/ScopeExit.h"
#include "DualContourUtils.h"
#include "Async/ParallelFor.h"

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

bool FDualContourEditContext::GetSampleBounds(const UVolumeSampler& Sampler, const FTransform* SampleTransform,
	FIntVector& Min, FIntVector& Max) const
{
	if (!IsOpen())
		return false;
	FBox Bounds = Sampler.GetBounds();
	if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN())
		return false;
	if (SampleTransform)
	{
		const FVector PivotPosition = Sampler.Pivot * Sampler.VolumeSize;
		FBox TransformedBounds(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector CornerPosition((Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
				(Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
				(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
			TransformedBounds += PivotPosition + SampleTransform->TransformPosition(CornerPosition - PivotPosition);
		}
		Bounds = TransformedBounds;
	}
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

bool FDualContourEditContext::ApplyDensity(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler, float Strength)
{
	return ApplyDensityInternal(Operation, Sampler, nullptr, Strength);
}

bool FDualContourEditContext::ApplyDensity(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler,
	const FTransform& SampleTransform, float Strength)
{
	return ApplyDensityInternal(Operation, Sampler, &SampleTransform, Strength);
}

bool FDualContourEditContext::ApplyDensityInternal(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler,
	const FTransform* SampleTransform, float Strength)
{
	check(IsInGameThread());
	FText Error;
	if (!IsOpen() || !Sampler.Prepare(Error))
		return false;
	ON_SCOPE_EXIT
	{
		Sampler.Finish();
	};
	FIntVector Min, Max;
	if (!FMath::IsFinite(Strength) || Strength <= 0 || !GetSampleBounds(Sampler, SampleTransform, Min, Max))
		return false;
	Strength = FMath::Min(Strength, 1.0f);
	const FIntVector Dims = Max - Min + FIntVector(1);
	const int64 Count = int64(Dims.X) * Dims.Y * Dims.Z;
	if (Count <= 0 || Count > MAX_int32)
		return false;
	TArray<float> SmoothValues;
	FIntVector SmoothMin = FIntVector::ZeroValue;
	FIntVector SmoothDims = FIntVector::ZeroValue;
	if (Operation == EDualContourDensityOperation::Smooth)
	{
		FIntVector SmoothMax;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			SmoothMin[Axis] = FMath::Max(0, Min[Axis] - 1);
			SmoothMax[Axis] = FMath::Min(Target->CellCount[Axis], Max[Axis] + 1);
		}
		SmoothDims = SmoothMax - SmoothMin + FIntVector(1);
		const int64 SmoothCount = int64(SmoothDims.X) * SmoothDims.Y * SmoothDims.Z;
		if (SmoothCount > MAX_int32)
			return false;
		SmoothValues.SetNumUninitialized(static_cast<int32>(SmoothCount));
		for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
			for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
				for (int32 X = 0; X < SmoothDims.X; ++X)
					SmoothValues[DualContourUtils::LinearIndex(SmoothDims, X, Y, Z)] =
						GetDensity(SmoothMin + FIntVector(X, Y, Z));
		TArray<float> Pass;
		Pass.SetNumUninitialized(SmoothValues.Num());
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (int32 Z = 0; Z < SmoothDims.Z; ++Z)
				for (int32 Y = 0; Y < SmoothDims.Y; ++Y)
					for (int32 X = 0; X < SmoothDims.X; ++X)
					{
						FIntVector Left(X, Y, Z), Right(X, Y, Z);
						Left[Axis] = FMath::Max(0, Left[Axis] - 1);
						Right[Axis] = FMath::Min(SmoothDims[Axis] - 1, Right[Axis] + 1);
						const int32 Index = DualContourUtils::LinearIndex(SmoothDims, X, Y, Z);
						Pass[Index] = (SmoothValues[DualContourUtils::LinearIndex(SmoothDims, Left.X, Left.Y, Left.Z)] +
						               2.0f * SmoothValues[Index] +
						               SmoothValues[DualContourUtils::LinearIndex(SmoothDims, Right.X, Right.Y, Right.Z)]) *
						              0.25f;
					}
			Swap(SmoothValues, Pass);
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
		FVector SamplePosition = Target->GetSampleLocalPosition(X, Y, Z);
		if (SampleTransform)
		{
			const FVector PivotPosition = Sampler.Pivot * Sampler.VolumeSize;
			SamplePosition = PivotPosition + SampleTransform->InverseTransformVector(
				                 SamplePosition - PivotPosition - SampleTransform->GetTranslation());
		}
		float Value = 0, Weight = 0;
		if (!Sampler.Sample(SamplePosition, Value, Weight) || !FMath::IsFinite(Value) ||
		    !FMath::IsFinite(Weight) || Weight <= 0)
			return;
		const float Before = GetDensity(Coord);
		switch (Operation)
		{
			case EDualContourDensityOperation::Add:
				Value = Before + GDualContourMaxLinearDensity;
				break;
			case EDualContourDensityOperation::Subtract:
				Value = Before - GDualContourMaxLinearDensity;
				break;
			case EDualContourDensityOperation::Union:
				Value = FMath::Max(Before, Value);
				break;
			case EDualContourDensityOperation::Difference:
				Value = FMath::Min(Before, Value >= GDualContourLinearIsoValue ? -Value : GDualContourMaxLinearDensity);
				break;
			case EDualContourDensityOperation::Replace:
				break;
			case EDualContourDensityOperation::Smooth:
			{
				const FIntVector Local = Coord - SmoothMin;
				Value = SmoothValues[DualContourUtils::LinearIndex(SmoothDims, Local.X, Local.Y, Local.Z)];
				break;
			}
			default:
				return;
		}
		Value = FMath::Lerp(Before, Value, FMath::Clamp(Weight, 0.0f, 1.0f) * FMath::Clamp(Strength, 0.0f, 1.0f));
		if (!FMath::IsFinite(Value))
			return;
		Value = FMath::Clamp(Value, GDualContourMinLinearDensity, GDualContourMaxLinearDensity);
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

bool FDualContourEditContext::ApplyMaterial(const UVolumeSampler& Sampler, uint8 MaterialId, float Threshold, bool bSolidOnly)
{
	check(IsInGameThread());
	FText Error;
	if (!IsOpen() || !Sampler.Prepare(Error))
		return false;
	ON_SCOPE_EXIT
	{
		Sampler.Finish();
	};
	FIntVector Min, Max;
	if (!FMath::IsFinite(Threshold) || !GetSampleBounds(Sampler, nullptr, Min, Max))
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
	FDualContourDensityChangedCallback OnDensityChanged)
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
