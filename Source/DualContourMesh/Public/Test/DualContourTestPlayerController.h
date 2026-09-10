#pragma once
#include "CoreMinimal.h"
#include "Test/DualContourVisualSweepPlayerController.h"
#include "DualContourTestPlayerController.generated.h"

class UVolumeSampler;
class ADualContourMeshActor;
class UDualContourModifierComponent;

UCLASS()
class DUALCONTOURMESH_API ADualContourTestPlayerController : public ADualContourVisualSweepPlayerController
{
	GENERATED_BODY()

public:
	ADualContourTestPlayerController();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour", meta = (ClampMin = "1.01"))
	float SamplerScaleStep = 1.25f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DualContour")
	TObjectPtr<UDualContourModifierComponent> ModifierComponent;

	/** Index into ModifierComponent->Samplers. Custom sampler arrays can select any valid index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour", meta = (ClampMin = "0"))
	int32 SelectedSamplerIndex = 0;

	/** Custom sampler instances appended to ModifierComponent->Samplers at input setup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "DualContour")
	TArray<TObjectPtr<UVolumeSampler>> AdditionalSamplers;

	/** Material ID assigned by the selected sampler when applying an edit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 MaterialId = 0;

	/** Selects a configured sampler, including custom sampler classes, by component-array index. */
	UFUNCTION(BlueprintCallable, Category = "DualContour")
	void SetSelectedSamplerIndex(int32 SamplerIndex);

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
	void InitializeSamplers();
	ADualContourMeshActor* FindDualContourMeshActor() const;
};
