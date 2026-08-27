#include "DualContour.h"

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

bool IsValidSampleRange(const FVectorInt& FullDimensions, const FVectorInt& SampleMin,
	const FVectorInt& SampleDimensions, int32 NumSamples)
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
	if (!Source || !Source->HasCurrentGeneratedData())
		return false;
	if (Source == this)
		return true;

	Modify();
	CellCount = Source->CellCount;
	CellSize = Source->CellSize;
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

bool UDualContour::ReplaceDensitySamplesInRange(FVectorInt SampleMin, FVectorInt SampleDimensions,
	TConstArrayView<uint8> Samples)
{
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

	DensityChunks.Reset();
	for (int32 Z = 0; Z < SampleDimensions.Z; ++Z)
		for (int32 Y = 0; Y < SampleDimensions.Y; ++Y)
			for (int32 X = 0; X < SampleDimensions.X; ++X)
			{
				const uint8 Density = Samples[SampleDimensions.LinearIndex(X, Y, Z)];
				if (Density != 0)
					SetDensity(SampleMin.X + X, SampleMin.Y + Y, SampleMin.Z + Z, Density);
			}

	ContourChunks.Reset();
	LastBuiltCellCount = CellCount;
	bRebuildRequired = false;
	if (Samples.IsEmpty())
	{
		OnCellsRebuilt.Broadcast(FVectorInt(0, 0, 0), CellCount);
	}
	else
	{
		const FVectorInt SampleMax(SampleMin.X + SampleDimensions.X, SampleMin.Y + SampleDimensions.Y, SampleMin.Z + SampleDimensions.Z);
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
	OutAffectedCellMin = FVectorInt();
	OutAffectedCellMax = FVectorInt();
	const FVectorInt FullSampleDimensions = GetSampleDims();
	if (!HasCurrentGeneratedData() || !IsValidSampleRange(FullSampleDimensions, SampleMin, SampleDimensions, Samples.Num()))
		return false;

	bool bModified = false;
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
				SetDensity(SampleX, SampleY, SampleZ, ClampedDensity);
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

void UDualContour::SetDensity(int32 SampleX, int32 SampleY, int32 SampleZ, uint8 Value)
{
	const FIntVector ChunkCoord(SampleX / GDualContourChunkSize, SampleY / GDualContourChunkSize, SampleZ / GDualContourChunkSize);
	FDensityChunk& Chunk = DensityChunks.FindOrAdd(ChunkCoord);
	Chunk.Expand();
	const int32 LocalX = SampleX % GDualContourChunkSize;
	const int32 LocalY = SampleY % GDualContourChunkSize;
	const int32 LocalZ = SampleZ % GDualContourChunkSize;
	Chunk.DensitySamples[LocalX + LocalY * GDualContourChunkSize
	                     + LocalZ * GDualContourChunkSize * GDualContourChunkSize] = Value;
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

void UDualContour::RebuildCellsInRange(FVectorInt RangeMin, FVectorInt RangeMax)
{
	RangeMin = FVectorInt(FMath::Max(0, RangeMin.X), FMath::Max(0, RangeMin.Y), FMath::Max(0, RangeMin.Z));
	RangeMax = FVectorInt(FMath::Min(CellCount.X, RangeMax.X), FMath::Min(CellCount.Y, RangeMax.Y), FMath::Min(CellCount.Z, RangeMax.Z));
	constexpr double Lambda = 0.1;

	for (int32 CellZ = RangeMin.Z; CellZ < RangeMax.Z; ++CellZ)
		for (int32 CellY = RangeMin.Y; CellY < RangeMax.Y; ++CellY)
			for (int32 CellX = RangeMin.X; CellX < RangeMax.X; ++CellX)
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
				{
					SetContourCell(CellX, CellY, CellZ, Cell);
					continue;
				}

				double Matrix[3][3] = {};
				double Vector[3] = {};
				FVector AccumNormal = FVector::ZeroVector;
				int32 NumIntersections = 0;
				for (int32 EdgeIndex = 0; EdgeIndex < 12; ++EdgeIndex)
				{
					const int32* A = EdgeCorners[EdgeIndex][0];
					const int32* B = EdgeCorners[EdgeIndex][1];
					const int32 AX = CellX + A[0], AY = CellY + A[1], AZ = CellZ + A[2];
					const int32 BX = CellX + B[0], BY = CellY + B[1], BZ = CellZ + B[2];
					const int32 DensityA = GetDensity(AX, AY, AZ), DensityB = GetDensity(BX, BY, BZ);
					if ((DensityA < GDualContourIsoValue) == (DensityB < GDualContourIsoValue))
						continue;

					const float Alpha = (static_cast<float>(GDualContourIsoValue) - DensityA) / (DensityB - DensityA);
					const FVector GridPosition = FVector(AX, AY, AZ) + Alpha * (FVector(BX, BY, BZ) - FVector(AX, AY, AZ));
					const FVector WorldPosition = GridPosition * CellSize;
					const FVector Normal = (-ComputeGradient(GridPosition)).GetSafeNormal();
					if (Normal.IsNearlyZero())
						continue;

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
					AccumNormal += Normal;
					++NumIntersections;
				}

				if (NumIntersections > 0)
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
					Cell.Normal = AccumNormal.GetSafeNormal();
					if (Cell.Normal.IsNearlyZero())
						Cell.Normal = FVector::UpVector;
				}
				SetContourCell(CellX, CellY, CellZ, Cell);
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
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellSize))
	{
		bRebuildRequired = true;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
