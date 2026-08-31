#include "DualContour.h"
#include "DualContourUtils.h"
#include "VolumeSampledDualContour.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/ObjectSaveContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogDualContour, Log, All);

namespace
{
constexpr int32 EdgeCorners[12][2][3] = {
	{{0, 0, 0}, {1, 0, 0}}, {{0, 1, 0}, {1, 1, 0}}, {{0, 0, 1}, {1, 0, 1}}, {{0, 1, 1}, {1, 1, 1}},
	{{0, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {1, 1, 0}}, {{0, 0, 1}, {0, 1, 1}}, {{1, 0, 1}, {1, 1, 1}},
	{{0, 0, 0}, {0, 0, 1}}, {{1, 0, 0}, {1, 0, 1}}, {{0, 1, 0}, {0, 1, 1}}, {{1, 1, 0}, {1, 1, 1}}
};

bool Solve3x3(const double Matrix[3][3], const double RightHandSide[3], double Solution[3])
{
	const double C00 = Matrix[1][1] * Matrix[2][2] - Matrix[1][2] * Matrix[2][1];
	const double C01 = -(Matrix[1][0] * Matrix[2][2] - Matrix[1][2] * Matrix[2][0]);
	const double C02 = Matrix[1][0] * Matrix[2][1] - Matrix[1][1] * Matrix[2][0];
	const double Determinant = Matrix[0][0] * C00 + Matrix[0][1] * C01 + Matrix[0][2] * C02;
	if (FMath::Abs(Determinant) < 1e-10)
		return false;

	const double C10 = -(Matrix[0][1] * Matrix[2][2] - Matrix[0][2] * Matrix[2][1]);
	const double C11 = Matrix[0][0] * Matrix[2][2] - Matrix[0][2] * Matrix[2][0];
	const double C12 = -(Matrix[0][0] * Matrix[2][1] - Matrix[0][1] * Matrix[2][0]);
	const double C20 = Matrix[0][1] * Matrix[1][2] - Matrix[0][2] * Matrix[1][1];
	const double C21 = -(Matrix[0][0] * Matrix[1][2] - Matrix[0][2] * Matrix[1][0]);
	const double C22 = Matrix[0][0] * Matrix[1][1] - Matrix[0][1] * Matrix[1][0];
	const double InverseDeterminant = 1.0 / Determinant;

	Solution[0] = InverseDeterminant * (C00 * RightHandSide[0] + C10 * RightHandSide[1] + C20 * RightHandSide[2]);
	Solution[1] = InverseDeterminant * (C01 * RightHandSide[0] + C11 * RightHandSide[1] + C21 * RightHandSide[2]);
	Solution[2] = InverseDeterminant * (C02 * RightHandSide[0] + C12 * RightHandSide[1] + C22 * RightHandSide[2]);
	return true;
}

bool IsValidSampleBounds(const FIntVector& FullDimensions, const FIntVector& SampleMin, const FIntVector& SampleDimensions)
{
	if (SampleMin.X < 0 || SampleMin.Y < 0 || SampleMin.Z < 0
	    || SampleDimensions.X < 0 || SampleDimensions.Y < 0 || SampleDimensions.Z < 0)
		return false;

	const bool bEmpty = SampleDimensions.X == 0 || SampleDimensions.Y == 0 || SampleDimensions.Z == 0;
	if (bEmpty)
	{
		return SampleDimensions.X == 0 && SampleDimensions.Y == 0 && SampleDimensions.Z == 0
		       && SampleMin.X <= FullDimensions.X && SampleMin.Y <= FullDimensions.Y && SampleMin.Z <= FullDimensions.Z;
	}

	return SampleMin.X < FullDimensions.X && SampleMin.Y < FullDimensions.Y && SampleMin.Z < FullDimensions.Z
	       && SampleDimensions.X <= FullDimensions.X - SampleMin.X
	       && SampleDimensions.Y <= FullDimensions.Y - SampleMin.Y
	       && SampleDimensions.Z <= FullDimensions.Z - SampleMin.Z;
}

bool IsValidSampledRegion(const FIntVector& FullDimensions, const FDualContourSampledRegion& Region)
{
	if (!IsValidSampleBounds(FullDimensions, Region.SampleMin, Region.SampleDimensions))
		return false;
	if (Region.SampleDimensions == FIntVector::ZeroValue)
		return Region.Chunks.IsEmpty();

	const FIntVector SampleMax = Region.SampleMin + Region.SampleDimensions;
	const FIntVector ChunkMin(Region.SampleMin.X / GDualContourChunkSize,
		Region.SampleMin.Y / GDualContourChunkSize, Region.SampleMin.Z / GDualContourChunkSize);
	const FIntVector ChunkMaxExclusive(FMath::DivideAndRoundUp(SampleMax.X, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleMax.Y, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleMax.Z, GDualContourChunkSize));
	TSet<FIntVector> UniqueChunkCoords;
	UniqueChunkCoords.Reserve(Region.Chunks.Num());
	const int32 ExpandedChunkSize = GDualContourChunkSize * GDualContourChunkSize * GDualContourChunkSize;
	for (const FDualContourSampledChunk& SampledChunk : Region.Chunks)
	{
		const FIntVector& Coord = SampledChunk.ChunkCoord;
		if (Coord.X < ChunkMin.X || Coord.Y < ChunkMin.Y || Coord.Z < ChunkMin.Z
		    || Coord.X >= ChunkMaxExclusive.X || Coord.Y >= ChunkMaxExclusive.Y || Coord.Z >= ChunkMaxExclusive.Z
		    || UniqueChunkCoords.Contains(Coord)
		    || (!SampledChunk.Density.IsUniform() && SampledChunk.Density.DensitySamples.Num() != ExpandedChunkSize))
		{
			return false;
		}
		UniqueChunkCoords.Add(Coord);
	}
	return true;
}
}

bool UDualContour::HasCurrentGeneratedData() const
{
	return !bRebuildRequired && LastBuiltCellCount.X == CellCount.X && LastBuiltCellCount.Y == CellCount.Y && LastBuiltCellCount.Z == CellCount.Z;
}

void UDualContour::PostLoad()
{
	Super::PostLoad();
	CompactAllDensityChunks();

	// CellChunks is derived entirely from the persistent density grid. Keeping it transient
	// avoids serializing the large nested map while preserving the existing runtime query API.
	CellChunks.Reset();
	if (HasCurrentGeneratedData() && !DensityChunks.IsEmpty())
		BuildCells();
}

void UDualContour::PreSave(FObjectPreSaveContext SaveContext)
{
	// Public mutation paths already compact their affected chunks. This full pass is a
	// safety net for legacy data and any future mutation path added without a flush.
	CompactAllDensityChunks();
	Super::PreSave(SaveContext);
}

bool UDualContour::ValidateGenerationSettings() const
{
	if (CellCount.X <= 0 || CellCount.Y <= 0 || CellCount.Z <= 0)
	{
		UE_LOG(LogDualContour, Error, TEXT("Rebuild aborted for %s: CellCount must be positive (current: %d, %d, %d)."),
			*GetNameSafe(GetOuter()), CellCount.X, CellCount.Y, CellCount.Z);
		return false;
	}
	if (CellSize <= 0.f)
	{
		UE_LOG(LogDualContour, Error, TEXT("Rebuild aborted for %s: CellSize must be greater than zero (current: %g)."),
			*GetNameSafe(GetOuter()), CellSize);
		return false;
	}

	// Each axis needs one additional density sample. Chunk-native samplers do not require
	// the complete grid to fit in one TArray, but the boundary addition must remain valid.
	if (CellCount.X >= MAX_int32 || CellCount.Y >= MAX_int32 || CellCount.Z >= MAX_int32)
	{
		UE_LOG(LogDualContour, Error, TEXT("Rebuild aborted for %s: CellCount is too large to add boundary samples."),
			*GetNameSafe(GetOuter()));
		return false;
	}
	return true;
}

bool UDualContour::Rebuild()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_Rebuild);
	bRebuildRequired = true;
	if (!ValidateGenerationSettings())
		return false;

	LastBuiltCellCount = CellCount;
	bRebuildRequired = false;
	BuildCells();
	return true;
}

