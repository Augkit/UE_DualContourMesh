#include "DualContourMeshActor.h"

static const int32 GEdgeCorners[12][2][3] = {
	// Along X
	{{0, 0, 0}, {1, 0, 0}},
    {{0, 1, 0}, {1, 1, 0}},
    {{0, 0, 1}, {1, 0, 1}},
    {{0, 1, 1}, {1, 1, 1}},
	// Along Y
	{{0, 0, 0}, {0, 1, 0}},
    {{1, 0, 0}, {1, 1, 0}},
    {{0, 0, 1}, {0, 1, 1}},
    {{1, 0, 1}, {1, 1, 1}},
	// Along Z
	{{0, 0, 0}, {0, 0, 1}},
    {{1, 0, 0}, {1, 0, 1}},
    {{0, 1, 0}, {0, 1, 1}},
    {{1, 1, 0}, {1, 1, 1}}
};

static bool Solve3x3(const double M[3][3], const double b[3], double x[3])
{
	double C00 = M[1][1] * M[2][2] - M[1][2] * M[2][1];
	double C01 = -(M[1][0] * M[2][2] - M[1][2] * M[2][0]);
	double C02 = M[1][0] * M[2][1] - M[1][1] * M[2][0];
	double det = M[0][0] * C00 + M[0][1] * C01 + M[0][2] * C02;
	if (FMath::Abs(det) < 1e-10)
		return false;

	double C10 = -(M[0][1] * M[2][2] - M[0][2] * M[2][1]);
	double C11 = M[0][0] * M[2][2] - M[0][2] * M[2][0];
	double C12 = -(M[0][0] * M[2][1] - M[0][1] * M[2][0]);
	double C20 = M[0][1] * M[1][2] - M[0][2] * M[1][1];
	double C21 = -(M[0][0] * M[1][2] - M[0][2] * M[1][0]);
	double C22 = M[0][0] * M[1][1] - M[0][1] * M[1][0];

	double invDet = 1.0 / det;
	x[0] = invDet * (C00 * b[0] + C10 * b[1] + C20 * b[2]);
	x[1] = invDet * (C01 * b[0] + C11 * b[1] + C21 * b[2]);
	x[2] = invDet * (C02 * b[0] + C12 * b[1] + C22 * b[2]);
	return true;
}

int32 ADualContourMeshActor::SampleIndex(int32 X, int32 Y, int32 Z) const
{
	FVectorInt D = GetSampleDims();
	return X + Y * D.X + Z * D.X * D.Y;
}

int32 ADualContourMeshActor::CellIndex(int32 X, int32 Y, int32 Z) const
{
	return X + Y * CellCount.X + Z * CellCount.X * CellCount.Y;
}

uint8 ADualContourMeshActor::GetSample(int32 X, int32 Y, int32 Z) const
{
	FVectorInt D = GetSampleDims();
	if (X < 0 || X >= D.X || Y < 0 || Y >= D.Y || Z < 0 || Z >= D.Z)
		return 0;
	return SamplePointGrid[SampleIndex(X, Y, Z)];
}

float ADualContourMeshActor::TrilinearSample(FVector GridPos) const
{
	FVectorInt D = GetSampleDims();
	float gx = FMath::Clamp(GridPos.X, 0., (double)(D.X - 1));
	float gy = FMath::Clamp(GridPos.Y, 0., (double)(D.Y - 1));
	float gz = FMath::Clamp(GridPos.Z, 0., (double)(D.Z - 1));

	int32 x0 = FMath::Clamp(FMath::FloorToInt(gx), 0, D.X - 2);
	int32 y0 = FMath::Clamp(FMath::FloorToInt(gy), 0, D.Y - 2);
	int32 z0 = FMath::Clamp(FMath::FloorToInt(gz), 0, D.Z - 2);
	int32 x1 = x0 + 1, y1 = y0 + 1, z1 = z0 + 1;

	float tx = gx - x0, ty = gy - y0, tz = gz - z0;

	float d000 = (float)SamplePointGrid[SampleIndex(x0, y0, z0)];
	float d100 = (float)SamplePointGrid[SampleIndex(x1, y0, z0)];
	float d010 = (float)SamplePointGrid[SampleIndex(x0, y1, z0)];
	float d110 = (float)SamplePointGrid[SampleIndex(x1, y1, z0)];
	float d001 = (float)SamplePointGrid[SampleIndex(x0, y0, z1)];
	float d101 = (float)SamplePointGrid[SampleIndex(x1, y0, z1)];
	float d011 = (float)SamplePointGrid[SampleIndex(x0, y1, z1)];
	float d111 = (float)SamplePointGrid[SampleIndex(x1, y1, z1)];

	return FMath::Lerp(FMath::Lerp(FMath::Lerp(d000, d100, tx), FMath::Lerp(d010, d110, tx), ty),
		FMath::Lerp(FMath::Lerp(d001, d101, tx), FMath::Lerp(d011, d111, tx), ty), tz);
}

