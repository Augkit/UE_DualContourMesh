#pragma once
#include "CoreMinimal.h"
#include "Test/DualContourVisualSweepPlayerController.h"
#include "DualContourTestPlayerController.generated.h"

class UProceduralVolumeSampler;
class ADualContourMeshActor;

UCLASS()
class DUALCONTOURMESH_API ADualContourTestPlayerController : public ADualContourVisualSweepPlayerController
{
	GENERATED_BODY()

public:
	/** Uniform scale applied to the selected procedural sampler. Use [ and ] to adjust it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour", meta = (ClampMin = "0.01"))
	float SamplerScale = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour", meta = (ClampMin = "1.01"))
	float SamplerScaleStep = 1.25f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "DualContour")
	TObjectPtr<UProceduralVolumeSampler> SelectedSampler;

protected:
	virtual void SetupInputComponent() override;

private:
	void OnLeftClick();
	void OnRightClick();
	void ApplySelectedSampler(bool bExcavate);
	void SelectSphereSampler();
	void SelectBoxSampler();
	void SelectCylinderSampler();
	void SelectCapsuleSampler();
	void SelectTorusSampler();
	void DecreaseSamplerScale();
	void IncreaseSamplerScale();
	void SaveRuntimeDensityIncrement();
	void LoadRuntimeDensityIncrement();
	ADualContourMeshActor* FindDualContourMeshActor() const;
	void SelectSampler(TSubclassOf<UProceduralVolumeSampler> SamplerClass);
};
