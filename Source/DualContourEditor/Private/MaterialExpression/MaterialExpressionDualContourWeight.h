#pragma once

#include "Materials/MaterialExpression.h"
#include "MaterialExpressionDualContourWeight.generated.h"

/** Reads the four packed normalized material weights emitted in Vertex Color. */
UCLASS(CollapseCategories, HideCategories = Object)
class UMaterialExpressionDualContourWeight final : public UMaterialExpression
{
	GENERATED_BODY()

public:
	UMaterialExpressionDualContourWeight(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual EMaterialValueType GetOutputValueType(int32 OutputIndex) override;
#endif
};
