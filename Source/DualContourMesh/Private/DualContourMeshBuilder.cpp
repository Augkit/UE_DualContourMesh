#include "DualContourMeshBuilder.h"

#include "DualContour.h"
#include "DualContourUtils.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
class FDualContourMeshBuildContext
{
public:
	FDualContourMeshBuildContext(const UDualContour& InDualContour, FDualContourMeshData& InMeshData)
		: DualContour(InDualContour), MeshData(InMeshData) {}

	void GenerateQuadsForCell(int32 CellX, int32 CellY, int32 CellZ)
	{
		const FIntVector& CellCounts = DualContour.CellCount;
		const auto GetCell = [this, &CellCounts](int32 QueryCellX, int32 QueryCellY, int32 QueryCellZ)
			-> const FDualContourCell*
		{
			if (!DualContourUtils::IsValidCoordinate(CellCounts, QueryCellX, QueryCellY, QueryCellZ))
				return nullptr;
			return DualContour.GetCell(QueryCellX, QueryCellY, QueryCellZ);
		};

		// Reversed winding, (0,2,1) + (0,3,2), makes faces visible from the outward side in UE.
		const auto AddQuad = [this](const FDualContourCell* Cell0, FVector2f UV0,
			const FDualContourCell* Cell1, FVector2f UV1, const FDualContourCell* Cell2, FVector2f UV2,
			const FDualContourCell* Cell3, FVector2f UV3)
		{
			if (!Cell0 || !Cell1 || !Cell2 || !Cell3)
				return;

			const uint32 BaseVertexIndex = static_cast<uint32>(MeshData.Positions.Num());
			MeshData.Positions.Append({Cell0->Center, Cell1->Center, Cell2->Center, Cell3->Center});
			MeshData.Normals.Append({Cell0->Normal, Cell1->Normal, Cell2->Normal, Cell3->Normal});
			MeshData.UVs.Append({UV0, UV1, UV2, UV3});
			MeshData.Indices.Append({BaseVertexIndex, BaseVertexIndex + 2, BaseVertexIndex + 1,
			                         BaseVertexIndex, BaseVertexIndex + 3, BaseVertexIndex + 2});
		};

		// X-axis edge: the four adjacent cells lie in the Y-Z plane.
		if (CellY + 1 < CellCounts.Y && CellZ + 1 < CellCounts.Z)
		{
			const uint16 DensityA = DualContour.GetDensity(CellX, CellY + 1, CellZ + 1);
			const uint16 DensityB = DualContour.GetDensity(CellX + 1, CellY + 1, CellZ + 1);
			if ((DensityA < GDualContourIsoValue) != (DensityB < GDualContourIsoValue))
			{
				const FDualContourCell* C00 = GetCell(CellX, CellY, CellZ);
				const FDualContourCell* C10 = GetCell(CellX, CellY + 1, CellZ);
				const FDualContourCell* C11 = GetCell(CellX, CellY + 1, CellZ + 1);
				const FDualContourCell* C01 = GetCell(CellX, CellY, CellZ + 1);
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
				const FDualContourCell* C00 = GetCell(CellX, CellY, CellZ);
				const FDualContourCell* C10 = GetCell(CellX + 1, CellY, CellZ);
				const FDualContourCell* C11 = GetCell(CellX + 1, CellY, CellZ + 1);
				const FDualContourCell* C01 = GetCell(CellX, CellY, CellZ + 1);
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
				const FDualContourCell* C00 = GetCell(CellX, CellY, CellZ);
				const FDualContourCell* C10 = GetCell(CellX + 1, CellY, CellZ);
				const FDualContourCell* C11 = GetCell(CellX + 1, CellY + 1, CellZ);
				const FDualContourCell* C01 = GetCell(CellX, CellY + 1, CellZ);
				if (DensityA >= GDualContourIsoValue)
					AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
				else
					AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
			}
		}
	}

private:
	const UDualContour& DualContour;
	FDualContourMeshData& MeshData;
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
