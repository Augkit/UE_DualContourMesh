#include "VolumeSampler/TextureSDFSampler.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "Math/Float16.h"
#include "TextureResource.h"

namespace
{
bool ReadFloatTexture(const UTexture& Texture, const FTexturePlatformData* PlatformData,
	FVectorInt& OutResolution, TArray<float>& OutValues, FText& OutError)
{
#if WITH_EDITORONLY_DATA
	FTextureSource& Source = const_cast<UTexture&>(Texture).Source;
	const ETextureSourceFormat Format = Source.GetFormat();
	if (Format == TSF_R16F || Format == TSF_R32F)
	{
		OutResolution = FVectorInt(Source.GetSizeX(), Source.GetSizeY(), FMath::Max(1, Source.GetNumSlices()));
		const int64 ValueCount = static_cast<int64>(OutResolution.X) * OutResolution.Y * OutResolution.Z;
		if (ValueCount <= 0 || ValueCount > MAX_int32)
		{
			OutError = NSLOCTEXT("VolumeSampler", "InvalidTextureSize", "The texture source resolution is invalid or too large.");
			return false;
		}

		TArray64<uint8> Bytes;
		if (!Source.GetMipData(Bytes, 0))
		{
			OutError = FText::Format(NSLOCTEXT("VolumeSampler", "TextureReadFailed",
				"Could not read mip 0 from {0}."), FText::FromString(Texture.GetPathName()));
			return false;
		}

		const int64 BytesPerValue = Format == TSF_R16F ? sizeof(FFloat16) : sizeof(float);
		if (Bytes.Num() < ValueCount * BytesPerValue)
		{
			OutError = NSLOCTEXT("VolumeSampler", "TextureDataTruncated", "The texture source mip is smaller than its declared resolution.");
			return false;
		}

		OutValues.SetNumUninitialized(static_cast<int32>(ValueCount));
		if (Format == TSF_R16F)
		{
			const FFloat16* Values = reinterpret_cast<const FFloat16*>(Bytes.GetData());
			for (int32 Index = 0; Index < OutValues.Num(); ++Index)
				OutValues[Index] = Values[Index].GetFloat();
		}
		else
		{
			FMemory::Memcpy(OutValues.GetData(), Bytes.GetData(), ValueCount * sizeof(float));
		}
		return true;
	}
#endif

	if (!PlatformData || PlatformData->Mips.IsEmpty())
	{
		OutError = FText::Format(NSLOCTEXT("VolumeSampler", "MissingPlatformData",
			"{0} has no readable source or platform mip data."), FText::FromString(Texture.GetPathName()));
		return false;
	}
	const EPixelFormat PixelFormat = PlatformData->PixelFormat;
	if (PixelFormat != PF_R16F && PixelFormat != PF_R16F_FILTER && PixelFormat != PF_R32_FLOAT)
	{
		OutError = FText::Format(NSLOCTEXT("VolumeSampler", "UnsupportedTextureFormat",
			"{0} must use an uncompressed R16F or R32F pixel format for CPU sampling."), FText::FromString(Texture.GetPathName()));
		return false;
	}

	const FTexture2DMipMap& Mip = PlatformData->Mips[0];
	OutResolution = FVectorInt(Mip.SizeX, Mip.SizeY, FMath::Max<int32>(1, Mip.SizeZ));
	const int64 ValueCount = static_cast<int64>(OutResolution.X) * OutResolution.Y * OutResolution.Z;
	const int64 BytesPerValue = PixelFormat == PF_R32_FLOAT ? sizeof(float) : sizeof(FFloat16);
	if (ValueCount <= 0 || ValueCount > MAX_int32 || Mip.BulkData.GetBulkDataSize() < ValueCount * BytesPerValue)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidPlatformMip", "The texture platform mip is invalid or incomplete.");
		return false;
	}

	const void* MipData = Mip.BulkData.LockReadOnly();
	if (!MipData)
	{
		OutError = NSLOCTEXT("VolumeSampler", "PlatformMipReadFailed", "The texture platform mip could not be locked for CPU reading.");
		return false;
	}
	OutValues.SetNumUninitialized(static_cast<int32>(ValueCount));
	if (PixelFormat == PF_R32_FLOAT)
		FMemory::Memcpy(OutValues.GetData(), MipData, ValueCount * sizeof(float));
	else
	{
		const FFloat16* Values = static_cast<const FFloat16*>(MipData);
		for (int32 Index = 0; Index < OutValues.Num(); ++Index)
			OutValues[Index] = Values[Index].GetFloat();
	}
	Mip.BulkData.Unlock();
	return true;
}
}

float UTextureSDFSampler::SignedDistanceToDensity(float SignedDistance) const
{
	return static_cast<float>(GDualContourIsoValue) + DensityBias - SignedDistance * DensityScale;
}

