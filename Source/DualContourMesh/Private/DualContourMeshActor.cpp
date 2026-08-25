#include "DualContourMeshActor.h"
#include "Engine/CollisionProfile.h"
#include "SVTDensityField.h"

DEFINE_LOG_CATEGORY_STATIC(LogDualContourMesh, Log, All);

ADualContourMeshActor::ADualContourMeshActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
	DualContour = CreateDefaultSubobject<UDualContour>(TEXT("DualContour"));
	CollisionSettings.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

#if WITH_EDITOR
	DebugComponent = CreateDefaultSubobject<UDualContourDebugComponent>(TEXT("DebugCells"));
	DebugComponent->SetupAttachment(RootComponent);
	DebugComponent->bSelectable = false;
#endif
}

void ADualContourMeshActor::ApplyCollisionSettings(UDualContourMeshComponent* MeshComponent) const
{
	if (!MeshComponent)
		return;

	if (CollisionSettings.DoesUseCollisionProfile())
	{
		MeshComponent->SetCollisionProfileName(CollisionSettings.GetCollisionProfileName());
	}
	else
	{
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
	for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
		ApplyCollisionSettings(Pair.Value);
}

void ADualContourMeshActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
		if (Pair.Value)
			Pair.Value->DualContour = DualContour;
	RefreshCollisionSettings();

#if WITH_EDITOR
	if (DualContour && DualContour->HasCurrentGeneratedData())
		RefreshDebugComponent();
#endif
}

#if WITH_EDITOR
void ADualContourMeshActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.MemberProperty
	    && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, Divisions))
	{
		RecreateMeshComponents();
	}
	RefreshCollisionSettings();
}

void ADualContourMeshActor::RefreshDebugComponent()
{
	if (DebugComponent && DualContour)
	{
		DebugComponent->UpdateFromGrid(DualContour->GetContourChunks(), DualContour->CellCount, DualContour->CellSize);
		DebugComponent->MarkRenderStateDirty();
	}
}
#endif

void ADualContourMeshActor::RebuildMesh()
{
	if (InitialDensityField)
	{
		if (!InitialDensityField->DualContour)
		{
			UE_LOG(LogDualContourMesh, Warning,
				TEXT("Mesh rebuild aborted for %s because InitialDensityField has no DualContour data."), *GetName());
			return;
		}

		const FName DuplicateName = MakeUniqueObjectName(this, UDualContour::StaticClass(), TEXT("DualContour"));
		UDualContour* DensityCopy = DuplicateObject<UDualContour>(InitialDensityField->DualContour, this, DuplicateName);
		if (!DensityCopy)
		{
			UE_LOG(LogDualContourMesh, Error,
				TEXT("Mesh rebuild aborted for %s because InitialDensityField could not be copied."), *GetName());
			return;
		}

		DensityCopy->SetFlags(RF_Transactional);
		SetGeneratedDualContour(DensityCopy);
		return;
	}

	if (!DualContour || !DualContour->Rebuild())
		return;
	RecreateMeshComponents();
}

bool ADualContourMeshActor::SetGeneratedDualContour(UDualContour* InDualContour)
{
	if (!InDualContour || !InDualContour->HasCurrentGeneratedData())
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("Generated contour data was not applied to %s because it is missing or requires a rebuild."), *GetName());
		return false;
	}

	if (DualContour == InDualContour)
		return true;

	DualContour = InDualContour;

	RecreateMeshComponents();
	return true;
}

