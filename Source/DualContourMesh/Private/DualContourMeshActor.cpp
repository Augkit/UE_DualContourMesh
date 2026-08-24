#include "DualContourMeshActor.h"
#include "Engine/CollisionProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogDualContourMesh, Log, All);

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

// Sample densities are uint8, so a half-integer threshold avoids ambiguous samples exactly on the isosurface.
static constexpr float GDualContourIsoValue = 127.5f;

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

bool ADualContourMeshActor::HasCurrentGeneratedData() const
{
	if (bMeshRebuildRequired)
		return false;

	const int64 ExpectedCellCount = static_cast<int64>(CellCount.X) * CellCount.Y * CellCount.Z;
	const int64 ExpectedSampleCount = (static_cast<int64>(CellCount.X) + 1) * (static_cast<int64>(CellCount.Y) + 1)
	                                  * (static_cast<int64>(CellCount.Z) + 1);
	return ExpectedCellCount == DualContourGrid.Num() && ExpectedSampleCount == SamplePointGrid.Num();
}

bool ADualContourMeshActor::ValidateMeshGenerationSettings() const
{
	if (CellCount.X <= 0 || CellCount.Y <= 0 || CellCount.Z <= 0)
	{
		UE_LOG(LogDualContourMesh, Error, TEXT("RebuildMesh aborted for %s: CellCount must be positive (current: %d, %d, %d)."),
			*GetName(), CellCount.X, CellCount.Y, CellCount.Z);
		return false;
	}
	if (Divisions.X <= 0 || Divisions.Y <= 0 || Divisions.Z <= 0)
	{
		UE_LOG(LogDualContourMesh, Error, TEXT("RebuildMesh aborted for %s: Divisions must be positive (current: %d, %d, %d)."),
			*GetName(), Divisions.X, Divisions.Y, Divisions.Z);
		return false;
	}
	if (CellSize <= 0.f)
	{
		UE_LOG(LogDualContourMesh, Error, TEXT("RebuildMesh aborted for %s: CellSize must be greater than zero (current: %g)."),
			*GetName(), CellSize);
		return false;
	}

	const auto FitsInArray = [](int64 SizeX, int64 SizeY, int64 SizeZ)
	{
		return SizeX <= MAX_int32 && SizeY <= MAX_int32 && SizeZ <= MAX_int32
		       && SizeX <= MAX_int32 / SizeY
		       && SizeX * SizeY <= MAX_int32 / SizeZ;
	};
	if (!FitsInArray(CellCount.X, CellCount.Y, CellCount.Z)
	    || !FitsInArray(static_cast<int64>(CellCount.X) + 1, static_cast<int64>(CellCount.Y) + 1,
		    static_cast<int64>(CellCount.Z) + 1))
	{
		UE_LOG(LogDualContourMesh, Error, TEXT("RebuildMesh aborted for %s: requested grid exceeds TArray's int32 capacity."), *GetName());
		return false;
	}

	return true;
}

uint8 ADualContourMeshActor::GetSample(int32 SampleX, int32 SampleY, int32 SampleZ) const
{
	FVectorInt SampleDimensions = GetSampleDims();
	if (SampleX < 0 || SampleX >= SampleDimensions.X || SampleY < 0 || SampleY >= SampleDimensions.Y || SampleZ < 0 || SampleZ >= SampleDimensions.Z)
		return 0;
	const int32 Index = SampleIndex(SampleX, SampleY, SampleZ);
	return SamplePointGrid.IsValidIndex(Index) ? SamplePointGrid[Index] : 0;
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
	DualContourGrid.SetNum(CellCount.Volume());
	RebuildCellsInRange(FVectorInt(0, 0, 0), CellCount);
}

