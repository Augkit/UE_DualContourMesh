#pragma once

#include "Materials/MaterialExpression.h"
#include "MaterialExpressionDualContourMaterialID.generated.h"

/** Reads the four packed material IDs emitted in UV channels 1 and 2. */
UCLASS(CollapseCategories, HideCategories = Object)
class UMaterialExpressionDualContourMaterialID final : public UMaterialExpression
{
	GENERATED_BODY()

public:
	UMaterialExpressionDualContourMaterialID(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual EMaterialValueType GetOutputValueType(int32 OutputIndex) override;
#endif
};
