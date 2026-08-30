#include "DualContour.h"
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

bool SampleVolumeMatches(const FVectorInt& Dimensions, int32 NumSamples)
{
	if (Dimensions.X > MAX_int32 / Dimensions.Y)
		return false;
	const int64 Area = static_cast<int64>(Dimensions.X) * Dimensions.Y;
	return Area <= MAX_int32 / Dimensions.Z && Area * Dimensions.Z == NumSamples;
}

bool IsValidSampleRange(const FVectorInt& FullDimensions, const FVectorInt& SampleMin, const FVectorInt& SampleDimensions, int32 NumSamples)
{
	if (SampleMin.X < 0 || SampleMin.Y < 0 || SampleMin.Z < 0
	    || SampleDimensions.X < 0 || SampleDimensions.Y < 0 || SampleDimensions.Z < 0)
		return false;

	const bool bEmpty = SampleDimensions.X == 0 || SampleDimensions.Y == 0 || SampleDimensions.Z == 0;
	if (bEmpty)
	{
		return SampleDimensions.X == 0 && SampleDimensions.Y == 0 && SampleDimensions.Z == 0
		       && SampleMin.X <= FullDimensions.X && SampleMin.Y <= FullDimensions.Y && SampleMin.Z <= FullDimensions.Z
		       && NumSamples == 0;
	}

	return SampleMin.X < FullDimensions.X && SampleMin.Y < FullDimensions.Y && SampleMin.Z < FullDimensions.Z
	       && SampleDimensions.X <= FullDimensions.X - SampleMin.X
	       && SampleDimensions.Y <= FullDimensions.Y - SampleMin.Y
	       && SampleDimensions.Z <= FullDimensions.Z - SampleMin.Z
	       && SampleVolumeMatches(SampleDimensions, NumSamples);
}
}

uint16 UDualContour::PackLocalContourKey(int32 CellX, int32 CellY, int32 CellZ)
{
	return static_cast<uint16>((CellX % GDualContourChunkSize) | ((CellY % GDualContourChunkSize) << 4) | ((CellZ % GDualContourChunkSize) << 8));
}

bool UDualContour::HasCurrentGeneratedData() const
{
	return !bRebuildRequired && LastBuiltCellCount.X == CellCount.X && LastBuiltCellCount.Y == CellCount.Y && LastBuiltCellCount.Z == CellCount.Z;
}

void UDualContour::PostLoad()
{
	Super::PostLoad();
	CompactAllDensityChunks();

	// ContourChunks is derived entirely from the persistent density grid. Keeping it transient
	// avoids serializing the large nested map while preserving the existing runtime query API.
	ContourChunks.Reset();
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

	// Each axis needs one additional density sample. Full-grid sample products may exceed
	// TArray capacity because range-based samplers no longer allocate the whole grid.
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
	ContourChunks = Source->ContourChunks;
	LastBuiltCellCount = Source->LastBuiltCellCount;
	OnCellsRebuilt.Broadcast(FVectorInt(0, 0, 0), CellCount);
	return true;
}

bool UDualContour::ReplaceDensitySamples(const TArray<uint8>& Samples)
{
	return ReplaceDensitySamplesInRange(FVectorInt(0, 0, 0), GetSampleDims(), Samples);
}

