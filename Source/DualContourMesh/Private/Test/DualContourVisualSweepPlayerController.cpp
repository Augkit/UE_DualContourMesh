#include "Test/DualContourVisualSweepPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "DualContourMeshActor.h"
#include "DualContourMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "VolumeSampler/NoiseVolumeSampler.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
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

TAutoConsoleVariable<float> CVarDualContourVisualTestLinearDensityScale(
	TEXT("dc.VisualTest.LinearDensityScale"), 16.0f,
	TEXT("LinearDensityScale assigned to the noise sampler in the visual test."), ECVF_Default);

TAutoConsoleVariable<int32> CVarDualContourVisualTestNoiseSeed(
	TEXT("dc.VisualTest.NoiseSeed"), 1337,
	TEXT("Seed assigned to the visual-test noise sampler."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestNoiseFrequency(
	TEXT("dc.VisualTest.NoiseFrequency"), 0.001f,
	TEXT("Frequency assigned to the visual-test noise sampler."), ECVF_Default);

TAutoConsoleVariable<int32> CVarDualContourVisualTestNoiseOctaves(
	TEXT("dc.VisualTest.NoiseOctaves"), 3,
	TEXT("FBm octave count assigned to the visual-test noise sampler."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestNoiseLacunarity(
	TEXT("dc.VisualTest.NoiseLacunarity"), 2.0f,
	TEXT("FBm lacunarity assigned to the visual-test noise sampler."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestNoiseGain(
	TEXT("dc.VisualTest.NoiseGain"), 0.5f,
	TEXT("FBm gain assigned to the visual-test noise sampler."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestHeightOffset(
	TEXT("dc.VisualTest.HeightOffset"), 0.0f,
	TEXT("Height offset assigned to the visual-test noise sampler."), ECVF_Default);

TAutoConsoleVariable<float> CVarDualContourVisualTestHeightAmplitude(
	TEXT("dc.VisualTest.HeightAmplitude"), 128.0f,
	TEXT("Height amplitude assigned to the visual-test noise sampler."), ECVF_Default);

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

TAutoConsoleVariable<FString> CVarDualContourVisualTestViews(
	TEXT("dc.VisualTest.Views"), TEXT(""),
	TEXT("Comma- or space-separated subset of visual sweep view names to capture "
		"(LowPosX, LowNegX, LowPosY, LowNegY, Top, IsoPPP, IsoNNP, IsoPNP); empty captures every view."), ECVF_Default);

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
	HiddenVisualSweepPawn = GetPawn();
	if (HiddenVisualSweepPawn.IsValid())
	{
		bVisualSweepPawnWasHidden = HiddenVisualSweepPawn->IsHidden();
		HiddenVisualSweepPawn->SetActorHiddenInGame(true);
	}
	VisualSweepSubjectIndex = 0;
	VisualSweepViewIndex = 0;
	VisualSweepSubjects.Reset();
	VisualSweepSubjectNames.Reset();
	const TArray<FVisualSweepView> AllVisualSweepViews = {
		{TEXT("LowPosX"), FVector(1, 0, 0.3).GetSafeNormal()},
		{TEXT("LowNegX"), FVector(-1, 0, 0.3).GetSafeNormal()},
		{TEXT("LowPosY"), FVector(0, 1, 0.3).GetSafeNormal()},
		{TEXT("LowNegY"), FVector(0, -1, 0.3).GetSafeNormal()},
		{TEXT("Top"), FVector(0, 0, 1)},
		{TEXT("IsoPPP"), FVector(1, 1, 1).GetSafeNormal()},
		{TEXT("IsoNNP"), FVector(-1, -1, 1).GetSafeNormal()},
		{TEXT("IsoPNP"), FVector(1, -1, 1).GetSafeNormal()}
	};
	VisualSweepViews = AllVisualSweepViews;
	// dc.VisualTest.Views lets callers capture a subset (e.g. "Top") instead of every view.
	const FString ViewFilter = CVarDualContourVisualTestViews.GetValueOnGameThread().TrimStartAndEnd();
	if (!ViewFilter.IsEmpty())
	{
		FString MutableFilter = ViewFilter;
		MutableFilter.ReplaceInline(TEXT(","), TEXT(" "));
		TArray<FString> RequestedViewNames;
		MutableFilter.ParseIntoArray(RequestedViewNames, TEXT(" "), /*InCullEmpty*/ true);
		TArray<FVisualSweepView> FilteredViews;
		for (const FString& RequestedViewName : RequestedViewNames)
		{
			const FVisualSweepView* Match = AllVisualSweepViews.FindByPredicate(
				[&RequestedViewName](const FVisualSweepView& View)
				{
					return View.Name.Equals(RequestedViewName, ESearchCase::IgnoreCase);
				});
			if (Match)
			{
				FilteredViews.Add(*Match);
			}
			else
			{
				UE_LOG(LogDualContourVisualSweep, Warning,
					TEXT("Ignoring unknown visual sweep view '%s' from dc.VisualTest.Views."), *RequestedViewName);
			}
		}
		if (!FilteredViews.IsEmpty())
		{
			VisualSweepViews = MoveTemp(FilteredViews);
		}
		else
		{
			UE_LOG(LogDualContourVisualSweep, Warning,
				TEXT("No views matched the dc.VisualTest.Views filter '%s'; capturing every view."), *ViewFilter);
		}
	}

	// Record the capture set so single-view runs can be distinguished from a full sweep.
	FString CapturedViewNames;
	for (int32 ViewIndex = 0; ViewIndex < VisualSweepViews.Num(); ++ViewIndex)
	{
		if (ViewIndex > 0)
		{
			CapturedViewNames += TEXT(", ");
		}
		CapturedViewNames += VisualSweepViews[ViewIndex].Name;
	}
	UE_LOG(LogDualContourVisualSweep, Display, TEXT("Visual sweep will capture %d view(s): %s"),
		VisualSweepViews.Num(), *CapturedViewNames);

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	VisualSweepOutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DualContourVisualSweep"), Timestamp);
	IFileManager::Get().MakeDirectory(*VisualSweepOutputDirectory, true);

	const int32 Resolution = FMath::Clamp(CVarDualContourVisualTestResolution.GetValueOnGameThread(), 16, 128);
	constexpr float CellSize = 10.0f;
	const float Extent = Resolution * CellSize;
	const float LinearDensityScale = FMath::Max(0.0001f, CVarDualContourVisualTestLinearDensityScale.GetValueOnGameThread());
	// Place the generated height field over the Basic template stage while preserving
	// every authored actor, light, sky and post-process volume in the scene.
	VisualSweepCenter = FVector(0.0, 0.0, 100.0);
	const FVector SweepOrigin = VisualSweepCenter - FVector(Extent * 0.5);

	ADualContourMeshActor* MeshActor = World->SpawnActor<ADualContourMeshActor>(SweepOrigin, FRotator::ZeroRotator);
	if (!MeshActor || !MeshActor->DualContour)
	{
		FinishVisualSweep(false);
		return;
	}
	MeshActor->SetActorEnableCollision(false);
	MeshActor->bGenerateOverlapEvents = false;
	MeshActor->MeshComponentsPerFrame = 64;
	MeshActor->DualContour->CellCount = FIntVector(Resolution, Resolution, Resolution);
	MeshActor->DualContour->CellSize = CellSize;
	UMaterialInterface* NormalVisualizationMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/M_Normal.M_Normal"));
	if (!NormalVisualizationMaterial)
	{
		UE_LOG(LogDualContourVisualSweep, Error,
			TEXT("Failed to load normal visualization material /Game/M_Normal.M_Normal."));
		FinishVisualSweep(false);
		return;
	}
	MeshActor->MeshMaterial = NormalVisualizationMaterial;

	UNoiseVolumeSampler* Sampler = NewObject<UNoiseVolumeSampler>(this);
	Sampler->VolumeSize = FVector(Extent);
	Sampler->DensityScale = LinearDensityScale;
	Sampler->Dimension = ENoiseSamplerDimension::HeightField2D;
	Sampler->Seed = CVarDualContourVisualTestNoiseSeed.GetValueOnGameThread();
	Sampler->Frequency = FMath::Max(0.000001f, CVarDualContourVisualTestNoiseFrequency.GetValueOnGameThread());
	Sampler->NoiseType = ENoiseSamplerType::OpenSimplex2S;
	Sampler->Rotation3D = ENoiseSamplerRotation3D::ImproveXZPlanes;
	Sampler->FractalType = ENoiseSamplerFractalType::FBm;
	Sampler->Octaves = FMath::Clamp(CVarDualContourVisualTestNoiseOctaves.GetValueOnGameThread(), 1, 30);
	Sampler->Lacunarity = FMath::Max(0.0001f, CVarDualContourVisualTestNoiseLacunarity.GetValueOnGameThread());
	Sampler->Gain = FMath::Clamp(CVarDualContourVisualTestNoiseGain.GetValueOnGameThread(), 0.0f, 1.0f);
	Sampler->HeightOffset = CVarDualContourVisualTestHeightOffset.GetValueOnGameThread();
	Sampler->HeightAmplitude = FMath::Max(0.0f, CVarDualContourVisualTestHeightAmplitude.GetValueOnGameThread());

	FText Error;
	if (!Sampler->ReplaceDualContour(MeshActor->DualContour, FTransform::Identity, Error))
	{
		UE_LOG(LogDualContourVisualSweep, Error, TEXT("Failed to build Noise: %s"), *Error.ToString());
		FinishVisualSweep(false);
		return;
	}
	MeshActor->SetActorHiddenInGame(true);
	VisualSweepSubjects.Add(MeshActor);
	VisualSweepSubjectNames.Add(TEXT("Noise"));

	VisualSweepCamera = World->SpawnActor<ACameraActor>(VisualSweepCenter, FRotator::ZeroRotator);
	UCameraComponent* CameraComponent = VisualSweepCamera->GetCameraComponent();
	CameraComponent->SetFieldOfView(35.0f);
	CameraComponent->SetConstraintAspectRatio(false);
	CameraComponent->SetAspectRatio(1.0f);
	CameraComponent->PostProcessBlendWeight = 1.0f;
	FPostProcessSettings& CameraPostProcess = CameraComponent->PostProcessSettings;
	CameraPostProcess.bOverride_AutoExposureMethod = true;
	CameraPostProcess.AutoExposureMethod = AEM_Manual;
	CameraPostProcess.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	CameraPostProcess.AutoExposureApplyPhysicalCameraExposure = false;
	CameraPostProcess.bOverride_AutoExposureBias = true;
	CameraPostProcess.AutoExposureBias = 0.0f;
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
	FString Manifest = FString::Printf(
		TEXT("Engine=%s\nResolution=%d\nCellSize=%.6g\nLinearDensityScale=%.6g\nNoiseDimension=HeightField2D\nNoiseSeed=%d\nNoiseFrequency=%.9g\nNoiseType=OpenSimplex2S\nNoiseRotation3D=ImproveXZPlanes\nFractalType=FBm\nNoiseOctaves=%d\nNoiseLacunarity=%.6g\nNoiseGain=%.6g\nHeightOffset=%.6g\nHeightAmplitude=%.6g\nVertexPosition=Power4HermiteIntersectionCentroid\nNormal=CenterCentralDifference\nGradientStep=0.125\nDensityStorage=BiasedSignedUInt16\nEncodedIsoValue=32768\nFixedPointScale=64\nVertexRelaxation=%.6g\nRelaxationNormalCosine=%.6g\nMeshNormal=PositionWeldedAreaWeighted\nMeshNormalBlend=0.25\nMaterial=/Game/M_Normal.M_Normal\nMaterialPurpose=NormalVisualization\nWireframeOverlay=%s\nViews=%s\nExposureMode=Manual\nExposureBias=0\nKeyLightIntensity=%s\nShadowBias=%s\nSkyLightIntensity=%s\nCastShadows=%s\n"),
		*FEngineVersion::Current().ToString(), Resolution, CellSize,
		LinearDensityScale,
		Sampler->Seed, Sampler->Frequency, Sampler->Octaves, Sampler->Lacunarity, Sampler->Gain,
		Sampler->HeightOffset, Sampler->HeightAmplitude,
		SweepDualContour ? SweepDualContour->VertexRelaxation : 0.0f,
		SweepDualContour ? SweepDualContour->RelaxationNormalCosine : 0.0f,
		*ReadConsoleVariable(TEXT("dc.VisualTest.WireframeOverlay")),
		*CapturedViewNames,
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
	// Use tighter framing for the low-angle diagnostic views and extra room for diagonals.
	const double CameraDistance = GridExtent * (View.Name.StartsWith(TEXT("Iso")) ? 2.4 : 1.8);
	const FVector CameraLocation = VisualSweepCenter + View.Direction * CameraDistance;
	VisualSweepCamera->SetActorLocationAndRotation(CameraLocation, (VisualSweepCenter - CameraLocation).Rotation());
	// Each axis change is a teleport. Reset temporal AA/Lumen view history so the
	// previous silhouette cannot appear as a bright or dark ghost in this capture.
	if (PlayerCameraManager)
		PlayerCameraManager->SetGameCameraCutThisFrame();

	// Camera motion, visibility changes and a newly created material must be rendered
	// for the same settling interval on every view. In particular, do not capture the
	// first view in the same frame that the subject becomes visible.
	const float SettleSeconds = FMath::Max(0.1f, CVarDualContourVisualTestSettleSeconds.GetValueOnGameThread());
	GetWorldTimerManager().SetTimer(VisualSweepTimerHandle, this,
		&ADualContourVisualSweepPlayerController::CapturePreparedVisualSweepView, SettleSeconds, false);
}

void ADualContourVisualSweepPlayerController::CapturePreparedVisualSweepView()
{
	if (!bVisualSweepRunning || VisualSweepSubjectIndex >= VisualSweepSubjects.Num()
		|| VisualSweepViewIndex >= VisualSweepViews.Num())
		return;

	const FVisualSweepView& View = VisualSweepViews[VisualSweepViewIndex];
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
	// Prepare the next view on the following frame; CaptureNextVisualSweepView will
	// start that view's own settling interval before requesting its screenshot.
	GetWorldTimerManager().SetTimer(VisualSweepTimerHandle, this,
		&ADualContourVisualSweepPlayerController::CaptureNextVisualSweepView, 0.01f, false);
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
	if (HiddenVisualSweepPawn.IsValid())
	{
		HiddenVisualSweepPawn->SetActorHiddenInGame(bVisualSweepPawnWasHidden);
		HiddenVisualSweepPawn.Reset();
	}

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
