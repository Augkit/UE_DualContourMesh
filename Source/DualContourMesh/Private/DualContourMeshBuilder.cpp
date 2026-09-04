#include "DualContourMeshBuilder.h"

#include "DualContour.h"
#include "DualContourUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogDualContourMeshBuilder, Log, All);

namespace
{
FVector2f ProjectBoxUV(const FVector& Position, const FVector& ProjectionNormal, float WorldSize)
{
	const float SafeWorldSize = FMath::Max(WorldSize, 1.0f);
	const FVector AbsNormal = ProjectionNormal.GetAbs();

	// Keep the projection basis stable across a quad and across neighbouring
	// mesh divisions. The local position is intentionally not rebased per chunk.
	if (AbsNormal.X >= AbsNormal.Y && AbsNormal.X >= AbsNormal.Z)
		return FVector2f(static_cast<float>(Position.Y / SafeWorldSize), static_cast<float>(Position.Z / SafeWorldSize));
	if (AbsNormal.Y >= AbsNormal.Z)
		return FVector2f(static_cast<float>(Position.X / SafeWorldSize), static_cast<float>(Position.Z / SafeWorldSize));
	return FVector2f(static_cast<float>(Position.X / SafeWorldSize), static_cast<float>(Position.Y / SafeWorldSize));
}

struct FDualContourCellRef
{
	const FDualContourCell* Cell = nullptr;
	FIntVector Coord = FIntVector::ZeroValue;
};

FColor PackChannels(const TStaticArray<uint8, 4>& Values)
{
	return FColor(Values[0], Values[1], Values[2], Values[3]);
}

FColor PackNormalizedWeights(const TStaticArray<float, 4>& Weights)
{
	TStaticArray<uint8, 4> Packed{0, 0, 0, 0};
	int32 Sum = 0;
	int32 LargestIndex = 0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Packed[Index] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Weights[Index] * 255.0f), 0, 255));
		Sum += Packed[Index];
		if (Weights[Index] > Weights[LargestIndex])
			LargestIndex = Index;
	}
	Packed[LargestIndex] = static_cast<uint8>(FMath::Clamp(static_cast<int32>(Packed[LargestIndex]) + 255 - Sum, 0, 255));
	return PackChannels(Packed);
}

class FDualContourMeshBuildContext
{
public:
	FDualContourMeshBuildContext(const UDualContour& InDualContour, FDualContourMeshData& InMeshData)
		: DualContour(InDualContour), MeshData(InMeshData) {}

