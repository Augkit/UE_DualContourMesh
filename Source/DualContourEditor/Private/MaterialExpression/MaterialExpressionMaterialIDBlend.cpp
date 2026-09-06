#include "MaterialExpressionMaterialIDBlend.h"

#include "Materials/MaterialAttributeDefinitionMap.h"
#include "MaterialCompiler.h"
#include "EdGraph/EdGraphNode.h"

UMaterialExpressionMaterialIDBlend::UMaterialExpressionMaterialIDBlend(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MenuCategories.Add(FText::FromString(TEXT("DualContour")));
	Outputs[0] = FExpressionOutput(TEXT("Material Attributes"));
	ResizeMaterialAttributes();
}

void UMaterialExpressionMaterialIDBlend::ResizeMaterialAttributes()
{
	MaterialCount = FMath::Clamp(MaterialCount, 1, 32);
	MaterialAttributes.SetNum(MaterialCount);
	CachedInputs.Reset();
}

#if WITH_EDITOR

int32 UMaterialExpressionMaterialIDBlend::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (OutputIndex != 0)
	{
		return Compiler->Errorf(TEXT("Invalid output index for Material ID Blend."));
	}

	const int32 CompiledWeights = Weights.Expression ? Weights.Compile(Compiler) : INDEX_NONE;
	const int32 CompiledIDs = MaterialIDs.Expression ? MaterialIDs.Compile(Compiler) : INDEX_NONE;
	if (CompiledWeights == INDEX_NONE || CompiledIDs == INDEX_NONE)
	{
		return Compiler->Errorf(TEXT("Material ID Blend requires Weights and Material IDs inputs."));
	}

	const int32 WeightsValue = Compiler->Saturate(CompiledWeights);
	const int32 IDsValue = Compiler->Floor(
		Compiler->Add(CompiledIDs, Compiler->Constant4(0.5f, 0.5f, 0.5f, 0.5f)));

	// The mesh has four palette slots. Each slot contributes to the matching
	// global material entry; duplicate IDs are intentionally accumulated.
	int32 ValidWeight = Compiler->Constant(0.0f);
	TArray<int32> EntryWeights;
	EntryWeights.Reserve(MaterialAttributes.Num());

	for (int32 EntryIndex = 0; EntryIndex < MaterialAttributes.Num(); ++EntryIndex)
	{
		const int32 Delta = Compiler->Abs(
			Compiler->Sub(IDsValue, Compiler->Constant4(
				static_cast<float>(EntryIndex),
				static_cast<float>(EntryIndex),
				static_cast<float>(EntryIndex),
				static_cast<float>(EntryIndex))));
		// IDs are floored integers, so 1 - saturate(abs(ID - EntryIndex)) is
		// exactly the equality mask and avoids a vector step dependency.
		const int32 Mask = Compiler->Sub(Compiler->Constant(1.0f), Compiler->Saturate(Delta));
		const int32 EntryWeight = Compiler->Dot(WeightsValue, Mask);
		EntryWeights.Add(EntryWeight);
		ValidWeight = Compiler->Add(ValidWeight, EntryWeight);
	}

	const FGuid AttributeID = Compiler->GetMaterialAttribute();

	// Material Attributes are compiled one property at a time. Do not pass the
	// MaterialAttributes value itself to Lerp; Lerp only accepts primitive types.
	int32 Result = FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, AttributeID);
	if (AttributeID == FMaterialAttributeDefinitionMap::GetID(MP_BaseColor))
	{
		// Preserve the original test behavior for the invalid-ID case.
		Result = Compiler->Constant3(1.0f, 0.0f, 1.0f);
	}

	int32 AccumulatedWeight = Compiler->Constant(0.0f);
	const MaterialAttributeBlendFunction BlendFunction = FMaterialAttributeDefinitionMap::GetBlendFunction(AttributeID);

	for (int32 EntryIndex = 0; EntryIndex < MaterialAttributes.Num(); ++EntryIndex)
	{
		const int32 AttributeValue = MaterialAttributes[EntryIndex].CompileWithDefault(Compiler, AttributeID);
		const int32 EntryWeight = EntryWeights[EntryIndex];
		const int32 NextAccumulatedWeight = Compiler->Add(AccumulatedWeight, EntryWeight);
		const int32 SafeNextAccumulatedWeight = Compiler->Max(NextAccumulatedWeight, Compiler->Constant(1.0e-5f));
		const int32 Alpha = Compiler->Div(EntryWeight, SafeNextAccumulatedWeight);

		if (BlendFunction)
		{
			Result = BlendFunction(Compiler, Result, AttributeValue, Alpha);
		}
		else
		{
			Result = Compiler->Lerp(Result, AttributeValue, Alpha);
		}
		AccumulatedWeight = NextAccumulatedWeight;
	}

	return Result;
}

void UMaterialExpressionMaterialIDBlend::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Material ID Blend"));
}

TArrayView<FExpressionInput*> UMaterialExpressionMaterialIDBlend::GetInputsView()
{
	CachedInputs.Reset();
	CachedInputs.Add(&Weights);
	CachedInputs.Add(&MaterialIDs);
	for (FMaterialAttributesInput& Attribute : MaterialAttributes)
	{
		CachedInputs.Add(&Attribute);
	}
	return CachedInputs;
}

FExpressionInput* UMaterialExpressionMaterialIDBlend::GetInput(int32 InputIndex)
{
	GetInputsView();
	return CachedInputs.IsValidIndex(InputIndex) ? CachedInputs[InputIndex] : nullptr;
}

FName UMaterialExpressionMaterialIDBlend::GetInputName(int32 InputIndex) const
{
	if (InputIndex == 0)
	{
		return TEXT("Weights");
	}
	if (InputIndex == 1)
	{
		return TEXT("Material IDs");
	}
	return MaterialAttributes.IsValidIndex(InputIndex - 2)
		       ? FName(*FString::Printf(TEXT("Material %d"), InputIndex - 2))
		       : NAME_None;
}

EMaterialValueType UMaterialExpressionMaterialIDBlend::GetInputValueType(int32 InputIndex)
{
	return InputIndex >= 2 ? MCT_MaterialAttributes : MCT_Float4;
}

EMaterialValueType UMaterialExpressionMaterialIDBlend::GetOutputValueType(int32 OutputIndex)
{
	return OutputIndex == 0 ? MCT_MaterialAttributes : MCT_Unknown;
}

void UMaterialExpressionMaterialIDBlend::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMaterialExpressionMaterialIDBlend,
		    MaterialCount))
	{
		ResizeMaterialAttributes();
		if (GraphNode)
		{
			GraphNode->ReconstructNode();
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif
