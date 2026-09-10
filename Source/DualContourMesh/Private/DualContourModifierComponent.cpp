#include "DualContourModifierComponent.h"

#include "DualContourMeshActor.h"
#include "DualContourMeshComponent.h"
#include "Engine/World.h"
#include "VolumeSampler/VolumeSampler.h"

UDualContourModifierComponent::UDualContourModifierComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UDualContourModifierComponent::AddSampler(TSubclassOf<UVolumeSampler> SamplerClass)
{
	if (!SamplerClass || SamplerClass->HasAnyClassFlags(CLASS_Abstract))
		return INDEX_NONE;

	return Samplers.Add(NewObject<UVolumeSampler>(this, SamplerClass));
}

bool UDualContourModifierComponent::ModifyDualContourWithRay(const FVector& WorldRayOrigin,
	const FVector& WorldRayDirection, int32 SamplerIndex, uint8 MaterialId, bool bExcavate)
{
	if (!GetWorld() || WorldRayOrigin.ContainsNaN() || WorldRayDirection.ContainsNaN())
		return false;
	if (!FMath::IsFinite(HitPositionRetreatDistance) || HitPositionRetreatDistance < 0.0f)
		return false;

	const FVector NormalizedRayDirection = WorldRayDirection.GetSafeNormal();
	if (NormalizedRayDirection.IsNearlyZero())
		return false;

	FHitResult HitResult;
	if (!GetWorld()->LineTraceSingleByChannel(HitResult, WorldRayOrigin,
		WorldRayOrigin + NormalizedRayDirection * 100000.0f, ECC_Visibility))
		return false;

	if (!HitResult.GetComponent() || !HitResult.GetComponent()->IsA<UDualContourMeshComponent>())
		return false;

	const FVector AdjustedHitPosition = HitResult.ImpactPoint - NormalizedRayDirection * HitPositionRetreatDistance;
	return ModifyDualContourWithSamplerAndDirection(AdjustedHitPosition, HitResult.ImpactNormal,
		NormalizedRayDirection, Cast<ADualContourMeshActor>(HitResult.GetActor()), SamplerIndex, MaterialId, bExcavate);
}

bool UDualContourModifierComponent::ModifyDualContourWithSamplerAndDirection(const FVector& WorldHitPos,
	const FVector& WorldHitNormal, const FVector& WorldRayDirection, ADualContourMeshActor* MeshActor,
	int32 SamplerIndex, uint8 MaterialId, bool bExcavate)
{
	if (!IsValid(MeshActor) || !Samplers.IsValidIndex(SamplerIndex)
	    || !FMath::IsFinite(SamplerScale) || SamplerScale <= UE_SMALL_NUMBER)
		return false;

	const TObjectPtr<UVolumeSampler>& Sampler = Samplers[SamplerIndex];
	if (!IsValid(Sampler))
		return false;

	const bool bDensityModified = MeshActor->ModifyDensityWithSampler(
		WorldHitPos, FVector::UpVector, Sampler, SamplerScale, bExcavate, WorldRayDirection);
	const bool bMaterialModified = MeshActor->ModifyMaterialWithSampler(
		WorldHitPos, FVector::UpVector, Sampler, SamplerScale * 1.3f, MaterialId, WorldRayDirection);
	return bDensityModified || bMaterialModified;
}
