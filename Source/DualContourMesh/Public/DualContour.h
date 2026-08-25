#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DualContourTypes.h"
#include "DualContour.generated.h"

/** Owns the dual-contour source data, generation settings, and contour-building algorithms. */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class DUALCONTOURMESH_API UDualContour : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVectorInt CellCount = FVectorInt(16, 16, 16);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	float CellSize = 10.f;

	/** True when generation settings have changed since the last successful rebuild. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour")
	bool bRebuildRequired = true;

	bool Rebuild();
	/** Replaces the complete sample grid and rebuilds contour cells. Samples are X-major. */
	bool SetDensitySamples(const TArray<uint8>& Samples);
	/** Modifies density and returns the half-open range of contour cells rebuilt by the operation. */
	bool ModifyDensityWithHemisphere(const FVector& LocalHitPos, const FVector& LocalHitNormal, float Radius,
		bool bExcavate, FVectorInt& OutAffectedCellMin, FVectorInt& OutAffectedCellMax);

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
