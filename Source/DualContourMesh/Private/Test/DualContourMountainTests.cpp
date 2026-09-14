#include "DualContour.h"
#include "DualContourEditContext.h"
#include "DualContourMeshBuilder.h"
#include "DualContourUtils.h"
#include "Engine/VolumeTexture.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "VolumeSampler/TextureSDFSampler.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourMountainTest, "DualContour.Geometry.MountainTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourMountainTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UVolumeTexture> Texture(LoadObject<UVolumeTexture>(nullptr, TEXT("/Game/SDF_Mountain_2001.SDF_Mountain_2001")));
	if (!Texture.IsValid())
	{
		AddWarning(TEXT("Project fixture /Game/SDF_Mountain_2001 is unavailable; mountain test skipped."));
		return true;
	}
	for (double Yaw : {0.0, 45.0})
		for (const FVector Offset : {FVector::ZeroVector, FVector(2.3, -1.7, 0.6)})
		{
			TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
			Grid->CellCount = FIntVector(128);
			Grid->CellSize = 10.0f;
			Grid->Rebuild();
			TStrongObjectPtr<UTex3DSDFSampler> Sampler(NewObject<UTex3DSDFSampler>());
			Sampler->Texture = Texture.Get();
			FDualContourEditContext Edit(*Grid);
			if (!TestTrue(TEXT("Stage mountain through brush stamp path"), Edit.ApplyDensity(
				EDualContourDensityOperation::Union, *Sampler, FVector(640), FTransform(FRotator(0, Yaw, 0), FVector(640) + Offset)))
				|| !TestTrue(TEXT("Commit mountain stamp"), Edit.Commit()))
				return false;
			FDualContourMeshData Mesh;
			FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, Mesh);
			TestTrue(TEXT("Mountain is nonempty"), Mesh.Indices.Num() > 40000);
			const auto Measure = [&](const FDualContourMeshData& Data, const TCHAR* Stage)
			{
				int32 Folded = 0, Reversed = 0, Degenerate = 0;
				double MaxAngle = 0;
				for (int32 I = 0; I < Data.Indices.Num(); I += 6)
				{
					const auto Normal = [&](int32 T)
					{
						const FVector A = Data.Positions[Data.Indices[T]], B = Data.Positions[Data.Indices[T + 1]], C = Data.Positions[Data.Indices[T + 2]];
						return FVector::CrossProduct(B - A, C - A).GetSafeNormal();
					};
					const FVector A = Normal(I), B = Normal(I + 3);
					if (A.IsNearlyZero() || B.IsNearlyZero()) { ++Degenerate; continue; }
					const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0)));
					MaxAngle = FMath::Max(MaxAngle, Angle);
					Folded += Angle > 45.0;
					Reversed += Angle > 90.0;
				}
				AddInfo(FString::Printf(TEXT("%s Yaw=%.0f Offset=%s Quads=%d Folded45=%d Reversed90=%d Degenerate=%d MaxAngle=%.3f"),
					Stage, Yaw, *Offset.ToString(), Data.Indices.Num() / 6, Folded, Reversed, Degenerate, MaxAngle));
				TestTrue(TEXT("Severe fold count stays below one percent"), Folded < Data.Indices.Num() / 600);
				TestEqual(TEXT("Mountain has no reversed quad"), Reversed, 0);
			};
			Measure(Mesh, TEXT("Stamp"));
			if (Yaw == 0 && Offset.IsZero())
			{
				TMap<FVector, TArray<int32>> QuadsByFirstCorner;
				for (int32 I = 0; I < Mesh.Positions.Num(); I += 4)
					QuadsByFirstCorner.FindOrAdd(Mesh.Positions[I]).Add(I);
				int32 IndexCount = 0;
				bool bSame = true;
				// Put the split directly beside the ambiguous mountain cell, so
				// its separate sheets also participate in the normal/material halo.
				for (int32 Part = 0; Part < 2; ++Part)
				{
					FDualContourMeshData Division;
					FDualContourMeshBuilder::Build(*Grid, FIntVector(Part ? 71 : 0, 0, 0),
						FIntVector(Part ? 128 : 71, 128, 128), Division);
					IndexCount += Division.Indices.Num();
					for (int32 I = 0; I < Division.Positions.Num(); I += 4)
					{
						bool bFound = false;
						if (const TArray<int32>* Candidates = QuadsByFirstCorner.Find(Division.Positions[I]))
							for (const int32 Candidate : *Candidates)
							{
								bool bMatches = true;
								for (int32 J = 0; J < 4; ++J)
									bMatches &= Division.Positions[I + J] == Mesh.Positions[Candidate + J]
										&& Division.Normals[I + J].Equals(Mesh.Normals[Candidate + J], 1.e-6)
										&& Division.MaterialIds[I + J] == Mesh.MaterialIds[Candidate + J]
										&& Division.MaterialWeights[I + J] == Mesh.MaterialWeights[Candidate + J];
								for (int32 J = 0; J < 6; ++J)
									bMatches &= Division.Indices[I / 4 * 6 + J] - I == Mesh.Indices[Candidate / 4 * 6 + J] - Candidate;
								bFound |= bMatches;
							}
						bSame &= bFound;
					}
				}
				TestEqual(TEXT("Divisions emit every mountain triangle once"), IndexCount, Mesh.Indices.Num());
				TestTrue(TEXT("Geometry, normals and materials agree across divisions"), bSame);
			}
			Grid->Rebuild();
			FDualContourMeshData Rebuilt;
			FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, Rebuilt);
			TestTrue(TEXT("Local stamp and full rebuild positions agree"), Mesh.Positions == Rebuilt.Positions);
			TestTrue(TEXT("Local stamp and full rebuild triangulation agree"), Mesh.Indices == Rebuilt.Indices);
		}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourNoiseMountainUnionTest, "DualContour.Geometry.NoiseMountainUnion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourNoiseMountainUnionTest::RunTest(const FString& Parameters)
{
	const UDualContour* Noise = LoadObject<UDualContour>(nullptr, TEXT("/Game/NoiseVS.NoiseVS"));
	UVolumeTexture* Texture = LoadObject<UVolumeTexture>(nullptr, TEXT("/Game/SDF_Mountain_2001.SDF_Mountain_2001"));
	if (!Noise || !Texture) { AddWarning(TEXT("NoiseVS or mountain fixture unavailable; test skipped.")); return true; }
	const auto Run = [&](bool bNormalize, int32& OutChangedNearSurface, FDualContourMeshData& OutMesh)
	{
		TStrongObjectPtr<UDualContour> Grid(NewObject<UDualContour>());
		if (!Grid->Initialize(Noise)) return false;
		TArray<float> Before;
		Before.SetNumUninitialized(129 * 129 * 129);
		for (int32 Z = 0; Z < 129; ++Z) for (int32 Y = 0; Y < 129; ++Y) for (int32 X = 0; X < 129; ++X)
			Before[X + 129 * (Y + 129 * Z)] = Grid->GetLinearDensity(X, Y, Z);
		TStrongObjectPtr<UTex3DSDFSampler> Mountain(NewObject<UTex3DSDFSampler>());
		Mountain->Texture = Texture;
		FDualContourEditContext Edit(*Grid);
		if (!Edit.ApplyDensity(EDualContourDensityOperation::Union, *Mountain, FVector(640),
			FTransform(FVector(640))) || !Edit.Commit()) return false;
		OutChangedNearSurface = 0;
		for (int32 Z = 0; Z < 129; ++Z) for (int32 Y = 0; Y < 129; ++Y) for (int32 X = 0; X < 129; ++X)
		{
			const int32 I = X + 129 * (Y + 129 * Z);
			if (FMath::Abs(Before[I]) < 8192.0f && Grid->GetLinearDensity(X, Y, Z) != Before[I]) ++OutChangedNearSurface;
		}
		FDualContourMeshBuilder::Build(*Grid, FIntVector::ZeroValue, Grid->CellCount, OutMesh);
		return true;
	};
	int32 LegacyChanged = 0, NormalizedChanged = 0;
	FDualContourMeshData LegacyMesh, NormalizedMesh;
	if (!TestTrue(TEXT("Legacy union sampled"), Run(false, LegacyChanged, LegacyMesh))
		|| !TestTrue(TEXT("Normalized union sampled"), Run(true, NormalizedChanged, NormalizedMesh))) return false;
	AddInfo(FString::Printf(TEXT("NoiseVS near-surface samples changed: legacy=%d normalized=%d"), LegacyChanged, NormalizedChanged));
	TestTrue(TEXT("Distance normalization reduces unintended terrain surface edits"), NormalizedChanged < LegacyChanged);
	TestTrue(TEXT("Normalized NoiseVS mountain union is nonempty"), NormalizedMesh.Indices.Num() > LegacyMesh.Indices.Num() / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDualContourTriangulationTest, "DualContour.Geometry.QuadTriangulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDualContourTriangulationTest::RunTest(const FString& Parameters)
{
	for (double Scale : {0.0001, 1.0, 10000.0})
	{
		const FTransform Transform(FRotator(23, 45, 17), FVector(100, 200, 300), FVector(Scale));
		const FVector A = Transform.TransformPosition(FVector::ZeroVector);
		const FVector B = Transform.TransformPosition(FVector(0.2, 0.5, 0));
		const FVector C = Transform.TransformPosition(FVector(1, 1, 0));
		const FVector D = Transform.TransformPosition(FVector(0, 1, 0));
		TestTrue(TEXT("Concave quad uses interior diagonal"), DualContourUtils::UseAlternateQuadDiagonal(A, B, C, D));
		TestTrue(TEXT("Reversed winding keeps physical diagonal"), DualContourUtils::UseAlternateQuadDiagonal(A, D, C, B));
		TestFalse(TEXT("Convex planar quad retains original split"), DualContourUtils::UseAlternateQuadDiagonal(
			A, Transform.TransformPosition(FVector(1, 0, 0)), C, D));
		TestFalse(TEXT("Collapsed quad has deterministic split"), DualContourUtils::UseAlternateQuadDiagonal(A, A, A, A));
	}
	return true;
}
#endif