void ADualContourMeshActor::RebuildCellsInRange(FVectorInt RangeMin, FVectorInt RangeMax)
{
	RangeMin = FVectorInt(FMath::Max(0, RangeMin.X), FMath::Max(0, RangeMin.Y), FMath::Max(0, RangeMin.Z));
	RangeMax = FVectorInt(FMath::Min(CellCount.X, RangeMax.X), FMath::Min(CellCount.Y, RangeMax.Y), FMath::Min(CellCount.Z, RangeMax.Z));

	const double Lambda = 0.1;

	for (int32 CellZ = RangeMin.Z; CellZ < RangeMax.Z; CellZ++)
		for (int32 CellY = RangeMin.Y; CellY < RangeMax.Y; CellY++)
			for (int32 CellX = RangeMin.X; CellX < RangeMax.X; CellX++)
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
							if (Density >= GDualContourIsoValue)
								bHasInside = true;
							else
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

					if ((DensityA < GDualContourIsoValue) == (DensityB < GDualContourIsoValue))
						continue;

					float InterpolationAlpha = (GDualContourIsoValue - DensityA) / (float)(DensityB - DensityA);

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

#if WITH_EDITOR
	DebugComponent = CreateDefaultSubobject<UDualContourDebugComponent>(TEXT("DebugCells"));
	DebugComponent->SetupAttachment(RootComponent);
	DebugComponent->bSelectable = false;
#endif
}

#if WITH_EDITOR
void ADualContourMeshActor::RefreshDebugComponent()
{
	if (DebugComponent)
	{
		DebugComponent->UpdateFromGrid(DualContourGrid, CellCount, CellSize);
		DebugComponent->MarkRenderStateDirty();
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
	for (auto& Pair : MeshComponents)
		ApplyCollisionSettings(Pair.Value);
}

void ADualContourMeshActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	RefreshCollisionSettings();

#if WITH_EDITOR
	// CellEntries is an editor-only runtime snapshot and is not serialized with the actor.
	// Restore it after loading a level or hot-reloading the module when generated grid data is still valid.
	if (HasCurrentGeneratedData())
		RefreshDebugComponent();
#endif
}

#if WITH_EDITOR
void ADualContourMeshActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, CellCount)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, CellSize)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, Divisions)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, SphereCenter)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, SphereRadius))
	{
		bMeshRebuildRequired = true;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshCollisionSettings();
}
#endif

void ADualContourMeshActor::RebuildMesh()
{
	bMeshRebuildRequired = true;
	if (!ValidateMeshGenerationSettings())
		return;

	FillSphereDensity();
	BuildCells();

	bMeshRebuildRequired = false;

#if WITH_EDITOR
	RefreshDebugComponent();
#endif

	for (auto& Pair : MeshComponents)
		if (Pair.Value)
			Pair.Value->DestroyComponent();
	MeshComponents.Reset();

	for (int32 DivisionZ = 0; DivisionZ < Divisions.Z; DivisionZ++)
		for (int32 DivisionY = 0; DivisionY < Divisions.Y; DivisionY++)
			for (int32 DivisionX = 0; DivisionX < Divisions.X; DivisionX++)
			{
				FVectorInt CellMin = DivisionCellMin(DivisionX, DivisionY, DivisionZ);
				FVectorInt CellMax = DivisionCellMax(DivisionX, DivisionY, DivisionZ);

				bool bHasActive = false;
				for (int32 CellZ = CellMin.Z; CellZ < CellMax.Z && !bHasActive; CellZ++)
					for (int32 CellY = CellMin.Y; CellY < CellMax.Y && !bHasActive; CellY++)
						for (int32 CellX = CellMin.X; CellX < CellMax.X && !bHasActive; CellX++)
							bHasActive = DualContourGrid[CellIndex(CellX, CellY, CellZ)].bActive;
				if (!bHasActive)
					continue;

				MeshComponents.Add(DivisionIndex(DivisionX, DivisionY, DivisionZ), CreateMeshComponent(CellMin, CellMax));
			}
}

int32 ADualContourMeshActor::DivisionIndex(int32 DivX, int32 DivY, int32 DivZ) const
{
	return DivX + DivY * Divisions.X + DivZ * Divisions.X * Divisions.Y;
}

FVectorInt ADualContourMeshActor::DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	return FVectorInt(
		FMath::Clamp(CellX * Divisions.X / CellCount.X, 0, Divisions.X - 1),
		FMath::Clamp(CellY * Divisions.Y / CellCount.Y, 0, Divisions.Y - 1),
		FMath::Clamp(CellZ * Divisions.Z / CellCount.Z, 0, Divisions.Z - 1));
}

