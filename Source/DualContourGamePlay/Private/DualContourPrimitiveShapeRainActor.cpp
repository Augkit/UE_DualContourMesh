#include "DualContourPrimitiveShapeRainActor.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADualContourPrimitiveShapeRainActor::ADualContourPrimitiveShapeRainActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	LoadDefaultShapeMeshes();

#if WITH_EDITORONLY_DATA
	AreaVisualization = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("AreaVisualization"));
	if (AreaVisualization)
	{
		AreaVisualization->SetupAttachment(SceneRoot);
		AreaVisualization->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AreaVisualization->SetGenerateOverlapEvents(false);
		AreaVisualization->SetHiddenInGame(true);
		AreaVisualization->ShapeColor = FColor(64, 192, 255);
		AreaVisualization->bDrawOnlyIfSelected = false;
	}
#endif
}

void ADualContourPrimitiveShapeRainActor::LoadDefaultShapeMeshes()
{
	if (ShapeMeshes.Num() > 0)
		return;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));

	if (CubeMesh.Succeeded()) ShapeMeshes.Add(CubeMesh.Object);
	if (SphereMesh.Succeeded()) ShapeMeshes.Add(SphereMesh.Object);
	if (CylinderMesh.Succeeded()) ShapeMeshes.Add(CylinderMesh.Object);
	if (ConeMesh.Succeeded()) ShapeMeshes.Add(ConeMesh.Object);
}

void ADualContourPrimitiveShapeRainActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateAreaVisualization();
}

void ADualContourPrimitiveShapeRainActor::BeginPlay()
{
	Super::BeginPlay();
	if (bStartAutomatically)
		StartShapeRain();
}

void ADualContourPrimitiveShapeRainActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopShapeRain();
	ClearSpawnedShapes();
	Super::EndPlay(EndPlayReason);
}

void ADualContourPrimitiveShapeRainActor::StartShapeRain()
{
	StopShapeRain();
	ClearSpawnedShapes();
	SpawnedShapeCount = 0;

	if (!GetWorld() || ShapeMeshes.Num() == 0 || ShapeCount <= 0)
		return;

	if (!FMath::IsFinite(SpawnDuration) || SpawnDuration <= 0.0f)
	{
		while (SpawnedShapeCount < ShapeCount)
			SpawnShape();
		BP_OnShapeRainFinished();
		return;
	}

	const float SpawnInterval = SpawnDuration / static_cast<float>(ShapeCount);
	GetWorldTimerManager().SetTimer(
		SpawnTimerHandle, this, &ADualContourPrimitiveShapeRainActor::SpawnNextShape,
		SpawnInterval, true, SpawnInterval);
}

void ADualContourPrimitiveShapeRainActor::StopShapeRain()
{
	GetWorldTimerManager().ClearTimer(SpawnTimerHandle);
}

void ADualContourPrimitiveShapeRainActor::ClearSpawnedShapes()
{
	for (AStaticMeshActor* Shape : SpawnedShapes)
	{
		if (IsValid(Shape))
			Shape->Destroy();
	}
	SpawnedShapes.Reset();
}

bool ADualContourPrimitiveShapeRainActor::IsShapeRainActive() const
{
	return GetWorld() && GetWorldTimerManager().IsTimerActive(SpawnTimerHandle);
}

void ADualContourPrimitiveShapeRainActor::SpawnNextShape()
{
	if (SpawnedShapeCount >= ShapeCount)
	{
		StopShapeRain();
		return;
	}

	SpawnShape();
	if (SpawnedShapeCount >= ShapeCount)
	{
		StopShapeRain();
		BP_OnShapeRainFinished();
	}
}

void ADualContourPrimitiveShapeRainActor::SpawnShape()
{
	UWorld* World = GetWorld();
	if (!World || ShapeMeshes.Num() == 0)
		return;

	const FVector2D SafeAreaSize(
		FMath::Max(FMath::Abs(AreaSize.X), 1.0f),
		FMath::Max(FMath::Abs(AreaSize.Y), 1.0f));
	const FVector LocalOffset(
		FMath::FRandRange(-SafeAreaSize.X * 0.5f, SafeAreaSize.X * 0.5f),
		FMath::FRandRange(-SafeAreaSize.Y * 0.5f, SafeAreaSize.Y * 0.5f),
		0.0f);
	const FVector SpawnLocation = GetActorTransform().TransformPositionNoScale(LocalOffset);
	const FRotator SpawnRotation = FRotator(
		FMath::FRandRange(0.0f, 360.0f), FMath::FRandRange(0.0f, 360.0f), FMath::FRandRange(0.0f, 360.0f));

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = GetInstigator();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Shape = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), SpawnLocation, SpawnRotation, SpawnParameters);
	if (!Shape || !Shape->GetStaticMeshComponent())
		return;

	UStaticMeshComponent* MeshComponent = Shape->GetStaticMeshComponent();
	// AStaticMeshActor defaults to Static mobility. Switch it before assigning the
	// mesh and enabling physics so the spawned shape can move and receive velocity.
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetStaticMesh(ShapeMeshes[FMath::RandHelper(ShapeMeshes.Num())]);
	MeshComponent->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	MeshComponent->SetNotifyRigidBodyCollision(true);
	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	const float MinScale = FMath::Max(FMath::Min(ShapeScaleRange.X, ShapeScaleRange.Y), 0.01f);
	const float MaxScale = FMath::Max(FMath::Max(ShapeScaleRange.X, ShapeScaleRange.Y), MinScale);
	MeshComponent->SetWorldScale3D(FVector(FMath::FRandRange(MinScale, MaxScale)));

	if (InitialDownwardSpeed > 0.0f)
		MeshComponent->SetPhysicsLinearVelocity(-GetActorUpVector() * InitialDownwardSpeed);
	if (ShapeLifeSpan > 0.0f && FMath::IsFinite(ShapeLifeSpan))
		Shape->SetLifeSpan(ShapeLifeSpan);

	SpawnedShapes.Add(Shape);
	++SpawnedShapeCount;
}

void ADualContourPrimitiveShapeRainActor::UpdateAreaVisualization()
{
#if WITH_EDITORONLY_DATA
	if (AreaVisualization)
	{
		AreaVisualization->SetBoxExtent(FVector(
			FMath::Max(FMath::Abs(AreaSize.X) * 0.5f, 0.5f),
			FMath::Max(FMath::Abs(AreaSize.Y) * 0.5f, 0.5f),
			10.0f));
	}
#endif
}

#if WITH_EDITOR
void ADualContourPrimitiveShapeRainActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateAreaVisualization();
}
#endif
