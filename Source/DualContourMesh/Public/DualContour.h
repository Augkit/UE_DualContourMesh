#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DualContourTypes.h"
#include "DualContour.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDualContourCellsRebuilt, FIntVector, FIntVector);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDirtyDualContourChunksRebuilt, const FDualContourDirtyRegion&);

/** Owns the dual-contour source data, generation settings, and contour-building algorithms. */
UCLASS(BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UDualContour : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	FIntVector CellCount = FIntVector(64, 64, 64);

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

	bool HasCurrentGeneratedData() const;

	uint8 GetDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	/** Returns the chunk overlay accumulated by runtime density mutation paths. */
	const FDualContourDensityChunks& GetModifiedDensityChunks() const { return ModifiedDensityChunks; }

	const FDualContourCell* GetCell(int32 CellX, int32 CellY, int32 CellZ) const;
	bool HasActiveCellInRange(FIntVector CellMin, FIntVector CellMax) const;

	/**
	 * Initializes this runtime contour from a current base contour and optionally overlays saved density chunks.
	 * The base CellChunks are copied as-is; only chunks affected by the overlay are rebuilt.
	 */
	bool Initialize(const UDualContour* InitialDualContour, const FDualContourDensityChunks* InModifiedDensityChunks = nullptr);
	/** Copies persistent grid settings and generated data from another current DualContour. */
	bool CopyFrom(const UDualContour* Source, bool bBroadcastCellsRebuilt = true);

	bool Rebuild();
	/** Moves sampler-built density chunks into this grid; density outside the sampled range becomes zero. */
	bool ReplaceDensityChunks(FDualContourSampledRegion&& SampledRegion, bool bBroadcastCellsRebuilt = true);
	/** Combines sampler-built density chunks and rebuilds only changed cells. */
	bool ModifyDensityChunks(const FDualContourSampledRegion& SampledRegion, bool bExcavate,
		FIntVector& OutAffectedCellMin, FIntVector& OutAffectedCellMax);

	/** Applies an accumulated stroke batch, rebuilds only dirty contour chunks, and returns sparse undo deltas. */
	bool ApplyEditBatch(FDualContourEditBatch& Batch, FDualContourEditResult& OutResult);
	/** Restores either side of a sparse edit and performs the same local rebuild path. */
	bool ApplyEditDeltas(TConstArrayView<FDualContourSampleDelta> Deltas, bool bUseAfterValues, FDualContourEditResult* OutResult = nullptr);

	/** Applies a validated chunk overlay to the current density grid and records it for subsequent saves. */
	bool ApplyModifiedDensityChunks(const FDualContourDensityChunks& InModifiedDensityChunks);

	FIntVector GetSampleDimensions() const { return FIntVector(CellCount.X + 1, CellCount.Y + 1, CellCount.Z + 1); }

	FVector GetSampleLocalPosition(int32 SampleX, int32 SampleY, int32 SampleZ) const
	{
		return FVector(static_cast<double>(SampleX), static_cast<double>(SampleY), static_cast<double>(SampleZ)) * CellSize;
	}

	float TrilinearDensity(const FVector& GridPos) const;

	virtual void PostLoad() override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

private:
	UPROPERTY(NonTransactional)
	TMap<FIntVector, FDensityChunk> DensityChunks;

	/** Latest complete state of every density chunk touched since the base contour was copied or generated. */
	UPROPERTY(Transient, NonTransactional)
	TMap<FIntVector, FDensityChunk> ModifiedDensityChunks;

	/** Runtime cache rebuilt from DensityChunks after loading. It is intentionally excluded from assets. */
	UPROPERTY(Transient, NonTransactional)
	TMap<FIntVector, FCellChunk> CellChunks;

	UPROPERTY()
	FIntVector LastBuiltCellCount = FIntVector(0, 0, 0);

	bool ValidateGenerationSettings() const;

	void WriteDirtyDensitySample(int32 SampleX, int32 SampleY, int32 SampleZ, uint8 Density, TSet<FIntVector>& DirtyChunks);
	void CompactAllDensityChunks();
	void CompactDensityChunks(const TSet<FIntVector>& ChunkCoords);

	void RebuildCells();
	FDualContourCell CreateNewCell(int32 CellX, int32 CellY, int32 CellZ) const;
	void RebuildCellsInRange(FIntVector RangeMin, FIntVector RangeMax, bool bBroadcastCellsRebuilt = true);
	void RebuildDirtyCellChunks(const TSet<FIntVector>& DirtyDensityChunks, FDualContourDirtyRegion& OutDirtyRegion);

	void RecordModifiedDensityChunks(const TSet<FIntVector>& ChunkCoords);
};
