#pragma once

#include "Materials/MaterialExpression.h"
#include "MaterialExpressionMaterialIDBlend.generated.h"

/**
 * Selects and blends Material Attributes by the integer IDs stored in a float4.
 * The four weight and ID channels correspond to the four palette slots written
 * by the dual-contour mesh. Material Attributes inputs are the global palette.
 */
UCLASS(CollapseCategories, HideCategories = Object)
class UMaterialExpressionMaterialIDBlend final : public UMaterialExpression
{
	GENERATED_BODY()

public:
	UMaterialExpressionMaterialIDBlend(const FObjectInitializer& ObjectInitializer);

	/** Number of global material entries exposed as Material Attributes pins. */
	UPROPERTY(EditAnywhere, Category = MaterialIDBlend, meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaterialCount = 4;

	/** Per-vertex blend weights. RGB and A are the four palette-slot weights. */
	UPROPERTY()
	FExpressionInput Weights;

	/** Per-vertex material IDs. XYZW are the four palette-slot IDs. */
	UPROPERTY()
	FExpressionInput MaterialIDs;

	/** Global material palette, indexed by Material ID. */
	UPROPERTY()
	TArray<FMaterialAttributesInput> MaterialAttributes;

#if WITH_EDITOR
	virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual TArrayView<FExpressionInput*> GetInputsView() override;
	virtual FExpressionInput* GetInput(int32 InputIndex) override;
	virtual FName GetInputName(int32 InputIndex) const override;
	virtual EMaterialValueType GetInputValueType(int32 InputIndex) override;
	virtual EMaterialValueType GetOutputValueType(int32 OutputIndex) override;
	virtual bool IsResultMaterialAttributes(int32 OutputIndex) override { return true; }

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void ResizeMaterialAttributes();

	TArray<FExpressionInput*> CachedInputs;
};
