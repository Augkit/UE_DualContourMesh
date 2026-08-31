#include "Test/DualContourTestPlayerController.h"
#include "DualContourMeshActor.h"
#include "DualContourMeshComponent.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"

void ADualContourTestPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ADualContourTestPlayerController::OnLeftClick);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ADualContourTestPlayerController::OnRightClick);
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ADualContourTestPlayerController::SelectSphereSampler);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ADualContourTestPlayerController::SelectBoxSampler);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ADualContourTestPlayerController::SelectCylinderSampler);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ADualContourTestPlayerController::SelectCapsuleSampler);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ADualContourTestPlayerController::SelectTorusSampler);
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ADualContourTestPlayerController::DecreaseSamplerScale);
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ADualContourTestPlayerController::IncreaseSamplerScale);
	InputComponent->BindKey(EKeys::K, IE_Pressed, this, &ADualContourTestPlayerController::SaveRuntimeDensityIncrement);
	InputComponent->BindKey(EKeys::L, IE_Pressed, this, &ADualContourTestPlayerController::LoadRuntimeDensityIncrement);

	if (!SelectedSampler)
		SelectSphereSampler();
}

void ADualContourTestPlayerController::OnLeftClick()
{
	ApplySelectedSampler(true);
}

void ADualContourTestPlayerController::OnRightClick()
{
	ApplySelectedSampler(false);
}

void ADualContourTestPlayerController::SelectSampler(TSubclassOf<UProceduralVolumeSampler> SamplerClass)
{
	if (SamplerClass)
		SelectedSampler = NewObject<UProceduralVolumeSampler>(this, SamplerClass);
}

void ADualContourTestPlayerController::SelectSphereSampler()
{
	SelectSampler(USphereVolumeSampler::StaticClass());
}

void ADualContourTestPlayerController::SelectBoxSampler()
{
	SelectSampler(UBoxVolumeSampler::StaticClass());
}

void ADualContourTestPlayerController::SelectCylinderSampler()
{
	SelectSampler(UCylinderVolumeSampler::StaticClass());
}

void ADualContourTestPlayerController::SelectCapsuleSampler()
{
	SelectSampler(UCapsuleVolumeSampler::StaticClass());
}

void ADualContourTestPlayerController::SelectTorusSampler()
{
	SelectSampler(UTorusVolumeSampler::StaticClass());
}

void ADualContourTestPlayerController::DecreaseSamplerScale()
{
	SamplerScale = FMath::Max(0.01f, SamplerScale / FMath::Max(1.01f, SamplerScaleStep));
}

void ADualContourTestPlayerController::IncreaseSamplerScale()
{
	SamplerScale = FMath::Min(10.0f, SamplerScale * FMath::Max(1.01f, SamplerScaleStep));
}

void ADualContourTestPlayerController::SaveRuntimeDensityIncrement()
{
	if (ADualContourMeshActor* MeshActor = FindDualContourMeshActor())
		MeshActor->TestSaveRuntimeDensityIncrement();
}

void ADualContourTestPlayerController::LoadRuntimeDensityIncrement()
{
	if (ADualContourMeshActor* MeshActor = FindDualContourMeshActor())
		MeshActor->TestLoadRuntimeDensityIncrement();
}

ADualContourMeshActor* ADualContourTestPlayerController::FindDualContourMeshActor() const
{
	UWorld* World = GetWorld();
	if (!World)
		return nullptr;

	for (TActorIterator<ADualContourMeshActor> It(World); It; ++It)
		return *It;
	return nullptr;
}

void ADualContourTestPlayerController::ApplySelectedSampler(bool bExcavate)
{
	UWorld* World = GetWorld();
	if (!World || !SelectedSampler)
		return;

	UGameViewportClient* Viewport = World->GetGameViewport();
	if (!Viewport)
		return;

	FVector2D ViewportSize;
	Viewport->GetViewportSize(ViewportSize);

	FVector WorldOrigin, WorldDir;
	if (!DeprojectScreenPositionToWorld(ViewportSize.X * 0.5f, ViewportSize.Y * 0.5f, WorldOrigin, WorldDir))
		return;

	FHitResult HitResult;
	if (!World->LineTraceSingleByChannel(HitResult, WorldOrigin, WorldOrigin + WorldDir * 100000.f, ECC_Visibility))
		return;

	if (!HitResult.GetComponent() || !HitResult.GetComponent()->IsA<UDualContourMeshComponent>())
		return;

	ADualContourMeshActor* MeshActor = Cast<ADualContourMeshActor>(HitResult.GetActor());
	if (!MeshActor)
		return;

	MeshActor->ModifyDensityWithSampler(HitResult.ImpactPoint, HitResult.ImpactNormal, SelectedSampler, SamplerScale, bExcavate);
}
