#pragma once
#include "CoreMinimal.h"
#include "DualContourTypes.generated.h"

inline constexpr float GDualContourMinLinearDensity = -32768.0f;
inline constexpr float GDualContourLinearIsoValue = 0.0f;
inline constexpr float GDualContourMaxLinearDensity = 32767.0f;
inline constexpr uint16 GDualContourIsoValue = 32768;
/** Sub-units generated per authored density unit before offset-binary quantization. */
inline constexpr float GDualContourLinearDensityFixedPointScale = 64.0f;
inline constexpr int32 GDualContourChunkSize = 16;

UENUM(BlueprintType)
enum class EDualContourDensityEditOperation : uint8
{
	Sculpt,
	SculptSubtract,
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

/** Coordinate generation used by the generated render mesh. */
UENUM(BlueprintType)
enum class EDualContourUVMode : uint8
{
	/** Projects each quad onto its dominant normal axis using local position. */
	WorldAlignedBox UMETA(DisplayName = "World Aligned Box Projection"),
	/** The original per-quad [0,1] coordinates. Kept for backwards comparison. */
	QuadLocalLegacy UMETA(DisplayName = "Per-Quad Legacy"),
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

	/** Offset-binary signed density used when the chunk is uniform. Zero is saturated outside. */
	UPROPERTY(SaveGame)
	uint16 UniformValue = 0;

	/** Offset-binary signed densities; size = ChunkSize^3 when expanded. */
	UPROPERTY(SaveGame)
	TArray<uint16> DensitySamples;

	static uint16 EncodeDensity(float LinearDensity)
	{
		// Density is already expressed in centered fixed-point sub-units. Encoding is
		// therefore only clamp, round and bias; decoding stays branchless in hot paths.
		const float FiniteLinearDensity = FMath::IsFinite(LinearDensity) ? LinearDensity : GDualContourMinLinearDensity;
		const int32 QuantizedLinearDensity = FMath::RoundToInt(FMath::Clamp(FiniteLinearDensity, GDualContourMinLinearDensity,
			GDualContourMaxLinearDensity));
		return static_cast<uint16>(QuantizedLinearDensity + GDualContourIsoValue);
	}

	static float DecodeLinearDensity(uint16 Density)
	{
		return static_cast<float>(static_cast<int32>(Density) - GDualContourIsoValue);
	}

	float GetLinearDensitySample(int32 Index) const
	{
		return DecodeLinearDensity(IsUniform() ? UniformValue : DensitySamples[Index]);
	}

	void SetLinearDensitySample(int32 Index, float LinearDensity)
	{
		Expand();
		DensitySamples[Index] = EncodeDensity(LinearDensity);
	}

	void SetDensitySample(int32 Index, uint16 Density)
	{
		Expand();
		DensitySamples[Index] = Density;
	}

	bool IsUniform() const { return DensitySamples.IsEmpty(); }

	/** Collapses an expanded chunk when every stored sample has the same value. */
	bool TryCollapse()
	{
		if (IsUniform())
			return true;
		const uint16 Candidate = DensitySamples[0];
		for (const uint16 Value : DensitySamples)
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
		DensitySamples.Init(UniformValue, N);
	}
};

/** Sparse density storage shared by DualContour runtime data and runtime save games. */
#define FDualContourDensityChunks TMap<FIntVector, FDensityChunk>

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
	uint16 Before = 0;
	uint16 After = 0;
};

struct DUALCONTOURMESH_API FDualContourEditResult
{
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
	uint16 Before = 0;
	float WorkingValue = 0.0f;
};

/** Mutable state shared by all stamps in one stroke. */
struct DUALCONTOURMESH_API FDualContourEditBatch
{
	UDualContour* Owner = nullptr;
	TMap<FIntVector, TMap<uint16, FDualContourPendingSample>> ChunkSamples;
	bool bOpen = false;
};
