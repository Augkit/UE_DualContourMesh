#include "VolumeSampler/NoiseVolumeSampler.h"

#include "ThirdParty/FastNoiseLite/FastNoiseLite.h"

namespace
{
FastNoiseLite::NoiseType ToFastNoiseType(const ENoiseSamplerType Value)
{
	switch (Value)
	{
		case ENoiseSamplerType::OpenSimplex2S:
			return FastNoiseLite::NoiseType_OpenSimplex2S;
		case ENoiseSamplerType::Cellular:
			return FastNoiseLite::NoiseType_Cellular;
		case ENoiseSamplerType::Perlin:
			return FastNoiseLite::NoiseType_Perlin;
		case ENoiseSamplerType::ValueCubic:
			return FastNoiseLite::NoiseType_ValueCubic;
		case ENoiseSamplerType::Value:
			return FastNoiseLite::NoiseType_Value;
		case ENoiseSamplerType::OpenSimplex2:
		default:
			return FastNoiseLite::NoiseType_OpenSimplex2;
	}
}

FastNoiseLite::RotationType3D ToFastNoiseRotation(const ENoiseSamplerRotation3D Value)
{
	switch (Value)
	{
		case ENoiseSamplerRotation3D::ImproveXYPlanes:
			return FastNoiseLite::RotationType3D_ImproveXYPlanes;
		case ENoiseSamplerRotation3D::ImproveXZPlanes:
			return FastNoiseLite::RotationType3D_ImproveXZPlanes;
		case ENoiseSamplerRotation3D::None:
		default:
			return FastNoiseLite::RotationType3D_None;
	}
}

FastNoiseLite::FractalType ToFastNoiseFractal(const ENoiseSamplerFractalType Value)
{
	switch (Value)
	{
		case ENoiseSamplerFractalType::FBm:
			return FastNoiseLite::FractalType_FBm;
		case ENoiseSamplerFractalType::Ridged:
			return FastNoiseLite::FractalType_Ridged;
		case ENoiseSamplerFractalType::PingPong:
			return FastNoiseLite::FractalType_PingPong;
		case ENoiseSamplerFractalType::None:
		default:
			return FastNoiseLite::FractalType_None;
	}
}

FastNoiseLite::CellularDistanceFunction ToFastNoiseCellularDistance(const ENoiseSamplerCellularDistance Value)
{
	switch (Value)
	{
		case ENoiseSamplerCellularDistance::Euclidean:
			return FastNoiseLite::CellularDistanceFunction_Euclidean;
		case ENoiseSamplerCellularDistance::Manhattan:
			return FastNoiseLite::CellularDistanceFunction_Manhattan;
		case ENoiseSamplerCellularDistance::Hybrid:
			return FastNoiseLite::CellularDistanceFunction_Hybrid;
		case ENoiseSamplerCellularDistance::EuclideanSquared:
		default:
			return FastNoiseLite::CellularDistanceFunction_EuclideanSq;
	}
}

FastNoiseLite::CellularReturnType ToFastNoiseCellularReturn(const ENoiseSamplerCellularReturn Value)
{
	switch (Value)
	{
		case ENoiseSamplerCellularReturn::CellValue:
			return FastNoiseLite::CellularReturnType_CellValue;
		case ENoiseSamplerCellularReturn::Distance2:
			return FastNoiseLite::CellularReturnType_Distance2;
		case ENoiseSamplerCellularReturn::Distance2Add:
			return FastNoiseLite::CellularReturnType_Distance2Add;
		case ENoiseSamplerCellularReturn::Distance2Subtract:
			return FastNoiseLite::CellularReturnType_Distance2Sub;
		case ENoiseSamplerCellularReturn::Distance2Multiply:
			return FastNoiseLite::CellularReturnType_Distance2Mul;
		case ENoiseSamplerCellularReturn::Distance2Divide:
			return FastNoiseLite::CellularReturnType_Distance2Div;
		case ENoiseSamplerCellularReturn::Distance:
		default:
			return FastNoiseLite::CellularReturnType_Distance;
	}
}

bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}
}

UNoiseVolumeSampler::UNoiseVolumeSampler() = default;

UNoiseVolumeSampler::~UNoiseVolumeSampler() = default;

