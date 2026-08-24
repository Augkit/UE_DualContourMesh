#include "DualContourMeshActor.h"
#include "Engine/CollisionProfile.h"

#if WITH_EDITOR
#include "DrawDebugHelpers.h"

static TAutoConsoleVariable<int32> CVarDrawDualContourCells(TEXT("DualContour.Debug.DrawCells"), 0,
	TEXT("Draw dual contour cell debug boxes (editor only).\n"
		"  0: off\n"
		"  1: active cells only (green)\n"
		"  2: all cells (green=active, red=inactive) -- slow on large grids"),
	ECVF_Default);
#endif

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

static bool Solve3x3(const double Matrix[3][3], const double RightHandSide[3], double Solution[3])
{
	double C00 = Matrix[1][1] * Matrix[2][2] - Matrix[1][2] * Matrix[2][1];
	double C01 = -(Matrix[1][0] * Matrix[2][2] - Matrix[1][2] * Matrix[2][0]);
	double C02 = Matrix[1][0] * Matrix[2][1] - Matrix[1][1] * Matrix[2][0];
	double Determinant = Matrix[0][0] * C00 + Matrix[0][1] * C01 + Matrix[0][2] * C02;
	if (FMath::Abs(Determinant) < 1e-10)
		return false;

	double C10 = -(Matrix[0][1] * Matrix[2][2] - Matrix[0][2] * Matrix[2][1]);
	double C11 = Matrix[0][0] * Matrix[2][2] - Matrix[0][2] * Matrix[2][0];
	double C12 = -(Matrix[0][0] * Matrix[2][1] - Matrix[0][1] * Matrix[2][0]);
	double C20 = Matrix[0][1] * Matrix[1][2] - Matrix[0][2] * Matrix[1][1];
	double C21 = -(Matrix[0][0] * Matrix[1][2] - Matrix[0][2] * Matrix[1][0]);
	double C22 = Matrix[0][0] * Matrix[1][1] - Matrix[0][1] * Matrix[1][0];

	double InverseDeterminant = 1.0 / Determinant;
	Solution[0] = InverseDeterminant * (C00 * RightHandSide[0] + C10 * RightHandSide[1] + C20 * RightHandSide[2]);
	Solution[1] = InverseDeterminant * (C01 * RightHandSide[0] + C11 * RightHandSide[1] + C21 * RightHandSide[2]);
	Solution[2] = InverseDeterminant * (C02 * RightHandSide[0] + C12 * RightHandSide[1] + C22 * RightHandSide[2]);
	return true;
}

int32 ADualContourMeshActor::SampleIndex(int32 SampleX, int32 SampleY, int32 SampleZ) const
{
	FVectorInt SampleDimensions = GetSampleDims();
	return SampleX + SampleY * SampleDimensions.X + SampleZ * SampleDimensions.X * SampleDimensions.Y;
}

int32 ADualContourMeshActor::CellIndex(int32 CellX, int32 CellY, int32 CellZ) const
{
	return CellX + CellY * CellCount.X + CellZ * CellCount.X * CellCount.Y;
}

uint8 ADualContourMeshActor::GetSample(int32 SampleX, int32 SampleY, int32 SampleZ) const
{
	FVectorInt SampleDimensions = GetSampleDims();
	if (SampleX < 0 || SampleX >= SampleDimensions.X || SampleY < 0 || SampleY >= SampleDimensions.Y || SampleZ < 0 || SampleZ >= SampleDimensions.Z)
		return 0;
	return SamplePointGrid[SampleIndex(SampleX, SampleY, SampleZ)];
}

float ADualContourMeshActor::TrilinearSample(FVector GridPos) const
{
	FVectorInt SampleDimensions = GetSampleDims();
	float GridX = FMath::Clamp(GridPos.X, 0., (double)(SampleDimensions.X - 1));
	float GridY = FMath::Clamp(GridPos.Y, 0., (double)(SampleDimensions.Y - 1));
	float GridZ = FMath::Clamp(GridPos.Z, 0., (double)(SampleDimensions.Z - 1));

	int32 LowerX = FMath::Clamp(FMath::FloorToInt(GridX), 0, SampleDimensions.X - 2);
	int32 LowerY = FMath::Clamp(FMath::FloorToInt(GridY), 0, SampleDimensions.Y - 2);
	int32 LowerZ = FMath::Clamp(FMath::FloorToInt(GridZ), 0, SampleDimensions.Z - 2);
	int32 UpperX = LowerX + 1, UpperY = LowerY + 1, UpperZ = LowerZ + 1;

	float BlendX = GridX - LowerX, BlendY = GridY - LowerY, BlendZ = GridZ - LowerZ;

	float Density000 = (float)SamplePointGrid[SampleIndex(LowerX, LowerY, LowerZ)];
	float Density100 = (float)SamplePointGrid[SampleIndex(UpperX, LowerY, LowerZ)];
	float Density010 = (float)SamplePointGrid[SampleIndex(LowerX, UpperY, LowerZ)];
	float Density110 = (float)SamplePointGrid[SampleIndex(UpperX, UpperY, LowerZ)];
	float Density001 = (float)SamplePointGrid[SampleIndex(LowerX, LowerY, UpperZ)];
	float Density101 = (float)SamplePointGrid[SampleIndex(UpperX, LowerY, UpperZ)];
	float Density011 = (float)SamplePointGrid[SampleIndex(LowerX, UpperY, UpperZ)];
	float Density111 = (float)SamplePointGrid[SampleIndex(UpperX, UpperY, UpperZ)];

	return FMath::Lerp(FMath::Lerp(FMath::Lerp(Density000, Density100, BlendX), FMath::Lerp(Density010, Density110, BlendX), BlendY),
		FMath::Lerp(FMath::Lerp(Density001, Density101, BlendX), FMath::Lerp(Density011, Density111, BlendX), BlendY), BlendZ);
}

