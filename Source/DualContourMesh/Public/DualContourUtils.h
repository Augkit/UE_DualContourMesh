#pragma once

#include "CoreMinimal.h"
#include "DualContourTypes.h"

namespace DualContourUtils
{
	static_assert(GDualContourChunkSize > 0, "Dual contour chunk size must be positive.");
	static_assert(static_cast<int64>(GDualContourChunkSize) * GDualContourChunkSize * GDualContourChunkSize
		<= static_cast<int64>(MAX_uint16) + 1,
		"Dual contour chunk-local indices must fit in uint16.");

	FORCEINLINE int32 Volume(const FIntVector& Dimensions)
	{
		return Dimensions.X * Dimensions.Y * Dimensions.Z;
	}

	FORCEINLINE int32 LinearIndex(const FIntVector& Dimensions, int32 X, int32 Y, int32 Z)
	{
		return X + Y * Dimensions.X + Z * Dimensions.X * Dimensions.Y;
	}

	FORCEINLINE bool IsValidCoordinate(const FIntVector& Dimensions, int32 X, int32 Y, int32 Z)
	{
		return X >= 0 && X < Dimensions.X
			&& Y >= 0 && Y < Dimensions.Y
			&& Z >= 0 && Z < Dimensions.Z;
	}

	/** Returns the owning chunk for a non-negative grid coordinate. */
	FORCEINLINE FIntVector ChunkCoord(int32 X, int32 Y, int32 Z)
	{
		return FIntVector(X / GDualContourChunkSize, Y / GDualContourChunkSize, Z / GDualContourChunkSize);
	}

	FORCEINLINE FIntVector ChunkOrigin(const FIntVector& ChunkCoord)
	{
		return ChunkCoord * GDualContourChunkSize;
	}

	/** Packs a non-negative grid coordinate into its chunk-local linear index. */
	FORCEINLINE uint16 ChunkLocalIndex(int32 X, int32 Y, int32 Z)
	{
		return static_cast<uint16>((X % GDualContourChunkSize)
			+ (Y % GDualContourChunkSize) * GDualContourChunkSize
			+ (Z % GDualContourChunkSize) * GDualContourChunkSize * GDualContourChunkSize);
	}

	FORCEINLINE FIntVector ChunkLocalCoord(uint16 LocalIndex)
	{
		return FIntVector(
			LocalIndex % GDualContourChunkSize,
			(LocalIndex / GDualContourChunkSize) % GDualContourChunkSize,
			LocalIndex / (GDualContourChunkSize * GDualContourChunkSize));
	}
}