void ADualContourMeshActor::RecreateMeshComponents()
{
	if (!DualContour || !DualContour->HasCurrentGeneratedData())
		return;
	if (!HasValidDivisions())
	{
		for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
			if (Pair.Value)
				Pair.Value->DestroyComponent();
		MeshComponents.Reset();
		return;
	}

#if WITH_EDITOR
	RefreshDebugComponent();
#endif

	for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
		if (Pair.Value)
			Pair.Value->DestroyComponent();
	MeshComponents.Reset();

	for (int32 DivisionZ = 0; DivisionZ < Divisions.Z; ++DivisionZ)
		for (int32 DivisionY = 0; DivisionY < Divisions.Y; ++DivisionY)
			for (int32 DivisionX = 0; DivisionX < Divisions.X; ++DivisionX)
			{
				const FVectorInt CellMin = DivisionCellMin(DivisionX, DivisionY, DivisionZ);
				const FVectorInt CellMax = DivisionCellMax(DivisionX, DivisionY, DivisionZ);
				if (!DualContour->HasActiveCellInRange(CellMin, CellMax))
					continue;
				MeshComponents.Add(DivisionIndex(DivisionX, DivisionY, DivisionZ),
					CreateMeshComponent(CellMin, CellMax));
			}
}

UDualContourMeshComponent* ADualContourMeshActor::CreateMeshComponent(FVectorInt CellMin, FVectorInt CellMax)
{
	UDualContourMeshComponent* NewComponent = NewObject<UDualContourMeshComponent>(this, NAME_None, RF_Transactional);
	NewComponent->DualContour = DualContour;
	NewComponent->CellRangeMin = CellMin;
	NewComponent->CellRangeMax = CellMax;
	NewComponent->SetMaterial(0, MeshMaterial);
	ApplyCollisionSettings(NewComponent);
	NewComponent->RegisterComponent();
	NewComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	NewComponent->BuildAndRefreshMesh();
	return NewComponent;
}

bool ADualContourMeshActor::HasValidDivisions() const
{
	if (DualContour && Divisions.X > 0 && Divisions.Y > 0 && Divisions.Z > 0
	    && Divisions.X <= DualContour->CellCount.X && Divisions.Y <= DualContour->CellCount.Y
	    && Divisions.Z <= DualContour->CellCount.Z)
		return true;

	UE_LOG(LogDualContourMesh, Error,
		TEXT("Mesh component generation aborted for %s: Divisions must be positive and no greater than CellCount "
			"(Divisions: %d, %d, %d; CellCount: %d, %d, %d)."),
		*GetName(), Divisions.X, Divisions.Y, Divisions.Z, DualContour ? DualContour->CellCount.X : 0,
		DualContour ? DualContour->CellCount.Y : 0, DualContour ? DualContour->CellCount.Z : 0);
	return false;
}

int32 ADualContourMeshActor::DivisionIndex(int32 DivX, int32 DivY, int32 DivZ) const
{
	return DivX + DivY * Divisions.X + DivZ * Divisions.X * Divisions.Y;
}

FVectorInt ADualContourMeshActor::DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	return FVectorInt(FMath::Clamp(CellX * Divisions.X / DualContour->CellCount.X, 0, Divisions.X - 1),
		FMath::Clamp(CellY * Divisions.Y / DualContour->CellCount.Y, 0, Divisions.Y - 1),
		FMath::Clamp(CellZ * Divisions.Z / DualContour->CellCount.Z, 0, Divisions.Z - 1));
}

FVectorInt ADualContourMeshActor::DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const
{
	return FVectorInt(DivX * DualContour->CellCount.X / Divisions.X, DivY * DualContour->CellCount.Y / Divisions.Y,
		DivZ * DualContour->CellCount.Z / Divisions.Z);
}

FVectorInt ADualContourMeshActor::DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const
{
	return FVectorInt((DivX + 1) * DualContour->CellCount.X / Divisions.X,
		(DivY + 1) * DualContour->CellCount.Y / Divisions.Y, (DivZ + 1) * DualContour->CellCount.Z / Divisions.Z);
}

