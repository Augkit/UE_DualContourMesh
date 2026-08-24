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
struct FDensityChunk
{
	uint8 UniformValue = 0;
	TArray<uint8> DensitySamples; // size = ChunkSize^3 when expanded

	bool IsUniform() const { return DensitySamples.IsEmpty(); }

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
struct FContourChunk
{
	// Key: LocalX | (LocalY << 4) | (LocalZ << 8), each in [0, ChunkSize).
	TMap<uint16, FDualContourCell> ActiveCells;
};
