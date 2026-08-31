#pragma once

#include "CoreMinimal.h"

namespace DualContourUtils
{
	FORCEINLINE int32 Volume(const FIntVector& Dimensions)
	{
		return Dimensions.X * Dimensions.Y * Dimensions.Z;
	}

	FORCEINLINE int32 LinearIndex(const FIntVector& Dimensions, int32 X, int32 Y, int32 Z)
	{
		return X + Y * Dimensions.X + Z * Dimensions.X * Dimensions.Y;
	}

	FORCEINLINE bool IsValidCoordinate(const FIntVector& Dimensions, int32 X, int32 Y, int32 Z)
	{
		return X >= 0 && X < Dimensions.X
			&& Y >= 0 && Y < Dimensions.Y
			&& Z >= 0 && Z < Dimensions.Z;
	}
}