bool UDualContour::ReplaceDensitySamplesInRange(FVectorInt SampleMin, FVectorInt SampleDimensions, TConstArrayView<uint8> Samples)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_ReplaceDensitySamplesInRange);
	bRebuildRequired = true;
	if (!ValidateGenerationSettings())
		return false;

	const FVectorInt FullSampleDimensions = GetSampleDims();
	if (!IsValidSampleRange(FullSampleDimensions, SampleMin, SampleDimensions, Samples.Num()))
	{
		UE_LOG(LogDualContour, Error,
			TEXT("SetDensitySamplesInRange aborted for %s: invalid min (%d, %d, %d), dimensions (%d, %d, %d), or sample count %d."),
			*GetNameSafe(GetOuter()), SampleMin.X, SampleMin.Y, SampleMin.Z,
			SampleDimensions.X, SampleDimensions.Y, SampleDimensions.Z, Samples.Num());
		return false;
	}

	const FVectorInt SampleMax(SampleMin.X + SampleDimensions.X, SampleMin.Y + SampleDimensions.Y, SampleMin.Z + SampleDimensions.Z);
	TArray<FIntVector> DensityChunkCoords;
	if (!Samples.IsEmpty())
	{
		const FIntVector ChunkMin(SampleMin.X / GDualContourChunkSize, SampleMin.Y / GDualContourChunkSize, SampleMin.Z / GDualContourChunkSize);
		const FIntVector ChunkMaxExclusive(FMath::DivideAndRoundUp(SampleMax.X, GDualContourChunkSize),
			FMath::DivideAndRoundUp(SampleMax.Y, GDualContourChunkSize),
			FMath::DivideAndRoundUp(SampleMax.Z, GDualContourChunkSize));
		for (int32 ChunkZ = ChunkMin.Z; ChunkZ < ChunkMaxExclusive.Z; ++ChunkZ)
			for (int32 ChunkY = ChunkMin.Y; ChunkY < ChunkMaxExclusive.Y; ++ChunkY)
				for (int32 ChunkX = ChunkMin.X; ChunkX < ChunkMaxExclusive.X; ++ChunkX)
					DensityChunkCoords.Emplace(ChunkX, ChunkY, ChunkZ);
	}

	TArray<FDensityChunk> BuiltDensityChunks;
	BuiltDensityChunks.SetNum(DensityChunkCoords.Num());
	// Each worker owns one output chunk. Samples is read-only and ParallelFor blocks until
	// all workers finish, so neither the source view nor DensityChunks needs synchronization.
	ParallelFor(TEXT("DualContour.PopulateDensityChunks"), DensityChunkCoords.Num(), 1,
		[SampleMin, SampleMax, SampleDimensions, Samples, &DensityChunkCoords,
			&BuiltDensityChunks](int32 Index)
		{
			const FIntVector ChunkCoord = DensityChunkCoords[Index];
			const FVectorInt ChunkOrigin(ChunkCoord.X * GDualContourChunkSize,
				ChunkCoord.Y * GDualContourChunkSize, ChunkCoord.Z * GDualContourChunkSize);
			const FVectorInt BuildMin(FMath::Max(SampleMin.X, ChunkOrigin.X),
				FMath::Max(SampleMin.Y, ChunkOrigin.Y), FMath::Max(SampleMin.Z, ChunkOrigin.Z));
			const FVectorInt BuildMax(FMath::Min(SampleMax.X, ChunkOrigin.X + GDualContourChunkSize),
				FMath::Min(SampleMax.Y, ChunkOrigin.Y + GDualContourChunkSize),
				FMath::Min(SampleMax.Z, ChunkOrigin.Z + GDualContourChunkSize));

			FDensityChunk& BuiltChunk = BuiltDensityChunks[Index];
			bool bExpanded = false;
			for (int32 SampleZ = BuildMin.Z; SampleZ < BuildMax.Z; ++SampleZ)
				for (int32 SampleY = BuildMin.Y; SampleY < BuildMax.Y; ++SampleY)
					for (int32 SampleX = BuildMin.X; SampleX < BuildMax.X; ++SampleX)
					{
						const uint8 Density = Samples[SampleDimensions.LinearIndex(
							SampleX - SampleMin.X, SampleY - SampleMin.Y, SampleZ - SampleMin.Z)];
						if (Density == 0)
							continue;

						if (!bExpanded)
						{
							BuiltChunk.Expand();
							bExpanded = true;
						}
						const int32 LocalX = SampleX - ChunkOrigin.X;
						const int32 LocalY = SampleY - ChunkOrigin.Y;
						const int32 LocalZ = SampleZ - ChunkOrigin.Z;
						BuiltChunk.DensitySamples[LocalX
						                          + LocalY * GDualContourChunkSize
						                          + LocalZ * GDualContourChunkSize * GDualContourChunkSize] = Density;
					}
		}, EParallelForFlags::Unbalanced);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_MergeDensityChunks);
		DensityChunks.Reset();
		for (int32 Index = 0; Index < DensityChunkCoords.Num(); ++Index)
		{
			FDensityChunk& BuiltChunk = BuiltDensityChunks[Index];
			if (!BuiltChunk.IsUniform())
				DensityChunks.Add(DensityChunkCoords[Index], MoveTemp(BuiltChunk));
		}
	}
	CompactAllDensityChunks();

	ContourChunks.Reset();
	LastBuiltCellCount = CellCount;
	bRebuildRequired = false;
	if (Samples.IsEmpty())
	{
		OnCellsRebuilt.Broadcast(FVectorInt(0, 0, 0), CellCount);
	}
	else
	{
		const FVectorInt CellRangeMin(FMath::Max(0, SampleMin.X - 1), FMath::Max(0, SampleMin.Y - 1),
			FMath::Max(0, SampleMin.Z - 1));
		const FVectorInt CellRangeMax(FMath::Min(CellCount.X, SampleMax.X), FMath::Min(CellCount.Y, SampleMax.Y),
			FMath::Min(CellCount.Z, SampleMax.Z));
		RebuildCellsInRange(CellRangeMin, CellRangeMax);
	}
	return true;
}

