#pragma once
#include "CoreMinimal.h"
#include "DualContourTypes.generated.h"

inline constexpr uint8 GDualContourIsoValue = 127;
inline constexpr int32 GDualContourChunkSize = 16;

USTRUCT(BlueprintType)
struct DUALCONTOURMESH_API FVectorInt
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Y = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Z = 0;

	FVectorInt() = default;
	FVectorInt(int32 InX, int32 InY, int32 InZ) : X(InX), Y(InY), Z(InZ) {}

	int32 Volume() const { return X * Y * Z; }

	int32 LinearIndex(int32 InX, int32 InY, int32 InZ) const { return InX + InY * X + InZ * X * Y; }

	bool IsValid(int32 InX, int32 InY, int32 InZ) const { return InX >= 0 && InX < X && InY >= 0 && InY < Y && InZ >= 0 && InZ < Z; }
};

USTRUCT(BlueprintType)
struct DUALCONTOURMESH_API FDualContourCell
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly)
	FVector Center = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FVector Normal = FVector::UpVector;
};

// Sparse density chunk. Empty DensitySamples means the whole chunk has UniformValue.
USTRUCT(BlueprintType)
struct DUALCONTOURMESH_API FDensityChunk
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 UniformValue = 0;

	UPROPERTY()
	TArray<uint8> DensitySamples; // size = ChunkSize^3 when expanded

	bool IsUniform() const { return DensitySamples.IsEmpty(); }

	/** Collapses an expanded chunk when every stored sample has the same value. */
	bool TryCollapse()
	{
		if (IsUniform())
			return true;

		const uint8 Candidate = DensitySamples[0];
		for (const uint8 Value : DensitySamples)
		{
			if (Value != Candidate)
				return false;
		}

		UniformValue = Candidate;
		DensitySamples.Empty();
		return true;
	}

	void Expand()
	{
		if (!IsUniform())
			return;
		const int32 N = GDualContourChunkSize * GDualContourChunkSize * GDualContourChunkSize;
		DensitySamples.SetNumUninitialized(N);
		FMemory::Memset(DensitySamples.GetData(), UniformValue, N);
	}
};

// Sparse contour chunk. Only active (surface-crossing) cells are stored.
USTRUCT(BlueprintType)
struct DUALCONTOURMESH_API FContourChunk
{
	GENERATED_BODY()

	// Key: LocalX | (LocalY << 4) | (LocalZ << 8), each in [0, ChunkSize).
	UPROPERTY()
	TMap<uint16, FDualContourCell> ActiveCells;
};

/** Plain CPU mesh data produced by the dual-contour mesh builder. */
struct DUALCONTOURMESH_API FDualContourMeshData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2f> UVs;
	TArray<uint32> Indices;
	FBox LocalBounds = FBox(ForceInit);

	void Reset()
	{
		Positions.Reset();
		Normals.Reset();
		UVs.Reset();
		Indices.Reset();
		LocalBounds = FBox(ForceInit);
	}
};