bool UDualContour::CopyFrom(const UDualContour* Source)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_CopyFrom);
	if (!Source || !Source->HasCurrentGeneratedData())
		return false;
	if (Source == this)
		return true;

	Modify();
	CellCount = Source->CellCount;
	CellSize = Source->CellSize;
	VertexSolveMode = Source->VertexSolveMode;
	VertexRelaxation = Source->VertexRelaxation;
	RelaxationNormalCosine = Source->RelaxationNormalCosine;
	bRebuildRequired = Source->bRebuildRequired;
	DensityChunks = Source->DensityChunks;
	CellChunks = Source->CellChunks;
	LastBuiltCellCount = Source->LastBuiltCellCount;
	OnCellsRebuilt.Broadcast(FIntVector(0, 0, 0), CellCount);
	return true;
}

bool UDualContour::ReplaceDensityChunks(FDualContourSampledRegion&& SampledRegion)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_ReplaceDensityChunks);
	bRebuildRequired = true;
	if (!ValidateGenerationSettings() || !IsValidSampledRegion(GetSampleDimensions(), SampledRegion))
	{
		UE_LOG(LogDualContour, Error, TEXT("ReplaceDensityChunks aborted for %s because the sampled region is invalid."),
			*GetNameSafe(GetOuter()));
		return false;
	}

	const FIntVector SampleMin = SampledRegion.SampleMin;
	const FIntVector SampleDimensions = SampledRegion.SampleDimensions;
	const FIntVector SampleMax = SampleMin + SampleDimensions;
	DensityChunks.Empty(SampledRegion.Chunks.Num());
	for (FDualContourSampledChunk& SampledChunk : SampledRegion.Chunks)
	{
		SampledChunk.Density.TryCollapse();
		if (!SampledChunk.Density.IsUniform() || SampledChunk.Density.UniformValue != 0)
			DensityChunks.Add(SampledChunk.ChunkCoord, MoveTemp(SampledChunk.Density));
	}

	CellChunks.Reset();
	LastBuiltCellCount = CellCount;
	bRebuildRequired = false;
	if (SampleDimensions == FIntVector::ZeroValue)
	{
		OnCellsRebuilt.Broadcast(FIntVector::ZeroValue, CellCount);
	}
	else
	{
		const FIntVector CellRangeMin(FMath::Max(0, SampleMin.X - 1), FMath::Max(0, SampleMin.Y - 1),
			FMath::Max(0, SampleMin.Z - 1));
		const FIntVector CellRangeMax(FMath::Min(CellCount.X, SampleMax.X), FMath::Min(CellCount.Y, SampleMax.Y),
			FMath::Min(CellCount.Z, SampleMax.Z));
		RebuildCellsInRange(CellRangeMin, CellRangeMax);
	}
	return true;
}

