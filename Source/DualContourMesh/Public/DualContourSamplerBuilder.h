#pragma once

#include "CoreMinimal.h"
#include "DualContourTypes.h"

class UDualContour;
class UVolumeSampler;

/** Builds DualContour density chunks from a volume sampler. */
class DUALCONTOURMESH_API FDualContourSamplerBuilder
{
public:
	static bool BuildDensityChunks(const UVolumeSampler& Sampler, UDualContour* Target,
		const FTransform& SampleTransform, FDualContourSampledRegion& OutRegion,
		FText& OutError);
};