FVector ADualContourMeshActor::ComputeGradient(FVector GridPos) const
{
	const float h = 0.5f;
	float gx = TrilinearSample(GridPos + FVector(h, 0, 0)) - TrilinearSample(GridPos - FVector(h, 0, 0));
	float gy = TrilinearSample(GridPos + FVector(0, h, 0)) - TrilinearSample(GridPos - FVector(0, h, 0));
	float gz = TrilinearSample(GridPos + FVector(0, 0, h)) - TrilinearSample(GridPos - FVector(0, 0, h));
	return FVector(gx, gy, gz);
}

void ADualContourMeshActor::FillSphereDensity()
{
	FVectorInt D = GetSampleDims();
	SamplePointGrid.SetNumUninitialized(D.Volume());

	for (int32 sz = 0; sz < D.Z; sz++)
		for (int32 sy = 0; sy < D.Y; sy++)
			for (int32 sx = 0; sx < D.X; sx++)
			{
				FVector WorldPos = GetSampleWorldPos(sx, sy, sz);
				float SignedDist = SphereRadius - (float)FVector::Dist(WorldPos, SphereCenter);
				float d = FMath::Clamp(127.5f + SignedDist * 127.5f / CellSize, 0.f, 255.f);
				SamplePointGrid[SampleIndex(sx, sy, sz)] = (uint8)FMath::RoundToInt(d);
			}
}

