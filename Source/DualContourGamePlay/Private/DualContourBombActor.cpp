#include "DualContourBombActor.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DualContourMeshActor.h"
#include "ExplosionSphereActor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"

ADualContourBombActor::ADualContourBombActor()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	SetRootComponent(CollisionComponent);
	CollisionComponent->InitSphereRadius(25.0f);
	CollisionComponent->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	CollisionComponent->SetNotifyRigidBodyCollision(true);
	CollisionComponent->SetSimulatePhysics(true);
	CollisionComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	BombMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BombMesh"));
	BombMesh->SetupAttachment(CollisionComponent);
	BombMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
		BombMesh->SetStaticMesh(SphereMesh.Object);

	USphereVolumeSampler* SphereSampler = CreateDefaultSubobject<USphereVolumeSampler>(TEXT("ExplosionSampler"));
	SphereSampler->Radius = 0.5f;
	ExplosionSampler = SphereSampler;

	ExplosionEffectClass = AExplosionSphereActor::StaticClass();
}

void ADualContourBombActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (BombMesh)
		BombMesh->SetRelativeScale3D(FVector(FMath::Max(BombSizeMultiplier, 0.01f)));
}

void ADualContourBombActor::BeginPlay()
{
	Super::BeginPlay();
	CollisionComponent->IgnoreActorWhenMoving(GetOwner(), true);
	CollisionComponent->IgnoreActorWhenMoving(GetInstigator(), true);
}

void ADualContourBombActor::LaunchBomb(const FVector& Velocity)
{
	if (!bHasExploded && CollisionComponent->IsSimulatingPhysics())
		CollisionComponent->SetPhysicsLinearVelocity(Velocity);
}

void ADualContourBombActor::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	if (Other && (Other == GetOwner() || Other == GetInstigator()))
		return;
	Explode();
}

bool ADualContourBombActor::Explode()
{
	if (bHasExploded || !IsValid(ExplosionSampler) || !FMath::IsFinite(ExplosionDiameter)
		|| ExplosionDiameter <= UE_SMALL_NUMBER)
	{
		return false;
	}

	bHasExploded = true;
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionComponent->SetSimulatePhysics(false);
	BombMesh->SetVisibility(false, true);

	const FVector ExplosionCenter = GetActorLocation();
	const float ExplosionRadius = ExplosionDiameter * 0.5f;
	bool bModifiedDualContour = false;

	// Iterating the terrain actors avoids depending on their collision object channel and also supports
	// an explosion whose first contact was with a non-terrain actor next to the terrain.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ADualContourMeshActor> It(World); It; ++It)
		{
			ADualContourMeshActor& MeshActor = **It;
			const FBox MeshBounds = MeshActor.GetComponentsBoundingBox(true);
			if (MeshBounds.IsValid && MeshBounds.ComputeSquaredDistanceToPoint(ExplosionCenter) <= FMath::Square(ExplosionRadius))
				bModifiedDualContour |= ExcavateActor(MeshActor, ExplosionCenter);
		}
	}

	// The visual effect is independent of the terrain edit, so it also plays when
	// the bomb hits a regular actor or an empty part of the level.
	// BasicShapes/Sphere 的直径是 100 cm。将效果球的最终直径对齐到
	// ExplosionDiameter，使它与采样器的爆炸半径一致。
	float EffectScaleMultiplier = FMath::Max(ExplosionEffectSizeMultiplier, 0.01f);
	if (ExplosionEffectClass)
	{
		const AExplosionSphereActor* EffectCDO = ExplosionEffectClass->GetDefaultObject<AExplosionSphereActor>();
		const float EffectEndScale = EffectCDO ? FMath::Max(EffectCDO->EndScale, 0.01f) : 8.0f;
		const float EffectFinalDiameter = 100.0f * EffectEndScale;
		EffectScaleMultiplier *= ExplosionDiameter / EffectFinalDiameter;
	}

	AExplosionSphereActor::SpawnExplosion(this, ExplosionEffectClass, ExplosionCenter, EffectScaleMultiplier);

	BP_OnExploded(bModifiedDualContour);
	if (DestroyDelay <= 0.0f)
	{
		Destroy();
	}
	else
	{
		SetLifeSpan(DestroyDelay);
	}
	return bModifiedDualContour;
}

bool ADualContourBombActor::ExcavateActor(ADualContourMeshActor& MeshActor, const FVector& ExplosionCenter) const
{
	const FVector AbsoluteScale = MeshActor.GetActorScale3D().GetAbs();
	if (AbsoluteScale.GetMin() <= UE_SMALL_NUMBER)
		return false;

	// Sampling is performed in the target actor's local space. Compensating for target scale keeps
	// ExplosionDiameter expressed in world units and makes the default sampler spherical in world space.
	const FVector LocalSamplingSize(
		ExplosionDiameter / AbsoluteScale.X,
		ExplosionDiameter / AbsoluteScale.Y,
		ExplosionDiameter / AbsoluteScale.Z);

	return MeshActor.ModifyDensityAndMaterialWithSampler(
		ExplosionCenter,
		FVector::UpVector,
		ExplosionSampler,
		LocalSamplingSize,
		LocalSamplingSize,
		true,
		FVector::ZeroVector,
		CutSurfaceMaterialId);
}