bool UDualContour::ModifyDensitySamples(const TArray<uint8>& Samples, bool bExcavate, FVectorInt& OutAffectedCellMin, FVectorInt& OutAffectedCellMax)
{
	return ModifyDensitySamplesInRange(FVectorInt(0, 0, 0), GetSampleDims(), Samples, bExcavate,
		OutAffectedCellMin, OutAffectedCellMax);
}

bool UDualContour::ModifyDensitySamplesInRange(FVectorInt SampleMin, FVectorInt SampleDimensions, TConstArrayView<uint8> Samples, bool bExcavate,
	FVectorInt& OutAffectedCellMin, FVectorInt& OutAffectedCellMax)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_ModifyDensitySamplesInRange);
	OutAffectedCellMin = FVectorInt();
	OutAffectedCellMax = FVectorInt();
	const FVectorInt FullSampleDimensions = GetSampleDims();
	if (!HasCurrentGeneratedData() || !IsValidSampleRange(FullSampleDimensions, SampleMin, SampleDimensions, Samples.Num()))
		return false;

	bool bModified = false;
	TSet<FIntVector> ModifiedChunks;
	FVectorInt ModifiedSampleMin(FullSampleDimensions.X, FullSampleDimensions.Y, FullSampleDimensions.Z);
	FVectorInt ModifiedSampleMax(-1, -1, -1);
	for (int32 Z = 0; Z < SampleDimensions.Z; ++Z)
		for (int32 Y = 0; Y < SampleDimensions.Y; ++Y)
			for (int32 X = 0; X < SampleDimensions.X; ++X)
			{
				const int32 SampleX = SampleMin.X + X;
				const int32 SampleY = SampleMin.Y + Y;
				const int32 SampleZ = SampleMin.Z + Z;
				const int32 SamplerDensity = Samples[SampleDimensions.LinearIndex(X, Y, Z)];
				const int32 OldDensity = GetDensity(SampleX, SampleY, SampleZ);
				int32 NewDensity = FMath::Max(OldDensity, SamplerDensity);
				if (bExcavate)
				{
					// Samples outside the brush surface are neutral for subtraction. This also prevents
					// the zero-filled area outside VolumeSize from changing unrelated solid samples.
					const int32 DifferenceDensity = SamplerDensity >= GDualContourIsoValue
						                                ? 2 * static_cast<int32>(GDualContourIsoValue) - SamplerDensity
						                                : 255;
					NewDensity = FMath::Min(OldDensity, DifferenceDensity);
				}
				const uint8 ClampedDensity = static_cast<uint8>(FMath::Clamp(NewDensity, 0, 255));
				if (ClampedDensity == OldDensity)
					continue;
				const FIntVector ChunkCoord(SampleX / GDualContourChunkSize, SampleY / GDualContourChunkSize, SampleZ / GDualContourChunkSize);
				FDensityChunk& Chunk = DensityChunks.FindOrAdd(ChunkCoord);
				Chunk.Expand();
				const int32 LocalX = SampleX % GDualContourChunkSize;
				const int32 LocalY = SampleY % GDualContourChunkSize;
				const int32 LocalZ = SampleZ % GDualContourChunkSize;
				Chunk.DensitySamples[LocalX + LocalY * GDualContourChunkSize + LocalZ * GDualContourChunkSize * GDualContourChunkSize]
					= ClampedDensity;
				ModifiedChunks.Add(
					FIntVector(SampleX / GDualContourChunkSize, SampleY / GDualContourChunkSize, SampleZ / GDualContourChunkSize));
				ModifiedSampleMin.X = FMath::Min(ModifiedSampleMin.X, SampleX);
				ModifiedSampleMin.Y = FMath::Min(ModifiedSampleMin.Y, SampleY);
				ModifiedSampleMin.Z = FMath::Min(ModifiedSampleMin.Z, SampleZ);
				ModifiedSampleMax.X = FMath::Max(ModifiedSampleMax.X, SampleX);
				ModifiedSampleMax.Y = FMath::Max(ModifiedSampleMax.Y, SampleY);
				ModifiedSampleMax.Z = FMath::Max(ModifiedSampleMax.Z, SampleZ);
				bModified = true;
			}
	if (!bModified)
		return false;
	CompactDensityChunks(ModifiedChunks);

	const FVectorInt CellRangeMin(FMath::Max(0, ModifiedSampleMin.X - 1), FMath::Max(0, ModifiedSampleMin.Y - 1),
		FMath::Max(0, ModifiedSampleMin.Z - 1));
	const FVectorInt CellRangeMax(FMath::Min(CellCount.X, ModifiedSampleMax.X + 1),
		FMath::Min(CellCount.Y, ModifiedSampleMax.Y + 1), FMath::Min(CellCount.Z, ModifiedSampleMax.Z + 1));
	RebuildCellsInRange(CellRangeMin, CellRangeMax);
	OutAffectedCellMin = CellRangeMin;
	OutAffectedCellMax = CellRangeMax;
	return true;
}