	void GenerateQuadsForCell(int32 CellX, int32 CellY, int32 CellZ)
	{
		const FIntVector& CellCounts = DualContour.CellCount;
		const auto GetCell = [this, &CellCounts](int32 QueryCellX, int32 QueryCellY, int32 QueryCellZ) -> FDualContourCellRef
		{
			if (!DualContourUtils::IsValidCoordinate(CellCounts, QueryCellX, QueryCellY, QueryCellZ))
				return {};
			return {DualContour.GetCell(QueryCellX, QueryCellY, QueryCellZ), FIntVector(QueryCellX, QueryCellY, QueryCellZ)};
		};

		// Reversed winding, (0,2,1) + (0,3,2), makes faces visible from the outward side in UE.
		const auto AddQuad = [this](FDualContourCellRef Cell0, FVector2f UV0, FDualContourCellRef Cell1, FVector2f UV1,
			FDualContourCellRef Cell2, FVector2f UV2, FDualContourCellRef Cell3, FVector2f UV3)
		{
			if (!Cell0.Cell || !Cell1.Cell || !Cell2.Cell || !Cell3.Cell)
				return;
			const TStaticArray<FDualContourCellRef, 4> Cells{Cell0, Cell1, Cell2, Cell3};
			TStaticArray<FDualContourMaterialBlend, 4> Blends;
			for (int32 Index = 0; Index < 4; ++Index)
				Blends[Index] = EvaluateCellMaterialBlend(Cells[Index]);

			TSet<uint8> RequiredIds;
			TStaticArray<float, 256> Scores(InPlace, 0.0f);
			for (const FDualContourMaterialBlend& Blend : Blends)
			{
				RequiredIds.Add(Blend.Ids[0]);
				for (int32 Layer = 0; Layer < 4; ++Layer)
					Scores[Blend.Ids[Layer]] += Blend.Weights[Layer];
			}
			TArray<uint8> Palette = RequiredIds.Array();
			TArray<uint8> Candidates;
			for (int32 Id = 0; Id < 256; ++Id)
				if (Scores[Id] > 0.0f && !RequiredIds.Contains(static_cast<uint8>(Id)))
					Candidates.Add(static_cast<uint8>(Id));
			Candidates.Sort([&Scores](uint8 A, uint8 B)
			{
				return Scores[A] == Scores[B] ? A < B : Scores[A] > Scores[B];
			});
			for (uint8 Candidate : Candidates)
				if (Palette.Num() < 4)
					Palette.Add(Candidate);
			if (RequiredIds.Num() + Candidates.Num() > 4)
				++TruncatedQuadCount;
			while (Palette.Num() < 4)
				Palette.Add(0);
			Palette.Sort();
			TStaticArray<uint8, 4> PaletteIds{Palette[0], Palette[1], Palette[2], Palette[3]};
			const FColor PackedIds = PackChannels(PaletteIds);
			for (const FDualContourMaterialBlend& Blend : Blends)
			{
				TStaticArray<float, 4> Remapped{0.0f, 0.0f, 0.0f, 0.0f};
				for (int32 SourceLayer = 0; SourceLayer < 4; ++SourceLayer)
					for (int32 PaletteLayer = 0; PaletteLayer < 4; ++PaletteLayer)
						if (Blend.Ids[SourceLayer] == PaletteIds[PaletteLayer])
						{
							Remapped[PaletteLayer] += Blend.Weights[SourceLayer];
							break;
						}
				float Sum = Remapped[0] + Remapped[1] + Remapped[2] + Remapped[3];
				if (Sum <= UE_SMALL_NUMBER)
					Remapped[0] = Sum = 1.0f;
				for (float& Weight : Remapped)
					Weight /= Sum;
				MeshData.MaterialWeights.Add(PackNormalizedWeights(Remapped));
				MeshData.MaterialIds.Add(PackedIds);
			}

			const uint32 BaseVertexIndex = static_cast<uint32>(MeshData.Positions.Num());
			MeshData.Positions.Append({Cell0.Cell->Center, Cell1.Cell->Center, Cell2.Cell->Center, Cell3.Cell->Center});
			MeshData.Normals.Append({Cell0.Cell->Normal, Cell1.Cell->Normal, Cell2.Cell->Normal, Cell3.Cell->Normal});
			if (DualContour.UVMode == EDualContourUVMode::WorldAlignedBox)
			{
				const FVector QuadNormal = (Cell0.Cell->Normal + Cell1.Cell->Normal + Cell2.Cell->Normal + Cell3.Cell->Normal).GetSafeNormal();
				const float WorldSize = FMath::Max(DualContour.UVWorldSize, 1.0f);
				MeshData.UVs.Append({ProjectBoxUV(Cell0.Cell->Center, QuadNormal, WorldSize),
				                     ProjectBoxUV(Cell1.Cell->Center, QuadNormal, WorldSize),
				                     ProjectBoxUV(Cell2.Cell->Center, QuadNormal, WorldSize),
				                     ProjectBoxUV(Cell3.Cell->Center, QuadNormal, WorldSize)});
			}
			else
			{
				MeshData.UVs.Append({UV0, UV1, UV2, UV3});
			}
			MeshData.Indices.Append(
				{BaseVertexIndex, BaseVertexIndex + 2, BaseVertexIndex + 1, BaseVertexIndex, BaseVertexIndex + 3, BaseVertexIndex + 2});
			check(MeshData.Positions.Num() == MeshData.Normals.Num()
				&& MeshData.Positions.Num() == MeshData.UVs.Num()
				&& MeshData.Positions.Num() == MeshData.MaterialWeights.Num()
				&& MeshData.Positions.Num() == MeshData.MaterialIds.Num());
		};

		// X-axis edge: the four adjacent cells lie in the Y-Z plane.
		if (CellY + 1 < CellCounts.Y && CellZ + 1 < CellCounts.Z)
		{
			const uint16 DensityA = DualContour.GetDensity(CellX, CellY + 1, CellZ + 1);
			const uint16 DensityB = DualContour.GetDensity(CellX + 1, CellY + 1, CellZ + 1);
			if ((DensityA < GDualContourIsoValue) != (DensityB < GDualContourIsoValue))
			{
				const FDualContourCellRef C00 = GetCell(CellX, CellY, CellZ);
				const FDualContourCellRef C10 = GetCell(CellX, CellY + 1, CellZ);
				const FDualContourCellRef C11 = GetCell(CellX, CellY + 1, CellZ + 1);
				const FDualContourCellRef C01 = GetCell(CellX, CellY, CellZ + 1);
				if (DensityA >= GDualContourIsoValue)
					AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
				else
					AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
			}
		}

		// Y-axis edge: the four adjacent cells lie in the X-Z plane.
		if (CellX + 1 < CellCounts.X && CellZ + 1 < CellCounts.Z)
		{
			const uint16 DensityA = DualContour.GetDensity(CellX + 1, CellY, CellZ + 1);
			const uint16 DensityB = DualContour.GetDensity(CellX + 1, CellY + 1, CellZ + 1);
			if ((DensityA < GDualContourIsoValue) != (DensityB < GDualContourIsoValue))
			{
				const FDualContourCellRef C00 = GetCell(CellX, CellY, CellZ);
				const FDualContourCellRef C10 = GetCell(CellX + 1, CellY, CellZ);
				const FDualContourCellRef C11 = GetCell(CellX + 1, CellY, CellZ + 1);
				const FDualContourCellRef C01 = GetCell(CellX, CellY, CellZ + 1);
				if (DensityA >= GDualContourIsoValue)
					AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
				else
					AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
			}
		}

		// Z-axis edge: the four adjacent cells lie in the X-Y plane.
		if (CellX + 1 < CellCounts.X && CellY + 1 < CellCounts.Y)
		{
			const uint16 DensityA = DualContour.GetDensity(CellX + 1, CellY + 1, CellZ);
			const uint16 DensityB = DualContour.GetDensity(CellX + 1, CellY + 1, CellZ + 1);
			if ((DensityA < GDualContourIsoValue) != (DensityB < GDualContourIsoValue))
			{
				const FDualContourCellRef C00 = GetCell(CellX, CellY, CellZ);
				const FDualContourCellRef C10 = GetCell(CellX + 1, CellY, CellZ);
				const FDualContourCellRef C11 = GetCell(CellX + 1, CellY + 1, CellZ);
				const FDualContourCellRef C01 = GetCell(CellX, CellY + 1, CellZ);
				if (DensityA >= GDualContourIsoValue)
					AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
				else
					AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
			}
		}
	}

