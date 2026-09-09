#pragma once

#include "CoreMinimal.h"
#include "VolumeSampler/ProceduralVolumeSampler.h"
#include "TunnelVolumeSampler.generated.h"

/**
 * Solid tunnel-shaped volume running along the local X axis.
 *
 * The cross-section has a flat floor, vertical lower walls and an elliptical
 * arch. The -X entrance can remain enlarged for a short distance before it
 * eases into the regular cross-section. The +X end closes with a rounded cap.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, AutoExpandCategories = ("Tunnel"))
class DUALCONTOURMESH_API UTunnelVolumeSampler : public UProceduralVolumeSampler
{
	GENERATED_BODY()

public:
	UTunnelVolumeSampler();

	/** Total distance from the flat entrance plane to the rounded head tip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Length",
		meta = (ClampMin = "0.0001", Units = "cm"))
	float Length = 1000.0f;

	/** Length occupied by the rounded +X head; it must be shorter than Length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Length",
		meta = (ClampMin = "0.0001", Units = "cm"))
	float HeadLength = 180.0f;

	/** Local Z coordinate shared by the entrance, transition and regular floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Cross Section", meta = (Units = "cm"))
	float FloorZ = -200.0f;

	/** Half-width of the regular tunnel section. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Cross Section",
		meta = (ClampMin = "0.0001", Units = "cm"))
	float RegularHalfWidth = 180.0f;

	/** Height of the regular vertical side walls above the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Cross Section",
		meta = (ClampMin = "0.0", Units = "cm"))
	float RegularWallHeight = 120.0f;

	/** Height of the regular elliptical arch above the side walls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Cross Section",
		meta = (ClampMin = "0.0001", Units = "cm"))
	float RegularRoofHeight = 180.0f;

	/** Half-width at the enlarged -X entrance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Entrance",
		meta = (ClampMin = "0.0001", Units = "cm"))
	float EntranceHalfWidth = 260.0f;

	/** Height of the entrance's vertical side walls above the shared floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Entrance",
		meta = (ClampMin = "0.0", Units = "cm"))
	float EntranceWallHeight = 160.0f;

	/** Height of the enlarged entrance arch above its side walls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Entrance",
		meta = (ClampMin = "0.0001", Units = "cm"))
	float EntranceRoofHeight = 260.0f;

	/** Distance for which the enlarged entrance cross-section remains unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Entrance",
		meta = (ClampMin = "0.0", Units = "cm"))
	float EntranceStraightLength = 100.0f;

	/** Smooth transition distance from the enlarged entrance to the regular section. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tunnel|Entrance",
		meta = (ClampMin = "0.0001", Units = "cm"))
	float EntranceTransitionLength = 240.0f;

	virtual float GetSignedDistance_Implementation(const FVector& CenteredLocalPosition) const override;

protected:
	virtual bool Prepare(FText& OutError) const override;
};
