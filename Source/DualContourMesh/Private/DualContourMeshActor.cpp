#include "DualContourMeshActor.h"
#include "Engine/CollisionProfile.h"

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
	if (!DualContour || !DualContour->Rebuild())
		return;
	RecreateMeshComponents();
}

void ADualContourMeshActor::SetDualContour(UDualContour* InDualContour)
{
	if (!InDualContour || DualContour == InDualContour)
		return;

	DualContour = InDualContour;
	for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
		if (Pair.Value)
			Pair.Value->DualContour = DualContour;
}

void ADualContourMeshActor::RefreshMeshFromCurrentData()
{
	if (!DualContour || !DualContour->HasCurrentGeneratedData())
		return;
	RecreateMeshComponents();
}

void ADualContourMeshActor::RecreateMeshComponents()
{

#if WITH_EDITOR
	RefreshDebugComponent();
#endif

	for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
		if (Pair.Value)
			Pair.Value->DestroyComponent();
	MeshComponents.Reset();

	for (int32 DivisionZ = 0; DivisionZ < DualContour->Divisions.Z; ++DivisionZ)
		for (int32 DivisionY = 0; DivisionY < DualContour->Divisions.Y; ++DivisionY)
			for (int32 DivisionX = 0; DivisionX < DualContour->Divisions.X; ++DivisionX)
			{
				const FVectorInt CellMin = DualContour->DivisionCellMin(DivisionX, DivisionY, DivisionZ);
				const FVectorInt CellMax = DualContour->DivisionCellMax(DivisionX, DivisionY, DivisionZ);
				if (!DualContour->HasActiveCellInRange(CellMin, CellMax))
					continue;
				MeshComponents.Add(DualContour->DivisionIndex(DivisionX, DivisionY, DivisionZ),
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

void ADualContourMeshActor::PartialUpdateComponents(const TSet<int32>& AffectedDivisions)
{
	if (!DualContour)
		return;

	for (const int32 DivisionIndex : AffectedDivisions)
	{
		const int32 DivisionX = DivisionIndex % DualContour->Divisions.X;
		const int32 DivisionY = (DivisionIndex / DualContour->Divisions.X) % DualContour->Divisions.Y;
		const int32 DivisionZ = DivisionIndex / (DualContour->Divisions.X * DualContour->Divisions.Y);
		if (DivisionX < 0 || DivisionX >= DualContour->Divisions.X || DivisionY < 0
		    || DivisionY >= DualContour->Divisions.Y || DivisionZ < 0 || DivisionZ >= DualContour->Divisions.Z)
			continue;

		const FVectorInt CellMin = DualContour->DivisionCellMin(DivisionX, DivisionY, DivisionZ);
		const FVectorInt CellMax = DualContour->DivisionCellMax(DivisionX, DivisionY, DivisionZ);
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
	TSet<int32> AffectedDivisions;
	if (!DualContour->ModifyDensityWithHemisphere(LocalHitPosition, LocalHitNormal, Radius, bExcavate, AffectedDivisions))
		return;

#if WITH_EDITOR
	RefreshDebugComponent();
#endif
	PartialUpdateComponents(AffectedDivisions);
}
