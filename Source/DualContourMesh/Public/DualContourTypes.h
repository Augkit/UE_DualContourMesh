#pragma once
#include "CoreMinimal.h"
#include "DualContourTypes.generated.h"

inline constexpr uint8 GDualContourIsoValue = 127;
inline constexpr int32 GDualContourChunkSize = 16;

UENUM(BlueprintType)
enum class EDualContourDensityEditOperation : uint8
{
	Sculpt,
	Erase,
	Smooth,
	StampUnion,
	StampDifference,
};

UENUM(BlueprintType)
enum class EDualContourBrushShape : uint8
{
	Sphere,
	Box,
};

UENUM(BlueprintType)
enum class EDualContourBrushFalloff : uint8
{
	Smooth,
	Linear,
	Spherical,
	Tip,
};

UENUM(BlueprintType)
enum class EDualContourVertexSolveMode : uint8
{
	HermiteIntersectionCentroid UMETA(DisplayName = "Hermite Intersection Centroid"),
	QEF UMETA(DisplayName = "Regularized QEF"),
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

/** One chunk produced by a volume sampler before it is merged into a DualContour. */
struct DUALCONTOURMESH_API FDualContourSampledChunk
{
	FIntVector ChunkCoord = FIntVector::ZeroValue;
	FDensityChunk Density;
};

/** Chunk-native density samples covering the half-open range [SampleMin, SampleMin + SampleDimensions). */
struct DUALCONTOURMESH_API FDualContourSampledRegion
{
	FIntVector SampleMin = FIntVector::ZeroValue;
	FIntVector SampleDimensions = FIntVector::ZeroValue;
	TArray<FDualContourSampledChunk> Chunks;

	void Reset()
	{
		SampleMin = FIntVector::ZeroValue;
		SampleDimensions = FIntVector::ZeroValue;
		Chunks.Reset();
	}
};

// Sparse cell chunk. Only active (surface-crossing) cells are stored.
USTRUCT(BlueprintType)
struct DUALCONTOURMESH_API FCellChunk
{
	GENERATED_BODY()

	// Key: chunk-local linear index, with X as the fastest-changing axis.
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

/** One changed density sample. Edit-mode undo stores only these sparse values. */
struct DUALCONTOURMESH_API FDualContourSampleDelta
{
	FIntVector SampleCoord = FIntVector::ZeroValue;
	uint8 Before = 0;
	uint8 After = 0;
};

/** Chunk sets touched by a density edit. These are also the boundary for future async rebuilds. */
struct DUALCONTOURMESH_API FDualContourDirtyRegion
{
	TSet<FIntVector> DensityChunks;
	TSet<FIntVector> CellChunks;

	void Reset()
	{
		DensityChunks.Reset();
		CellChunks.Reset();
	}
};

struct DUALCONTOURMESH_API FDualContourEditResult
{
	FDualContourDirtyRegion DirtyRegion;
	TArray<FDualContourSampleDelta> Deltas;

	bool IsEmpty() const { return Deltas.IsEmpty(); }
};

class UVolumeSampledDualContour;
class UDualContour;

/** Runtime brush description. All positions and sizes are in the target DualContour's local space. */
struct DUALCONTOURMESH_API FDualContourBrushStamp
{
	EDualContourDensityEditOperation Operation = EDualContourDensityEditOperation::Sculpt;
	EDualContourBrushShape Shape = EDualContourBrushShape::Sphere;
	EDualContourBrushFalloff FalloffType = EDualContourBrushFalloff::Smooth;
	FVector LocalCenter = FVector::ZeroVector;
	FVector LocalNormal = FVector::UpVector;
	FVector ClayPlaneOrigin = FVector::ZeroVector;
	float Radius = 100.0f;
	float Falloff = 0.5f;
	float Strength = 0.3f;
	float TimeScale = 1.0f;
	bool bUseClayBrush = false;

	/** Used only by StampUnion/StampDifference; maps source local positions into target local space. */
	UVolumeSampledDualContour* VolumeBrush = nullptr;
	FTransform VolumeToTarget = FTransform::Identity;
};

struct FDualContourPendingSample
{
	uint8 Before = 0;
	float WorkingValue = 0.0f;
};

/** Mutable state shared by all stamps in one stroke. */
struct DUALCONTOURMESH_API FDualContourEditBatch
{
	UDualContour* Owner = nullptr;
	TMap<FIntVector, TMap<uint16, FDualContourPendingSample>> ChunkSamples;
	bool bOpen = false;
};
