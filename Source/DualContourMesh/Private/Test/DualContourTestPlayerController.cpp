#include "Test/DualContourTestPlayerController.h"
#include "DualContourMeshActor.h"
#include "DualContourModifierComponent.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"

ADualContourTestPlayerController::ADualContourTestPlayerController()
{
	ModifierComponent = CreateDefaultSubobject<UDualContourModifierComponent>(TEXT("DualContourModifier"));
}

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

	InitializeSamplers();
}

void ADualContourTestPlayerController::OnLeftClick()
{
	ApplySelectedSampler(true);
}

void ADualContourTestPlayerController::OnRightClick()
{
	ApplySelectedSampler(false);
}

void ADualContourTestPlayerController::InitializeSamplers()
{
	if (!ModifierComponent)
		return;

	// Preserve samplers configured on the component, including Blueprint subclasses.
	// When AdditionalSamplers are supplied, they replace the built-in test samplers.
	if (ModifierComponent->Samplers.IsEmpty())
	{
		bool bHasAdditionalSampler = false;
		for (const TObjectPtr<UVolumeSampler>& Sampler : AdditionalSamplers)
		{
			if (IsValid(Sampler))
			{
				bHasAdditionalSampler = true;
				break;
			}
		}

		if (!bHasAdditionalSampler)
		{
			ModifierComponent->AddSampler(USphereVolumeSampler::StaticClass());
			ModifierComponent->AddSampler(UBoxVolumeSampler::StaticClass());
			ModifierComponent->AddSampler(UCylinderVolumeSampler::StaticClass());
			ModifierComponent->AddSampler(UCapsuleVolumeSampler::StaticClass());
			ModifierComponent->AddSampler(UTorusVolumeSampler::StaticClass());
		}
	}

	for (const TObjectPtr<UVolumeSampler>& Sampler : AdditionalSamplers)
		if (IsValid(Sampler))
			ModifierComponent->Samplers.Add(Sampler);

	if (!ModifierComponent->Samplers.IsEmpty())
		SelectedSamplerIndex = FMath::Clamp(SelectedSamplerIndex, 0, ModifierComponent->Samplers.Num() - 1);
}

void ADualContourTestPlayerController::SetSelectedSamplerIndex(int32 SamplerIndex)
{
	if (ModifierComponent && ModifierComponent->Samplers.IsValidIndex(SamplerIndex))
		SelectedSamplerIndex = SamplerIndex;
}

void ADualContourTestPlayerController::SelectSphereSampler()
{
	SelectedSamplerIndex = 0;
}

void ADualContourTestPlayerController::SelectBoxSampler()
{
	SelectedSamplerIndex = 1;
}

void ADualContourTestPlayerController::SelectCylinderSampler()
{
	SelectedSamplerIndex = 2;
}

void ADualContourTestPlayerController::SelectCapsuleSampler()
{
	SelectedSamplerIndex = 3;
}

void ADualContourTestPlayerController::SelectTorusSampler()
{
	SelectedSamplerIndex = 4;
}

void ADualContourTestPlayerController::DecreaseSamplerScale()
{
	if (ModifierComponent)
		ModifierComponent->SamplerScale = FMath::Max(0.01f,
			ModifierComponent->SamplerScale / FMath::Max(1.01f, SamplerScaleStep));
}

void ADualContourTestPlayerController::IncreaseSamplerScale()
{
	if (ModifierComponent)
		ModifierComponent->SamplerScale = FMath::Min(10.0f,
			ModifierComponent->SamplerScale * FMath::Max(1.01f, SamplerScaleStep));
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
	if (!World || !ModifierComponent || !ModifierComponent->Samplers.IsValidIndex(SelectedSamplerIndex))
		return;

	UGameViewportClient* Viewport = World->GetGameViewport();
	if (!Viewport)
		return;

	FVector2D ViewportSize;
	Viewport->GetViewportSize(ViewportSize);

	FVector WorldOrigin, WorldDir;
	if (!DeprojectScreenPositionToWorld(ViewportSize.X * 0.5f, ViewportSize.Y * 0.5f, WorldOrigin, WorldDir))
		return;

	ModifierComponent->ModifyDualContourWithRay(WorldOrigin, WorldDir, SelectedSamplerIndex, MaterialId, bExcavate);
}