FVector ADualContourMeshActor::ComputeGradient(FVector GridPos) const
{
	const float GradientStep = 0.5f;
	float GradientX = TrilinearSample(GridPos + FVector(GradientStep, 0, 0)) - TrilinearSample(GridPos - FVector(GradientStep, 0, 0));
	float GradientY = TrilinearSample(GridPos + FVector(0, GradientStep, 0)) - TrilinearSample(GridPos - FVector(0, GradientStep, 0));
	float GradientZ = TrilinearSample(GridPos + FVector(0, 0, GradientStep)) - TrilinearSample(GridPos - FVector(0, 0, GradientStep));
	return FVector(GradientX, GradientY, GradientZ);
}

void ADualContourMeshActor::FillSphereDensity()
{
	FVectorInt SampleDimensions = GetSampleDims();
	SamplePointGrid.SetNumUninitialized(SampleDimensions.Volume());

	for (int32 SampleZ = 0; SampleZ < SampleDimensions.Z; SampleZ++)
		for (int32 SampleY = 0; SampleY < SampleDimensions.Y; SampleY++)
			for (int32 SampleX = 0; SampleX < SampleDimensions.X; SampleX++)
			{
				FVector WorldPosition = GetSampleWorldPos(SampleX, SampleY, SampleZ);
				float SignedDistance = SphereRadius - (float)FVector::Dist(WorldPosition, SphereCenter);
				float DensityValue = FMath::Clamp(127.5f + SignedDistance * 127.5f / CellSize, 0.f, 255.f);
				SamplePointGrid[SampleIndex(SampleX, SampleY, SampleZ)] = (uint8)FMath::RoundToInt(DensityValue);
			}
}

