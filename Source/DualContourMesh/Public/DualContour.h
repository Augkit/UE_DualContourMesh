#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DualContourTypes.h"
#include "DualContour.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDualContourCellsRebuilt, FVectorInt, FVectorInt);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDirtyDualContourChunksRebuilt, const FDualContourDirtyRegion&);

/** Owns the dual-contour source data, generation settings, and contour-building algorithms. */
UCLASS(BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UDualContour : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	FVectorInt CellCount = FVectorInt(64, 64, 64);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	float CellSize = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	EDualContourVertexSolveMode VertexSolveMode = EDualContourVertexSolveMode::HermiteIntersectionCentroid;

	/** Tangent-plane smoothing strength. Set to zero to disable the relaxation pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Relaxation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VertexRelaxation = 0.25f;

	/** Minimum normal dot product required for neighbouring cells to smooth each other. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Relaxation", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float RelaxationNormalCosine = 0.5f;

	/** True when generation settings have changed since the last successful rebuild. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour")
	bool bRebuildRequired = true;

	/** Broadcast after generated contour data changes. The range is [CellMin, CellMax). */
	FOnDualContourCellsRebuilt OnCellsRebuilt;
	/** Broadcast once per edit-batch flush after all unique cell chunks have been rebuilt. */
	FOnDirtyDualContourChunksRebuilt OnDirtyChunksRebuilt;

	bool Rebuild();
	/** Copies persistent grid settings and generated data from another current DualContour. */
	bool CopyFrom(const UDualContour* Source);
	/** Replaces the complete sample grid and rebuilds contour cells. Samples are X-major. */
	bool ReplaceDensitySamples(const TArray<uint8>& Samples);
	/** Replaces the density grid with an X-major subrange; samples outside the range become zero. */
	bool ReplaceDensitySamplesInRange(FVectorInt SampleMin, FVectorInt SampleDimensions, TConstArrayView<uint8> Samples);
	/** Combines a complete sampler grid using density union/difference and rebuilds only changed cells. */
	bool ModifyDensitySamples(const TArray<uint8>& Samples, bool bExcavate, FVectorInt& OutAffectedCellMin, FVectorInt& OutAffectedCellMax);
	/** Combines an X-major sampler subrange and rebuilds only changed cells. */
	bool ModifyDensitySamplesInRange(FVectorInt SampleMin, FVectorInt SampleDimensions, TConstArrayView<uint8> Samples, bool bExcavate,
		FVectorInt& OutAffectedCellMin, FVectorInt& OutAffectedCellMax);

	/** Starts a stroke-style edit. Density writes are accumulated and contour rebuilds are deferred to EndEditBatch. */
	FDualContourEditBatch BeginEditBatch() const;
	/** Applies one local brush stamp in O(samples covered by the brush). */
	bool ApplyBrushStamp(FDualContourEditBatch& Batch, const FDualContourBrushStamp& Stamp);
	/** Quantizes the stroke, rebuilds only dirty contour chunks, and returns sparse undo deltas. */
	bool EndEditBatch(FDualContourEditBatch& Batch, FDualContourEditResult& OutResult);
	/** Restores either side of a sparse edit and performs the same local rebuild path. */
	bool ApplyEditDeltas(TConstArrayView<FDualContourSampleDelta> Deltas, bool bUseAfterValues,
		FDualContourEditResult* OutResult = nullptr);

	uint8 GetDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	const FDualContourCell* GetCell(int32 CellX, int32 CellY, int32 CellZ) const;
	float TrilinearDensity(FVector GridPos) const;
	FVector ComputeGradient(FVector GridPos) const;
	bool HasCurrentGeneratedData() const;
	bool HasActiveCellInRange(FVectorInt CellMin, FVectorInt CellMax) const;

	FVector GetSampleLocalPosition(int32 SampleX, int32 SampleY, int32 SampleZ) const
	{
		return FVector(static_cast<double>(SampleX), static_cast<double>(SampleY), static_cast<double>(SampleZ)) * CellSize;
	}

	const TMap<FIntVector, FDensityChunk>& GetDensityChunks() const { return DensityChunks; }
	const TMap<FIntVector, FCellChunk>& GetCellChunks() const { return CellChunks; }

	virtual void PostLoad() override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

private:
	UPROPERTY(NonTransactional)
	TMap<FIntVector, FDensityChunk> DensityChunks;

	/** Runtime cache rebuilt from DensityChunks after loading. It is intentionally excluded from assets. */
	UPROPERTY(Transient, NonTransactional)
	TMap<FIntVector, FCellChunk> CellChunks;

	UPROPERTY()
	FVectorInt LastBuiltCellCount;

	FVectorInt GetSampleDims() const { return FVectorInt(CellCount.X + 1, CellCount.Y + 1, CellCount.Z + 1); }
	void BuildCells();
	FDualContourCell BuildNewCell(int32 CellX, int32 CellY, int32 CellZ) const;
	void CompactAllDensityChunks();
	void CompactDensityChunks(const TSet<FIntVector>& ChunkCoords);
	void RebuildCellsInRange(FVectorInt RangeMin, FVectorInt RangeMax);
	void WriteDensitySample(int32 SampleX, int32 SampleY, int32 SampleZ, uint8 Density, TSet<FIntVector>& DirtyChunks);
	void RebuildDirtyDensityChunks(const TSet<FIntVector>& DirtyDensityChunks, FDualContourDirtyRegion& OutDirtyRegion);
	void RebuildDirtyCellChunks(const TSet<FIntVector>& ChunkCoords);
	bool ValidateGenerationSettings() const;
	static uint16 PackLocalCellKey(int32 CellX, int32 CellY, int32 CellZ);
};