bool UDualContour::ModifyDensityChunks(const FDualContourSampledRegion& SampledRegion, bool bExcavate,
	FIntVector& OutAffectedCellMin, FIntVector& OutAffectedCellMax)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_ModifyDensityChunks);
	OutAffectedCellMin = FIntVector::ZeroValue;
	OutAffectedCellMax = FIntVector::ZeroValue;
	const FIntVector FullSampleDimensions = GetSampleDimensions();
	if (!HasCurrentGeneratedData() || !IsValidSampledRegion(FullSampleDimensions, SampledRegion))
		return false;

	TSet<FIntVector> ModifiedChunks;
	FIntVector ModifiedSampleMin = FullSampleDimensions;
	FIntVector ModifiedSampleMax(-1, -1, -1);
	const FIntVector SampleMax = SampledRegion.SampleMin + SampledRegion.SampleDimensions;
	for (const FDualContourSampledChunk& SampledChunk : SampledRegion.Chunks)
	{
		if (SampledChunk.Density.IsUniform()
		    && (SampledChunk.Density.UniformValue == 0 || (bExcavate && SampledChunk.Density.UniformValue < GDualContourIsoValue)))
		{
			continue;
		}

		const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(SampledChunk.ChunkCoord);
		const FIntVector BuildMin(
			FMath::Max(SampledRegion.SampleMin.X, ChunkOrigin.X),
			FMath::Max(SampledRegion.SampleMin.Y, ChunkOrigin.Y),
			FMath::Max(SampledRegion.SampleMin.Z, ChunkOrigin.Z));
		const FIntVector BuildMax(
			FMath::Min(SampleMax.X, ChunkOrigin.X + GDualContourChunkSize),
			FMath::Min(SampleMax.Y, ChunkOrigin.Y + GDualContourChunkSize),
			FMath::Min(SampleMax.Z, ChunkOrigin.Z + GDualContourChunkSize));
		FDensityChunk* TargetChunk = DensityChunks.Find(SampledChunk.ChunkCoord);
		for (int32 SampleZ = BuildMin.Z; SampleZ < BuildMax.Z; ++SampleZ)
			for (int32 SampleY = BuildMin.Y; SampleY < BuildMax.Y; ++SampleY)
				for (int32 SampleX = BuildMin.X; SampleX < BuildMax.X; ++SampleX)
				{
					const uint16 LocalIndex = DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ);
					const int32 SamplerDensity = SampledChunk.Density.IsUniform()
						                             ? SampledChunk.Density.UniformValue
						                             : SampledChunk.Density.DensitySamples[LocalIndex];
					if (SamplerDensity == 0 || (bExcavate && SamplerDensity < GDualContourIsoValue))
						continue;

					const int32 OldDensity = !TargetChunk
						                         ? 0
						                         : (TargetChunk->IsUniform() ? TargetChunk->UniformValue : TargetChunk->DensitySamples[LocalIndex]);
					int32 NewDensity = FMath::Max(OldDensity, SamplerDensity);
					if (bExcavate)
					{
						const int32 DifferenceDensity = 2 * static_cast<int32>(GDualContourIsoValue) - SamplerDensity;
						NewDensity = FMath::Min(OldDensity, DifferenceDensity);
					}
					const uint8 ClampedDensity = static_cast<uint8>(FMath::Clamp(NewDensity, 0, 255));
					if (ClampedDensity == OldDensity)
						continue;

					if (!TargetChunk)
						TargetChunk = &DensityChunks.FindOrAdd(SampledChunk.ChunkCoord);
					TargetChunk->Expand();
					TargetChunk->DensitySamples[LocalIndex] = ClampedDensity;
					ModifiedChunks.Add(SampledChunk.ChunkCoord);
					ModifiedSampleMin.X = FMath::Min(ModifiedSampleMin.X, SampleX);
					ModifiedSampleMin.Y = FMath::Min(ModifiedSampleMin.Y, SampleY);
					ModifiedSampleMin.Z = FMath::Min(ModifiedSampleMin.Z, SampleZ);
					ModifiedSampleMax.X = FMath::Max(ModifiedSampleMax.X, SampleX);
					ModifiedSampleMax.Y = FMath::Max(ModifiedSampleMax.Y, SampleY);
					ModifiedSampleMax.Z = FMath::Max(ModifiedSampleMax.Z, SampleZ);
				}
	}
	if (ModifiedChunks.IsEmpty())
		return false;
	CompactDensityChunks(ModifiedChunks);

	const FIntVector CellRangeMin(FMath::Max(0, ModifiedSampleMin.X - 1), FMath::Max(0, ModifiedSampleMin.Y - 1),
		FMath::Max(0, ModifiedSampleMin.Z - 1));
	const FIntVector CellRangeMax(FMath::Min(CellCount.X, ModifiedSampleMax.X + 1),
		FMath::Min(CellCount.Y, ModifiedSampleMax.Y + 1), FMath::Min(CellCount.Z, ModifiedSampleMax.Z + 1));
	RebuildCellsInRange(CellRangeMin, CellRangeMax);
	OutAffectedCellMin = CellRangeMin;
	OutAffectedCellMax = CellRangeMax;
	return true;
}

