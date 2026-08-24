#include "Test/DualContourTestPlayerController.h"
#include "DualContourMeshActor.h"
#include "DualContourMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

void ADualContourTestPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ADualContourTestPlayerController::OnLeftClick);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ADualContourTestPlayerController::OnRightClick);
}

void ADualContourTestPlayerController::OnLeftClick()
{
	DoHemisphereEdit(true);
}

void ADualContourTestPlayerController::OnRightClick()
{
	DoHemisphereEdit(false);
}

void ADualContourTestPlayerController::DoHemisphereEdit(bool bExcavate)
{
	UWorld* World = GetWorld();
	if (!World)
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

	MeshActor->ModifyDensityWithHemisphere(HitResult.ImpactPoint, HitResult.ImpactNormal, HemisphereRadius, bExcavate);
}
