#include "DualContourBombRainActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "DualContourBombActor.h"
#include "DualContourCleanViewGameMode.h"
#include "Engine/World.h"
#include "TimerManager.h"

ADualContourBombRainActor::ADualContourBombRainActor()
{
	PrimaryActorTick.bCanEverTick = false;
	BombClass = ADualContourBombActor::StaticClass();

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

#if WITH_EDITORONLY_DATA
	AreaVisualization = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("AreaVisualization"));
	if (AreaVisualization)
	{
		AreaVisualization->SetupAttachment(SceneRoot);
		AreaVisualization->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AreaVisualization->SetGenerateOverlapEvents(false);
		AreaVisualization->SetHiddenInGame(true);
		AreaVisualization->ShapeColor = FColor(255, 96, 32);
		AreaVisualization->bDrawOnlyIfSelected = false;
	}
#endif
}

void ADualContourBombRainActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateAreaVisualization();
}

void ADualContourBombRainActor::BeginPlay()
{
	Super::BeginPlay();
	const bool bWaitForDualContour = GetWorld() && GetWorld()->GetAuthGameMode()
		&& GetWorld()->GetAuthGameMode()->IsA(ADualContourCleanViewGameMode::StaticClass());
	if (bStartAutomatically && !bWaitForDualContour)
		StartBombRain();
}

void ADualContourBombRainActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBombRain();
	Super::EndPlay(EndPlayReason);
}

void ADualContourBombRainActor::StartBombRain()
{
	StopBombRain();
	SpawnedBombCount = 0;

	if (!GetWorld() || !BombClass || BombCount <= 0)
		return;

	if (!FMath::IsFinite(SpawnDuration) || SpawnDuration <= 0.0f)
	{
		while (SpawnedBombCount < BombCount)
			SpawnBomb();
		BP_OnBombRainFinished();
		return;
	}

	const float SpawnInterval = SpawnDuration / static_cast<float>(BombCount);
	GetWorldTimerManager().SetTimer(
		SpawnTimerHandle, this, &ADualContourBombRainActor::SpawnNextBomb,
		SpawnInterval, true, SpawnInterval);
}

void ADualContourBombRainActor::StopBombRain()
{
	GetWorldTimerManager().ClearTimer(SpawnTimerHandle);
}

bool ADualContourBombRainActor::IsBombRainActive() const
{
	return GetWorld() && GetWorldTimerManager().IsTimerActive(SpawnTimerHandle);
}

void ADualContourBombRainActor::SpawnNextBomb()
{
	if (SpawnedBombCount >= BombCount)
	{
		StopBombRain();
		return;
	}

	SpawnBomb();
	if (SpawnedBombCount >= BombCount)
	{
		StopBombRain();
		BP_OnBombRainFinished();
	}
}

void ADualContourBombRainActor::SpawnBomb()
{
	UWorld* World = GetWorld();
	if (!World || !BombClass)
		return;

	const FVector2D SafeAreaSize(
		FMath::Max(FMath::Abs(AreaSize.X), 1.0),
		FMath::Max(FMath::Abs(AreaSize.Y), 1.0));
	const FVector LocalOffset(
		FMath::FRandRange(-SafeAreaSize.X * 0.5f, SafeAreaSize.X * 0.5f),
		FMath::FRandRange(-SafeAreaSize.Y * 0.5f, SafeAreaSize.Y * 0.5f),
		0.0f);
	const FVector SpawnLocation = GetActorTransform().TransformPositionNoScale(LocalOffset);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = GetInstigator();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ADualContourBombActor* Bomb = World->SpawnActor<ADualContourBombActor>(
		BombClass, SpawnLocation, GetActorRotation(), SpawnParameters))
	{
		Bomb->ActivateBomb();
		if (InitialDownwardSpeed > 0.0f)
			Bomb->LaunchBomb(-GetActorUpVector() * InitialDownwardSpeed);
		++SpawnedBombCount;
	}
}

void ADualContourBombRainActor::UpdateAreaVisualization()
{
#if WITH_EDITORONLY_DATA
	if (AreaVisualization)
	{
		const FVector BoxExtent(
			FMath::Max(FMath::Abs(AreaSize.X) * 0.5f, 0.5f),
			FMath::Max(FMath::Abs(AreaSize.Y) * 0.5f, 0.5f),
			10.0f);
		AreaVisualization->SetBoxExtent(BoxExtent);
	}
#endif
}

#if WITH_EDITOR
void ADualContourBombRainActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateAreaVisualization();
}
#endif