FDualContourEditBatch UDualContour::BeginEditBatch() const
{
	FDualContourEditBatch Batch;
	Batch.bOpen = HasCurrentGeneratedData();
	Batch.Owner = Batch.bOpen ? const_cast<UDualContour*>(this) : nullptr;
	return Batch;
}

bool UDualContour::EndEditBatch(FDualContourEditBatch& Batch, FDualContourEditResult& OutResult)
{
	OutResult = FDualContourEditResult();
	if (!Batch.bOpen || Batch.Owner != this)
		return false;
	Batch.bOpen = false;
	Batch.Owner = nullptr;

	TSet<FIntVector> ActuallyDirtyChunks;
	for (const TPair<FIntVector, TMap<uint16, FDualContourPendingSample>>& ChunkPair : Batch.ChunkSamples)
	{
		const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkPair.Key);
		for (const TPair<uint16, FDualContourPendingSample>& SamplePair : ChunkPair.Value)
		{
			const uint8 After = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(SamplePair.Value.WorkingValue), 0, 255));
			if (After == SamplePair.Value.Before)
				continue;
			const FIntVector SampleCoord = ChunkOrigin + DualContourUtils::ChunkLocalCoord(SamplePair.Key);
			WriteDensitySample(SampleCoord.X, SampleCoord.Y, SampleCoord.Z, After, ActuallyDirtyChunks);
			FDualContourSampleDelta& Delta = OutResult.Deltas.AddDefaulted_GetRef();
			Delta.SampleCoord = SampleCoord;
			Delta.Before = SamplePair.Value.Before;
			Delta.After = After;
		}
	}
	Batch.ChunkSamples.Reset();
	if (OutResult.Deltas.IsEmpty())
		return false;

	CompactDensityChunks(ActuallyDirtyChunks);
	RebuildDirtyCellChunks(ActuallyDirtyChunks, OutResult.DirtyRegion);
	return true;
}

bool UDualContour::ApplyEditDeltas(TConstArrayView<FDualContourSampleDelta> Deltas, bool bUseAfterValues, FDualContourEditResult* OutResult)
{
	if (!HasCurrentGeneratedData() || Deltas.IsEmpty())
		return false;
	TSet<FIntVector> DirtyChunks;
	for (const FDualContourSampleDelta& Delta : Deltas)
		WriteDensitySample(Delta.SampleCoord.X, Delta.SampleCoord.Y, Delta.SampleCoord.Z, bUseAfterValues ? Delta.After : Delta.Before, DirtyChunks);
	if (DirtyChunks.IsEmpty())
		return false;
	CompactDensityChunks(DirtyChunks);
	FDualContourDirtyRegion DirtyRegion;
	RebuildDirtyCellChunks(DirtyChunks, DirtyRegion);
	if (OutResult)
	{
		OutResult->DirtyRegion = MoveTemp(DirtyRegion);
		OutResult->Deltas = TArray<FDualContourSampleDelta>(Deltas);
	}
	return true;
}

void UDualContour::WriteDensitySample(int32 SampleX, int32 SampleY, int32 SampleZ, uint8 Density, TSet<FIntVector>& DirtyChunks)
{
	const FIntVector SampleDims = GetSampleDimensions();
	if (!DualContourUtils::IsValidCoordinate(SampleDims, SampleX, SampleY, SampleZ) || GetDensity(SampleX, SampleY, SampleZ) == Density)
		return;
	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(SampleX, SampleY, SampleZ);
	FDensityChunk& Chunk = DensityChunks.FindOrAdd(ChunkCoord);
	Chunk.Expand();
	Chunk.DensitySamples[DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ)] = Density;
	DirtyChunks.Add(ChunkCoord);
}

