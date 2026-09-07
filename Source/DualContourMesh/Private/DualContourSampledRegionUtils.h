#pragma once

#include "DualContourTypes.h"

namespace DualContourSampling
{
inline bool IsValidSampleBounds(const FIntVector& FullDimensions, const FIntVector& SampleMin, const FIntVector& SampleDimensions)
{
	if (SampleMin.X < 0 || SampleMin.Y < 0 || SampleMin.Z < 0 || SampleDimensions.X < 0 || SampleDimensions.Y < 0 || SampleDimensions.Z < 0)
		return false;

	const bool bEmpty = SampleDimensions.X == 0 || SampleDimensions.Y == 0 || SampleDimensions.Z == 0;
	if (bEmpty)
	{
		return SampleDimensions.X == 0 && SampleDimensions.Y == 0 && SampleDimensions.Z == 0 && SampleMin.X <= FullDimensions.X &&
		       SampleMin.Y <= FullDimensions.Y && SampleMin.Z <= FullDimensions.Z;
	}

	return SampleMin.X < FullDimensions.X && SampleMin.Y < FullDimensions.Y && SampleMin.Z < FullDimensions.Z &&
	       SampleDimensions.X <= FullDimensions.X - SampleMin.X && SampleDimensions.Y <= FullDimensions.Y - SampleMin.Y &&
	       SampleDimensions.Z <= FullDimensions.Z - SampleMin.Z;
}

inline bool IsValidSampledRegion(const FIntVector& FullDimensions, const FDualContourSampledRegion& Region)
{
	if (!IsValidSampleBounds(FullDimensions, Region.SampleMin, Region.SampleDimensions))
		return false;
	if (Region.SampleDimensions == FIntVector::ZeroValue)
		return Region.Chunks.IsEmpty();

	const FIntVector SampleMax = Region.SampleMin + Region.SampleDimensions;
	const FIntVector ChunkMin(Region.SampleMin.X / GDualContourChunkSize, Region.SampleMin.Y / GDualContourChunkSize,
	                          Region.SampleMin.Z / GDualContourChunkSize);
	const FIntVector ChunkMaxExclusive(FMath::DivideAndRoundUp(SampleMax.X, GDualContourChunkSize),
	                                   FMath::DivideAndRoundUp(SampleMax.Y, GDualContourChunkSize),
	                                   FMath::DivideAndRoundUp(SampleMax.Z, GDualContourChunkSize));
	TSet<FIntVector> UniqueChunkCoords;
	UniqueChunkCoords.Reserve(Region.Chunks.Num());
	const int32 ExpandedChunkSize = GDualContourChunkSize * GDualContourChunkSize * GDualContourChunkSize;
	for (const FDualContourSampledChunk& SampledChunk : Region.Chunks)
	{
		const FIntVector& Coord = SampledChunk.ChunkCoord;
		if (Coord.X < ChunkMin.X || Coord.Y < ChunkMin.Y || Coord.Z < ChunkMin.Z || Coord.X >= ChunkMaxExclusive.X ||
		    Coord.Y >= ChunkMaxExclusive.Y || Coord.Z >= ChunkMaxExclusive.Z || UniqueChunkCoords.Contains(Coord) ||
		    (!SampledChunk.Density.IsUniform() && SampledChunk.Density.DensitySamples.Num() != ExpandedChunkSize))
		{
			return false;
		}
		UniqueChunkCoords.Add(Coord);
	}
	return true;
}
} // namespace DualContourSampling
