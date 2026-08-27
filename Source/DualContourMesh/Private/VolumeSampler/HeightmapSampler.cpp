#include "VolumeSampler/HeightmapSampler.h"

#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "Math/Float16.h"
#include "TextureResource.h"

namespace
{
bool ReadHeightmap(const UTexture2D& Texture, FIntPoint& OutSize, TArray<float>& OutValues, FText& OutError)
{
#if WITH_EDITORONLY_DATA
	FImage SourceImage;
	FTextureSource& Source = const_cast<UTexture2D&>(Texture).Source;
	if (Source.IsValid() && Source.GetMipImage(SourceImage, 0))
	{
		if (SourceImage.SizeX <= 0 || SourceImage.SizeY <= 0 || SourceImage.NumSlices != 1
		    || static_cast<int64>(SourceImage.SizeX) * SourceImage.SizeY > MAX_int32)
		{
			OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidHeightmapSource",
				"The Heightmap source must be a non-empty 2D texture small enough for CPU sampling.");
			return false;
		}

		OutSize = FIntPoint(SourceImage.SizeX, SourceImage.SizeY);
		OutValues.SetNumUninitialized(OutSize.X * OutSize.Y);
		for (int32 Y = 0; Y < OutSize.Y; ++Y)
			for (int32 X = 0; X < OutSize.X; ++X)
				OutValues[X + Y * OutSize.X] = FMath::Clamp(SourceImage.GetOnePixelLinear(X, Y).R, 0.0f, 1.0f);
		return true;
	}
#endif

	const FTexturePlatformData* PlatformData = Texture.GetPlatformData();
	if (!PlatformData || PlatformData->Mips.IsEmpty())
	{
		OutError = FText::Format(NSLOCTEXT("ProceduralVolumeSampler", "MissingHeightmapData",
			"{0} has no readable source or platform mip data."), FText::FromString(Texture.GetPathName()));
		return false;
	}

	const FTexture2DMipMap& Mip = PlatformData->Mips[0];
	if (Mip.SizeX <= 0 || Mip.SizeY <= 0 || static_cast<int64>(Mip.SizeX) * Mip.SizeY > MAX_int32)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidHeightmapMip",
			"The Heightmap platform mip has an invalid resolution.");
		return false;
	}

	int64 BytesPerPixel = 0;
	switch (PlatformData->PixelFormat)
	{
		case PF_G8:
		case PF_R8:
		case PF_R8_UINT:
			BytesPerPixel = sizeof(uint8);
			break;
		case PF_G16:
		case PF_R16F:
		case PF_R16F_FILTER:
			BytesPerPixel = sizeof(uint16);
			break;
		case PF_B8G8R8A8:
		case PF_R8G8B8A8:
			BytesPerPixel = sizeof(FColor);
			break;
		case PF_R32_FLOAT:
			BytesPerPixel = sizeof(float);
			break;
		default:
			OutError = FText::Format(NSLOCTEXT("ProceduralVolumeSampler", "UnsupportedHeightmapFormat",
					"{0} uses a compressed or unsupported pixel format. Use Grayscale or an uncompressed texture compression setting for cooked CPU sampling."),
				FText::FromString(Texture.GetPathName()));
			return false;
	}

	const int64 PixelCount = static_cast<int64>(Mip.SizeX) * Mip.SizeY;
	if (Mip.BulkData.GetBulkDataSize() < PixelCount * BytesPerPixel)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "TruncatedHeightmapMip",
			"The Heightmap platform mip is smaller than its declared resolution.");
		return false;
	}

	const void* MipData = Mip.BulkData.LockReadOnly();
	if (!MipData)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "HeightmapReadFailed",
			"The Heightmap platform mip could not be locked for CPU reading.");
		return false;
	}

	OutSize = FIntPoint(Mip.SizeX, Mip.SizeY);
	OutValues.SetNumUninitialized(static_cast<int32>(PixelCount));
	for (int32 Index = 0; Index < OutValues.Num(); ++Index)
	{
		float Height = 0.0f;
		switch (PlatformData->PixelFormat)
		{
			case PF_G8:
			case PF_R8:
			case PF_R8_UINT:
				Height = static_cast<const uint8*>(MipData)[Index] / 255.0f;
				break;
			case PF_G16:
				Height = static_cast<const uint16*>(MipData)[Index] / 65535.0f;
				break;
			case PF_R16F:
			case PF_R16F_FILTER:
				Height = static_cast<const FFloat16*>(MipData)[Index].GetFloat();
				break;
			case PF_B8G8R8A8:
				Height = static_cast<const FColor*>(MipData)[Index].R / 255.0f;
				break;
			case PF_R8G8B8A8:
				Height = static_cast<const uint8*>(MipData)[Index * 4] / 255.0f;
				break;
			case PF_R32_FLOAT:
				Height = static_cast<const float*>(MipData)[Index];
				break;
			default:
				break;
		}
		OutValues[Index] = FMath::Clamp(Height, 0.0f, 1.0f);
	}
	Mip.BulkData.Unlock();
	return true;
}
}