void UNoiseVolumeSampler::ConfigureNoise(FastNoiseLite& Noise) const
{
	Noise.SetSeed(Seed);
	Noise.SetFrequency(Frequency);
	Noise.SetNoiseType(ToFastNoiseType(NoiseType));
	Noise.SetRotationType3D(ToFastNoiseRotation(Rotation3D));
	Noise.SetFractalType(ToFastNoiseFractal(FractalType));
	Noise.SetFractalOctaves(Octaves);
	Noise.SetFractalLacunarity(Lacunarity);
	Noise.SetFractalGain(Gain);
	Noise.SetFractalWeightedStrength(WeightedStrength);
	Noise.SetFractalPingPongStrength(PingPongStrength);
	Noise.SetCellularDistanceFunction(ToFastNoiseCellularDistance(CellularDistance));
	Noise.SetCellularReturnType(ToFastNoiseCellularReturn(CellularReturn));
	Noise.SetCellularJitter(CellularJitter);
}

bool UNoiseVolumeSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;

	if (!FMath::IsFinite(Frequency) || Frequency <= UE_SMALL_NUMBER || !IsFiniteVector(CoordinateOffset)
	    || Octaves < 1 || Octaves > 30 || !FMath::IsFinite(Lacunarity) || Lacunarity <= UE_SMALL_NUMBER
	    || !FMath::IsFinite(Gain) || Gain < 0.0f || Gain > 1.0f
	    || !FMath::IsFinite(WeightedStrength) || WeightedStrength < 0.0f || WeightedStrength > 1.0f
	    || !FMath::IsFinite(PingPongStrength) || PingPongStrength <= UE_SMALL_NUMBER
	    || !FMath::IsFinite(CellularJitter) || CellularJitter < 0.0f || CellularJitter > 1.0f)
	{
		OutError = NSLOCTEXT("NoiseVolumeSampler", "InvalidNoiseSettings",
			"Noise frequency, coordinates, fractal settings, and cellular settings must be finite and within their documented ranges.");
		return false;
	}

	if (Dimension == ENoiseSamplerDimension::HeightField2D
	    && (!FMath::IsFinite(HeightOffset) || !FMath::IsFinite(HeightAmplitude) || HeightAmplitude < 0.0f))
	{
		OutError = NSLOCTEXT("NoiseVolumeSampler", "InvalidHeightFieldSettings",
			"HeightOffset must be finite and HeightAmplitude must be finite and non-negative.");
		return false;
	}

	if (Dimension == ENoiseSamplerDimension::Volume3D
	    && (!FMath::IsFinite(IsoLevel) || IsoLevel < -1.0f || IsoLevel > 1.0f
	        || !FMath::IsFinite(NoiseDistanceScale) || NoiseDistanceScale <= UE_SMALL_NUMBER))
	{
		OutError = NSLOCTEXT("NoiseVolumeSampler", "InvalidVolumeNoiseSettings",
			"IsoLevel must be between -1 and 1, and NoiseDistanceScale must be positive and finite.");
		return false;
	}

	CachedNoise = MakeUnique<FastNoiseLite>();
	ConfigureNoise(*CachedNoise);
	return true;
}

void UNoiseVolumeSampler::Finish() const
{
	CachedNoise.Reset();
	Super::Finish();
}

float UNoiseVolumeSampler::SampleNoise2D(const FVector2D& LocalPosition) const
{
	if (CachedNoise)
		return CachedNoise->GetNoise(
			static_cast<float>(LocalPosition.X + CoordinateOffset.X),
			static_cast<float>(LocalPosition.Y + CoordinateOffset.Y));

	FastNoiseLite Noise;
	ConfigureNoise(Noise);
	return Noise.GetNoise(
		static_cast<float>(LocalPosition.X + CoordinateOffset.X),
		static_cast<float>(LocalPosition.Y + CoordinateOffset.Y));
}

float UNoiseVolumeSampler::SampleNoise3D(const FVector& LocalPosition) const
{
	if (CachedNoise)
		return CachedNoise->GetNoise(
			static_cast<float>(LocalPosition.X + CoordinateOffset.X),
			static_cast<float>(LocalPosition.Y + CoordinateOffset.Y),
			static_cast<float>(LocalPosition.Z + CoordinateOffset.Z));

	FastNoiseLite Noise;
	ConfigureNoise(Noise);
	return Noise.GetNoise(
		static_cast<float>(LocalPosition.X + CoordinateOffset.X),
		static_cast<float>(LocalPosition.Y + CoordinateOffset.Y),
		static_cast<float>(LocalPosition.Z + CoordinateOffset.Z));
}

float UNoiseVolumeSampler::GetSignedDistance_Implementation(const FVector& LocalPosition) const
{
	if (Dimension == ENoiseSamplerDimension::HeightField2D)
	{
		const float SurfaceZ = HeightOffset + SampleNoise2D(FVector2D(LocalPosition.X, LocalPosition.Y)) * HeightAmplitude;
		return static_cast<float>(LocalPosition.Z) - SurfaceZ;
	}

	return (IsoLevel - SampleNoise3D(LocalPosition)) * NoiseDistanceScale;
}
