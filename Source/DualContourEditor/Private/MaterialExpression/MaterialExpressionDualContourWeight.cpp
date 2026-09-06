#include "MaterialExpressionDualContourWeight.h"

#include "MaterialCompiler.h"

UMaterialExpressionDualContourWeight::UMaterialExpressionDualContourWeight(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MenuCategories.Add(FText::FromString(TEXT("DualContour")));
	Outputs[0] = FExpressionOutput(TEXT("Weight"), 1, 1, 1, 1, 1);
}

#if WITH_EDITOR

int32 UMaterialExpressionDualContourWeight::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (OutputIndex != 0)
	{
		return Compiler->Errorf(TEXT("Invalid output index for Dual Contour Weight."));
	}

	return Compiler->VertexColor();
}

void UMaterialExpressionDualContourWeight::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Dual Contour Weight"));
}

EMaterialValueType UMaterialExpressionDualContourWeight::GetOutputValueType(int32 OutputIndex)
{
	return OutputIndex == 0 ? MCT_Float4 : MCT_Unknown;
}

#endif
