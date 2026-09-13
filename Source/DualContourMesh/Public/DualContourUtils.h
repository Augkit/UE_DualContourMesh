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

	// Choose the interior diagonal for concave quads and the less folded split
	// for non-planar quads. Ties retain the established 0-2 diagonal.
	FORCEINLINE bool UseAlternateQuadDiagonal(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3)
	{
		const double Scale = FMath::Max3((P1 - P0).GetAbs().GetMax(),
			(P2 - P0).GetAbs().GetMax(), (P3 - P0).GetAbs().GetMax());
		if (Scale <= UE_DOUBLE_SMALL_NUMBER)
			return false;
		const FVector A = (P1 - P0) / Scale, B = (P2 - P0) / Scale, C = (P3 - P0) / Scale;
		const auto Score = [](const FVector& N0, const FVector& N1)
		{
			const double Product = N0.SizeSquared() * N1.SizeSquared();
			return Product > 1.e-24 ? FVector::DotProduct(N0, N1) / FMath::Sqrt(Product) : -2.0;
		};
		return Score(FVector::CrossProduct(C, A), FVector::CrossProduct(C - A, B - A))
			> Score(FVector::CrossProduct(B, A), FVector::CrossProduct(C, B)) + 1.e-6;
	}
}