FVectorInt ADualContourMeshActor::DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const
{
	return FVectorInt(
		DivX * CellCount.X / Divisions.X,
		DivY * CellCount.Y / Divisions.Y,
		DivZ * CellCount.Z / Divisions.Z);
}

FVectorInt ADualContourMeshActor::DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const
{
	return FVectorInt(
		(DivX + 1) * CellCount.X / Divisions.X,
		(DivY + 1) * CellCount.Y / Divisions.Y,
		(DivZ + 1) * CellCount.Z / Divisions.Z);
}

UDualContourMeshComponent* ADualContourMeshActor::CreateMeshComponent(FVectorInt CellMin, FVectorInt CellMax)
{
	UDualContourMeshComponent* NewComponent = NewObject<UDualContourMeshComponent>(this, NAME_None, RF_Transactional);
	NewComponent->CellRangeMin = CellMin;
	NewComponent->CellRangeMax = CellMax;
	NewComponent->SetMaterial(0, MeshMaterial);
	ApplyCollisionSettings(NewComponent);
	NewComponent->RegisterComponent();
	NewComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	NewComponent->BuildAndRefreshMesh();
	return NewComponent;
}

void ADualContourMeshActor::PartialUpdateComponents(const TSet<int32>& AffectedDivisions)
{
	for (int32 DivIdx : AffectedDivisions)
	{
		const int32 DivX = DivIdx % Divisions.X;
		const int32 DivY = (DivIdx / Divisions.X) % Divisions.Y;
		const int32 DivZ = DivIdx / (Divisions.X * Divisions.Y);

		if (DivX < 0 || DivX >= Divisions.X || DivY < 0 || DivY >= Divisions.Y || DivZ < 0 || DivZ >= Divisions.Z)
			continue;

		FVectorInt CellMin = DivisionCellMin(DivX, DivY, DivZ);
		FVectorInt CellMax = DivisionCellMax(DivX, DivY, DivZ);

		bool bHasActive = false;
		for (int32 CellZ = CellMin.Z; CellZ < CellMax.Z && !bHasActive; CellZ++)
			for (int32 CellY = CellMin.Y; CellY < CellMax.Y && !bHasActive; CellY++)
				for (int32 CellX = CellMin.X; CellX < CellMax.X && !bHasActive; CellX++)
					bHasActive = DualContourGrid[CellIndex(CellX, CellY, CellZ)].bActive;

		TObjectPtr<UDualContourMeshComponent>* ExistingComp = MeshComponents.Find(DivIdx);
		if (ExistingComp && *ExistingComp)
		{
			if (!bHasActive)
			{
				(*ExistingComp)->DestroyComponent();
				MeshComponents.Remove(DivIdx);
			}
			else
			{
				(*ExistingComp)->BuildAndRefreshMesh();
			}
		}
		else if (bHasActive)
		{
			MeshComponents.Add(DivIdx, CreateMeshComponent(CellMin, CellMax));
		}
	}
}

