#include "DualContourEditContext.h"
#include "DualContourSamplerBuilder.h"
#include "VolumeSampler/DualContourBrushSamplers.h"
#include "UObject/StrongObjectPtr.h"
#include "DualContour.h"
#include "DualContourUtils.h"
#include "Misc/AutomationTest.h"

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
	TStrongObjectPtr<UDualContourShapeVolumeSampler> SphereOwner(NewObject<UDualContourShapeVolumeSampler>());
	auto& Sphere = *SphereOwner;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourFieldSamplerTest, "DualContour.EditContext.Samplers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourFieldSamplerTest::RunTest(const FString& Parameters)
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
	TStrongObjectPtr<UDualContourShapeVolumeSampler> ShapeOwner(NewObject<UDualContourShapeVolumeSampler>());
	auto& Shape = *ShapeOwner;
	Shape.Center = FVector(2);
	Shape.Radius = 4;
	Shape.Falloff = 0;
	TestTrue(TEXT("Smooth staged impulse"), Edit.ApplyDensity(EDualContourDensityOperation::Smooth, Shape));
	TestEqual(TEXT("Gaussian center"), Edit.GetDensity(FIntVector(2)), 8.0f);
	TestEqual(TEXT("Symmetric negative neighbor"), Edit.GetDensity(FIntVector(1, 2, 2)), 4.0f);
	TestEqual(TEXT("Symmetric positive neighbor"), Edit.GetDensity(FIntVector(3, 2, 2)), 4.0f);
	Shape.Radius = 0.75f;
	TStrongObjectPtr<UDualContourPlaneVolumeSampler> PlaneOwner(NewObject<UDualContourPlaneVolumeSampler>());
	auto& Plane = *PlaneOwner;
	Plane.Initialize(Shape, FVector(2, 2, 3), FVector::UpVector, 64);
	Edit.ApplyDensity(EDualContourDensityOperation::Replace, Plane);
	TestEqual(TEXT("Plane sample"), Edit.GetDensity(FIntVector(2)), 64.0f);
	Edit.Commit();
	Grid->GetCell(0, 0, 0);
	TStrongObjectPtr<UDualContourVolumeBrushSampler> VolumeOwner(NewObject<UDualContourVolumeBrushSampler>());
	auto& Volume = *VolumeOwner;
	Volume.Initialize(*Grid, FTransform(FVector(10, 0, 0)));
	float Value = 0, Weight = 0;
	TestTrue(TEXT("Transformed volume sample"), Volume.Sample(FVector(12, 2, 2), Value, Weight));
	TestEqual(TEXT("Volume value"), Value, 64.0f);
	TestFalse(TEXT("Outside volume"), Volume.Sample(FVector(9, 2, 2), Value, Weight));
	FDualContourEditContext Difference(*Grid);
	TStrongObjectPtr<UDualContourVolumeBrushSampler> IdentityVolumeOwner(NewObject<UDualContourVolumeBrushSampler>());
	auto& IdentityVolume = *IdentityVolumeOwner;
	IdentityVolume.Initialize(*Grid, FTransform::Identity);
	TestTrue(TEXT("Parallel volume difference"), Difference.ApplyDensity(EDualContourDensityOperation::Difference, IdentityVolume));
	TestEqual(TEXT("Difference uses source snapshot"), Difference.GetDensity(FIntVector(2)), -64.0f);
	Difference.Commit();
	Grid->GetCell(0, 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourPendingBatchEditTest, "DualContour.EditContext.PendingBatch",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourPendingBatchEditTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
	Grid->CellCount = FIntVector(4);
	Grid->Rebuild();
	Grid->GetCell(0, 0, 0);
	auto ApplyDensity = [Grid = Grid.Get()](uint16 Density)
	{
		FDualContourPendingBatch Batch;
		Batch.Owner = Grid;
		Batch.bOpen = true;
		TMap<uint16, FDualContourPendingSample>& Chunk = Batch.ChunkSamples.FindOrAdd(FIntVector::ZeroValue);
		Chunk.Add(DualContourUtils::ChunkLocalIndex(2, 2, 2),
		          {Grid->GetDensity(2, 2, 2), FDensityChunk::DecodeLinearDensity(Density)});
		return Grid->ApplyPendingBatch(Batch);
	};
	TestTrue(TEXT("Pending union"), ApplyDensity(FDensityChunk::EncodeDensity(100)));
	TestEqual(TEXT("Union density"), Grid->GetLinearDensity(2, 2, 2), 100.0f);
	TestTrue(TEXT("Pending difference"), ApplyDensity(FDensityChunk::EncodeDensity(-100)));
	TestEqual(TEXT("Difference density"), Grid->GetLinearDensity(2, 2, 2), -100.0f);
	TestFalse(TEXT("No-op pending batch"), ApplyDensity(FDensityChunk::EncodeDensity(-100)));
	Grid->GetCell(0, 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourUnifiedVolumeTest, "DualContour.EditContext.UnifiedVolumeSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDualContourUnifiedVolumeTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
	Grid->CellCount = FIntVector(8);
	Grid->CellSize = 1;
	Grid->Rebuild();
	Grid->GetCell(0, 0, 0);
	TStrongObjectPtr<USphereVolumeSampler> Sphere(NewObject<USphereVolumeSampler>());
	Sphere->VolumeSize = FVector(4);
	Sphere->Radius = 1;
	Sphere->SamplingTransform = FTransform(FVector(2, 0, 0));
	FText Error;
	TestTrue(TEXT("Prepare existing SDF source"), static_cast<UVolumeSampler*>(Sphere.Get())->Prepare(Error));
	float Value = 0, Weight = 0;
	TestTrue(TEXT("Transformed center"), Sphere->Sample(FVector(4, 2, 2), Value, Weight));
	TestEqual(TEXT("Default influence"), Weight, 1.0f);
	TestFalse(TEXT("Outside finite bounds"), Sphere->Sample(FVector(1, 2, 2), Value, Weight));
	static_cast<UVolumeSampler*>(Sphere.Get())->Finish();

	FDualContourEditContext Edit(*Grid);
	TestTrue(TEXT("Existing source feeds edit"), Edit.ApplyDensity(EDualContourDensityOperation::Replace, *Sphere));
	Edit.Commit();
	const uint16 Edited = Grid->GetDensity(4, 2, 2);
	FDualContourSampledRegion SampledRegion;
	TestTrue(TEXT("Same source feeds generation"), FDualContourSamplerBuilder::BuildDensityChunks(*Sphere, Grid.Get(), FTransform::Identity, SampledRegion, Error));
	TestTrue(TEXT("Replace generated density"), Grid->ReplaceDensityChunks(MoveTemp(SampledRegion)));
	TestEqual(TEXT("Generation and edit agree"), Grid->GetDensity(4, 2, 2), Edited);
	TStrongObjectPtr<UDualContourShapeVolumeSampler> Brush(NewObject<UDualContourShapeVolumeSampler>());
	Brush->Center = FVector(3);
	Brush->Radius = 1.5f;
	SampledRegion.Reset();
	TestTrue(TEXT("New source feeds generation"), FDualContourSamplerBuilder::BuildDensityChunks(*Brush, Grid.Get(), FTransform::Identity, SampledRegion, Error));
	TestTrue(TEXT("Replace brush density"), Grid->ReplaceDensityChunks(MoveTemp(SampledRegion)));
	TestEqual(TEXT("New source center density"), Grid->GetDensity(3, 3, 3), FDensityChunk::EncodeDensity(GDualContourMaxLinearDensity));
	Grid->GetCell(0, 0, 0);
	return true;
}

#endif
