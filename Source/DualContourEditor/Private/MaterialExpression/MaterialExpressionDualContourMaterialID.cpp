#include "MaterialExpressionDualContourMaterialID.h"

#include "MaterialCompiler.h"

UMaterialExpressionDualContourMaterialID::UMaterialExpressionDualContourMaterialID(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MenuCategories.Add(FText::FromString(TEXT("DualContour")));
	Outputs[0] = FExpressionOutput(TEXT("Material ID"), 1, 1, 1, 1, 1);
}

#if WITH_EDITOR

int32 UMaterialExpressionDualContourMaterialID::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (OutputIndex != 0)
	{
		return Compiler->Errorf(TEXT("Invalid output index for Dual Contour Material ID."));
	}

	// UV1.xy stores PaletteIds.RG and UV2.xy stores PaletteIds.BA.
	return Compiler->AppendVector(
		Compiler->TextureCoordinate(1, false, false),
		Compiler->TextureCoordinate(2, false, false));
}

void UMaterialExpressionDualContourMaterialID::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Dual Contour Material ID"));
}

EMaterialValueType UMaterialExpressionDualContourMaterialID::GetOutputValueType(int32 OutputIndex)
{
	return OutputIndex == 0 ? MCT_Float4 : MCT_Unknown;
}

#endif