void ADualContourMeshActor::BuildCells()
{
	const int32 IsoValue = 127;
	const double Lambda = 0.1;

	DualContourGrid.SetNum(CellCount.Volume());

	for (int32 CellZ = 0; CellZ < CellCount.Z; CellZ++)
		for (int32 CellY = 0; CellY < CellCount.Y; CellY++)
			for (int32 CellX = 0; CellX < CellCount.X; CellX++)
			{
				FDualContourCell& Cell = DualContourGrid[CellIndex(CellX, CellY, CellZ)];
				Cell.bActive = false;
				Cell.Normal = FVector::UpVector;

				FVector CellMin = FVector((double)CellX, (double)CellY, (double)CellZ) * (double)CellSize;
				FVector CellMax = FVector((double)CellX + 1., (double)CellY + 1., (double)CellZ + 1.) * (double)CellSize;
				FVector CellCenter = (CellMin + CellMax) * 0.5;
				Cell.Center = CellCenter;

				bool bHasInside = false, bHasOutside = false;
				for (int32 CornerOffsetZ = 0; CornerOffsetZ <= 1; CornerOffsetZ++)
					for (int32 CornerOffsetY = 0; CornerOffsetY <= 1; CornerOffsetY++)
						for (int32 CornerOffsetX = 0; CornerOffsetX <= 1; CornerOffsetX++)
						{
							uint8 Density = GetSample(CellX + CornerOffsetX, CellY + CornerOffsetY, CellZ + CornerOffsetZ);
							if (Density > IsoValue)
								bHasInside = true;
							else if (Density < IsoValue)
								bHasOutside = true;
						}
				Cell.bActive = bHasInside && bHasOutside;
				if (!Cell.bActive)
					continue;

				double NormalEquationMatrix[3][3] = {};
				double NormalEquationVector[3] = {};
				FVector AccumNormal = FVector::ZeroVector;
				int32 NumIntersections = 0;

				for (int32 EdgeIndex = 0; EdgeIndex < 12; EdgeIndex++)
				{
					const int32* CornerOffsetA = GEdgeCorners[EdgeIndex][0];
					const int32* CornerOffsetB = GEdgeCorners[EdgeIndex][1];

					int32 SampleAX = CellX + CornerOffsetA[0], SampleAY = CellY + CornerOffsetA[1], SampleAZ = CellZ + CornerOffsetA[2];
					int32 SampleBX = CellX + CornerOffsetB[0], SampleBY = CellY + CornerOffsetB[1], SampleBZ = CellZ + CornerOffsetB[2];
					int32 DensityA = GetSample(SampleAX, SampleAY, SampleAZ);
					int32 DensityB = GetSample(SampleBX, SampleBY, SampleBZ);

					if (DensityA == IsoValue || DensityB == IsoValue)
						continue;
					if ((DensityA < IsoValue) == (DensityB < IsoValue))
						continue;

					float InterpolationAlpha = (float)(IsoValue - DensityA) / (float)(DensityB - DensityA);

					FVector GridPositionA((double)SampleAX, (double)SampleAY, (double)SampleAZ);
					FVector GridPositionB((double)SampleBX, (double)SampleBY, (double)SampleBZ);
					FVector GridPosition = GridPositionA + (double)InterpolationAlpha * (GridPositionB - GridPositionA);
					FVector WorldPosition = GridPosition * (double)CellSize;

					FVector Normal = (-ComputeGradient(GridPosition)).GetSafeNormal();
					if (Normal.IsNearlyZero())
						continue;

					double NormalX = Normal.X, NormalY = Normal.Y, NormalZ = Normal.Z;
					double PlaneDistance = NormalX * WorldPosition.X + NormalY * WorldPosition.Y + NormalZ * WorldPosition.Z;

					NormalEquationMatrix[0][0] += NormalX * NormalX;
					NormalEquationMatrix[0][1] += NormalX * NormalY;
					NormalEquationMatrix[0][2] += NormalX * NormalZ;
					NormalEquationMatrix[1][0] += NormalY * NormalX;
					NormalEquationMatrix[1][1] += NormalY * NormalY;
					NormalEquationMatrix[1][2] += NormalY * NormalZ;
					NormalEquationMatrix[2][0] += NormalZ * NormalX;
					NormalEquationMatrix[2][1] += NormalZ * NormalY;
					NormalEquationMatrix[2][2] += NormalZ * NormalZ;
					NormalEquationVector[0] += NormalX * PlaneDistance;
					NormalEquationVector[1] += NormalY * PlaneDistance;
					NormalEquationVector[2] += NormalZ * PlaneDistance;

					AccumNormal += Normal;
					NumIntersections++;
				}

				if (NumIntersections == 0)
					continue;

				NormalEquationMatrix[0][0] += Lambda;
				NormalEquationMatrix[1][1] += Lambda;
				NormalEquationMatrix[2][2] += Lambda;
				NormalEquationVector[0] += Lambda * CellCenter.X;
				NormalEquationVector[1] += Lambda * CellCenter.Y;
				NormalEquationVector[2] += Lambda * CellCenter.Z;

				double SolvedPosition[3];
				if (Solve3x3(NormalEquationMatrix, NormalEquationVector, SolvedPosition))
				{
					Cell.Center.X = FMath::Clamp((float)SolvedPosition[0], (float)CellMin.X, (float)CellMax.X);
					Cell.Center.Y = FMath::Clamp((float)SolvedPosition[1], (float)CellMin.Y, (float)CellMax.Y);
					Cell.Center.Z = FMath::Clamp((float)SolvedPosition[2], (float)CellMin.Z, (float)CellMax.Z);
				}

				Cell.Normal = AccumNormal.GetSafeNormal();
				if (Cell.Normal.IsNearlyZero())
					Cell.Normal = FVector::UpVector;
			}
}

ADualContourMeshActor::ADualContourMeshActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);

	CollisionSettings.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ADualContourMeshActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if WITH_EDITOR
	DrawCellDebug();
#endif
}

bool ADualContourMeshActor::ShouldTickIfViewportsOnly() const
{
#if WITH_EDITOR
	return CVarDrawDualContourCells.GetValueOnGameThread() != 0;
#else
	return false;
#endif
}

