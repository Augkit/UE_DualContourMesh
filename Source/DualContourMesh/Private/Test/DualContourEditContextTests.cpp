#include "DualContourEditContext.h"
#include "VolumeSampler/DualContourBrushSamplers.h"
#include "UObject/StrongObjectPtr.h"
#include "DualContour.h"
#include "DualContourUtils.h"
#include "Misc/AutomationTest.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"

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
	Sphere.TargetLocalCenter = FVector(Center);
	Sphere.Radius = 0.75f;
	FDualContourEditContext Edit(*Grid);
	TestTrue(TEXT("Stage solid density"), Edit.SetDensity(Center, 100.0f));
	TestTrue(TEXT("Material sees staged solid density"), Edit.ApplyMaterial(Sphere, FVector(8), 7, 0.0f));
	TestEqual(TEXT("Outside mask excluded even at zero threshold"), Edit.GetMaterial(FIntVector(1, 1, 1)), uint8(0));
	TestEqual(TEXT("No density write before commit"), Grid->GetDensity(2, 2, 2), uint16(0));
	TestEqual(TEXT("No material write before commit"), Grid->GetMaterialId(2, 2, 2), uint8(0));
	bool bMaterialObservedBoth = false;
	bool bDensityObservedBoth = false;
	TArray<FDualContourMaterialSampleDelta> MaterialDeltas;
	TestTrue(TEXT("Mixed commit"), Edit.Commit(
		[&](const FIntVector& Coord, uint16 Before, uint16 After)
		{
			bDensityObservedBoth = Grid->GetMaterialId(Coord.X, Coord.Y, Coord.Z) == 7 && Before == 0 &&
			                       After == FDensityChunk::EncodeDensity(100.0f);
		},
		[&](const FIntVector& Coord, uint8 Before, uint8 After)
		{
			MaterialDeltas.Add({Coord, Before, After});
			bMaterialObservedBoth = Grid->GetDensity(Coord.X, Coord.Y, Coord.Z) ==
			                        FDensityChunk::EncodeDensity(100.0f);
		}));
	TestTrue(TEXT("Material callback sees both final stores"), bMaterialObservedBoth);
	TestTrue(TEXT("Density callback sees both final stores"), bDensityObservedBoth);
	TestEqual(TEXT("One actual material delta"), MaterialDeltas.Num(), 1);
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
	MaterialDeltas.Reset();
	TestFalse(TEXT("Returning to original values has no changes"), NoOp.Commit(
		[](const FIntVector&, uint16, uint16) {},
		[&MaterialDeltas](const FIntVector& Coord, uint8 Before, uint8 After)
		{
			MaterialDeltas.Add({Coord, Before, After});
		}));
	TestTrue(TEXT("No spurious undo delta"), MaterialDeltas.IsEmpty());
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
	Shape.TargetLocalCenter = FVector(2);
	Shape.Radius = 4;
	Shape.Falloff = 0;
	TestTrue(TEXT("Smooth staged impulse"), Edit.ApplyDensity(EDualContourDensityOperation::Smooth, Shape, FVector(8)));
	TestEqual(TEXT("Gaussian center"), Edit.GetDensity(FIntVector(2)), 8.0f);
	TestEqual(TEXT("Symmetric negative neighbor"), Edit.GetDensity(FIntVector(1, 2, 2)), 4.0f);
	TestEqual(TEXT("Symmetric positive neighbor"), Edit.GetDensity(FIntVector(3, 2, 2)), 4.0f);
	Shape.Radius = 0.75f;
	TStrongObjectPtr<UDualContourPlaneVolumeSampler> PlaneOwner(NewObject<UDualContourPlaneVolumeSampler>());
	auto& Plane = *PlaneOwner;
	Plane.Initialize(Shape, FVector(2, 2, 3), FVector::UpVector, 64);
	Edit.ApplyDensity(EDualContourDensityOperation::Replace, Plane, FVector(8));
	TestEqual(TEXT("Plane sample"), Edit.GetDensity(FIntVector(2)), 64.0f);
	Edit.Commit();
	Grid->GetCell(0, 0, 0);
	TStrongObjectPtr<UDualContourVolumeBrushSampler> VolumeOwner(NewObject<UDualContourVolumeBrushSampler>());
	auto& Volume = *VolumeOwner;
	Volume.Initialize(*Grid, FTransform(FVector(10, 0, 0)));
	float Value = 0, Weight = 0;
	TestTrue(TEXT("Transformed volume sample"), Volume.Sample(FVector(12, 2, 2), FVolumeSamplerPlacement(), Value, Weight));
	TestEqual(TEXT("Volume value"), Value, 64.0f);
	TestFalse(TEXT("Outside volume"), Volume.Sample(FVector(9, 2, 2), FVolumeSamplerPlacement(), Value, Weight));
	FDualContourEditContext Difference(*Grid);
	TStrongObjectPtr<UDualContourVolumeBrushSampler> IdentityVolumeOwner(NewObject<UDualContourVolumeBrushSampler>());
	auto& IdentityVolume = *IdentityVolumeOwner;
	IdentityVolume.Initialize(*Grid, FTransform::Identity);
	TestTrue(TEXT("Parallel volume difference"), Difference.ApplyDensity(EDualContourDensityOperation::Difference, IdentityVolume, FVector(8)));
	TestEqual(TEXT("Difference uses source snapshot"), Difference.GetDensity(FIntVector(2)), -64.0f);
	Difference.Commit();
	Grid->GetCell(0, 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourSculptDensityUnitsTest, "DualContour.EditContext.SculptDensityUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourSculptDensityUnitsTest::RunTest(const FString& Parameters)
{
	for (const float CellSize : {1.0f, 10.0f, 25.0f})
	{
		TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
		Grid->CellCount = FIntVector(8);
		Grid->CellSize = CellSize;
		if (!TestTrue(TEXT("Initialize sculpt grid"), Grid->Rebuild()))
			return false;
		TStrongObjectPtr<UDualContourShapeVolumeSampler> Shape(NewObject<UDualContourShapeVolumeSampler>());
		Shape->TargetLocalCenter = FVector(4.0f * CellSize);
		Shape->Radius = 2.0f * CellSize;
		Shape->Falloff = 0.0f;
		FDualContourEditContext Edit(*Grid);
		const FIntVector Center(4);
		Edit.SetDensity(Center, 0.0f);
		constexpr float Strength = 0.3f;
		const float ExpectedDelta = Strength * CellSize * GDualContourLinearDensityFixedPointScale;
		if (!TestTrue(TEXT("Sculpt adds density"), Edit.ApplyDensity(EDualContourDensityOperation::Add, *Shape,
			FVector(4.0f * CellSize), Strength)))
			return false;
		TestTrue(TEXT("Sculpt strength scales with cell size"),
			FMath::IsNearlyEqual(Edit.GetDensity(Center), ExpectedDelta, 0.01f));
		TestTrue(TEXT("Sculpt subtracts density"), Edit.ApplyDensity(EDualContourDensityOperation::Subtract, *Shape,
			FVector(4.0f * CellSize), Strength));
		TestTrue(TEXT("Subtract reverses sculpt at the same strength"),
			FMath::IsNearlyZero(Edit.GetDensity(Center), 0.01f));
	}
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
		FDualContourPendingDensityBatch Batch;
		Batch.Owner = Grid;
		Batch.bOpen = true;
		TMap<uint16, FDualContourPendingDensitySample>& Chunk = Batch.ChunkSamples.FindOrAdd(FIntVector::ZeroValue);
		Chunk.Add(DualContourUtils::ChunkLocalIndex(2, 2, 2),
			{Grid->GetDensity(2, 2, 2), FDensityChunk::DecodeLinearDensity(Density)});
		return Grid->ApplyPendingDensityBatch(Batch);
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
	Sphere->Radius = 0.25f;
	const FTransform SphereTransform(FVector(4, 2, 2));
	FText Error;
	TestTrue(TEXT("Prepare existing SDF source"), static_cast<UVolumeSampler*>(Sphere.Get())->Prepare(Error));
	const FVolumeSamplerPlacement Placement = static_cast<UVolumeSampler*>(Sphere.Get())->MakePlacement(FVector(4), &SphereTransform);
	float Value = 0, Weight = 0;
	TestTrue(TEXT("Transformed center"), Sphere->Sample(FVector(4, 2, 2), Placement, Value, Weight));
	TestEqual(TEXT("Default influence"), Weight, 1.0f);
	TestFalse(TEXT("Outside finite bounds"), Sphere->Sample(FVector(1, 2, 2), Placement, Value, Weight));
	static_cast<UVolumeSampler*>(Sphere.Get())->Finish();

	FDualContourEditContext Edit(*Grid);
	TestTrue(TEXT("Existing source feeds edit"), Edit.ApplyDensity(EDualContourDensityOperation::Replace, *Sphere, FVector(4)));
	Edit.Commit();
	const uint16 Edited = Grid->GetDensity(4, 2, 2);
	TestTrue(TEXT("Same source feeds generation"), Grid->ReplaceDensityFromSampler(*Sphere, FVector(4), FTransform(FVector(2)), Error));
	TestEqual(TEXT("Generation and edit agree"), Grid->GetDensity(4, 2, 2), Edited);
	TStrongObjectPtr<UDualContourShapeVolumeSampler> Brush(NewObject<UDualContourShapeVolumeSampler>());
	Brush->TargetLocalCenter = FVector(3);
	Brush->Radius = 1.5f;
	TestTrue(TEXT("New source feeds generation"), Grid->ReplaceDensityFromSampler(*Brush, FVector(8), FTransform::Identity, Error));
	TestEqual(TEXT("New source center density"), Grid->GetDensity(3, 3, 3), FDensityChunk::EncodeDensity(GDualContourMaxLinearDensity));
	Grid->GetCell(0, 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourProceduralDistanceUnitsTest, "DualContour.EditContext.ProceduralDistanceUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourProceduralDistanceUnitsTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USphereVolumeSampler> Sphere(NewObject<USphereVolumeSampler>());
	Sphere->Radius = 0.25f;
	FText Error;
	if (!TestTrue(TEXT("Prepare procedural SDF"), static_cast<UVolumeSampler*>(Sphere.Get())->Prepare(Error)))
		return false;
	for (const double Size : {400.0, 640.0})
	{
		const FTransform Transform{FVector(Size)};
		for (const float SlopeLimit : {0.0f, GDualContourMaxLinearDensity / 40.0f})
		{
			const FVolumeSamplerPlacement Placement = Sphere->MakePlacement(FVector(Size), &Transform, SlopeLimit);
			float Inside = 0, Surface = 0, Weight = 0;
			if (!TestTrue(TEXT("Sample inside sphere"), Sphere->Sample(FVector(Size * 1.24, Size, Size), Placement, Inside, Weight))
			    || !TestTrue(TEXT("Sample sphere surface"), Sphere->Sample(FVector(Size * 1.25, Size, Size), Placement, Surface, Weight)))
				return false;
			const double ActualSlope = (Inside - Surface) / (Size * 0.01);
			const double UncappedSlope = Sphere->DensityScale * GDualContourLinearDensityFixedPointScale;
			const double SlopeBound = UncappedSlope * FMath::Sqrt(3.0);
			const double ExpectedSlope = SlopeLimit > 0 ? UncappedSlope * FMath::Min(1.0, SlopeLimit / SlopeBound) : UncappedSlope;
			TestTrue(TEXT("Procedural density slope remains in target-local units"), FMath::Abs(ActualSlope - ExpectedSlope) < 0.1);
		}
	}
	Sphere->Finish();
	return true;
}

#endif