	int32 GetTruncatedQuadCount() const { return TruncatedQuadCount; }

private:
	FDualContourMaterialBlend EvaluateCellMaterialBlend(const FDualContourCellRef& CellRef)
	{
		if (const FDualContourMaterialBlend* Cached = CellMaterialCache.Find(CellRef.Coord))
			return *Cached;
		FDualContourMaterialBlend Result;
		TStaticArray<float, 256> Scores(InPlace, 0.0f);
		const FVector CellMin = FVector(CellRef.Coord) * DualContour.CellSize;
		const FVector UnclampedLocal = (CellRef.Cell->Center - CellMin) / FMath::Max(DualContour.CellSize, UE_SMALL_NUMBER);
		const FVector Local(FMath::Clamp(UnclampedLocal.X, 0.0, 1.0), FMath::Clamp(UnclampedLocal.Y, 0.0, 1.0),
			FMath::Clamp(UnclampedLocal.Z, 0.0, 1.0));
		for (int32 Z = 0; Z <= 1; ++Z)
			for (int32 Y = 0; Y <= 1; ++Y)
				for (int32 X = 0; X <= 1; ++X)
				{
					const int32 SampleX = CellRef.Coord.X + X;
					const int32 SampleY = CellRef.Coord.Y + Y;
					const int32 SampleZ = CellRef.Coord.Z + Z;
					if (DualContour.GetDensity(SampleX, SampleY, SampleZ) < GDualContourIsoValue)
						continue;
					const float Weight = (X ? Local.X : 1.0 - Local.X) * (Y ? Local.Y : 1.0 - Local.Y)
					                     * (Z ? Local.Z : 1.0 - Local.Z);
					Scores[DualContour.GetMaterialId(SampleX, SampleY, SampleZ)] += Weight;
				}
		TArray<uint8> RankedIds;
		for (int32 Id = 0; Id < 256; ++Id)
			if (Scores[Id] > 0.0f)
				RankedIds.Add(static_cast<uint8>(Id));
		RankedIds.Sort([&Scores](uint8 A, uint8 B)
		{
			return Scores[A] == Scores[B] ? A < B : Scores[A] > Scores[B];
		});
		const int32 LayerCount = FMath::Min(RankedIds.Num(), 4);
		float Total = 0.0f;
		for (int32 Layer = 0; Layer < LayerCount; ++Layer)
		{
			Result.Ids[Layer] = RankedIds[Layer];
			Result.Weights[Layer] = Scores[RankedIds[Layer]];
			Total += Result.Weights[Layer];
		}
		if (Total > UE_SMALL_NUMBER)
			for (int32 Layer = 0; Layer < 4; ++Layer)
				Result.Weights[Layer] /= Total;
		CellMaterialCache.Add(CellRef.Coord, Result);
		return Result;
	}