void UDualContour::RebuildDirtyCellChunks(const TSet<FIntVector>& DirtyDensityChunks, FDualContourDirtyRegion& OutDirtyRegion)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_RebuildDirtyCellChunks);
	check(IsInGameThread());
	OutDirtyRegion.DensityChunks.Append(DirtyDensityChunks);
	for (const FIntVector& DensityChunk : DirtyDensityChunks)
	{
		const FIntVector SampleMin = DualContourUtils::ChunkOrigin(DensityChunk);
		const FIntVector SampleMax(FMath::Min(CellCount.X, SampleMin.X + GDualContourChunkSize),
			FMath::Min(CellCount.Y, SampleMin.Y + GDualContourChunkSize), FMath::Min(CellCount.Z, SampleMin.Z + GDualContourChunkSize));
		const FIntVector CellMin(FMath::Max(0, SampleMin.X - 2), FMath::Max(0, SampleMin.Y - 2), FMath::Max(0, SampleMin.Z - 2));
		const FIntVector CellMax(FMath::Min(CellCount.X, SampleMax.X + 2), FMath::Min(CellCount.Y, SampleMax.Y + 2),
			FMath::Min(CellCount.Z, SampleMax.Z + 2));
		for (int32 ChunkZ = CellMin.Z / GDualContourChunkSize; ChunkZ <= (CellMax.Z - 1) / GDualContourChunkSize; ++ChunkZ)
			for (int32 ChunkY = CellMin.Y / GDualContourChunkSize; ChunkY <= (CellMax.Y - 1) / GDualContourChunkSize; ++ChunkY)
				for (int32 ChunkX = CellMin.X / GDualContourChunkSize; ChunkX <= (CellMax.X - 1) / GDualContourChunkSize; ++ChunkX)
					OutDirtyRegion.CellChunks.Add(FIntVector(ChunkX, ChunkY, ChunkZ));
	}

	if (OutDirtyRegion.CellChunks.IsEmpty())
		return;

	TArray<FIntVector> ChunkCoords = OutDirtyRegion.CellChunks.Array();
	TArray<FCellChunk> BuiltChunks;
	BuiltChunks.SetNum(ChunkCoords.Num());
	ParallelFor(TEXT("DualContour.BuildDirtyCellChunks"), ChunkCoords.Num(), 1,
		[this, &ChunkCoords, &BuiltChunks](int32 Index)
		{
			const FIntVector ChunkCoord = ChunkCoords[Index];
			const FIntVector BuildMin(ChunkCoord.X * GDualContourChunkSize,
				ChunkCoord.Y * GDualContourChunkSize, ChunkCoord.Z * GDualContourChunkSize);
			const FIntVector BuildMax(FMath::Min(CellCount.X, BuildMin.X + GDualContourChunkSize),
				FMath::Min(CellCount.Y, BuildMin.Y + GDualContourChunkSize),
				FMath::Min(CellCount.Z, BuildMin.Z + GDualContourChunkSize));
			FCellChunk& BuiltChunk = BuiltChunks[Index];
			for (int32 CellZ = BuildMin.Z; CellZ < BuildMax.Z; ++CellZ)
				for (int32 CellY = BuildMin.Y; CellY < BuildMax.Y; ++CellY)
					for (int32 CellX = BuildMin.X; CellX < BuildMax.X; ++CellX)
					{
						FDualContourCell Cell = BuildNewCell(CellX, CellY, CellZ);
						if (Cell.bActive)
							BuiltChunk.ActiveCells.Add(DualContourUtils::ChunkLocalIndex(CellX, CellY, CellZ), MoveTemp(Cell));
					}
		}, EParallelForFlags::Unbalanced);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_MergeDirtyCellChunks);
		for (int32 Index = 0; Index < ChunkCoords.Num(); ++Index)
		{
			if (BuiltChunks[Index].ActiveCells.IsEmpty())
				CellChunks.Remove(ChunkCoords[Index]);
			else
				CellChunks.Add(ChunkCoords[Index], MoveTemp(BuiltChunks[Index]));
		}
	}

	OnDirtyChunksRebuilt.Broadcast(OutDirtyRegion);
}

uint8 UDualContour::GetDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const
{
	const FIntVector SampleDimensions = GetSampleDimensions();
	if (!DualContourUtils::IsValidCoordinate(SampleDimensions, SampleX, SampleY, SampleZ))
		return 0;

	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(SampleX, SampleY, SampleZ);
	const FDensityChunk* Chunk = DensityChunks.Find(ChunkCoord);
	if (!Chunk)
		return 0;
	if (Chunk->IsUniform())
		return Chunk->UniformValue;

	return Chunk->DensitySamples[DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ)];
}

void UDualContour::CompactAllDensityChunks()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_CompactAllDensityChunks);
	for (auto ChunkIt = DensityChunks.CreateIterator(); ChunkIt; ++ChunkIt)
	{
		FDensityChunk& Chunk = ChunkIt.Value();
		if (Chunk.TryCollapse() && Chunk.UniformValue == 0)
			ChunkIt.RemoveCurrent();
	}
	DensityChunks.Compact();
}

void UDualContour::CompactDensityChunks(const TSet<FIntVector>& ChunkCoords)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_CompactDensityChunks);
	for (const FIntVector& ChunkCoord : ChunkCoords)
	{
		FDensityChunk* Chunk = DensityChunks.Find(ChunkCoord);
		if (Chunk && Chunk->TryCollapse() && Chunk->UniformValue == 0)
			DensityChunks.Remove(ChunkCoord);
	}
}

const FDualContourCell* UDualContour::GetCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	if (!DualContourUtils::IsValidCoordinate(CellCount, CellX, CellY, CellZ))
		return nullptr;

	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(CellX, CellY, CellZ);
	const FCellChunk* Chunk = CellChunks.Find(ChunkCoord);
	return Chunk ? Chunk->ActiveCells.Find(DualContourUtils::ChunkLocalIndex(CellX, CellY, CellZ)) : nullptr;
}

bool UDualContour::HasActiveCellInRange(FIntVector CellMin, FIntVector CellMax) const
{
	const int32 ChunkMinX = CellMin.X / GDualContourChunkSize;
	const int32 ChunkMinY = CellMin.Y / GDualContourChunkSize;
	const int32 ChunkMinZ = CellMin.Z / GDualContourChunkSize;
	const int32 ChunkMaxX = (CellMax.X - 1) / GDualContourChunkSize;
	const int32 ChunkMaxY = (CellMax.Y - 1) / GDualContourChunkSize;
	const int32 ChunkMaxZ = (CellMax.Z - 1) / GDualContourChunkSize;

	for (int32 ChunkZ = ChunkMinZ; ChunkZ <= ChunkMaxZ; ++ChunkZ)
		for (int32 ChunkY = ChunkMinY; ChunkY <= ChunkMaxY; ++ChunkY)
			for (int32 ChunkX = ChunkMinX; ChunkX <= ChunkMaxX; ++ChunkX)
			{
				const FCellChunk* Chunk = CellChunks.Find(FIntVector(ChunkX, ChunkY, ChunkZ));
				if (!Chunk)
					continue;
				for (const TPair<uint16, FDualContourCell>& Pair : Chunk->ActiveCells)
				{
					const FIntVector CellCoord = DualContourUtils::ChunkOrigin(FIntVector(ChunkX, ChunkY, ChunkZ))
						+ DualContourUtils::ChunkLocalCoord(Pair.Key);
					if (CellCoord.X >= CellMin.X && CellCoord.X < CellMax.X && CellCoord.Y >= CellMin.Y && CellCoord.Y < CellMax.Y
					    && CellCoord.Z >= CellMin.Z && CellCoord.Z < CellMax.Z)
						return true;
				}
			}
	return false;
}

