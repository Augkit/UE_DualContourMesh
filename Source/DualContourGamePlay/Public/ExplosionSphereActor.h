// 爆炸球 Actor：指定位置生成、扩张并消散的场景折射球
// 材质：/DualContourMesh/FX/M_ExplosionSphere（半透明 + 2D Refraction）
// 用法：AExplosionSphereActor::SpawnExplosion(this, AExplosionSphereActor::StaticClass(), Hit.ImpactPoint);

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExplosionSphereActor.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(BlueprintType)
class DUALCONTOURGAMEPLAY_API AExplosionSphereActor : public AActor
{
	GENERATED_BODY()

public:
	AExplosionSphereActor();

	/** 在指定位置生成爆炸球并自动开始播放（WorldContext 传 this 即可） */
	UFUNCTION(BlueprintCallable, Category = "Explosion", meta = (WorldContext = "WorldContextObject", DisplayName = "Spawn Explosion Sphere"))
	static AExplosionSphereActor* SpawnExplosion(UObject* WorldContextObject, TSubclassOf<AExplosionSphereActor> ActorClass, FVector Location, float SizeMultiplier = 1.0f);

	/** 重新开始播放 */
	UFUNCTION(BlueprintCallable, Category = "Explosion")
	void Restart();

	/** 效果总时长（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	float Lifetime = 1.6f;

	/** 播放速度倍率；4.0 表示按原始速度的四倍播放。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion", meta = (ClampMin = "0.01"))
	float PlaybackSpeed = 8.0f;

	/** 起始缩放（引擎基础球半径 0.5m × 缩放） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	float StartScale = 1.2f;

	/** 结束缩放（长满时的球体大小） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	float EndScale = 8.0f;

	/** 播放结束后自动重新播放（测试用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	bool bLoop = false;

	/** 兼容旧蓝图的点光源参数；场景扭曲版本始终关闭点光源。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	float PeakLightIntensity = 0.0f;

	/** 播放结束后自动销毁自身 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	bool bAutoDestroy = true;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Explosion")
	TObjectPtr<UStaticMeshComponent> SphereMesh;

	UPROPERTY(VisibleAnywhere, Category = "Explosion")
	TObjectPtr<UPointLightComponent> PointLight;

	/** 爆炸球材质（/DualContourMesh/FX/M_ExplosionSphere），运行时会自动创建 MID 实例 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	TObjectPtr<UMaterialInterface> BaseMaterial;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	/** SpawnExplosion 传入的尺寸倍率；避免每帧基础缩放覆盖它。 */
	float SpawnSizeMultiplier = 1.0f;
	float Age = 0.f;
	bool bPlaying = false;
};
