#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DualContourBombActor.generated.h"

class ADualContourMeshActor;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;
class UVolumeSampler;
class AExplosionSphereActor;

/**
 * Physics bomb that removes density from every intersecting DualContour mesh when it hits something.
 * The sampler is instanced so derived Blueprints can replace the default sphere with another volume shape.
 */
UCLASS(Blueprintable)
class DUALCONTOURGAMEPLAY_API ADualContourBombActor : public AActor
{
	GENERATED_BODY()

public:
	ADualContourBombActor();

	/** Detonates the bomb once. Returns true when at least one DualContour mesh was modified. */
	UFUNCTION(BlueprintCallable, Category = "Bomb")
	bool Explode();

	UFUNCTION(BlueprintPure, Category = "Bomb")
	bool HasExploded() const { return bHasExploded; }

	/** Sets the rigid body's world-space launch velocity. */
	UFUNCTION(BlueprintCallable, Category = "Bomb")
	void LaunchBomb(const FVector& Velocity);

	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse,
		const FHitResult& Hit) override;
	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	/** Called after the density edit has been submitted. Use this to spawn sound, particles, or camera shake. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Bomb", meta = (DisplayName = "On Exploded"))
	void BP_OnExploded(bool bModifiedDualContour);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bomb|Components")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bomb|Components")
	TObjectPtr<UStaticMeshComponent> BombMesh;

	/** Visual size multiplier for the bomb mesh. Collision and explosion size are configured separately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb|Visual", meta = (ClampMin = "0.01"))
	float BombSizeMultiplier = 0.5f;

	/** World-space diameter of the sampling volume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb|Explosion", meta = (ClampMin = "1.0", Units = "cm"))
	float ExplosionDiameter = 400.0f;

	/** Volume used for the Difference edit. Defaults to a sphere filling ExplosionDiameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Bomb|Explosion")
	TObjectPtr<UVolumeSampler> ExplosionSampler;

	/** Material id used by the shared runtime edit path at the newly exposed surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb|Explosion", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 CutSurfaceMaterialId = 0;

	/** Delay before destroying the actor, allowing Blueprint explosion effects to begin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb|Explosion", meta = (ClampMin = "0.0", Units = "s"))
	float DestroyDelay = 0.1f;

	/** Explosion sphere spawned at the impact point after the bomb detonates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb|Explosion")
	TSubclassOf<AExplosionSphereActor> ExplosionEffectClass;

	/** Additional size multiplier applied after matching the sampler radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bomb|Explosion", meta = (ClampMin = "0.01"))
	float ExplosionEffectSizeMultiplier = 1.0f;

private:
	bool ExcavateActor(ADualContourMeshActor& MeshActor, const FVector& ExplosionCenter) const;

	UPROPERTY(Transient)
	bool bHasExploded = false;
};
