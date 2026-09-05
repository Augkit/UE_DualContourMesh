#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DualContourTypes.h"
#include "Async/Future.h"
#include "Templates/Function.h"
#include "DualContour.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDualContourCellsRebuilt, FIntVector, FIntVector);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDualContourMaterialsChanged, FIntVector, FIntVector);

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

	/** UV projection used by generated mesh components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|UV")
	EDualContourUVMode UVMode = EDualContourUVMode::WorldAlignedBox;

	/** World/local-space distance, in Unreal units, covered by one texture repeat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|UV", meta = (ClampMin = "1.0"))
	float UVWorldSize = 100.0f;

	/** True when generation settings have changed since the last successful rebuild. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour")
	bool bRebuildRequired = true;

	/** Broadcast after generated contour data changes. The range is [CellMin, CellMax). */
	FOnDualContourCellsRebuilt OnCellsRebuilt;
	/** Broadcast after material samples change. The affected cell range is [CellMin, CellMax). */
	FOnDualContourMaterialsChanged OnMaterialsChanged;

	bool HasCurrentGeneratedData() const;

	uint16 GetDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	float GetLinearDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	uint8 GetMaterialId(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	/** Returns the chunk overlay accumulated by runtime density mutation paths. */
	const FDualContourDensityChunks& GetModifiedDensityChunks() const { return ModifiedDensityChunks; }
	const FDualContourMaterialChunks& GetModifiedMaterialChunks() const { return ModifiedMaterialChunks; }

	const FDualContourCell* GetCell(int32 CellX, int32 CellY, int32 CellZ) const;
	bool HasActiveCellInRange(FIntVector CellMin, FIntVector CellMax) const;

	/**
	 * Initializes this runtime contour from a current base contour and optionally overlays saved density chunks.
	 * The base CellChunks are copied as-is; only chunks affected by the overlay are rebuilt.
	 */
	bool Initialize(const UDualContour* InitialDualContour, const FDualContourDensityChunks* InModifiedDensityChunks = nullptr,
		const FDualContourMaterialChunks* InModifiedMaterialChunks = nullptr);
	/** Copies persistent grid settings and generated data from another current DualContour. */
	bool CopyFrom(const UDualContour* Source, bool bBroadcastCellsRebuilt = true);

	bool Rebuild();
	/** Moves sampler-built density chunks into this grid; density outside the sampled range becomes zero. */
	bool ReplaceDensityChunks(FDualContourSampledRegion&& SampledRegion, bool bBroadcastCellsRebuilt = true);
	/** Combines sampler-built density chunks and rebuilds only changed cells. */
	bool ModifyDensityChunks(const FDualContourSampledRegion& SampledRegion, bool bExcavate,
		FIntVector& OutAffectedCellMin, FIntVector& OutAffectedCellMax);

	/** Consumes pending writes and rebuilds changed chunks. Callback receives actual encoded changes; it must not mutate this grid or batch. */
	bool ApplyPendingBatch(FDualContourPendingBatch& Batch,
		TFunctionRef<void(const FIntVector&, uint16, uint16)> OnSampleChanged = [](const FIntVector&, uint16, uint16) {});

	/** Applies a validated chunk overlay to the current density grid and records it for subsequent saves. */
	bool ApplyModifiedDensityChunks(const FDualContourDensityChunks& InModifiedDensityChunks);

	bool ApplyPendingMaterialBatch(FDualContourPendingMaterialBatch& Batch, FDualContourMaterialEditResult& OutResult);
	bool ApplyMaterialEditDeltas(TConstArrayView<FDualContourMaterialSampleDelta> Deltas, bool bUseAfterValues,
		FDualContourMaterialEditResult* OutResult = nullptr);
	bool ApplyModifiedMaterialChunks(const FDualContourMaterialChunks& InModifiedMaterialChunks);

	FIntVector GetSampleDimensions() const { return FIntVector(CellCount.X + 1, CellCount.Y + 1, CellCount.Z + 1); }

	FVector GetSampleLocalPosition(int32 SampleX, int32 SampleY, int32 SampleZ) const
	{
		return FVector(static_cast<double>(SampleX), static_cast<double>(SampleY), static_cast<double>(SampleZ)) * CellSize;
	}

	float TrilinearDensity(const FVector& GridPos) const;

	virtual void PostLoad() override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual bool SampleSource() { return false; }

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

private:
	UPROPERTY(NonTransactional)
	TMap<FIntVector, FDensityChunk> DensityChunks;

	/** Latest complete state of every density chunk touched since the base contour was copied or generated. */
	UPROPERTY(Transient, NonTransactional)
	TMap<FIntVector, FDensityChunk> ModifiedDensityChunks;

	UPROPERTY(NonTransactional)
	TMap<FIntVector, FMaterialIdChunk> MaterialChunks;

	UPROPERTY(Transient, NonTransactional)
	TMap<FIntVector, FMaterialIdChunk> ModifiedMaterialChunks;

	/** Runtime cache rebuilt from DensityChunks after loading. It is intentionally excluded from assets. */
	UPROPERTY(Transient, NonTransactional)
	TMap<FIntVector, FCellChunk> CellChunks;

	/** Future tracking the most recently dispatched background rebuild task. */
	mutable TFuture<void> PendingRebuildFuture;

	UPROPERTY()
	FIntVector LastBuiltCellCount = FIntVector(0, 0, 0);

	bool ValidateGenerationSettings() const;

	void EnsureRebuildComplete() const;

	void WriteDirtyDensitySample(int32 SampleX, int32 SampleY, int32 SampleZ, uint16 Density, TSet<FIntVector>& DirtyChunks);
	void CompactAllDensityChunks();
	void CompactDensityChunks(const TSet<FIntVector>& ChunkCoords);
	void WriteDirtyMaterialSample(int32 SampleX, int32 SampleY, int32 SampleZ, uint8 MaterialId, TSet<FIntVector>& DirtyChunks);
	void CompactAllMaterialChunks();
	void CompactMaterialChunks(const TSet<FIntVector>& ChunkCoords);
	void BroadcastMaterialSampleRange(FIntVector SampleMin, FIntVector SampleMaxInclusive);

	FVector CalculateCentralDifferenceNormal(const FVector& GridPosition) const;

	FDualContourCell CreateNewCell(int32 CellX, int32 CellY, int32 CellZ) const;
	void AsyncRebuildCells(bool bBroadcastCellsRebuilt = true);
	void RebuildCellsInRange(FIntVector RangeMin, FIntVector RangeMax, bool bBroadcastCellsRebuilt = true);
	void AsyncRebuildCellsInRange(FIntVector RangeMin, FIntVector RangeMax, bool bBroadcastCellsRebuilt = true);
	void RebuildDirtyCellChunks(const TSet<FIntVector>& DirtyDensityChunks, bool bBroadcastCellsRebuilt = true);
	void AsyncRebuildDirtyCellChunks(TSet<FIntVector>&& DirtyDensityChunks, bool bBroadcastCellsRebuilt = true);

	void RecordModifiedDensityChunks(const TSet<FIntVector>& ChunkCoords);
	void RecordModifiedMaterialChunks(const TSet<FIntVector>& ChunkCoords);
};
