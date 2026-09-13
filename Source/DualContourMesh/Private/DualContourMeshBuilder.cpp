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
	int32 PatchIndex = INDEX_NONE;
	const FVector& Position() const { return PatchIndex == INDEX_NONE ? Cell->Center : Cell->Patches[PatchIndex].Center; }
	const FVector& Normal() const { return PatchIndex == INDEX_NONE ? Cell->Normal : Cell->Patches[PatchIndex].Normal; }

	FIntVector VertexKey() const
	{
		// At most twelve edge components; keep distinct sheets out of each
		// other's normal and material caches, including division halo faces.
		return FIntVector(Coord.X * 13 + PatchIndex + 1, Coord.Y, Coord.Z);
	}
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

	void GenerateQuadsForCell(int32 CellX, int32 CellY, int32 CellZ, bool bEmit = true)
	{
		const FIntVector& CellCounts = DualContour.CellCount;
		const auto GetCell = [this, &CellCounts](int32 QueryCellX, int32 QueryCellY, int32 QueryCellZ,
			int32 Axis, const FIntVector& EdgeStart) -> FDualContourCellRef
		{
			if (!DualContourUtils::IsValidCoordinate(CellCounts, QueryCellX, QueryCellY, QueryCellZ))
				return {};
			FDualContourCellRef Ref{DualContour.GetCell(QueryCellX, QueryCellY, QueryCellZ), FIntVector(QueryCellX, QueryCellY, QueryCellZ)};
			if (Ref.Cell && !Ref.Cell->Patches.IsEmpty())
			{
				const uint16 EdgeMask = 1u << DualContourUtils::CellEdgeIndex(Axis, EdgeStart - Ref.Coord);
				for (int32 I = 0; I < Ref.Cell->Patches.Num(); ++I)
					if (Ref.Cell->Patches[I].EdgeMask & EdgeMask)
					{
						Ref.PatchIndex = I;
						break;
					}
			}
			return Ref;
		};

		// Reversed winding, (0,2,1) + (0,3,2), makes faces visible from the outward side in UE.
		const auto AddQuad = [this, bEmit](FDualContourCellRef Cell0, FVector2f UV0, FDualContourCellRef Cell1, FVector2f UV1,
			FDualContourCellRef Cell2, FVector2f UV2, FDualContourCellRef Cell3, FVector2f UV3)
		{
			if (!Cell0.Cell || !Cell1.Cell || !Cell2.Cell || !Cell3.Cell)
				return;
			const TStaticArray<FDualContourCellRef, 4> Cells{Cell0, Cell1, Cell2, Cell3};
			// One area-weighted normal per quad avoids weighting a corner twice
			// merely because it lies on the triangulation diagonal.
			FVector AreaNormal = FVector::CrossProduct(Cell2.Position() - Cell0.Position(),
				                     Cell1.Position() - Cell0.Position())
			                     + FVector::CrossProduct(Cell3.Position() - Cell0.Position(), Cell2.Position() - Cell0.Position());
			const FVector Reference = Cell0.Normal() + Cell1.Normal() + Cell2.Normal() + Cell3.Normal();
			if (FVector::DotProduct(AreaNormal, Reference) < 0.0)
				AreaNormal *= -1.0;
			if (!AreaNormal.IsNearlyZero())
				for (const FDualContourCellRef& Cell : Cells)
					IncidentQuadNormals.FindOrAdd(Cell.VertexKey()).Add(AreaNormal);
			if (bEmit)
			{
				OutputQuadNormals.Add(AreaNormal.GetSafeNormal());
				for (const FDualContourCellRef& Cell : Cells)
					OutputCellCoords.Add(Cell.VertexKey());
			}
			if (!bEmit)
				return;
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
			MeshData.Positions.Append({Cell0.Position(), Cell1.Position(), Cell2.Position(), Cell3.Position()});
			MeshData.Normals.Append({Cell0.Normal(), Cell1.Normal(), Cell2.Normal(), Cell3.Normal()});
			if (DualContour.UVMode == EDualContourUVMode::WorldAlignedBox)
			{
				const FVector QuadNormal = (Cell0.Normal() + Cell1.Normal() + Cell2.Normal() + Cell3.Normal()).GetSafeNormal();
				const float WorldSize = FMath::Max(DualContour.UVWorldSize, 1.0f);
				MeshData.UVs.Append({ProjectBoxUV(Cell0.Position(), QuadNormal, WorldSize),
				                     ProjectBoxUV(Cell1.Position(), QuadNormal, WorldSize),
				                     ProjectBoxUV(Cell2.Position(), QuadNormal, WorldSize),
				                     ProjectBoxUV(Cell3.Position(), QuadNormal, WorldSize)});
			}
			else
			{
				MeshData.UVs.Append({UV0, UV1, UV2, UV3});
			}
			// QEF quads need not be convex or planar. A fixed diagonal can turn
			// a concave corner into overlapping triangles with opposite normals.
			if (DualContourUtils::UseAlternateQuadDiagonal(Cell0.Position(), Cell1.Position(),
				Cell2.Position(), Cell3.Position()))
				MeshData.Indices.Append({BaseVertexIndex, BaseVertexIndex + 3, BaseVertexIndex + 1,
				                         BaseVertexIndex + 1, BaseVertexIndex + 3, BaseVertexIndex + 2});
			else
				MeshData.Indices.Append({BaseVertexIndex, BaseVertexIndex + 2, BaseVertexIndex + 1,
				                         BaseVertexIndex, BaseVertexIndex + 3, BaseVertexIndex + 2});
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
				const FDualContourCellRef C00 = GetCell(CellX, CellY, CellZ, 0, FIntVector(CellX, CellY + 1, CellZ + 1));
				const FDualContourCellRef C10 = GetCell(CellX, CellY + 1, CellZ, 0, FIntVector(CellX, CellY + 1, CellZ + 1));
				const FDualContourCellRef C11 = GetCell(CellX, CellY + 1, CellZ + 1, 0, FIntVector(CellX, CellY + 1, CellZ + 1));
				const FDualContourCellRef C01 = GetCell(CellX, CellY, CellZ + 1, 0, FIntVector(CellX, CellY + 1, CellZ + 1));
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
				const FDualContourCellRef C00 = GetCell(CellX, CellY, CellZ, 1, FIntVector(CellX + 1, CellY, CellZ + 1));
				const FDualContourCellRef C10 = GetCell(CellX + 1, CellY, CellZ, 1, FIntVector(CellX + 1, CellY, CellZ + 1));
				const FDualContourCellRef C11 = GetCell(CellX + 1, CellY, CellZ + 1, 1, FIntVector(CellX + 1, CellY, CellZ + 1));
				const FDualContourCellRef C01 = GetCell(CellX, CellY, CellZ + 1, 1, FIntVector(CellX + 1, CellY, CellZ + 1));
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
				const FDualContourCellRef C00 = GetCell(CellX, CellY, CellZ, 2, FIntVector(CellX + 1, CellY + 1, CellZ));
				const FDualContourCellRef C10 = GetCell(CellX + 1, CellY, CellZ, 2, FIntVector(CellX + 1, CellY + 1, CellZ));
				const FDualContourCellRef C11 = GetCell(CellX + 1, CellY + 1, CellZ, 2, FIntVector(CellX + 1, CellY + 1, CellZ));
				const FDualContourCellRef C01 = GetCell(CellX, CellY + 1, CellZ, 2, FIntVector(CellX + 1, CellY + 1, CellZ));
				if (DensityA >= GDualContourIsoValue)
					AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
				else
					AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
			}
		}
	}

	int32 GetTruncatedQuadCount() const { return TruncatedQuadCount; }

	void ApplyQEFNormals()
	{
		// Quad corners are already separate render vertices. Smooth only with faces
		// within 45 degrees of this face, retaining distinct normals across a crease.
		constexpr double SmoothCosine = 0.7071067811865476;
		for (int32 Index = 0; Index < MeshData.Normals.Num(); ++Index)
		{
			const FVector Reference = OutputQuadNormals[Index / 4];
			if (Reference.IsNearlyZero())
				continue;
			FVector Sum = FVector::ZeroVector;
			if (const TArray<FVector>* Incident = IncidentQuadNormals.Find(OutputCellCoords[Index]))
				for (const FVector& AreaNormal : *Incident)
					if (FVector::DotProduct(Reference, AreaNormal.GetSafeNormal()) >= SmoothCosine)
						Sum += AreaNormal;
			MeshData.Normals[Index] = Sum.IsNearlyZero() ? Reference : Sum.GetSafeNormal();
		}
	}