uint8 UDualContour::GetDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const
{
	const FVectorInt SampleDimensions = GetSampleDims();
	if (!SampleDimensions.IsValid(SampleX, SampleY, SampleZ))
		return 0;

	const FIntVector ChunkCoord(SampleX / GDualContourChunkSize, SampleY / GDualContourChunkSize, SampleZ / GDualContourChunkSize);
	const FDensityChunk* Chunk = DensityChunks.Find(ChunkCoord);
	if (!Chunk)
		return 0;
	if (Chunk->IsUniform())
		return Chunk->UniformValue;

	const int32 LocalX = SampleX % GDualContourChunkSize;
	const int32 LocalY = SampleY % GDualContourChunkSize;
	const int32 LocalZ = SampleZ % GDualContourChunkSize;
	return Chunk->DensitySamples[LocalX + LocalY * GDualContourChunkSize
	                             + LocalZ * GDualContourChunkSize * GDualContourChunkSize];
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

const FDualContourCell* UDualContour::GetContourCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	const FIntVector ChunkCoord(CellX / GDualContourChunkSize, CellY / GDualContourChunkSize, CellZ / GDualContourChunkSize);
	const FContourChunk* Chunk = ContourChunks.Find(ChunkCoord);
	return Chunk ? Chunk->ActiveCells.Find(PackLocalContourKey(CellX, CellY, CellZ)) : nullptr;
}

void UDualContour::SetContourCell(int32 CellX, int32 CellY, int32 CellZ, const FDualContourCell& Cell)
{
	const FIntVector ChunkCoord(CellX / GDualContourChunkSize, CellY / GDualContourChunkSize, CellZ / GDualContourChunkSize);
	if (!Cell.bActive)
	{
		if (FContourChunk* Chunk = ContourChunks.Find(ChunkCoord))
		{
			Chunk->ActiveCells.Remove(PackLocalContourKey(CellX, CellY, CellZ));
			if (Chunk->ActiveCells.IsEmpty())
				ContourChunks.Remove(ChunkCoord);
		}
		return;
	}
	ContourChunks.FindOrAdd(ChunkCoord).ActiveCells.Add(PackLocalContourKey(CellX, CellY, CellZ), Cell);
}

