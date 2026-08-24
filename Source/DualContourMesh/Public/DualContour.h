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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVectorInt Divisions = FVectorInt(1, 1, 1);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TestSphere")
	FVector SphereCenter = FVector(80.f, 80.f, 80.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TestSphere")
	float SphereRadius = 60.f;

	/** True when generation settings have changed since the last successful rebuild. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour")
	bool bRebuildRequired = true;

	bool Rebuild();
	bool ModifyDensityWithHemisphere(const FVector& LocalHitPos, const FVector& LocalHitNormal, float Radius,
		bool bExcavate, TSet<int32>& OutAffectedDivisions);

	void FillSphereDensity();
	void BuildCells();
	uint8 GetSample(int32 SampleX, int32 SampleY, int32 SampleZ) const;
	const FDualContourCell* GetContourCell(int32 CellX, int32 CellY, int32 CellZ) const;
	float TrilinearSample(FVector GridPos) const;
	FVector ComputeGradient(FVector GridPos) const;
	bool HasCurrentGeneratedData() const;
	bool HasActiveCellInRange(FVectorInt CellMin, FVectorInt CellMax) const;

	int32 DivisionIndex(int32 DivX, int32 DivY, int32 DivZ) const;
	FVectorInt DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const;
	FVectorInt DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const;
	FVectorInt DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const;

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
	TMap<FIntVector, FDensityChunk> DensityChunks;
	TMap<FIntVector, FContourChunk> ContourChunks;

	FVectorInt LastBuiltCellCount;

	FVectorInt GetSampleDims() const { return FVectorInt(CellCount.X + 1, CellCount.Y + 1, CellCount.Z + 1); }
	void SetDensitySample(int32 SampleX, int32 SampleY, int32 SampleZ, uint8 Value);
	void SetContourCell(int32 CellX, int32 CellY, int32 CellZ, const FDualContourCell& Cell);
	void RebuildCellsInRange(FVectorInt RangeMin, FVectorInt RangeMax);
	bool ValidateGenerationSettings() const;
	static uint16 PackLocalContourKey(int32 CellX, int32 CellY, int32 CellZ);
};
