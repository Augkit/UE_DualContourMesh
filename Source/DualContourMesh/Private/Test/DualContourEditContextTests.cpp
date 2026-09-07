#include "DualContourEditContext.h"
#include "DualContour.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourEditContextTest, "DualContour.EditContext.BatchLifecycle",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourEditContextTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
	Grid->CellCount = FIntVector(4);
	Grid->CellSize = 1.0f;
	TestTrue(TEXT("Initialize"), Grid->Rebuild());
	Grid->GetCell(0, 0, 0);
	const FIntVector Center(2);
	FDualContourShapeSampler Sphere;
	Sphere.Center = FVector(Center);
	Sphere.Radius = 0.75f;
	FDualContourEditContext Edit(*Grid);
	TestTrue(TEXT("Stage solid density"), Edit.SetDensity(Center, 100.0f));
	TestTrue(TEXT("Material sees staged solid density"), Edit.ApplyMaterial(Sphere, 7, 0.0f));
	TestEqual(TEXT("Outside mask excluded even at zero threshold"), Edit.GetMaterial(FIntVector(1, 1, 1)), uint8(0));
	TestEqual(TEXT("No density write before commit"), Grid->GetDensity(2, 2, 2), uint16(0));
	TestEqual(TEXT("No material write before commit"), Grid->GetMaterialId(2, 2, 2), uint8(0));
	bool bObservedBoth = false;
	FDualContourMaterialEditResult Result;
	TestTrue(TEXT("Mixed commit"), Edit.Commit(Result,
	                                           [&](const FIntVector& Coord, uint16 Before, uint16 After)
	                                           {
		                                           bObservedBoth = Grid->GetMaterialId(Coord.X, Coord.Y, Coord.Z) == 7 && Before == 0 &&
		                                                           After == FDensityChunk::EncodeDensity(100.0f);
	                                           }));
	TestTrue(TEXT("Callback sees both final stores"), bObservedBoth);
	TestEqual(TEXT("One actual material delta"), Result.Deltas.Num(), 1);
	TestTrue(TEXT("Density save overlay"), !Grid->GetModifiedDensityChunks().IsEmpty());
	TestTrue(TEXT("Material save overlay"), !Grid->GetModifiedMaterialChunks().IsEmpty());
	TestFalse(TEXT("Cannot submit twice"), Edit.Commit());
	TestFalse(TEXT("Cannot write after commit"), Edit.SetDensity(Center, 200.0f));
	{
		FDualContourEditContext Discard(*Grid);
		Discard.SetMaterial(Center, 9);
	}
	TestEqual(TEXT("Destruction discards"), Grid->GetMaterialId(2, 2, 2), uint8(7));
	FDualContourEditContext NoOp(*Grid);
	NoOp.SetDensity(Center, 200.0f);
	NoOp.SetDensity(Center, 100.0f);
	NoOp.SetMaterial(Center, 8);
	NoOp.SetMaterial(Center, 7);
	TestFalse(TEXT("Returning to original values has no changes"), NoOp.Commit(Result));
	TestTrue(TEXT("No spurious undo delta"), Result.IsEmpty());
	Grid->GetCell(0, 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourEditSamplerTest, "DualContour.EditContext.Samplers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourEditSamplerTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
	Grid->CellCount = FIntVector(4);
	Grid->CellSize = 1;
	Grid->Rebuild();
	Grid->GetCell(0, 0, 0);
	FDualContourEditContext Edit(*Grid);
	for (int32 Z = 0; Z <= 4; ++Z)
		for (int32 Y = 0; Y <= 4; ++Y)
			for (int32 X = 0; X <= 4; ++X)
				Edit.SetDensity(FIntVector(X, Y, Z), 0);
	Edit.SetDensity(FIntVector(2), 64);
	FDualContourShapeSampler Shape;
	Shape.Center = FVector(2);
	Shape.Radius = 4;
	Shape.Falloff = 0;
	TestTrue(TEXT("Smooth staged impulse"), Edit.ApplyDensity(Shape, EDualContourEditOperation::Smooth));
	TestEqual(TEXT("Gaussian center"), Edit.GetDensity(FIntVector(2)), 8.0f);
	TestEqual(TEXT("Symmetric negative neighbor"), Edit.GetDensity(FIntVector(1, 2, 2)), 4.0f);
	TestEqual(TEXT("Symmetric positive neighbor"), Edit.GetDensity(FIntVector(3, 2, 2)), 4.0f);
	Shape.Radius = 0.75f;
	FDualContourPlaneSampler Plane(Shape, FVector(2, 2, 3), FVector::UpVector, 64);
	Edit.ApplyDensity(Plane, EDualContourEditOperation::Replace);
	TestEqual(TEXT("Plane sample"), Edit.GetDensity(FIntVector(2)), 64.0f);
	Edit.Commit();
	Grid->GetCell(0, 0, 0);
	FDualContourVolumeSampler Volume(*Grid, FTransform(FVector(10, 0, 0)));
	float Value = 0, Weight = 0;
	TestTrue(TEXT("Transformed volume sample"), Volume.Sample(FVector(12, 2, 2), Value, Weight));
	TestEqual(TEXT("Volume value"), Value, 64.0f);
	TestFalse(TEXT("Outside volume"), Volume.Sample(FVector(9, 2, 2), Value, Weight));
	FDualContourEditContext Difference(*Grid);
	FDualContourVolumeSampler IdentityVolume(*Grid, FTransform::Identity);
	TestTrue(TEXT("Parallel volume difference"), Difference.ApplyDensity(IdentityVolume, EDualContourEditOperation::Difference));
	TestEqual(TEXT("Difference uses source snapshot"), Difference.GetDensity(FIntVector(2)), -64.0f);
	Difference.Commit();
	Grid->GetCell(0, 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourSampledRegionEditTest, "DualContour.EditContext.SampledRegion",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourSampledRegionEditTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
	Grid->CellCount = FIntVector(4);
	Grid->Rebuild();
	Grid->GetCell(0, 0, 0);
	FDualContourSampledRegion Region;
	Region.SampleMin = FIntVector(2);
	Region.SampleDimensions = FIntVector(1);
	Region.Chunks.AddDefaulted();
	Region.Chunks[0].Density.UniformValue = FDensityChunk::EncodeDensity(100);
	FIntVector Min, Max;
	TestTrue(TEXT("Region union via context"), Grid->ModifyDensityChunks(Region, false, Min, Max));
	TestEqual(TEXT("Union density"), Grid->GetLinearDensity(2, 2, 2), 100.0f);
	TestEqual(TEXT("Affected min"), Min, FIntVector(1));
	TestEqual(TEXT("Affected exclusive max"), Max, FIntVector(3));
	TestTrue(TEXT("Region difference"), Grid->ModifyDensityChunks(Region, true, Min, Max));
	TestEqual(TEXT("Difference density"), Grid->GetLinearDensity(2, 2, 2), -100.0f);
	Region.Chunks[0].Density.UniformValue = 0;
	TestFalse(TEXT("Empty samples are neutral"), Grid->ModifyDensityChunks(Region, false, Min, Max));
	Region.Chunks.AddDefaulted();
	FDualContourEditContext Invalid(*Grid);
	TestFalse(TEXT("Duplicate chunks rejected"), Invalid.ApplySampledRegion(Region, false));
	TestFalse(TEXT("Invalid region stages nothing"), Invalid.Commit());
	Grid->GetCell(0, 0, 0);
	return true;
}

#endif