bool UDualContour::HasActiveCellInRange(FVectorInt CellMin, FVectorInt CellMax) const
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
				const FContourChunk* Chunk = ContourChunks.Find(FIntVector(ChunkX, ChunkY, ChunkZ));
				if (!Chunk)
					continue;
				for (const TPair<uint16, FDualContourCell>& Pair : Chunk->ActiveCells)
				{
					const int32 AbsX = ChunkX * GDualContourChunkSize + (Pair.Key & 0xF);
					const int32 AbsY = ChunkY * GDualContourChunkSize + ((Pair.Key >> 4) & 0xF);
					const int32 AbsZ = ChunkZ * GDualContourChunkSize + ((Pair.Key >> 8) & 0xF);
					if (AbsX >= CellMin.X && AbsX < CellMax.X && AbsY >= CellMin.Y && AbsY < CellMax.Y
					    && AbsZ >= CellMin.Z && AbsZ < CellMax.Z)
						return true;
				}
			}
	return false;
}

float UDualContour::TrilinearDensity(FVector GridPos) const
{
	const FVectorInt Dims = GetSampleDims();
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

FVector UDualContour::ComputeGradient(FVector GridPos) const
{
	constexpr float Step = 0.5f;
	return FVector(
		TrilinearDensity(GridPos + FVector(Step, 0, 0)) - TrilinearDensity(GridPos - FVector(Step, 0, 0)),
		TrilinearDensity(GridPos + FVector(0, Step, 0)) - TrilinearDensity(GridPos - FVector(0, Step, 0)),
		TrilinearDensity(GridPos + FVector(0, 0, Step)) - TrilinearDensity(GridPos - FVector(0, 0, Step)));
}

void UDualContour::BuildCells()
{
	ContourChunks.Reset();
	RebuildCellsInRange(FVectorInt(0, 0, 0), CellCount);
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
				(GetDensity(CellX + X, CellY + Y, CellZ + Z)
				 >= GDualContourIsoValue
					 ? bHasInside
					 : bHasOutside) = true;
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

void UDualContour::RebuildCellsInRange(FVectorInt RangeMin, FVectorInt RangeMax)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_RebuildCellsInRange);
	check(IsInGameThread());
	RangeMin = FVectorInt(FMath::Max(0, RangeMin.X), FMath::Max(0, RangeMin.Y), FMath::Max(0, RangeMin.Z));
	RangeMax = FVectorInt(FMath::Min(CellCount.X, RangeMax.X), FMath::Min(CellCount.Y, RangeMax.Y), FMath::Min(CellCount.Z, RangeMax.Z));
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

	TArray<FContourChunk> BuiltChunks;
	BuiltChunks.SetNum(ChunkCoords.Num());

	ParallelFor(TEXT("DualContour.BuildContourChunks"), ChunkCoords.Num(), 1,
		[this, RangeMin, RangeMax, &ChunkCoords, &BuiltChunks](int32 Index)
		{
			const FIntVector ChunkCoord = ChunkCoords[Index];
			const FVectorInt ChunkOrigin(ChunkCoord.X * GDualContourChunkSize,
				ChunkCoord.Y * GDualContourChunkSize, ChunkCoord.Z * GDualContourChunkSize);
			const FVectorInt BuildMin(FMath::Max(RangeMin.X, ChunkOrigin.X),
				FMath::Max(RangeMin.Y, ChunkOrigin.Y), FMath::Max(RangeMin.Z, ChunkOrigin.Z));
			const FVectorInt BuildMax(FMath::Min(RangeMax.X, ChunkOrigin.X + GDualContourChunkSize),
				FMath::Min(RangeMax.Y, ChunkOrigin.Y + GDualContourChunkSize),
				FMath::Min(RangeMax.Z, ChunkOrigin.Z + GDualContourChunkSize));

			FContourChunk& BuiltChunk = BuiltChunks[Index];
			for (int32 CellZ = BuildMin.Z; CellZ < BuildMax.Z; ++CellZ)
				for (int32 CellY = BuildMin.Y; CellY < BuildMax.Y; ++CellY)
					for (int32 CellX = BuildMin.X; CellX < BuildMax.X; ++CellX)
					{
						FDualContourCell Cell = BuildNewCell(CellX, CellY, CellZ);
						if (!Cell.bActive)
							continue;
						const uint16 LocalKey = static_cast<uint16>((CellX % GDualContourChunkSize)
						                                            | ((CellY % GDualContourChunkSize) << 4)
						                                            | ((CellZ % GDualContourChunkSize) << 8));
						BuiltChunk.ActiveCells.Add(LocalKey, MoveTemp(Cell));
					}
		}, EParallelForFlags::Unbalanced);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_MergeContourChunks);
		for (int32 Index = 0; Index < ChunkCoords.Num(); ++Index)
		{
			const FIntVector ChunkCoord = ChunkCoords[Index];
			const FVectorInt ChunkOrigin(ChunkCoord.X * GDualContourChunkSize,
				ChunkCoord.Y * GDualContourChunkSize, ChunkCoord.Z * GDualContourChunkSize);

			if (FContourChunk* ExistingChunk = ContourChunks.Find(ChunkCoord))
			{
				for (auto CellIt = ExistingChunk->ActiveCells.CreateIterator(); CellIt; ++CellIt)
				{
					const uint16 LocalKey = CellIt.Key();
					const int32 CellX = ChunkOrigin.X + (LocalKey & 0xF);
					const int32 CellY = ChunkOrigin.Y + ((LocalKey >> 4) & 0xF);
					const int32 CellZ = ChunkOrigin.Z + ((LocalKey >> 8) & 0xF);
					if (CellX >= RangeMin.X && CellX < RangeMax.X && CellY >= RangeMin.Y && CellY < RangeMax.Y
					    && CellZ >= RangeMin.Z && CellZ < RangeMax.Z)
						CellIt.RemoveCurrent();
				}
			}

			FContourChunk& BuiltChunk = BuiltChunks[Index];
			if (!BuiltChunk.ActiveCells.IsEmpty())
			{
				FContourChunk& TargetChunk = ContourChunks.FindOrAdd(ChunkCoord);
				for (TPair<uint16, FDualContourCell>& CellPair : BuiltChunk.ActiveCells)
					TargetChunk.ActiveCells.Add(CellPair.Key, MoveTemp(CellPair.Value));
			}

			const FContourChunk* UpdatedChunk = ContourChunks.Find(ChunkCoord);
			if (UpdatedChunk && UpdatedChunk->ActiveCells.IsEmpty())
				ContourChunks.Remove(ChunkCoord);
		}
	}

	const float Relaxation = FMath::Clamp(VertexRelaxation, 0.0f, 1.0f);
	if (Relaxation > 0.0f && RangeMin.X == 0 && RangeMin.Y == 0 && RangeMin.Z == 0
	    && RangeMax.X == CellCount.X && RangeMax.Y == CellCount.Y && RangeMax.Z == CellCount.Z)
	{
		const float MinimumNormalCosine = FMath::Clamp(RelaxationNormalCosine, -1.0f, 1.0f);
		TMap<FIntVector, FVector> RelaxedCenters;
		for (const TPair<FIntVector, FContourChunk>& ChunkPair : ContourChunks)
		{
			const FIntVector ChunkOrigin = ChunkPair.Key * GDualContourChunkSize;
			for (const TPair<uint16, FDualContourCell>& CellPair : ChunkPair.Value.ActiveCells)
			{
				const FIntVector CellCoord(ChunkOrigin.X + (CellPair.Key & 0xF),
					ChunkOrigin.Y + ((CellPair.Key >> 4) & 0xF), ChunkOrigin.Z + ((CellPair.Key >> 8) & 0xF));
				FVector Sum = FVector::ZeroVector;
				int32 Count = 0;
				for (const FIntVector& Offset : {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0),
				                                 FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1)})
				{
					if (const FDualContourCell* Neighbor = GetContourCell(CellCoord.X + Offset.X, CellCoord.Y + Offset.Y, CellCoord.Z + Offset.Z))
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
			if (FDualContourCell* Cell = const_cast<FDualContourCell*>(GetContourCell(Pair.Key.X, Pair.Key.Y, Pair.Key.Z)))
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
		OnCellsRebuilt.Broadcast(FVectorInt(0, 0, 0), CellCount);
}
#endif