void ADualContourMeshActor::BuildCells()
{
	const int32 IsoValue = 127;
	const double Lambda = 0.1;

	DualContourGrid.SetNum(CellCount.Volume());

	for (int32 cz = 0; cz < CellCount.Z; cz++)
		for (int32 cy = 0; cy < CellCount.Y; cy++)
			for (int32 cx = 0; cx < CellCount.X; cx++)
			{
				FDualContourCell& Cell = DualContourGrid[CellIndex(cx, cy, cz)];
				Cell.bActive = false;
				Cell.Normal = FVector::UpVector;

				FVector CellMin = FVector((double)cx, (double)cy, (double)cz) * (double)CellSize;
				FVector CellMax = FVector((double)cx + 1., (double)cy + 1., (double)cz + 1.) * (double)CellSize;
				FVector CellCenter = (CellMin + CellMax) * 0.5;
				Cell.Center = CellCenter;

				bool bHasInside = false, bHasOutside = false;
				for (int32 dz = 0; dz <= 1; dz++)
					for (int32 dy = 0; dy <= 1; dy++)
						for (int32 dx = 0; dx <= 1; dx++)
						{
							uint8 d = GetSample(cx + dx, cy + dy, cz + dz);
							if (d > IsoValue)
								bHasInside = true;
							else if (d < IsoValue)
								bHasOutside = true;
						}
				Cell.bActive = bHasInside && bHasOutside;
				if (!Cell.bActive)
					continue;

				double AtA[3][3] = {};
				double Atb[3] = {};
				FVector AccumNormal = FVector::ZeroVector;
				int32 NumIntersections = 0;

				for (int32 EdgeIdx = 0; EdgeIdx < 12; EdgeIdx++)
				{
					const int32* OffA = GEdgeCorners[EdgeIdx][0];
					const int32* OffB = GEdgeCorners[EdgeIdx][1];

					int32 sxA = cx + OffA[0], syA = cy + OffA[1], szA = cz + OffA[2];
					int32 sxB = cx + OffB[0], syB = cy + OffB[1], szB = cz + OffB[2];
					int32 dA = GetSample(sxA, syA, szA);
					int32 dB = GetSample(sxB, syB, szB);

					if (dA == IsoValue || dB == IsoValue)
						continue;
					if ((dA < IsoValue) == (dB < IsoValue))
						continue;

					float t = (float)(IsoValue - dA) / (float)(dB - dA);

					FVector GridPosA((double)sxA, (double)syA, (double)szA);
					FVector GridPosB((double)sxB, (double)syB, (double)szB);
					FVector GridPos = GridPosA + (double)t * (GridPosB - GridPosA);
					FVector WorldPos = GridPos * (double)CellSize;

					FVector Normal = (-ComputeGradient(GridPos)).GetSafeNormal();
					if (Normal.IsNearlyZero())
						continue;

					double nx = Normal.X, ny = Normal.Y, nz = Normal.Z;
					double dp = nx * WorldPos.X + ny * WorldPos.Y + nz * WorldPos.Z;

					AtA[0][0] += nx * nx;
					AtA[0][1] += nx * ny;
					AtA[0][2] += nx * nz;
					AtA[1][0] += ny * nx;
					AtA[1][1] += ny * ny;
					AtA[1][2] += ny * nz;
					AtA[2][0] += nz * nx;
					AtA[2][1] += nz * ny;
					AtA[2][2] += nz * nz;
					Atb[0] += nx * dp;
					Atb[1] += ny * dp;
					Atb[2] += nz * dp;

					AccumNormal += Normal;
					NumIntersections++;
				}

				if (NumIntersections == 0)
					continue;

				AtA[0][0] += Lambda;
				AtA[1][1] += Lambda;
				AtA[2][2] += Lambda;
				Atb[0] += Lambda * CellCenter.X;
				Atb[1] += Lambda * CellCenter.Y;
				Atb[2] += Lambda * CellCenter.Z;

				double Solved[3];
				if (Solve3x3(AtA, Atb, Solved))
				{
					Cell.Center.X = FMath::Clamp((float)Solved[0], (float)CellMin.X, (float)CellMax.X);
					Cell.Center.Y = FMath::Clamp((float)Solved[1], (float)CellMin.Y, (float)CellMax.Y);
					Cell.Center.Z = FMath::Clamp((float)Solved[2], (float)CellMin.Z, (float)CellMax.Z);
				}

				Cell.Normal = AccumNormal.GetSafeNormal();
				if (Cell.Normal.IsNearlyZero())
					Cell.Normal = FVector::UpVector;
			}
}

ADualContourMeshActor::ADualContourMeshActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADualContourMeshActor::RebuildMesh()
{
	FillSphereDensity();
	BuildCells();

	for (TObjectPtr<UDualContourMeshComponent>& Comp : MeshComponents)
		if (Comp)
			Comp->DestroyComponent();
	MeshComponents.Reset();

	const FVectorInt& Div = Divisions;
	const FVectorInt& CC = CellCount;

	for (int32 dz = 0; dz < Div.Z; dz++)
		for (int32 dy = 0; dy < Div.Y; dy++)
			for (int32 dx = 0; dx < Div.X; dx++)
			{
				FVectorInt CellMin(dx * CC.X / Div.X, dy * CC.Y / Div.Y, dz * CC.Z / Div.Z);
				FVectorInt CellMax((dx + 1) * CC.X / Div.X, (dy + 1) * CC.Y / Div.Y, (dz + 1) * CC.Z / Div.Z);

				bool bHasActive = false;
				for (int32 cz = CellMin.Z; cz < CellMax.Z && !bHasActive; cz++)
					for (int32 cy = CellMin.Y; cy < CellMax.Y && !bHasActive; cy++)
						for (int32 cx = CellMin.X; cx < CellMax.X && !bHasActive; cx++)
							bHasActive = DualContourGrid[CellCount.LinearIndex(cx, cy, cz)].bActive;
				if (!bHasActive)
					continue;

				UDualContourMeshComponent* NewComp = NewObject<UDualContourMeshComponent>(this, NAME_None, RF_Transactional);
				NewComp->CellRangeMin = CellMin;
				NewComp->CellRangeMax = CellMax;
				NewComp->SetMaterial(0, MeshMaterial);
				NewComp->RegisterComponent();
				NewComp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
				NewComp->BuildAndRefreshMesh();
				MeshComponents.Add(NewComp);
			}
}
