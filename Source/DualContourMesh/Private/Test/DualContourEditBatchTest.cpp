#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DualContour.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourEditBatchTest,
	"DualContour.EditMode.RuntimeBatchSparseUndo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourEditBatchTest::RunTest(const FString& Parameters)
{
	UDualContour* DualContour = NewObject<UDualContour>(GetTransientPackage());
	DualContour->CellCount = FVectorInt(16, 16, 16);
	DualContour->CellSize = 10.0f;
	DualContour->VertexRelaxation = 0.0f;
	TestTrue(TEXT("Empty density field builds"), DualContour->Rebuild());
	int32 ChunkRebuildNotificationCount = 0;
	DualContour->OnDirtyChunksRebuilt.AddLambda(
		[&ChunkRebuildNotificationCount](const FDualContourDirtyRegion&)
		{
			++ChunkRebuildNotificationCount;
		});

	FDualContourEditBatch Batch = DualContour->BeginEditBatch();
	TestTrue(TEXT("Batch opens for current generated data"), Batch.bOpen);
	FDualContourBrushStamp Stamp;
	Stamp.Operation = EDualContourDensityEditOperation::Sculpt;
	Stamp.LocalCenter = FVector(80.0, 80.0, 80.0);
	Stamp.Radius = 40.0f;
	Stamp.Strength = 1.0f;
	Stamp.Falloff = 0.25f;
	TestTrue(TEXT("Sculpt modifies covered samples"), DualContour->ApplyBrushStamp(Batch, Stamp));

	FDualContourEditResult Result;
	TestTrue(TEXT("Batch flush produces a result"), DualContour->EndEditBatch(Batch, Result));
	TestEqual(TEXT("A batch flush sends one chunk rebuild notification"), ChunkRebuildNotificationCount, 1);
	TestFalse(TEXT("Undo data is sparse and non-empty"), Result.Deltas.IsEmpty());
	TestTrue(TEXT("Center density became solid"), DualContour->GetDensity(8, 8, 8) > GDualContourIsoValue);
	TestTrue(TEXT("Uncovered sample stays empty"), DualContour->GetDensity(0, 0, 0) == 0);
	TestTrue(TEXT("Dirty chunks were collected"), !Result.DirtyRegion.DensityChunks.IsEmpty());

	TestTrue(TEXT("Sparse undo applies"), DualContour->ApplyEditDeltas(Result.Deltas, false));
	TestEqual(TEXT("Undo restores center density"), DualContour->GetDensity(8, 8, 8), static_cast<uint8>(0));
	TestTrue(TEXT("Sparse redo applies"), DualContour->ApplyEditDeltas(Result.Deltas, true));
	TestTrue(TEXT("Redo restores sculpted center"), DualContour->GetDensity(8, 8, 8) > GDualContourIsoValue);
	return true;
}

#endif