void ADualContourMeshActor::PartialUpdateComponents(FVectorInt AffectedCellMin, FVectorInt AffectedCellMax)
{
	if (!DualContour || !HasValidDivisions())
		return;

	TSet<int32> AffectedDivisions;
	for (int32 Z = AffectedCellMin.Z; Z < AffectedCellMax.Z; ++Z)
		for (int32 Y = AffectedCellMin.Y; Y < AffectedCellMax.Y; ++Y)
			for (int32 X = AffectedCellMin.X; X < AffectedCellMax.X; ++X)
			{
				const FVectorInt Div = DivisionFromCell(X, Y, Z);
				AffectedDivisions.Add(DivisionIndex(Div.X, Div.Y, Div.Z));
				const bool bNegX = Div.X > 0 && X == DivisionCellMax(Div.X - 1, Div.Y, Div.Z).X;
				const bool bNegY = Div.Y > 0 && Y == DivisionCellMax(Div.X, Div.Y - 1, Div.Z).Y;
				const bool bNegZ = Div.Z > 0 && Z == DivisionCellMax(Div.X, Div.Y, Div.Z - 1).Z;
				if (bNegX)
					AffectedDivisions.Add(DivisionIndex(Div.X - 1, Div.Y, Div.Z));
				if (bNegY)
					AffectedDivisions.Add(DivisionIndex(Div.X, Div.Y - 1, Div.Z));
				if (bNegZ)
					AffectedDivisions.Add(DivisionIndex(Div.X, Div.Y, Div.Z - 1));
				if (bNegX && bNegY)
					AffectedDivisions.Add(DivisionIndex(Div.X - 1, Div.Y - 1, Div.Z));
				if (bNegX && bNegZ)
					AffectedDivisions.Add(DivisionIndex(Div.X - 1, Div.Y, Div.Z - 1));
				if (bNegY && bNegZ)
					AffectedDivisions.Add(DivisionIndex(Div.X, Div.Y - 1, Div.Z - 1));
			}

	for (const int32 DivisionIndex : AffectedDivisions)
	{
		const int32 DivisionX = DivisionIndex % Divisions.X;
		const int32 DivisionY = (DivisionIndex / Divisions.X) % Divisions.Y;
		const int32 DivisionZ = DivisionIndex / (Divisions.X * Divisions.Y);
		if (DivisionX < 0 || DivisionX >= Divisions.X || DivisionY < 0
		    || DivisionY >= Divisions.Y || DivisionZ < 0 || DivisionZ >= Divisions.Z)
			continue;

		const FVectorInt CellMin = DivisionCellMin(DivisionX, DivisionY, DivisionZ);
		const FVectorInt CellMax = DivisionCellMax(DivisionX, DivisionY, DivisionZ);
		const bool bHasActiveCells = DualContour->HasActiveCellInRange(CellMin, CellMax);
		TObjectPtr<UDualContourMeshComponent>* ExistingComponent = MeshComponents.Find(DivisionIndex);
		if (ExistingComponent && *ExistingComponent)
		{
			if (!bHasActiveCells)
			{
				(*ExistingComponent)->DestroyComponent();
				MeshComponents.Remove(DivisionIndex);
			}
			else
			{
				(*ExistingComponent)->BuildAndRefreshMesh();
			}
		}
		else if (bHasActiveCells)
		{
			MeshComponents.Add(DivisionIndex, CreateMeshComponent(CellMin, CellMax));
		}
	}
}

void ADualContourMeshActor::ModifyDensityWithHemisphere(
	const FVector& WorldHitPos, const FVector& WorldHitNormal, float Radius, bool bExcavate)
{
	if (!DualContour || !DualContour->HasCurrentGeneratedData())
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("Density edit ignored for %s because generation settings changed. Call RebuildMesh first."), *GetName());
		return;
	}

	const FTransform& ActorTransform = GetActorTransform();
	const FVector LocalHitPosition = ActorTransform.InverseTransformPosition(WorldHitPos);
	const FVector LocalHitNormal = ActorTransform.InverseTransformVectorNoScale(WorldHitNormal).GetSafeNormal();
	if (!HasValidDivisions())
		return;

	FVectorInt AffectedCellMin;
	FVectorInt AffectedCellMax;
	if (!DualContour->ModifyDensityWithHemisphere(
		LocalHitPosition, LocalHitNormal, Radius, bExcavate, AffectedCellMin, AffectedCellMax))
		return;

#if WITH_EDITOR
	RefreshDebugComponent();
#endif
	PartialUpdateComponents(AffectedCellMin, AffectedCellMax);
}