private:
	FDualContourMaterialBlend EvaluateCellMaterialBlend(const FDualContourCellRef& CellRef)
	{
		if (const FDualContourMaterialBlend* Cached = CellMaterialCache.Find(CellRef.VertexKey()))
			return *Cached;
		FDualContourMaterialBlend Result;
		TStaticArray<float, 256> Scores(InPlace, 0.0f);
		const FVector CellMin = FVector(CellRef.Coord) * DualContour.CellSize;
		const FVector UnclampedLocal = (CellRef.Position() - CellMin) / FMath::Max(DualContour.CellSize, UE_SMALL_NUMBER);
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
		CellMaterialCache.Add(CellRef.VertexKey(), Result);
		return Result;
	}

	const UDualContour& DualContour;
	FDualContourMeshData& MeshData;
	TMap<FIntVector, FDualContourMaterialBlend> CellMaterialCache;
	TMap<FIntVector, TArray<FVector>> IncidentQuadNormals;
	TArray<FVector> OutputQuadNormals;
	TArray<FIntVector> OutputCellCoords;
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
	// Include every face incident to emitted vertices even on division boundaries.
	// Halo faces contribute normals only, never triangles/materials/bounds.
	constexpr int32 Halo = 1;
	for (int32 CellZ = FMath::Max(0, CellRangeMin.Z - Halo); CellZ < FMath::Min(DualContour.CellCount.Z, CellRangeMax.Z + Halo); ++CellZ)
		for (int32 CellY = FMath::Max(0, CellRangeMin.Y - Halo); CellY < FMath::Min(DualContour.CellCount.Y, CellRangeMax.Y + Halo); ++CellY)
			for (int32 CellX = FMath::Max(0, CellRangeMin.X - Halo); CellX < FMath::Min(DualContour.CellCount.X, CellRangeMax.X + Halo); ++CellX)
				Context.GenerateQuadsForCell(CellX, CellY, CellZ,
					CellX >= CellRangeMin.X && CellX < CellRangeMax.X && CellY >= CellRangeMin.Y && CellY < CellRangeMax.Y
					&& CellZ >= CellRangeMin.Z && CellZ < CellRangeMax.Z);
	if (Context.GetTruncatedQuadCount() > 0)
	{
		UE_LOG(LogDualContourMeshBuilder, Warning,
			TEXT("%d dual-contour quads contained more than four weighted material IDs; lower-weight layers were truncated."),
			Context.GetTruncatedQuadCount());
	}

	Context.ApplyQEFNormals();
	// Bounds include every emitted vertex, including centers read from the positive-axis neighbor ring.
	for (const FVector& Position : OutMeshData.Positions)
		OutMeshData.LocalBounds += Position;
	if (!OutMeshData.LocalBounds.IsValid)
		OutMeshData.LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
}
