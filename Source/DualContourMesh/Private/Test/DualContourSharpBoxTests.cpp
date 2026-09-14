#include "DualContour.h"
#include "DualContourEditContext.h"
#include "DualContourMeshBuilder.h"
#include "DualContourPlaneFit.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourSharpBoxTest, "DualContour.Geometry.SharpBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourSharpBoxTest::RunTest(const FString& Parameters)
{
	for (double Yaw : {0.0, 30.0, 45.0})
	{
		for (const FVector Offset : {FVector::ZeroVector, FVector(2.3, -1.7, 0.6)})
		{
			TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
			Grid->VertexRelaxation = 0.0f;
			TStrongObjectPtr<UBoxVolumeSampler> Box(NewObject<UBoxVolumeSampler>());
			const FTransform BoxTransform(FRotator(0, Yaw, 0), FVector(320) + Offset);
			const FVector Center = FVector(320) + Offset;
			FText Error;
			if (!TestTrue(TEXT("Sample default box"), Grid->ApplySampler(*Box, FVector(Grid->CellCount) * Grid->CellSize, BoxTransform, Error)))
				return false;
			FDualContourMeshData Mesh;
			FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, Mesh);
			TestTrue(TEXT("Box has triangles"), !Mesh.Indices.IsEmpty());
			TestEqual(TEXT("Cell size unchanged"), Grid->CellSize, 10.0f);
			TestEqual(TEXT("Cell count unchanged"), Grid->CellCount, FIntVector(64));
			const FQuat Rotation = BoxTransform.GetRotation();
			double SurfaceErrorSum = 0.0, NormalErrorSum = 0.0, MaxSurfaceError = 0.0;
			int32 Samples = 0;
			for (int32 Index = 0; Index < Mesh.Positions.Num(); ++Index)
			{
				const FVector P = Mesh.Positions[Index];
				// A default 512-unit box rotated in the 640-unit grid clips at its XY corners.
				// Measure the top/side rim away from that finite-volume boundary and box corners.
				if (P.X < 30 || P.Y < 30 || P.X > 610 || P.Y > 610 || P.Z < Center.Z + 240 || P.Z > Center.Z + 265)
					continue;
				const FVector Local = Rotation.UnrotateVector(P - Center);
				if (FMath::Max(FMath::Abs(Local.X), FMath::Abs(Local.Y)) < 240)
					continue;
				const double SurfaceError = FMath::Abs(Box->GetSignedDistance_Implementation(Local / 640.0));
				SurfaceErrorSum += SurfaceError;
				MaxSurfaceError = FMath::Max(MaxSurfaceError, SurfaceError);
				const FVector Normal = Rotation.UnrotateVector(Mesh.Normals[Index]);
				NormalErrorSum += FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.GetAbs().GetMax(), 0.0, 1.0)));
				TestTrue(TEXT("Finite vertex and unit normal"), !P.ContainsNaN() && Mesh.Normals[Index].IsNormalized());
				++Samples;
			}
			TestTrue(TEXT("Rim measured"), Samples > 0);
			TestTrue(TEXT("Rim mean surface error below 0.015 units at unchanged resolution"), SurfaceErrorSum / FMath::Max(Samples, 1) < 0.015);
			TestTrue(TEXT("No isolated rim vertex exceeds 0.05 units of surface error"), MaxSurfaceError < 0.05);
			TestTrue(TEXT("Hard-face normal mean error below 1.2 degrees"), NormalErrorSum / FMath::Max(Samples, 1) < 1.2);
			AddInfo(FString::Printf(TEXT("Yaw=%.0f Offset=%s Samples=%d MeanSurfaceError=%.6f MaxSurfaceError=%.6f MeanNormalAngle=%.6f"),
				Yaw, *Offset.ToString(), Samples, SurfaceErrorSum / FMath::Max(Samples, 1), MaxSurfaceError, NormalErrorSum / FMath::Max(Samples, 1)));
			// Distance to either face alone hides steps that slide down a side wall.
			// Measure both coordinates of the analytic top/side intersection instead.
			double MaxEdgeDistance = 0.0;
			int32 EdgeCells = 0;
			for (int32 Y = 3; Y < 60; ++Y)
				for (int32 X = 3; X < 60; ++X)
				{
					const FDualContourCell* Cell = Grid->GetCell(X, Y, FMath::FloorToInt((Center.Z + 256) / 10));
					if (!Cell)
						continue;
					for (int32 Axis = 0; Axis < 2; ++Axis)
						for (double Sign : {-1.0, 1.0})
						{
							double Min = TNumericLimits<double>::Max(), Max = TNumericLimits<double>::Lowest();
							for (int32 Corner = 0; Corner < 4; ++Corner)
							{
								const FVector P = Rotation.UnrotateVector(FVector((X + (Corner & 1)) * 10,
									(Y + ((Corner >> 1) & 1)) * 10, Center.Z + 256) - Center);
								Min = FMath::Min(Min, Sign * P[Axis]);
								Max = FMath::Max(Max, Sign * P[Axis]);
							}
							const FVector P = Rotation.UnrotateVector(Cell->Center - Center);
							if (Min > 256 || Max < 256 || FMath::Abs(P[1 - Axis]) > 220)
								continue;
							MaxEdgeDistance = FMath::Max(MaxEdgeDistance, FVector2D(P.Z - 256, Sign * P[Axis] - 256).Length());
							++EdgeCells;
						}
				}
			TestTrue(TEXT("Analytic crease cells measured"), EdgeCells > 0);
			TestTrue(TEXT("Crease vertices stay within 0.05 units of the true line"), MaxEdgeDistance < 0.05);
			AddInfo(FString::Printf(TEXT("Yaw=%.0f EdgeCells=%d MaxEdgeDistance=%.6f"), Yaw, EdgeCells, MaxEdgeDistance));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourQEFDivisionTest, "DualContour.Geometry.QEFDivisionAndEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourQEFDivisionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
	Grid->VertexRelaxation = 0.0f;
	TStrongObjectPtr<UBoxVolumeSampler> Box(NewObject<UBoxVolumeSampler>());
	const FTransform BoxTransform(FRotator(0, 45, 0), FVector(322.3, 318.3, 320.6));
	FText Error;
	if (!TestTrue(TEXT("Sample translated rotated box"), Grid->ApplySampler(*Box, FVector(Grid->CellCount) * Grid->CellSize, BoxTransform, Error)))
		return false;
	// Change samples on both sides of a density-chunk boundary near the top rim.
	FDualContourEditContext Edit(*Grid);
	for (int32 X : {47, 48})
		Edit.SetDensity(FIntVector(X, 52, 57), Edit.GetDensity(FIntVector(X, 52, 57)) - 2048.0f);
	TestTrue(TEXT("Commit local edit"), Edit.Commit());
	FDualContourMeshData Incremental, Whole;
	FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, Incremental);
	Grid->Rebuild();
	FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, Whole);
	TestEqual(TEXT("Incremental and full rebuild topology"), Incremental.Indices.Num(), Whole.Indices.Num());
	if (Incremental.Positions.Num() != Whole.Positions.Num())
		return false;
	bool bSameEdits = true;
	for (int32 Index = 0; Index < Whole.Positions.Num(); ++Index)
		bSameEdits &= Incremental.Positions[Index].Equals(Whole.Positions[Index], 1.e-6)
			&& Incremental.Normals[Index].Equals(Whole.Normals[Index], 1.e-6);
	TestTrue(TEXT("Local stencil invalidation matches a full rebuild"), bSameEdits);
	TMap<FVector, TArray<int32>> QuadsByFirstCorner;
	for (int32 Index = 0; Index < Whole.Positions.Num(); Index += 4)
		QuadsByFirstCorner.FindOrAdd(Whole.Positions[Index]).Add(Index);
	int32 DividedIndexCount = 0;
	bool bSameDivisions = true;
	for (int32 Z = 0; Z < 2; ++Z)
		for (int32 Y = 0; Y < 2; ++Y)
			for (int32 X = 0; X < 2; ++X)
			{
				FDualContourMeshData Division;
				const FIntVector Min(X * 32, Y * 32, Z * 32);
				FDualContourMeshBuilder::Build(*Grid, Min, Min + FIntVector(32), Division);
				DividedIndexCount += Division.Indices.Num();
				for (int32 Index = 0; Index < Division.Positions.Num(); Index += 4)
				{
					bool bFound = false;
					if (const TArray<int32>* Candidates = QuadsByFirstCorner.Find(Division.Positions[Index]))
						for (int32 Candidate : *Candidates)
						{
							bool bMatches = true;
							for (int32 Corner = 0; Corner < 4; ++Corner)
								bMatches &= Division.Positions[Index + Corner].Equals(Whole.Positions[Candidate + Corner], 1.e-6)
									&& Division.Normals[Index + Corner].Equals(Whole.Normals[Candidate + Corner], 1.e-6);
							bFound |= bMatches;
						}
					bSameDivisions &= bFound;
				}
			}
	TestEqual(TEXT("Halo emits no additional triangles"), DividedIndexCount, Whole.Indices.Num());
	TestTrue(TEXT("Eight divisions preserve whole-mesh positions and normals"), bSameDivisions);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourQEFSphereTest, "DualContour.Geometry.QEFSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourQEFSphereTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
	Grid->VertexRelaxation = 0.0f;
	TStrongObjectPtr<USphereVolumeSampler> Sphere(NewObject<USphereVolumeSampler>());
	FText Error;
	if (!TestTrue(TEXT("Sample smooth sphere"), Grid->ApplySampler(*Sphere, FVector(Grid->CellCount) * Grid->CellSize, FTransform(FVector(320)), Error)))
		return false;
	FDualContourMeshData Mesh;
	FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, Mesh);
	double MaxDistance = 0, MaxAngle = 0;
	for (int32 Index = 0; Index < Mesh.Positions.Num(); ++Index)
	{
		const FVector Radial = Mesh.Positions[Index] - FVector(320);
		MaxDistance = FMath::Max(MaxDistance, FMath::Abs(Radial.Length() - Sphere->Radius));
		MaxAngle = FMath::Max(MaxAngle, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Radial.GetSafeNormal(), Mesh.Normals[Index]), -1.0, 1.0))));
	}
	TestTrue(TEXT("Smooth sphere retains its surface"), MaxDistance < 1.0);
	TestTrue(TEXT("Smooth sphere retains smooth outward normals"), MaxAngle < 8.0);
	AddInfo(FString::Printf(TEXT("Sphere max distance=%.6f max normal angle=%.6f"), MaxDistance, MaxAngle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourCoarseStampTest, "DualContour.Geometry.CoarseStamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourCoarseStampTest::RunTest(const FString& Parameters)
{
	for (int32 PlacementCase = 0; PlacementCase < 3; ++PlacementCase)
	{
	for (bool bDifference : {false, true})
	{
		TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
		Grid->CellCount = FIntVector(128);
		Grid->CellSize = 25.0f;
		TStrongObjectPtr<UBoxVolumeSampler> Ground(NewObject<UBoxVolumeSampler>());
		Ground->HalfExtents = bDifference ? FVector(600) : FVector(600, 600, 200);
		FText Error;
		TestTrue(TEXT("Build underlying terrain volume"), Grid->ApplySampler(*Ground, FVector(1600),
			FTransform(FVector(1600, 1600, bDifference ? 1600 : 1300)), Error));
		TStrongObjectPtr<UBoxVolumeSampler> Box(NewObject<UBoxVolumeSampler>());
		const FQuat Rotation = FRotator(PlacementCase == 2 ? 15 : 0, 45, PlacementCase == 2 ? 8 : 0).Quaternion();
		const FVector Center = PlacementCase == 0 ? FVector(1602.3, 1598.3, 1600.6) : FVector(1612.5, 1607.3, 1617.8);
		FDualContourEditContext Edit(*Grid);
		TestTrue(TEXT("Stage box using brush stamp path"), Edit.ApplyDensity(bDifference
			? EDualContourDensityOperation::Difference : EDualContourDensityOperation::Union,
			*Box, FVector(640), FTransform(Rotation, Center)));
		TestTrue(TEXT("Commit stamp"), Edit.Commit());
		FDualContourMeshData Mesh;
		FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, Mesh);
		double MaxError = 0, ErrorSum = 0, NormalSum = 0;
		FVector WorstPosition = FVector::ZeroVector;
		int32 Count = 0;
		for (int32 Index = 0; Index < Mesh.Positions.Num(); ++Index)
		{
			const FVector P = Rotation.UnrotateVector(Mesh.Positions[Index] - Center);
			const double Side = FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y));
			if (P.Z < 220 || P.Z > 280 || Side < 220 || Side > 280
				|| FMath::Min(FMath::Abs(P.X), FMath::Abs(P.Y)) > 200)
				continue;
			const double Distance = FMath::Abs(Box->GetSignedDistance_Implementation(P / 640.0));
			if (Distance > MaxError)
			{
				MaxError = Distance;
				WorstPosition = Mesh.Positions[Index];
			}
			ErrorSum += Distance;
			const FVector N = Rotation.UnrotateVector(Mesh.Normals[Index]);
			NormalSum += FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(N.GetAbs().GetMax(), 0.0, 1.0)));
			++Count;
		}
		TestTrue(TEXT("Coarse stamp rim measured"), Count > 0);
		AddInfo(FString::Printf(TEXT("CoarseStamp Placement=%d Difference=%d Count=%d MaxError=%.6f MeanError=%.6f MeanNormalAngle=%.6f"),
			PlacementCase, bDifference, Count, MaxError, ErrorSum / FMath::Max(Count, 1), NormalSum / FMath::Max(Count, 1)));
		TestTrue(TEXT("Coarse rim max error under 0.1 units"), MaxError < 0.1);
		if (MaxError >= 0.1)
		{
			const FIntVector C(FMath::FloorToInt(WorstPosition.X / 25), FMath::FloorToInt(WorstPosition.Y / 25),
				FMath::FloorToInt(WorstPosition.Z / 25));
			double M[3][3] = {}, R[3] = {};
			int32 N = 0;
			FVector Normal;
			bool bSharp = false;
			const bool bFit = DualContourPlaneFit::Fit(*Grid, C, M, R, N, Normal, bSharp);
			AddInfo(FString::Printf(TEXT("Worst cell=%s position=%s fit=%d count=%d normal=%s rhs=(%.5f,%.5f,%.5f)"),
				*C.ToString(), *WorstPosition.ToString(), bFit, N, *Normal.ToString(), R[0], R[1], R[2]));
		}
		// Preserve recovered features even after a full rebuild with default relaxation.
		Grid->Rebuild();
		FDualContourMeshData Rebuilt;
		FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, Rebuilt);
		double RebuiltMaxError = 0;
		for (const FVector& Position : Rebuilt.Positions)
		{
			const FVector P = Rotation.UnrotateVector(Position - Center);
			const double Side = FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y));
			if (P.Z < 220 || P.Z > 280 || Side < 220 || Side > 280 || FMath::Min(FMath::Abs(P.X), FMath::Abs(P.Y)) > 200)
				continue;
			RebuiltMaxError = FMath::Max(RebuiltMaxError, double(FMath::Abs(Box->GetSignedDistance_Implementation(P / 640.0))));
		}
		AddInfo(FString::Printf(TEXT("CoarseStamp Placement=%d Difference=%d RebuiltMaxError=%.6f"), PlacementCase, bDifference, RebuiltMaxError));
		TestTrue(TEXT("Default relaxation preserves recovered coarse faces"), RebuiltMaxError < 0.1);
		TestEqual(TEXT("Target resolution preserved"), Grid->CellCount, FIntVector(128));
		TestEqual(TEXT("Target cell size preserved"), Grid->CellSize, 25.0f);
	}
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourCylDiagTest, "DualContour.Geometry.CylDiag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourCylDiagTest::RunTest(const FString& Parameters)
{
	for (float CellSize : {10.0f, 25.0f})
	{
		TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
		const int32 N = CellSize == 10.0f ? 64 : 128;
		Grid->CellCount = FIntVector(N);
		Grid->CellSize = CellSize;
		Grid->VertexRelaxation = 0.0f;
		TStrongObjectPtr<UCylinderVolumeSampler> Cyl(NewObject<UCylinderVolumeSampler>());
		const FVector Center(0.5 * N * CellSize);
		FText Error;
	Grid->ApplySampler(*Cyl, FVector(N * CellSize), FTransform(FVector(N * CellSize * 0.5f)), Error);
		const double R = Cyl->Radius, Half = Cyl->HalfHeight;
		const int32 TopLayer = FMath::FloorToInt((Center.Z + Half) / CellSize);
		double MaxRim = 0.0, SumRim = 0.0;
		TArray<double> DevR, DevZ;
		int32 Cells = 0, Bad = 0;
		for (int32 Y = 0; Y < N; ++Y)
			for (int32 X = 0; X < N; ++X)
			{
				const FDualContourCell* Cell = Grid->GetCell(X, Y, TopLayer);
				if (!Cell)
					continue;
				double MinR = TNumericLimits<double>::Max(), MaxR = TNumericLimits<double>::Lowest();
				for (int32 C = 0; C < 4; ++C)
				{
					const FVector Q((X + (C & 1)) * CellSize, (Y + ((C >> 1) & 1)) * CellSize, Center.Z + Half);
					const double Rad = FVector2D(Q.X - Center.X, Q.Y - Center.Y).Length();
					MinR = FMath::Min(MinR, Rad);
					MaxR = FMath::Max(MaxR, Rad);
				}
				if (MinR > R || MaxR < R)
					continue;
				const FVector P = Cell->Center - Center;
				const double Rad = FVector2D(P.X, P.Y).Length();
				const double D = FVector2D(Rad - R, P.Z - Half).Length();
				MaxRim = FMath::Max(MaxRim, D);
				SumRim += D;
				Bad += D > 0.05 ? 1 : 0;
				DevR.Add(Rad - R);
				DevZ.Add(P.Z - Half);
				++Cells;
			}
		double MR = 0.0, MZ = 0.0, SR = 0.0, SZ = 0.0, Jit = 0.0;
		for (double V : DevR) MR += V;
		for (double V : DevZ) MZ += V;
		MR /= FMath::Max(Cells, 1);
		MZ /= FMath::Max(Cells, 1);
		for (int32 I = 0; I < DevR.Num(); ++I)
		{
			SR += FMath::Square(DevR[I] - MR);
			SZ += FMath::Square(DevZ[I] - MZ);
			Jit += FVector2D(DevR[I] - MR, DevZ[I] - MZ).SizeSquared();
		}
		SR = FMath::Sqrt(SR / FMath::Max(Cells, 1));
		SZ = FMath::Sqrt(SZ / FMath::Max(Cells, 1));
		Jit = FMath::Sqrt(Jit / FMath::Max(Cells, 1));
		AddInfo(FString::Printf(TEXT("CylDiag Cell=%.0f RimCells=%d Bad=%d Mean=%.5f Max=%.5f Jitter=%.5f RadialMean=%.5f RadialSd=%.5f AxialMean=%.5f AxialSd=%.5f"),
			CellSize, Cells, Bad, SumRim / FMath::Max(Cells, 1), MaxRim, Jit, MR, SR, MZ, SZ));
		TestTrue(TEXT("Rim cells measured"), Cells > 0);
	}
	return true;
}

#endif