float UTextureSDFSampler::SampleCachedTexture(const FVector& UVW) const
{
	if (CachedSignedDistances.IsEmpty())
		return 0.0f;

	// Match clamped GPU texture sampling: voxel values live at (index + 0.5) / resolution.
	const FVector TexelPosition(
		FMath::Clamp(UVW.X * CachedResolution.X - 0.5, 0.0, static_cast<double>(CachedResolution.X - 1)),
		FMath::Clamp(UVW.Y * CachedResolution.Y - 0.5, 0.0, static_cast<double>(CachedResolution.Y - 1)),
		FMath::Clamp(UVW.Z * CachedResolution.Z - 0.5, 0.0, static_cast<double>(CachedResolution.Z - 1)));
	const int32 X0 = FMath::FloorToInt(TexelPosition.X), Y0 = FMath::FloorToInt(TexelPosition.Y), Z0 = FMath::FloorToInt(TexelPosition.Z);
	const int32 X1 = FMath::Min(X0 + 1, CachedResolution.X - 1);
	const int32 Y1 = FMath::Min(Y0 + 1, CachedResolution.Y - 1);
	const int32 Z1 = FMath::Min(Z0 + 1, CachedResolution.Z - 1);
	const float FX = TexelPosition.X - X0, FY = TexelPosition.Y - Y0, FZ = TexelPosition.Z - Z0;
	const auto Value = [this](int32 X, int32 Y, int32 Z)
	{
		return CachedSignedDistances[CachedResolution.LinearIndex(X, Y, Z)];
	};
	const float D0 = FMath::Lerp(FMath::Lerp(Value(X0, Y0, Z0), Value(X1, Y0, Z0), FX),
		FMath::Lerp(Value(X0, Y1, Z0), Value(X1, Y1, Z0), FX), FY);
	const float D1 = FMath::Lerp(FMath::Lerp(Value(X0, Y0, Z1), Value(X1, Y0, Z1), FX),
		FMath::Lerp(Value(X0, Y1, Z1), Value(X1, Y1, Z1), FX), FY);
	return SignedDistanceToDensity(FMath::Lerp(D0, D1, FZ));
}

void UTextureSDFSampler::Finish() const
{
	CachedResolution = FVectorInt();
	CachedSignedDistances.Reset();
}

bool UTex3DSDFSampler::Prepare(FText& OutError) const
{
	return Super::Prepare(OutError) && PrepareTexture(OutError);
}

bool UTex3DSDFSampler::PrepareTexture(FText& OutError) const
{
	if (!Texture)
	{
		OutError = NSLOCTEXT("VolumeSampler", "MissingTexture3D", "Tex3DSDFSampler requires a VolumeTexture.");
		return false;
	}
	return ReadFloatTexture(*Texture, Texture->GetPlatformData(), CachedResolution, CachedSignedDistances, OutError);
}

float UTex3DSDFSampler::SampleNormalized(const FVector& UVW) const
{
	return SampleCachedTexture(UVW);
}

bool UTex2DSDFSampler::Prepare(FText& OutError) const
{
	return Super::Prepare(OutError) && PrepareTexture(OutError);
}

bool UTex2DSDFSampler::PrepareTexture(FText& OutError) const
{
	if (!Texture)
	{
		OutError = NSLOCTEXT("VolumeSampler", "MissingTexture2D", "Tex2DSDFSampler requires a Texture2D atlas.");
		return false;
	}
	if (VolumeResolution.X <= 0 || VolumeResolution.Y <= 0 || VolumeResolution.Z <= 0)
	{
		OutError = NSLOCTEXT("VolumeSampler", "InvalidAtlasResolution", "VolumeResolution must be positive on every axis.");
		return false;
	}
	const int64 VoxelCount = static_cast<int64>(VolumeResolution.X) * VolumeResolution.Y * VolumeResolution.Z;
	if (VoxelCount > MAX_int32)
	{
		OutError = NSLOCTEXT("VolumeSampler", "AtlasVolumeTooLarge", "VolumeResolution exceeds TArray capacity.");
		return false;
	}

	FVectorInt AtlasResolution;
	TArray<float> AtlasValues;
	if (!ReadFloatTexture(*Texture, Texture->GetPlatformData(), AtlasResolution, AtlasValues, OutError))
		return false;
	const int32 TileColumns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(VolumeResolution.Z)));
	const int32 TileRows = FMath::DivideAndRoundUp(VolumeResolution.Z, TileColumns);
	const int64 RequiredAtlasX = static_cast<int64>(VolumeResolution.X) * TileColumns;
	const int64 RequiredAtlasY = static_cast<int64>(VolumeResolution.Y) * TileRows;
	if (RequiredAtlasX > MAX_int32 || RequiredAtlasY > MAX_int32
	    || AtlasResolution.X != RequiredAtlasX || AtlasResolution.Y != RequiredAtlasY)
	{
		OutError = FText::Format(NSLOCTEXT("VolumeSampler", "AtlasSizeMismatch",
				"Texture atlas is {0}x{1}, but VolumeResolution requires {2}x{3}."),
			FText::AsNumber(AtlasResolution.X), FText::AsNumber(AtlasResolution.Y),
			FText::AsNumber(RequiredAtlasX), FText::AsNumber(RequiredAtlasY));
		return false;
	}

	CachedResolution = VolumeResolution;
	CachedSignedDistances.SetNumUninitialized(static_cast<int32>(VoxelCount));
	for (int32 Z = 0; Z < VolumeResolution.Z; ++Z)
		for (int32 Y = 0; Y < VolumeResolution.Y; ++Y)
			for (int32 X = 0; X < VolumeResolution.X; ++X)
			{
				const int32 AtlasX = (Z % TileColumns) * VolumeResolution.X + X;
				const int32 AtlasY = (Z / TileColumns) * VolumeResolution.Y + Y;
				CachedSignedDistances[VolumeResolution.LinearIndex(X, Y, Z)] =
					AtlasValues[AtlasX + AtlasY * AtlasResolution.X];
			}
	return true;
}

float UTex2DSDFSampler::SampleNormalized(const FVector& UVW) const
{
	return SampleCachedTexture(UVW);
}