float UDualContour::TrilinearDensity(const FVector& GridPos) const
{
	const FIntVector Dims = GetSampleDimensions();
	const float GridX = FMath::Clamp(GridPos.X, 0., static_cast<double>(Dims.X - 1));
	const float GridY = FMath::Clamp(GridPos.Y, 0., static_cast<double>(Dims.Y - 1));
	const float GridZ = FMath::Clamp(GridPos.Z, 0., static_cast<double>(Dims.Z - 1));
	const int32 LowerX = FMath::Clamp(FMath::FloorToInt(GridX), 0, Dims.X - 2);
	const int32 LowerY = FMath::Clamp(FMath::FloorToInt(GridY), 0, Dims.Y - 2);
	const int32 LowerZ = FMath::Clamp(FMath::FloorToInt(GridZ), 0, Dims.Z - 2);
	const int32 UpperX = LowerX + 1, UpperY = LowerY + 1, UpperZ = LowerZ + 1;
	const float BlendX = GridX - LowerX, BlendY = GridY - LowerY, BlendZ = GridZ - LowerZ;

	return FMath::Lerp(
		FMath::Lerp(FMath::Lerp(static_cast<float>(GetDensity(LowerX, LowerY, LowerZ)), static_cast<float>(GetDensity(UpperX, LowerY, LowerZ)),
				BlendX),
			FMath::Lerp(static_cast<float>(GetDensity(LowerX, UpperY, LowerZ)), static_cast<float>(GetDensity(UpperX, UpperY, LowerZ)), BlendX),
			BlendY),
		FMath::Lerp(FMath::Lerp(static_cast<float>(GetDensity(LowerX, LowerY, UpperZ)), static_cast<float>(GetDensity(UpperX, LowerY, UpperZ)),
				BlendX),
			FMath::Lerp(static_cast<float>(GetDensity(LowerX, UpperY, UpperZ)), static_cast<float>(GetDensity(UpperX, UpperY, UpperZ)), BlendX),
			BlendY), BlendZ);
}

FVector UDualContour::ComputeGradient(const FVector& GridPos) const
{
	constexpr float Step = 0.5f;
	return FVector(
		TrilinearDensity(GridPos + FVector(Step, 0, 0)) - TrilinearDensity(GridPos - FVector(Step, 0, 0)),
		TrilinearDensity(GridPos + FVector(0, Step, 0)) - TrilinearDensity(GridPos - FVector(0, Step, 0)),
		TrilinearDensity(GridPos + FVector(0, 0, Step)) - TrilinearDensity(GridPos - FVector(0, 0, Step)));
}

void UDualContour::BuildCells()
{
	CellChunks.Reset();
	RebuildCellsInRange(FIntVector(0, 0, 0), CellCount);
}

