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

bool FDualContourEditContext::GetSampleBounds(const UVolumeSampler& Sampler, const FTransform* SamplerToTargetTransform,
	FIntVector& OutMinSampleCoord, FIntVector& OutMaxSampleCoord) const
{
	if (!IsOpen())
		return false;
	FBox TargetLocalBounds = Sampler.GetBounds();
	if (!TargetLocalBounds.IsValid || TargetLocalBounds.Min.ContainsNaN() || TargetLocalBounds.Max.ContainsNaN())
		return false;
	if (SamplerToTargetTransform)
	{
		TargetLocalBounds = UVolumeSampler::TransformBoxAroundPivot(
			TargetLocalBounds, *SamplerToTargetTransform, Sampler.Pivot * Sampler.VolumeSize);
	}
	const FVector TargetLocalMax = FVector(Target->CellCount) * Target->CellSize;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (TargetLocalBounds.Max[Axis] < 0 || TargetLocalBounds.Min[Axis] > TargetLocalMax[Axis])
			return false;
		OutMinSampleCoord[Axis] = FMath::FloorToInt(
			FMath::Clamp(TargetLocalBounds.Min[Axis] / Target->CellSize, 0.0, double(Target->CellCount[Axis])));
		OutMaxSampleCoord[Axis] = FMath::CeilToInt(
			FMath::Clamp(TargetLocalBounds.Max[Axis] / Target->CellSize, 0.0, double(Target->CellCount[Axis])));
	}
	return true;
}

bool FDualContourEditContext::ApplyDensity(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler, float Strength)
{
	return ApplyDensityInternal(Operation, Sampler, nullptr, Strength);
}

bool FDualContourEditContext::ApplyDensity(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler,
	const FTransform& SamplerToTargetTransform, float Strength)
{
	return ApplyDensityInternal(Operation, Sampler, &SamplerToTargetTransform, Strength);
}

bool FDualContourEditContext::ApplyDensityInternal(EDualContourDensityOperation Operation, const UVolumeSampler& Sampler,
	const FTransform* SamplerToTargetTransform, float Strength)
{
	check(IsInGameThread());
	FText Error;
	if (!IsOpen() || !Sampler.Prepare(Error))
		return false;
	ON_SCOPE_EXIT
	{
		Sampler.Finish();
	};
	const FVolumeSamplerPlacement Placement = Sampler.MakePlacement(SamplerToTargetTransform);
	FIntVector MinSampleCoord, MaxSampleCoord;
	if (!FMath::IsFinite(Strength) || Strength <= 0
	    || !GetSampleBounds(Sampler, SamplerToTargetTransform, MinSampleCoord, MaxSampleCoord))
		return false;
	Strength = FMath::Min(Strength, 1.0f);
	const FIntVector SampleDimensions = MaxSampleCoord - MinSampleCoord + FIntVector(1);
	const int64 SampleCount = int64(SampleDimensions.X) * SampleDimensions.Y * SampleDimensions.Z;
	if (SampleCount <= 0 || SampleCount > MAX_int32)
		return false;
	TArray<float> SmoothValues;
	FIntVector SmoothMin = FIntVector::ZeroValue;
	FIntVector SmoothDims = FIntVector::ZeroValue;
	if (Operation == EDualContourDensityOperation::Smooth)
	{
		FIntVector SmoothMax;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			SmoothMin[Axis] = FMath::Max(0, MinSampleCoord[Axis] - 1);
			SmoothMax[Axis] = FMath::Min(Target->CellCount[Axis], MaxSampleCoord[Axis] + 1);
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
	Writes.SetNum(static_cast<int32>(SampleCount));
	const auto Compute = [&](int32 Index)
	{
		const int32 X = MinSampleCoord.X + Index % SampleDimensions.X;
		const int32 Y = MinSampleCoord.Y + (Index / SampleDimensions.X) % SampleDimensions.Y;
		const int32 Z = MinSampleCoord.Z + Index / (SampleDimensions.X * SampleDimensions.Y);
		const FIntVector Coord(X, Y, Z);
		float Value = 0, Weight = 0;
		if (!Sampler.Sample(Target->GetSampleLocalPosition(X, Y, Z), Placement, Value, Weight) || !FMath::IsFinite(Value) ||
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
	return ApplyMaterialInternal(Sampler, MaterialId, nullptr, Threshold, bSolidOnly);
}

bool FDualContourEditContext::ApplyMaterial(const UVolumeSampler& Sampler, uint8 MaterialId,
	const FTransform& SamplerToTargetTransform, float Threshold, bool bSolidOnly)
{
	return ApplyMaterialInternal(Sampler, MaterialId, &SamplerToTargetTransform, Threshold, bSolidOnly);
}

bool FDualContourEditContext::ApplyMaterialInternal(const UVolumeSampler& Sampler, uint8 MaterialId,
	const FTransform* SamplerToTargetTransform, float Threshold, bool bSolidOnly)
{
	check(IsInGameThread());
	FText Error;
	if (!IsOpen() || !Sampler.Prepare(Error))
		return false;
	ON_SCOPE_EXIT
	{
		Sampler.Finish();
	};
	const FVolumeSamplerPlacement Placement = Sampler.MakePlacement(SamplerToTargetTransform);
	FIntVector MinSampleCoord, MaxSampleCoord;
	if (!FMath::IsFinite(Threshold)
	    || !GetSampleBounds(Sampler, SamplerToTargetTransform, MinSampleCoord, MaxSampleCoord))
		return false;
	Threshold = FMath::Clamp(Threshold, 0.0f, 1.0f);
	bool bChanged = false;
	for (int32 Z = MinSampleCoord.Z; Z <= MaxSampleCoord.Z; ++Z)
		for (int32 Y = MinSampleCoord.Y; Y <= MaxSampleCoord.Y; ++Y)
			for (int32 X = MinSampleCoord.X; X <= MaxSampleCoord.X; ++X)
			{
				float Value = 0, Weight = 0;
				if (!Sampler.Sample(Target->GetSampleLocalPosition(X, Y, Z), Placement, Value, Weight)
				    || !FMath::IsFinite(Value) || !FMath::IsFinite(Weight) || Weight <= 0
				    || Weight < Threshold || Value < GDualContourLinearIsoValue)
					continue;
				const FIntVector Coord(X, Y, Z);
				if (bSolidOnly && FDensityChunk::EncodeDensity(GetDensity(Coord)) < GDualContourIsoValue)
					continue;
				bChanged |= SetMaterial(Coord, MaterialId);
			}
	return bChanged;
}

bool FDualContourEditContext::Commit(FDualContourDensityChangedCallback OnDensityChanged, FDualContourMaterialChangedCallback OnMaterialChanged)
{
	check(IsInGameThread());
	if (!IsOpen())
		return false;
	bOpen = false;
	return Target->ApplyPendingEdit(DensityBatch, MaterialBatch, MoveTemp(OnDensityChanged), MoveTemp(OnMaterialChanged));
}