#if WITH_EDITOR
void ADualContourMeshActor::DrawCellDebug()
{
	UWorld* World = GetWorld();
	if (!World || DualContourGrid.IsEmpty())
		return;

	const int32 DrawMode = CVarDrawDualContourCells.GetValueOnGameThread();
	if (DrawMode == 0)
		return;

	const FVector HalfExtent(CellSize * 0.5f);
	const FVector ScaledHalfExtent = HalfExtent * GetActorScale3D();
	const FQuat ActorQuat = GetActorQuat();
	const FTransform& ActorTransform = GetActorTransform();

	for (int32 CellZ = 0; CellZ < CellCount.Z; CellZ++)
		for (int32 CellY = 0; CellY < CellCount.Y; CellY++)
			for (int32 CellX = 0; CellX < CellCount.X; CellX++)
			{
				const FDualContourCell& Cell = DualContourGrid[CellIndex(CellX, CellY, CellZ)];
				if (!Cell.bActive && DrawMode < 2)
					continue;

				FVector LocalCenter((CellX + 0.5f) * CellSize, (CellY + 0.5f) * CellSize, (CellZ + 0.5f) * CellSize);
				FVector WorldCenter = ActorTransform.TransformPosition(LocalCenter);
				FColor Color = Cell.bActive ? FColor::Green : FColor::Red;
				DrawDebugBox(World, WorldCenter, ScaledHalfExtent, ActorQuat, Color, false, -1.f);
			}
}
#endif

void ADualContourMeshActor::ApplyCollisionSettings(UDualContourMeshComponent* MeshComponent) const
{
	if (!MeshComponent)
		return;

	const FName ProfileName = CollisionSettings.GetCollisionProfileName();
	if (CollisionSettings.DoesUseCollisionProfile())
	{
		MeshComponent->SetCollisionProfileName(ProfileName);
	}
	else
	{
		// Applying these values individually intentionally leaves the component on the Custom profile.
		MeshComponent->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
		MeshComponent->SetCollisionEnabled(CollisionSettings.GetCollisionEnabled(false));
		MeshComponent->SetCollisionObjectType(CollisionSettings.GetObjectType());
		MeshComponent->SetCollisionResponseToChannels(CollisionSettings.GetResponseToChannels());
	}

	MeshComponent->SetGenerateOverlapEvents(bGenerateOverlapEvents);
	MeshComponent->SetNotifyRigidBodyCollision(CollisionSettings.bNotifyRigidBodyCollision);
	MeshComponent->SetSimulatePhysics(false);
}

void ADualContourMeshActor::RefreshCollisionSettings()
{
	for (UDualContourMeshComponent* MeshComponent : MeshComponents)
		ApplyCollisionSettings(MeshComponent);
}

void ADualContourMeshActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	RefreshCollisionSettings();
}

#if WITH_EDITOR
void ADualContourMeshActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshCollisionSettings();
}
#endif

void ADualContourMeshActor::RebuildMesh()
{
	FillSphereDensity();
	BuildCells();

	for (TObjectPtr<UDualContourMeshComponent>& MeshComponent : MeshComponents)
		if (MeshComponent)
			MeshComponent->DestroyComponent();
	MeshComponents.Reset();

	const FVectorInt& DivisionCounts = Divisions;
	const FVectorInt& CellCounts = CellCount;

	for (int32 DivisionZ = 0; DivisionZ < DivisionCounts.Z; DivisionZ++)
		for (int32 DivisionY = 0; DivisionY < DivisionCounts.Y; DivisionY++)
			for (int32 DivisionX = 0; DivisionX < DivisionCounts.X; DivisionX++)
			{
				FVectorInt CellMin(DivisionX * CellCounts.X / DivisionCounts.X, DivisionY * CellCounts.Y / DivisionCounts.Y,
					DivisionZ * CellCounts.Z / DivisionCounts.Z);
				FVectorInt CellMax((DivisionX + 1) * CellCounts.X / DivisionCounts.X, (DivisionY + 1) * CellCounts.Y / DivisionCounts.Y,
					(DivisionZ + 1) * CellCounts.Z / DivisionCounts.Z);

				bool bHasActive = false;
				for (int32 CellZ = CellMin.Z; CellZ < CellMax.Z && !bHasActive; CellZ++)
					for (int32 CellY = CellMin.Y; CellY < CellMax.Y && !bHasActive; CellY++)
						for (int32 CellX = CellMin.X; CellX < CellMax.X && !bHasActive; CellX++)
							bHasActive = DualContourGrid[CellCount.LinearIndex(CellX, CellY, CellZ)].bActive;
				if (!bHasActive)
					continue;

				UDualContourMeshComponent* NewMeshComponent = NewObject<UDualContourMeshComponent>(this, NAME_None, RF_Transactional);
				NewMeshComponent->CellRangeMin = CellMin;
				NewMeshComponent->CellRangeMax = CellMax;
				NewMeshComponent->SetMaterial(0, MeshMaterial);
				ApplyCollisionSettings(NewMeshComponent);
				NewMeshComponent->RegisterComponent();
				NewMeshComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
				NewMeshComponent->BuildAndRefreshMesh();
				MeshComponents.Add(NewMeshComponent);
			}
}
