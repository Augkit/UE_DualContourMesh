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

	// Edge numbering shared by Hermite sampling and mesh emission.
	FORCEINLINE int32 CellEdgeIndex(int32 Axis, const FIntVector& Start)
	{
		return Axis == 0 ? Start.Y + 2 * Start.Z
			: Axis == 1 ? 4 + Start.X + 2 * Start.Z : 8 + Start.X + 2 * Start.Y;
	}

	// Trace contour arcs on the six faces. Disconnected loops need distinct dual vertices.
	inline TArray<uint16, TInlineAllocator<4>> FindCellSurfacePatches(const double (&Density)[8])
	{
		int32 Parent[12];
		for (int32 I = 0; I < 12; ++I) Parent[I] = I;
		const auto Root = [&Parent](int32 I)
		{
			while (Parent[I] != I) I = Parent[I];
			return I;
		};
		const auto Join = [&Parent, &Root](int32 A, int32 B) { Parent[Root(A)] = Root(B); };
		uint16 ActiveEdges = 0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const int32 U = Axis == 0 ? 1 : 0;
				const int32 V = Axis == 2 ? 1 : 2;
				FIntVector Corners[4] = {};
				double Values[4];
				for (int32 I = 0; I < 4; ++I)
				{
					Corners[I][Axis] = Side;
					Corners[I][U] = I == 1 || I == 2;
					Corners[I][V] = I >= 2;
					Values[I] = Density[Corners[I].X + 2 * Corners[I].Y + 4 * Corners[I].Z];
				}
				int32 Edges[4], Crossings[4], Count = 0;
				for (int32 I = 0; I < 4; ++I)
				{
					const int32 Next = (I + 1) % 4;
					const FIntVector Start(FMath::Min(Corners[I].X, Corners[Next].X),
						FMath::Min(Corners[I].Y, Corners[Next].Y), FMath::Min(Corners[I].Z, Corners[Next].Z));
					Edges[I] = CellEdgeIndex(I % 2 == 0 ? U : V, Start);
					if ((Values[I] >= 0) != (Values[Next] >= 0))
					{
						Crossings[Count++] = Edges[I];
						ActiveEdges |= 1u << Edges[I];
					}
				}
				if (Count == 2) Join(Crossings[0], Crossings[1]);
				else if (Count == 4)
				{
					// Canonical face ordering and tie rule agree on both adjacent cells.
					const bool bConnect02 = Values[0] * Values[2] - Values[1] * Values[3] >= 0;
					for (int32 I = bConnect02 ? 1 : 0; I < 4; I += 2)
						Join(Edges[(I + 3) % 4], Edges[I]);
				}
			}
		TArray<uint16, TInlineAllocator<4>> Patches;
		for (int32 I = 0; I < 12; ++I)
		{
			if (!(ActiveEdges & (1u << I)) || Root(I) != I) continue;
			uint16 Mask = 0;
			for (int32 J = 0; J < 12; ++J)
				if ((ActiveEdges & (1u << J)) && Root(J) == I) Mask |= 1u << J;
			Patches.Add(Mask);
		}
		return Patches;
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