void ADualContourMeshActor::ModifyDensityWithHemisphere(
	const FVector& WorldHitPos, const FVector& WorldHitNormal, float Radius, bool bExcavate)
{
	if (!HasCurrentGeneratedData())
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("Density edit ignored for %s because mesh-generation settings changed. Call RebuildMesh first."), *GetName());
		return;
	}

	const FTransform& ActorTransform = GetActorTransform();
	const FVector LocalHitPos = ActorTransform.InverseTransformPosition(WorldHitPos);
	const FVector LocalHitNormal = ActorTransform.InverseTransformVectorNoScale(WorldHitNormal).GetSafeNormal();

	const FVectorInt SampleDims = GetSampleDims();
	const float GridRadius = Radius / CellSize;

	const int32 SampleMinX = FMath::Clamp(FMath::FloorToInt(LocalHitPos.X / CellSize - GridRadius), 0, SampleDims.X - 1);
	const int32 SampleMinY = FMath::Clamp(FMath::FloorToInt(LocalHitPos.Y / CellSize - GridRadius), 0, SampleDims.Y - 1);
	const int32 SampleMinZ = FMath::Clamp(FMath::FloorToInt(LocalHitPos.Z / CellSize - GridRadius), 0, SampleDims.Z - 1);
	const int32 SampleMaxX = FMath::Clamp(FMath::CeilToInt(LocalHitPos.X / CellSize + GridRadius), 0, SampleDims.X - 1);
	const int32 SampleMaxY = FMath::Clamp(FMath::CeilToInt(LocalHitPos.Y / CellSize + GridRadius), 0, SampleDims.Y - 1);
	const int32 SampleMaxZ = FMath::Clamp(FMath::CeilToInt(LocalHitPos.Z / CellSize + GridRadius), 0, SampleDims.Z - 1);

	bool bModified = false;
	for (int32 SampleZ = SampleMinZ; SampleZ <= SampleMaxZ; SampleZ++)
		for (int32 SampleY = SampleMinY; SampleY <= SampleMaxY; SampleY++)
			for (int32 SampleX = SampleMinX; SampleX <= SampleMaxX; SampleX++)
			{
				const FVector SampleLocalPos = GetSampleWorldPos(SampleX, SampleY, SampleZ);
				const FVector Delta = SampleLocalPos - LocalHitPos;
				const float Dist = (float)Delta.Size();
				if (Dist > Radius)
					continue;
				if (FVector::DotProduct(Delta, LocalHitNormal) < 0.f)
					continue;

				const int32 FalloffAmount = FMath::Max(1, FMath::RoundToInt(127.f * (1.f - Dist / Radius)));
				const int32 Idx = SampleIndex(SampleX, SampleY, SampleZ);
				const int32 OldVal = SamplePointGrid[Idx];
				SamplePointGrid[Idx] = (uint8)FMath::Clamp(
					bExcavate ? OldVal - FalloffAmount : OldVal + FalloffAmount, 0, 255);
				bModified = true;
			}

	if (!bModified)
		return;

	// Affected cells: a sample at (SX,SY,SZ) is a corner of cells [SX-1..SX, SY-1..SY, SZ-1..SZ]
	const FVectorInt CellRangeMin(
		FMath::Max(0, SampleMinX - 1),
		FMath::Max(0, SampleMinY - 1),
		FMath::Max(0, SampleMinZ - 1));
	const FVectorInt CellRangeMax(
		FMath::Min(CellCount.X, SampleMaxX + 1),
		FMath::Min(CellCount.Y, SampleMaxY + 1),
		FMath::Min(CellCount.Z, SampleMaxZ + 1));

	RebuildCellsInRange(CellRangeMin, CellRangeMax);

#if WITH_EDITOR
	RefreshDebugComponent();
#endif

	// Collect own division + any -X/-Y/-Z neighbor whose borrowed +X/+Y/+Z ring includes a changed cell
	TSet<int32> AffectedDivisions;
	for (int32 CellZ = CellRangeMin.Z; CellZ < CellRangeMax.Z; CellZ++)
		for (int32 CellY = CellRangeMin.Y; CellY < CellRangeMax.Y; CellY++)
			for (int32 CellX = CellRangeMin.X; CellX < CellRangeMax.X; CellX++)
			{
				const FVectorInt Div = DivisionFromCell(CellX, CellY, CellZ);
				AffectedDivisions.Add(DivisionIndex(Div.X, Div.Y, Div.Z));

				if (Div.X > 0 && CellX == DivisionCellMax(Div.X - 1, Div.Y, Div.Z).X)
					AffectedDivisions.Add(DivisionIndex(Div.X - 1, Div.Y, Div.Z));
				if (Div.Y > 0 && CellY == DivisionCellMax(Div.X, Div.Y - 1, Div.Z).Y)
					AffectedDivisions.Add(DivisionIndex(Div.X, Div.Y - 1, Div.Z));
				if (Div.Z > 0 && CellZ == DivisionCellMax(Div.X, Div.Y, Div.Z - 1).Z)
					AffectedDivisions.Add(DivisionIndex(Div.X, Div.Y, Div.Z - 1));
			}

	PartialUpdateComponents(AffectedDivisions);
}