FDualContourCell UDualContour::BuildNewCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	FDualContourCell Cell;
	const FVector CellMin = FVector(CellX, CellY, CellZ) * CellSize;
	const FVector CellMax = FVector(CellX + 1, CellY + 1, CellZ + 1) * CellSize;
	const FVector CellCenter = (CellMin + CellMax) * 0.5;
	Cell.Center = CellCenter;

	bool bHasInside = false, bHasOutside = false;
	for (int32 Z = 0; Z <= 1; ++Z)
		for (int32 Y = 0; Y <= 1; ++Y)
			for (int32 X = 0; X <= 1; ++X)
				(GetDensity(CellX + X, CellY + Y, CellZ + Z) >= GDualContourIsoValue ? bHasInside : bHasOutside) = true;
	Cell.bActive = bHasInside && bHasOutside;
	if (!Cell.bActive)
		return Cell;

	const bool bUseQEF = VertexSolveMode == EDualContourVertexSolveMode::QEF;
	constexpr double Lambda = 0.1;
	double Matrix[3][3] = {};
	double Vector[3] = {};
	FVector AccumNormal = FVector::ZeroVector;
	FVector IntersectionSum = FVector::ZeroVector;
	int32 NumIntersections = 0;
	for (int32 EdgeIndex = 0; EdgeIndex < 12; ++EdgeIndex)
	{
		const int32* A = EdgeCorners[EdgeIndex][0];
		const int32* B = EdgeCorners[EdgeIndex][1];
		const int32 AX = CellX + A[0], AY = CellY + A[1], AZ = CellZ + A[2];
		const int32 BX = CellX + B[0], BY = CellY + B[1], BZ = CellZ + B[2];
		const int32 DensityA = GetDensity(AX, AY, AZ);
		const int32 DensityB = GetDensity(BX, BY, BZ);
		if ((DensityA < GDualContourIsoValue) == (DensityB < GDualContourIsoValue))
			continue;

		const float Alpha = (static_cast<float>(GDualContourIsoValue) - DensityA) / (DensityB - DensityA);
		const FVector GridPosition = FVector(AX, AY, AZ) + Alpha * (FVector(BX, BY, BZ) - FVector(AX, AY, AZ));
		const FVector WorldPosition = GridPosition * CellSize;
		const FVector Normal = (-ComputeGradient(GridPosition)).GetSafeNormal();
		if (Normal.IsNearlyZero())
			continue;

		if (bUseQEF)
		{
			const double NX = Normal.X, NY = Normal.Y, NZ = Normal.Z;
			const double Distance = FVector::DotProduct(Normal, WorldPosition);
			Matrix[0][0] += NX * NX;
			Matrix[0][1] += NX * NY;
			Matrix[0][2] += NX * NZ;
			Matrix[1][0] += NY * NX;
			Matrix[1][1] += NY * NY;
			Matrix[1][2] += NY * NZ;
			Matrix[2][0] += NZ * NX;
			Matrix[2][1] += NZ * NY;
			Matrix[2][2] += NZ * NZ;
			Vector[0] += NX * Distance;
			Vector[1] += NY * Distance;
			Vector[2] += NZ * Distance;
		}
		else
		{
			IntersectionSum += WorldPosition;
		}
		AccumNormal += Normal;
		++NumIntersections;
	}

	if (NumIntersections > 0)
	{
		if (bUseQEF)
		{
			Matrix[0][0] += Lambda;
			Matrix[1][1] += Lambda;
			Matrix[2][2] += Lambda;
			Vector[0] += Lambda * CellCenter.X;
			Vector[1] += Lambda * CellCenter.Y;
			Vector[2] += Lambda * CellCenter.Z;
			double Position[3];
			if (Solve3x3(Matrix, Vector, Position))
			{
				Cell.Center.X = FMath::Clamp(Position[0], CellMin.X, CellMax.X);
				Cell.Center.Y = FMath::Clamp(Position[1], CellMin.Y, CellMax.Y);
				Cell.Center.Z = FMath::Clamp(Position[2], CellMin.Z, CellMax.Z);
			}
		}
		else
		{
			Cell.Center = IntersectionSum / NumIntersections;
		}
		Cell.Normal = AccumNormal.GetSafeNormal();
		if (Cell.Normal.IsNearlyZero())
			Cell.Normal = FVector::UpVector;
	}
	return Cell;
}

