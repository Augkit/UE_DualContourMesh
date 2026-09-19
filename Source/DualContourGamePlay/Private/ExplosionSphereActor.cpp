// 爆炸球 Actor 实现：每帧驱动缩放扩张、材质 Age01 参数与点光源闪光
#include "ExplosionSphereActor.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AExplosionSphereActor::AExplosionSphereActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SphereMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SphereMesh"));
	SetRootComponent(SphereMesh);
	SphereMesh->SetCollisionProfileName(TEXT("NoCollision"));
	SphereMesh->SetCastShadow(false);

	// 引擎自带球体（半径 0.5m），由缩放控制实际大小
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshFinder(TEXT("/Engine/BasicShapes/Sphere"));
	if (MeshFinder.Succeeded())
	{
		SphereMesh->SetStaticMesh(MeshFinder.Object);
	}

	// 爆炸球材质（脚本生成的插件资产）
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(TEXT("/DualContourMesh/FX/M_ExplosionSphere.M_ExplosionSphere"));
	if (MatFinder.Succeeded())
	{
		BaseMaterial = MatFinder.Object;
		SphereMesh->SetMaterial(0, MatFinder.Object);
	}

	// 爆心点光源闪光
	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PointLight"));
	PointLight->SetupAttachment(SphereMesh);
	PointLight->SetLightColor(FLinearColor(1.0f, 0.6f, 0.3f));
	PointLight->SetIntensity(0.0f);
	PointLight->SetAttenuationRadius(1200.0f);
}

void AExplosionSphereActor::BeginPlay()
{
	Super::BeginPlay();

	if (BaseMaterial)
	{
		MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		SphereMesh->SetMaterial(0, MID);
	}

	Restart();
}

void AExplosionSphereActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bPlaying)
	{
		return;
	}

	Age += DeltaSeconds * FMath::Max(PlaybackSpeed, 0.01f);
	const float SafeLifetime = FMath::Max(Lifetime, 0.01f);

	// 先把最后一帧完整提交给材质，再结束 Actor，避免在边界帧直接跳过动画尾部。
	const float K = FMath::Clamp(Age / SafeLifetime, 0.0f, 1.0f);

	// 扩张：0.7 秒内用平滑插值长到 EndScale（有可感知的生长过程）
	const float GrowK = FMath::Clamp(Age / 0.7f, 0.0f, 1.0f);
	const float GrowE = GrowK * GrowK * (3.0f - 2.0f * GrowK);
	const float Scale = FMath::Lerp(StartScale, EndScale, GrowE) * SpawnSizeMultiplier;
	SetActorScale3D(FVector(Scale));

	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Age01"), K);
	}

	// 场景扭曲效果不使用额外光源；保留组件仅为兼容已有蓝图/关卡实例。
	PointLight->SetIntensity(0.0f);

	// 生命周期结束：循环测试模式重播，否则隐藏/自毁。
	// 结束判断放在最后，确保 Age01=1 的尾帧已经被提交。
	if (Age >= SafeLifetime)
	{
		if (bLoop)
		{
			Restart();
			return;
		}
		bPlaying = false;
		PointLight->SetIntensity(0.0f);
		if (bAutoDestroy)
		{
			SphereMesh->SetVisibility(false);
			Destroy();
		}
		return;
	}
}

void AExplosionSphereActor::Restart()
{
	Age = 0.0f;
	bPlaying = true;
	SetActorScale3D(FVector(StartScale * SpawnSizeMultiplier));
	SphereMesh->SetVisibility(true);
	PointLight->SetIntensity(0.0f);
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Age01"), 0.0f);
	}
}

AExplosionSphereActor* AExplosionSphereActor::SpawnExplosion(UObject* WorldContextObject, TSubclassOf<AExplosionSphereActor> ActorClass, FVector Location, float SizeMultiplier)
{
	UWorld* World = GEngine && WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !ActorClass)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AExplosionSphereActor* Actor = World->SpawnActor<AExplosionSphereActor>(ActorClass, Location, FRotator::ZeroRotator, Params);
	if (Actor)
	{
		Actor->SpawnSizeMultiplier = FMath::Max(0.01f, SizeMultiplier);
		// SpawnActor 已经触发了 BeginPlay，因此这里要重新应用一次初始状态。
		Actor->Restart();
	}
	return Actor;
}
