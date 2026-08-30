#include "Test/DualContourVisualSweepPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "DualContourMeshActor.h"
#include "DualContourMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDualContourVisualSweep, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarDualContourVisualTestResolution(
	TEXT("dc.VisualTest.Resolution"), 128,
	TEXT("Cell count on every axis for generated visual-test contours."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestDensityScale(
	TEXT("dc.VisualTest.DensityScale"), 16.0f,
	TEXT("DensityScale assigned to every procedural sampler in the visual test."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestSettleSeconds(
	TEXT("dc.VisualTest.SettleSeconds"), 1.0f,
	TEXT("Delay after mesh generation and between screenshot views."), ECVF_Default);

TAutoConsoleVariable<int32> CVarDualContourVisualTestAutoQuit(
	TEXT("dc.VisualTest.AutoQuit"), 0,
	TEXT("If non-zero, quit the game after the visual sweep completes."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestKeyLightIntensity(
	TEXT("dc.VisualTest.KeyLightIntensity"), 8.0f,
	TEXT("Intensity of the shadow-casting key directional light."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestShadowBias(
	TEXT("dc.VisualTest.ShadowBias"), 2.0f,
	TEXT("Directional-light shadow bias used by the visual regression scene."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestSkyLightIntensity(
	TEXT("dc.VisualTest.SkyLightIntensity"), 0.35f,
	TEXT("Intensity of the neutral specified-cubemap skylight."), ECVF_Default);

TAutoConsoleVariable<int32> CVarDualContourVisualTestCastShadows(
	TEXT("dc.VisualTest.CastShadows"), 0,
	TEXT("Whether the key directional light casts shadows."), ECVF_Default);

FString ReadConsoleVariable(const TCHAR* Name)
{
	if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		return Variable->GetString();
	return TEXT("missing");
}

double TriangleFoldDegrees(const FVector& A0, const FVector& A1, const FVector& A2,
	const FVector& B0, const FVector& B1, const FVector& B2)
{
	const FVector NormalA = FVector::CrossProduct(A1 - A0, A2 - A0).GetSafeNormal();
	const FVector NormalB = FVector::CrossProduct(B1 - B0, B2 - B0).GetSafeNormal();
	if (NormalA.IsNearlyZero() || NormalB.IsNearlyZero())
		return 180.0;
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(NormalA, NormalB), -1.0, 1.0)));
}
}

void ADualContourVisualSweepPlayerController::RunDualContourVisualSweep()
{
	if (bVisualSweepRunning)
	{
		UE_LOG(LogDualContourVisualSweep, Warning, TEXT("A visual sweep is already running."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->GetGameViewport())
	{
		UE_LOG(LogDualContourVisualSweep, Error, TEXT("Visual sweep requires a game viewport."));
		return;
	}

	bVisualSweepRunning = true;
	VisualSweepSubjectIndex = 0;
	VisualSweepViewIndex = 0;
	VisualSweepSubjects.Reset();
	VisualSweepSubjectNames.Reset();
	VisualSweepActorsHiddenByTest.Reset();
	VisualSweepViews = {
		{TEXT("PosX"), FVector(1, 0, 0)}, {TEXT("NegX"), FVector(-1, 0, 0)},
		{TEXT("PosY"), FVector(0, 1, 0)}, {TEXT("NegY"), FVector(0, -1, 0)},
		{TEXT("PosZ"), FVector(0, 0, 1)}, {TEXT("NegZ"), FVector(0, 0, -1)},
		{TEXT("IsoPPP"), FVector(1, 1, 1).GetSafeNormal()},
		{TEXT("IsoNNP"), FVector(-1, -1, 1).GetSafeNormal()}
	};

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	VisualSweepOutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DualContourVisualSweep"), Timestamp);
	IFileManager::Get().MakeDirectory(*VisualSweepOutputDirectory, true);

	// Remove every level-authored visual influence (landscape, sky atmosphere, fog,
	// post-process volumes and lights) from the regression frame. Actors that were
	// already hidden are left untouched, and all changes are restored on completion.
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* ExistingActor = *ActorIt;
		if (!ExistingActor || ExistingActor == this || ExistingActor->IsHidden())
			continue;
		ExistingActor->SetActorHiddenInGame(true);
		VisualSweepActorsHiddenByTest.Add(ExistingActor);
	}

	const int32 Resolution = FMath::Clamp(CVarDualContourVisualTestResolution.GetValueOnGameThread(), 16, 128);
	constexpr float CellSize = 10.0f;
	const float Extent = Resolution * CellSize;
	const float DensityScale = FMath::Max(0.0001f, CVarDualContourVisualTestDensityScale.GetValueOnGameThread());
	const FVector SweepOrigin(0.0, 0.0, 20000.0);
	VisualSweepCenter = SweepOrigin + FVector(Extent * 0.5);

	struct FSamplerDefinition
	{
		const TCHAR* Name;
		TSubclassOf<UProceduralVolumeSampler> Class;
	};
	const TArray<FSamplerDefinition> Definitions = {
		{TEXT("Sphere"), USphereVolumeSampler::StaticClass()},
		{TEXT("Box"), UBoxVolumeSampler::StaticClass()},
		{TEXT("Cylinder"), UCylinderVolumeSampler::StaticClass()},
		{TEXT("Capsule"), UCapsuleVolumeSampler::StaticClass()},
		{TEXT("Torus"), UTorusVolumeSampler::StaticClass()}
	};

	for (const FSamplerDefinition& Definition : Definitions)
	{
		ADualContourMeshActor* MeshActor = World->SpawnActor<ADualContourMeshActor>(SweepOrigin, FRotator::ZeroRotator);
		if (!MeshActor || !MeshActor->DualContour)
		{
			FinishVisualSweep(false);
			return;
		}
		MeshActor->SetActorEnableCollision(false);
		MeshActor->bGenerateOverlapEvents = false;
		MeshActor->MeshComponentsPerFrame = 64;
		MeshActor->DualContour->CellCount = FVectorInt(Resolution, Resolution, Resolution);
		MeshActor->DualContour->CellSize = CellSize;

		UProceduralVolumeSampler* Sampler = NewObject<UProceduralVolumeSampler>(this, Definition.Class);
		Sampler->VolumeSize = FVector(Extent);
		Sampler->DensityScale = DensityScale;
		const float ShapeScale = Extent / 480.0f;
		if (USphereVolumeSampler* Sphere = Cast<USphereVolumeSampler>(Sampler))
			Sphere->Radius = 170.0f * ShapeScale;
		else if (UBoxVolumeSampler* Box = Cast<UBoxVolumeSampler>(Sampler))
		{
			Box->HalfExtents = FVector(155.0, 130.0, 105.0) * ShapeScale;
			Box->CornerRadius = 28.0f * ShapeScale;
		}
		else if (UCylinderVolumeSampler* Cylinder = Cast<UCylinderVolumeSampler>(Sampler))
		{
			Cylinder->Radius = 145.0f * ShapeScale;
			Cylinder->HalfHeight = 165.0f * ShapeScale;
		}
		else if (UCapsuleVolumeSampler* Capsule = Cast<UCapsuleVolumeSampler>(Sampler))
		{
			Capsule->Radius = 105.0f * ShapeScale;
			Capsule->SegmentHalfLength = 95.0f * ShapeScale;
		}
		else if (UTorusVolumeSampler* Torus = Cast<UTorusVolumeSampler>(Sampler))
		{
			Torus->MajorRadius = 140.0f * ShapeScale;
			Torus->MinorRadius = 55.0f * ShapeScale;
		}

		FText Error;
		if (!Sampler->ReplaceDualContour(MeshActor->DualContour, FTransform::Identity, Error))
		{
			UE_LOG(LogDualContourVisualSweep, Error, TEXT("Failed to build %s: %s"), Definition.Name, *Error.ToString());
			FinishVisualSweep(false);
			return;
		}
		MeshActor->SetActorHiddenInGame(true);
		VisualSweepSubjects.Add(MeshActor);
		VisualSweepSubjectNames.Add(Definition.Name);
	}

	VisualSweepCamera = World->SpawnActor<ACameraActor>(VisualSweepCenter, FRotator::ZeroRotator);
	UCameraComponent* CameraComponent = VisualSweepCamera->GetCameraComponent();
	CameraComponent->SetFieldOfView(35.0f);
	CameraComponent->SetConstraintAspectRatio(false);
	CameraComponent->SetAspectRatio(1.0f);
	PreviousViewTarget = GetViewTarget();
	SetViewTarget(VisualSweepCamera);

	VisualSweepDirectionalLight = World->SpawnActor<ADirectionalLight>(VisualSweepCenter, FRotator(-38.0, -42.0, 0.0));
	if (VisualSweepDirectionalLight)
	{
		VisualSweepDirectionalLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		VisualSweepDirectionalLight->GetLightComponent()->SetIntensity(
			FMath::Max(0.0f, CVarDualContourVisualTestKeyLightIntensity.GetValueOnGameThread()));
		VisualSweepDirectionalLight->GetLightComponent()->SetCastShadows(
			CVarDualContourVisualTestCastShadows.GetValueOnGameThread() != 0);
		VisualSweepDirectionalLight->GetLightComponent()->SetShadowBias(
			FMath::Max(0.0f, CVarDualContourVisualTestShadowBias.GetValueOnGameThread()));
	}
	VisualSweepSkyLight = World->SpawnActor<ASkyLight>(VisualSweepCenter, FRotator::ZeroRotator);
	if (VisualSweepSkyLight)
	{
		USkyLightComponent* SkyLightComponent = VisualSweepSkyLight->GetLightComponent();
		SkyLightComponent->SetMobility(EComponentMobility::Movable);
		SkyLightComponent->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
		SkyLightComponent->bLowerHemisphereIsBlack = false;
		SkyLightComponent->SetIntensity(
			FMath::Max(0.0f, CVarDualContourVisualTestSkyLightIntensity.GetValueOnGameThread()));
		UTextureCube* NeutralCubemap = LoadObject<UTextureCube>(
			nullptr, TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
		if (NeutralCubemap)
		{
			SkyLightComponent->SetCubemap(NeutralCubemap);
		}
		else
		{
			UE_LOG(LogDualContourVisualSweep, Error,
				TEXT("Could not load the neutral skylight cubemap; ambient lighting will be unavailable."));
		}
	}

	const UDualContour* SweepDualContour = VisualSweepSubjects.IsEmpty() ? nullptr : VisualSweepSubjects[0]->DualContour;
	const bool bUseQEF = !SweepDualContour || SweepDualContour->VertexSolveMode == EDualContourVertexSolveMode::QEF;
	FString Manifest = FString::Printf(
	TEXT("Engine=%s\nResolution=%d\nCellSize=%.6g\nQuadSplitMode=%s\nDensityScale=%.6g\nVertexPosition=%s\nNormal=AverageHermite\nVertexRelaxation=%.6g\nRelaxationNormalCosine=%.6g\nGradientStep=0.5\nKeyLightIntensity=%s\nShadowBias=%s\nSkyLightIntensity=%s\nCastShadows=%s\n"),
		*FEngineVersion::Current().ToString(), Resolution, CellSize,
		*ReadConsoleVariable(TEXT("dc.Mesh.QuadSplitMode")),
		DensityScale,
		bUseQEF ? TEXT("RegularizedQEF") : TEXT("HermiteIntersectionCentroid"),
		SweepDualContour ? SweepDualContour->VertexRelaxation : 0.0f,
		SweepDualContour ? SweepDualContour->RelaxationNormalCosine : 0.0f,
		*ReadConsoleVariable(TEXT("dc.VisualTest.KeyLightIntensity")),
		*ReadConsoleVariable(TEXT("dc.VisualTest.ShadowBias")),
		*ReadConsoleVariable(TEXT("dc.VisualTest.SkyLightIntensity")),
		*ReadConsoleVariable(TEXT("dc.VisualTest.CastShadows")));
	FFileHelper::SaveStringToFile(Manifest, *FPaths::Combine(VisualSweepOutputDirectory, TEXT("manifest.txt")));

	ScreenshotProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(
		this, &ADualContourVisualSweepPlayerController::HandleVisualSweepScreenshotProcessed);
	const float SettleSeconds = FMath::Max(0.1f, CVarDualContourVisualTestSettleSeconds.GetValueOnGameThread());
	World->GetTimerManager().SetTimer(VisualSweepTimerHandle, this,
		&ADualContourVisualSweepPlayerController::CaptureNextVisualSweepView, SettleSeconds, false);
	UE_LOG(LogDualContourVisualSweep, Display, TEXT("Visual sweep started. Output: %s"), *VisualSweepOutputDirectory);
}

void ADualContourVisualSweepPlayerController::CaptureNextVisualSweepView()
{
	if (!bVisualSweepRunning || !VisualSweepCamera)
		return;
	if (VisualSweepSubjectIndex >= VisualSweepSubjects.Num())
	{
		WriteVisualSweepMetrics();
		FinishVisualSweep(true);
		return;
	}

	for (int32 Index = 0; Index < VisualSweepSubjects.Num(); ++Index)
	{
		ADualContourMeshActor* Subject = VisualSweepSubjects[Index];
		if (!Subject)
			continue;
		const bool bVisible = Index == VisualSweepSubjectIndex;
		Subject->SetActorHiddenInGame(!bVisible);
		// Mesh components are queued and created after the actor's initial hidden-state
		// change. Apply visibility directly so those late components cannot leak into
		// another sampler's screenshot.
		for (const TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : Subject->MeshComponents)
		{
			if (!Pair.Value)
				continue;
			Pair.Value->SetVisibility(bVisible, true);
			Pair.Value->SetHiddenInGame(!bVisible, true);
		}
	}

	const FVisualSweepView& View = VisualSweepViews[VisualSweepViewIndex];
	const float GridExtent = FMath::Clamp(CVarDualContourVisualTestResolution.GetValueOnGameThread(), 16, 128) * 10.0f;
	// Keep the regression subject large enough to expose individual self-shadowing
	// streaks while retaining the complete silhouette in the 35-degree frame.
	const double CameraDistance = GridExtent * 1.05;
	const FVector CameraLocation = VisualSweepCenter + View.Direction * CameraDistance;
	VisualSweepCamera->SetActorLocationAndRotation(CameraLocation, (VisualSweepCenter - CameraLocation).Rotation());
	// Each axis change is a teleport. Reset temporal AA/Lumen view history so the
	// previous silhouette cannot appear as a bright or dark ghost in this capture.
	if (PlayerCameraManager)
		PlayerCameraManager->SetGameCameraCutThisFrame();

	const FString Filename = FPaths::Combine(VisualSweepOutputDirectory,
		FString::Printf(TEXT("%02d_%s_%s.png"), VisualSweepSubjectIndex,
			*VisualSweepSubjectNames[VisualSweepSubjectIndex], *View.Name));
	FScreenshotRequest::RequestScreenshot(Filename, false, false, false, FIntRect(), true);
}

void ADualContourVisualSweepPlayerController::HandleVisualSweepScreenshotProcessed()
{
	if (!bVisualSweepRunning)
		return;
	++VisualSweepViewIndex;
	if (VisualSweepViewIndex >= VisualSweepViews.Num())
	{
		VisualSweepViewIndex = 0;
		++VisualSweepSubjectIndex;
	}
	const float SettleSeconds = FMath::Max(0.1f, CVarDualContourVisualTestSettleSeconds.GetValueOnGameThread());
	GetWorldTimerManager().SetTimer(VisualSweepTimerHandle, this,
		&ADualContourVisualSweepPlayerController::CaptureNextVisualSweepView, SettleSeconds, false);
}

void ADualContourVisualSweepPlayerController::WriteVisualSweepMetrics() const
{
	FString Csv = TEXT("Sampler,Quads,MeanLegacyFoldDeg,MaxLegacyFoldDeg,MeanAlternateFoldDeg,MaxAlternateFoldDeg\n");
	for (int32 SubjectIndex = 0; SubjectIndex < VisualSweepSubjects.Num(); ++SubjectIndex)
	{
		const ADualContourMeshActor* Subject = VisualSweepSubjects[SubjectIndex];
		int64 QuadCount = 0;
		double LegacySum = 0.0, LegacyMax = 0.0, AlternateSum = 0.0, AlternateMax = 0.0;
		if (Subject)
		{
			for (const TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : Subject->MeshComponents)
			{
				const FDualContourMeshData& MeshData = Pair.Value->GetMeshData();
				for (int32 Base = 0; Base + 3 < MeshData.Positions.Num(); Base += 4)
				{
					const FVector& P0 = MeshData.Positions[Base];
					const FVector& P1 = MeshData.Positions[Base + 1];
					const FVector& P2 = MeshData.Positions[Base + 2];
					const FVector& P3 = MeshData.Positions[Base + 3];
					const double Legacy = TriangleFoldDegrees(P0, P2, P1, P0, P3, P2);
					const double Alternate = TriangleFoldDegrees(P0, P3, P1, P1, P3, P2);
					LegacySum += Legacy;
					AlternateSum += Alternate;
					LegacyMax = FMath::Max(LegacyMax, Legacy);
					AlternateMax = FMath::Max(AlternateMax, Alternate);
					++QuadCount;
				}
			}
		}
		const double Denominator = FMath::Max<int64>(QuadCount, 1);
		Csv += FString::Printf(TEXT("%s,%lld,%.6f,%.6f,%.6f,%.6f\n"),
			*VisualSweepSubjectNames[SubjectIndex], QuadCount, LegacySum / Denominator, LegacyMax,
			AlternateSum / Denominator, AlternateMax);
	}
	FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(VisualSweepOutputDirectory, TEXT("geometry_metrics.csv")));
}

void ADualContourVisualSweepPlayerController::FinishVisualSweep(bool bSucceeded)
{
	if (ScreenshotProcessedHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
		ScreenshotProcessedHandle.Reset();
	}
	GetWorldTimerManager().ClearTimer(VisualSweepTimerHandle);
	if (PreviousViewTarget.IsValid())
		SetViewTarget(PreviousViewTarget.Get());
	for (const TWeakObjectPtr<AActor>& HiddenActor : VisualSweepActorsHiddenByTest)
		if (HiddenActor.IsValid())
			HiddenActor->SetActorHiddenInGame(false);
	VisualSweepActorsHiddenByTest.Reset();

	const FString CompletionText = bSucceeded ? TEXT("SUCCESS\n") : TEXT("FAILED\n");
	if (!VisualSweepOutputDirectory.IsEmpty())
		FFileHelper::SaveStringToFile(CompletionText,
			*FPaths::Combine(VisualSweepOutputDirectory, bSucceeded ? TEXT("COMPLETE.txt") : TEXT("FAILED.txt")));
	bVisualSweepRunning = false;
	if (bSucceeded)
	{
		UE_LOG(LogDualContourVisualSweep, Display, TEXT("Visual sweep completed. Output: %s"), *VisualSweepOutputDirectory);
	}
	else
	{
		UE_LOG(LogDualContourVisualSweep, Error, TEXT("Visual sweep failed. Output: %s"), *VisualSweepOutputDirectory);
	}

	if (CVarDualContourVisualTestAutoQuit.GetValueOnGameThread() != 0)
		ConsoleCommand(TEXT("quit"));
}