	const UDualContour& DualContour;
	FDualContourMeshData& MeshData;
	TMap<FIntVector, FDualContourMaterialBlend> CellMaterialCache;
	int32 TruncatedQuadCount = 0;
};
}

void FDualContourMeshBuilder::Build(const UDualContour& DualContour, FIntVector CellRangeMin, FIntVector CellRangeMax,
	FDualContourMeshData& OutMeshData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMeshBuilder_Build);
	OutMeshData.Reset();
	if (!DualContour.HasCurrentGeneratedData())
	{
		OutMeshData.LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
		return;
	}

	FDualContourMeshBuildContext Context(DualContour, OutMeshData);
	for (int32 CellZ = CellRangeMin.Z; CellZ < CellRangeMax.Z; ++CellZ)
		for (int32 CellY = CellRangeMin.Y; CellY < CellRangeMax.Y; ++CellY)
			for (int32 CellX = CellRangeMin.X; CellX < CellRangeMax.X; ++CellX)
				Context.GenerateQuadsForCell(CellX, CellY, CellZ);
	if (Context.GetTruncatedQuadCount() > 0)
	{
		UE_LOG(LogDualContourMeshBuilder, Warning,
			TEXT("%d dual-contour quads contained more than four weighted material IDs; lower-weight layers were truncated."),
			Context.GetTruncatedQuadCount());
	}

	// Weld duplicated quad corners by position and accumulate unnormalized triangle
	// normals so each face contributes in proportion to area. Blending a small amount
	// of this geometric normal into the field normal removes triangulation-aligned
	// shading without hiding the sampled surface shape.
	TMap<FVector, FVector> PositionNormalSums;
	for (int32 TriangleIndex = 0; TriangleIndex + 2 < OutMeshData.Indices.Num(); TriangleIndex += 3)
	{
		const uint32 Index0 = OutMeshData.Indices[TriangleIndex];
		const uint32 Index1 = OutMeshData.Indices[TriangleIndex + 1];
		const uint32 Index2 = OutMeshData.Indices[TriangleIndex + 2];
		FVector FaceNormal = FVector::CrossProduct(
			OutMeshData.Positions[Index1] - OutMeshData.Positions[Index0],
			OutMeshData.Positions[Index2] - OutMeshData.Positions[Index0]);
		if (FaceNormal.IsNearlyZero())
			continue;
		const FVector ReferenceNormal = (OutMeshData.Normals[Index0] + OutMeshData.Normals[Index1]
		                                 + OutMeshData.Normals[Index2]).GetSafeNormal();
		if (!ReferenceNormal.IsNearlyZero() && FVector::DotProduct(FaceNormal, ReferenceNormal) < 0.0)
			FaceNormal *= -1.0;
		PositionNormalSums.FindOrAdd(OutMeshData.Positions[Index0]) += FaceNormal;
		PositionNormalSums.FindOrAdd(OutMeshData.Positions[Index1]) += FaceNormal;
		PositionNormalSums.FindOrAdd(OutMeshData.Positions[Index2]) += FaceNormal;
	}

	constexpr float GeometricNormalBlend = 0.25f;
	for (int32 VertexIndex = 0; VertexIndex < OutMeshData.Positions.Num(); ++VertexIndex)
	{
		const FVector GeometricNormal = PositionNormalSums.FindRef(OutMeshData.Positions[VertexIndex]).GetSafeNormal();
		if (!GeometricNormal.IsNearlyZero())
		{
			OutMeshData.Normals[VertexIndex] =
				FMath::Lerp(OutMeshData.Normals[VertexIndex].GetSafeNormal(), GeometricNormal, GeometricNormalBlend).GetSafeNormal();
		}
	}

	// Bounds include every emitted vertex, including centers read from the positive-axis neighbor ring.
	for (const FVector& Position : OutMeshData.Positions)
		OutMeshData.LocalBounds += Position;
	if (!OutMeshData.LocalBounds.IsValid)
		OutMeshData.LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
}