void UDualContour::RebuildCellsInRange(FIntVector RangeMin, FIntVector RangeMax)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_RebuildCellsInRange);
	check(IsInGameThread());
	RangeMin = FIntVector(FMath::Max(0, RangeMin.X), FMath::Max(0, RangeMin.Y), FMath::Max(0, RangeMin.Z));
	RangeMax = FIntVector(FMath::Min(CellCount.X, RangeMax.X), FMath::Min(CellCount.Y, RangeMax.Y), FMath::Min(CellCount.Z, RangeMax.Z));
	const int64 ProcessedCellCount = static_cast<int64>(FMath::Max(0, RangeMax.X - RangeMin.X))
	                                 * FMath::Max(0, RangeMax.Y - RangeMin.Y) * FMath::Max(0, RangeMax.Z - RangeMin.Z);
	if (ProcessedCellCount == 0)
	{
		return;
	}

	// The game thread remains blocked in ParallelFor, so worker threads can safely perform
	// const queries on this object while writing only to their own output slots.
	const FIntVector ChunkMin(RangeMin.X / GDualContourChunkSize, RangeMin.Y / GDualContourChunkSize,
		RangeMin.Z / GDualContourChunkSize);
	const FIntVector ChunkMaxExclusive(FMath::DivideAndRoundUp(RangeMax.X, GDualContourChunkSize),
		FMath::DivideAndRoundUp(RangeMax.Y, GDualContourChunkSize),
		FMath::DivideAndRoundUp(RangeMax.Z, GDualContourChunkSize));

	TArray<FIntVector> ChunkCoords;
	for (int32 ChunkZ = ChunkMin.Z; ChunkZ < ChunkMaxExclusive.Z; ++ChunkZ)
		for (int32 ChunkY = ChunkMin.Y; ChunkY < ChunkMaxExclusive.Y; ++ChunkY)
			for (int32 ChunkX = ChunkMin.X; ChunkX < ChunkMaxExclusive.X; ++ChunkX)
				ChunkCoords.Emplace(ChunkX, ChunkY, ChunkZ);

	TArray<FCellChunk> BuiltChunks;
	BuiltChunks.SetNum(ChunkCoords.Num());

	ParallelFor(TEXT("DualContour.BuildCellChunks"), ChunkCoords.Num(), 1,
		[this, RangeMin, RangeMax, &ChunkCoords, &BuiltChunks](int32 Index)
		{
			const FIntVector ChunkCoord = ChunkCoords[Index];
			const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkCoord);
			const FIntVector BuildMin(FMath::Max(RangeMin.X, ChunkOrigin.X),
				FMath::Max(RangeMin.Y, ChunkOrigin.Y), FMath::Max(RangeMin.Z, ChunkOrigin.Z));
			const FIntVector BuildMax(FMath::Min(RangeMax.X, ChunkOrigin.X + GDualContourChunkSize),
				FMath::Min(RangeMax.Y, ChunkOrigin.Y + GDualContourChunkSize),
				FMath::Min(RangeMax.Z, ChunkOrigin.Z + GDualContourChunkSize));

			FCellChunk& BuiltChunk = BuiltChunks[Index];
			for (int32 CellZ = BuildMin.Z; CellZ < BuildMax.Z; ++CellZ)
				for (int32 CellY = BuildMin.Y; CellY < BuildMax.Y; ++CellY)
					for (int32 CellX = BuildMin.X; CellX < BuildMax.X; ++CellX)
					{
						FDualContourCell Cell = BuildNewCell(CellX, CellY, CellZ);
						if (!Cell.bActive)
							continue;
						const uint16 LocalKey = DualContourUtils::ChunkLocalIndex(CellX, CellY, CellZ);
						BuiltChunk.ActiveCells.Add(LocalKey, MoveTemp(Cell));
					}
		}, EParallelForFlags::Unbalanced);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_MergeCellChunks);
		for (int32 Index = 0; Index < ChunkCoords.Num(); ++Index)
		{
			const FIntVector ChunkCoord = ChunkCoords[Index];
			const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkCoord);

			if (FCellChunk* ExistingChunk = CellChunks.Find(ChunkCoord))
			{
				for (auto CellIt = ExistingChunk->ActiveCells.CreateIterator(); CellIt; ++CellIt)
				{
					const uint16 LocalKey = CellIt.Key();
					const FIntVector CellCoord = ChunkOrigin + DualContourUtils::ChunkLocalCoord(LocalKey);
					if (CellCoord.X >= RangeMin.X && CellCoord.X < RangeMax.X && CellCoord.Y >= RangeMin.Y && CellCoord.Y < RangeMax.Y
					    && CellCoord.Z >= RangeMin.Z && CellCoord.Z < RangeMax.Z)
						CellIt.RemoveCurrent();
				}
			}

			FCellChunk& BuiltChunk = BuiltChunks[Index];
			if (!BuiltChunk.ActiveCells.IsEmpty())
			{
				FCellChunk& TargetChunk = CellChunks.FindOrAdd(ChunkCoord);
				for (TPair<uint16, FDualContourCell>& CellPair : BuiltChunk.ActiveCells)
					TargetChunk.ActiveCells.Add(CellPair.Key, MoveTemp(CellPair.Value));
			}

			const FCellChunk* UpdatedChunk = CellChunks.Find(ChunkCoord);
			if (UpdatedChunk && UpdatedChunk->ActiveCells.IsEmpty())
				CellChunks.Remove(ChunkCoord);
		}
	}

	const float Relaxation = FMath::Clamp(VertexRelaxation, 0.0f, 1.0f);
	if (Relaxation > 0.0f && RangeMin.X == 0 && RangeMin.Y == 0 && RangeMin.Z == 0
	    && RangeMax.X == CellCount.X && RangeMax.Y == CellCount.Y && RangeMax.Z == CellCount.Z)
	{
		const float MinimumNormalCosine = FMath::Clamp(RelaxationNormalCosine, -1.0f, 1.0f);
		TMap<FIntVector, FVector> RelaxedCenters;
		for (const TPair<FIntVector, FCellChunk>& ChunkPair : CellChunks)
		{
			const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkPair.Key);
			for (const TPair<uint16, FDualContourCell>& CellPair : ChunkPair.Value.ActiveCells)
			{
				const FIntVector CellCoord = ChunkOrigin + DualContourUtils::ChunkLocalCoord(CellPair.Key);
				FVector Sum = FVector::ZeroVector;
				int32 Count = 0;
				for (const FIntVector& Offset : {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0),
				                                 FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1)})
				{
					if (const FDualContourCell* Neighbor = GetCell(CellCoord.X + Offset.X, CellCoord.Y + Offset.Y, CellCoord.Z + Offset.Z))
					{
						if (FVector::DotProduct(CellPair.Value.Normal, Neighbor->Normal) < MinimumNormalCosine)
							continue;
						Sum += Neighbor->Center;
						++Count;
					}
				}
				if (Count > 0)
				{
					// Relaxing in 3D shrinks the surface and pulls vertices across cap/wall features.
					// Keep only the displacement tangent to this cell's Hermite normal so the pass
					// smooths neighbouring cells without moving the surface along its normal.
					FVector Delta = Sum / Count - CellPair.Value.Center;
					Delta -= CellPair.Value.Normal * FVector::DotProduct(Delta, CellPair.Value.Normal);
					RelaxedCenters.Add(CellCoord, CellPair.Value.Center + Delta * Relaxation);
				}
			}
		}
		for (const TPair<FIntVector, FVector>& Pair : RelaxedCenters)
			if (FDualContourCell* Cell = const_cast<FDualContourCell*>(GetCell(Pair.Key.X, Pair.Key.Y, Pair.Key.Z)))
				Cell->Center = Pair.Value;
	}
	if (RangeMin.X < RangeMax.X && RangeMin.Y < RangeMax.Y && RangeMin.Z < RangeMax.Z)
		OnCellsRebuilt.Broadcast(RangeMin, RangeMax);
}

#if WITH_EDITOR
void UDualContour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty
		                                 ? PropertyChangedEvent.MemberProperty->GetFName()
		                                 : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellCount)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellSize)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, VertexSolveMode)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, VertexRelaxation)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, RelaxationNormalCosine))
	{
		bRebuildRequired = true;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UDualContour::PostEditUndo()
{
	Super::PostEditUndo();
	if (HasCurrentGeneratedData())
		OnCellsRebuilt.Broadcast(FIntVector(0, 0, 0), CellCount);
}
#endif