UHeightmapSampler::UHeightmapSampler()
{
	FRichCurve* RichCurve = HeightCurve.GetRichCurve();
	RichCurve->AddKey(0.0f, 0.0f);
	RichCurve->AddKey(1.0f, 1.0f);
}

#if WITH_EDITOR
void UHeightmapSampler::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	ClampHeightCurve();
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void UHeightmapSampler::ClampHeightCurve()
{
	// External curves are shared assets and must not be modified by this sampler.
	if (HeightCurve.ExternalCurve)
		return;

	FRichCurve& RichCurve = HeightCurve.EditorCurveData;
	TArray<FKeyHandle> KeyHandles;
	KeyHandles.Reserve(RichCurve.GetNumKeys());
	for (auto It = RichCurve.GetKeyHandleIterator(); It; ++It)
		KeyHandles.Add(*It);

	for (const FKeyHandle KeyHandle : KeyHandles)
	{
		const float KeyValue = RichCurve.GetKeyValue(KeyHandle);
		const float ClampedValue = FMath::Clamp(KeyValue, 0.0f, 1.0f);
		if (ClampedValue != KeyValue)
			RichCurve.SetKeyValue(KeyHandle, ClampedValue, false);

		const float KeyTime = RichCurve.GetKeyTime(KeyHandle);
		const float ClampedTime = FMath::Clamp(KeyTime, 0.0f, 1.0f);
		if (ClampedTime != KeyTime)
			RichCurve.SetKeyTime(KeyHandle, ClampedTime);
	}
}
#endif

bool UHeightmapSampler::Prepare(FText& OutError) const
{
	if (!Super::Prepare(OutError))
		return false;
	if (!Heightmap)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "MissingHeightmap", "HeightmapSampler requires a Heightmap texture.");
		return false;
	}
	if (!FMath::IsFinite(Bias) || Tiling.ContainsNaN()
	    || !FMath::IsFinite(Tiling.X) || !FMath::IsFinite(Tiling.Y)
	    || Tiling.X <= UE_SMALL_NUMBER || Tiling.Y <= UE_SMALL_NUMBER)
	{
		OutError = NSLOCTEXT("ProceduralVolumeSampler", "InvalidHeightmapSettings",
			"Bias must be finite and both Tiling components must be positive and finite.");
		return false;
	}
	return ReadHeightmap(*Heightmap, CachedHeightmapSize, CachedHeightValues, OutError);
}

void UHeightmapSampler::Finish() const
{
	CachedHeightmapSize = FIntPoint::ZeroValue;
	CachedHeightValues.Reset();
	Super::Finish();
}

float UHeightmapSampler::SampleHeight(const FVector2D& UV) const
{
	if (CachedHeightValues.IsEmpty() || CachedHeightmapSize.X <= 0 || CachedHeightmapSize.Y <= 0)
		return 0.5f;

	const FVector2D WrappedUV(FMath::Frac(UV.X), FMath::Frac(UV.Y));
	const double TexelX = WrappedUV.X * CachedHeightmapSize.X - 0.5;
	const double TexelY = WrappedUV.Y * CachedHeightmapSize.Y - 0.5;
	const int32 X0Unwrapped = FMath::FloorToInt(TexelX);
	const int32 Y0Unwrapped = FMath::FloorToInt(TexelY);
	const int32 X0 = (X0Unwrapped % CachedHeightmapSize.X + CachedHeightmapSize.X) % CachedHeightmapSize.X;
	const int32 Y0 = (Y0Unwrapped % CachedHeightmapSize.Y + CachedHeightmapSize.Y) % CachedHeightmapSize.Y;
	const int32 X1 = (X0 + 1) % CachedHeightmapSize.X;
	const int32 Y1 = (Y0 + 1) % CachedHeightmapSize.Y;
	const float FractionX = static_cast<float>(TexelX - X0Unwrapped);
	const float FractionY = static_cast<float>(TexelY - Y0Unwrapped);
	const auto Value = [this](int32 X, int32 Y)
	{
		return CachedHeightValues[X + Y * CachedHeightmapSize.X];
	};
	return FMath::Lerp(FMath::Lerp(Value(X0, Y0), Value(X1, Y0), FractionX),
		FMath::Lerp(Value(X0, Y1), Value(X1, Y1), FractionX), FractionY);
}

float UHeightmapSampler::GetSignedDistance_Implementation(const FVector& LocalPosition) const
{
	const FVector2D UV(LocalPosition.X / VolumeSize.X + 0.5, LocalPosition.Y / VolumeSize.Y + 0.5);
	const float MappedHeight = FMath::Clamp(
		HeightCurve.GetRichCurveConst()->Eval(SampleHeight(UV * Tiling)), 0.0f, 1.0f);
	const float SurfaceZ = (MappedHeight + Bias - 1.0f) * VolumeSize.Z;
	return static_cast<float>(LocalPosition.Z) - SurfaceZ;
}
