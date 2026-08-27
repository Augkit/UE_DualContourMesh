#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DualContourTypes.h"
#include "DualContour.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDualContourCellsRebuilt, FVectorInt, FVectorInt);

/** Owns the dual-contour source data, generation settings, and contour-building algorithms. */
UCLASS(BlueprintType, EditInlineNew)
class DUALCONTOURMESH_API UDualContour : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVectorInt CellCount = FVectorInt(64, 64, 64);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	float CellSize = 10.f;

	/** True when generation settings have changed since the last successful rebuild. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour")
	bool bRebuildRequired = true;

	/** Broadcast after generated contour data changes. The range is [CellMin, CellMax). */
	FOnDualContourCellsRebuilt OnCellsRebuilt;

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

	uint8 GetDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	const FDualContourCell* GetContourCell(int32 CellX, int32 CellY, int32 CellZ) const;
	float TrilinearDensity(FVector GridPos) const;
	FVector ComputeGradient(FVector GridPos) const;
	bool HasCurrentGeneratedData() const;
	bool HasActiveCellInRange(FVectorInt CellMin, FVectorInt CellMax) const;

	FVector GetSampleLocalPosition(int32 SampleX, int32 SampleY, int32 SampleZ) const
	{
		return FVector(static_cast<double>(SampleX), static_cast<double>(SampleY), static_cast<double>(SampleZ)) * CellSize;
	}

	const TMap<FIntVector, FDensityChunk>& GetDensityChunks() const { return DensityChunks; }
	const TMap<FIntVector, FContourChunk>& GetContourChunks() const { return ContourChunks; }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY()
	TMap<FIntVector, FDensityChunk> DensityChunks;

	UPROPERTY()
	TMap<FIntVector, FContourChunk> ContourChunks;

	UPROPERTY()
	FVectorInt LastBuiltCellCount;

	FVectorInt GetSampleDims() const { return FVectorInt(CellCount.X + 1, CellCount.Y + 1, CellCount.Z + 1); }
	void BuildCells();
	void SetDensity(int32 SampleX, int32 SampleY, int32 SampleZ, uint8 Value);
	void SetContourCell(int32 CellX, int32 CellY, int32 CellZ, const FDualContourCell& Cell);
	void RebuildCellsInRange(FVectorInt RangeMin, FVectorInt RangeMax);
	bool ValidateGenerationSettings() const;
	static uint16 PackLocalContourKey(int32 CellX, int32 CellY, int32 CellZ);
};
