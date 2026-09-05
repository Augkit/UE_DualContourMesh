#pragma once

#include "CoreMinimal.h"
#include "DualContourEditorTypes.generated.h"

UENUM(BlueprintType)
enum class EDualContourDensityEditOperation : uint8
{
	Sculpt,
	SculptSubtract,
	Flatten,
	Erase,
	Smooth,
	StampUnion,
	StampDifference,
};

UENUM(BlueprintType)
enum class EDualContourBrushShape : uint8
{
	Sphere,
	Box,
};

UENUM(BlueprintType)
enum class EDualContourBrushFalloff : uint8
{
	Smooth,
	Linear,
	Spherical,
	Tip,
};

class UVolumeSampledDualContour;

/** Editor brush description. All positions and sizes are in the target DualContour's local space. */
struct DUALCONTOUREDITOR_API FDualContourBrushStamp
{
	EDualContourDensityEditOperation Operation = EDualContourDensityEditOperation::Sculpt;
	EDualContourBrushShape Shape = EDualContourBrushShape::Sphere;
	EDualContourBrushFalloff FalloffType = EDualContourBrushFalloff::Smooth;
	FVector LocalCenter = FVector::ZeroVector;
	FVector LocalNormal = FVector::UpVector;
	FVector ClayPlaneOrigin = FVector::ZeroVector;
	FVector FlattenPlaneOrigin = FVector::ZeroVector;
	FVector FlattenPlaneNormal = FVector::UpVector;
	float Radius = 100.0f;
	float Falloff = 0.5f;
	float Strength = 0.3f;
	float TimeScale = 1.0f;
	bool bUseClayBrush = false;
	/** Makes radial falloff define the axial profile of a stationary sculpt stamp. */
	bool bUseDirectionalFalloff = false;

	/** Used only by StampUnion/StampDifference; maps source local positions into target local space. */
	UVolumeSampledDualContour* VolumeBrush = nullptr;
	FTransform VolumeToTarget = FTransform::Identity;
};
