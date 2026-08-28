#include "DualContourMeshActor.h"
#include "Engine/CollisionProfile.h"
#include "VolumeSampler/VolumeSampler.h"

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
	BindToDualContour();
	for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
		if (Pair.Value)
			Pair.Value->DualContour = DualContour;
	RefreshCollisionSettings();

#if WITH_EDITOR
	if (bRebuildInitialDualContourAfterLoad && InitialDualContour && GetWorld()
	    && GetWorld()->WorldType == EWorldType::Editor && !IsTemplate())
	{
		bRebuildInitialDualContourAfterLoad = false;
		RebuildMesh();
	}

	if (DualContour && DualContour->HasCurrentGeneratedData())
		RefreshDebugComponent();
#endif
}

void ADualContourMeshActor::BeginPlay()
{
	Super::BeginPlay();

	// InitialDualContour is fully loaded and all actor components are registered at this point.
	// Avoid doing this from construction/registration callbacks, which can run repeatedly in the editor.
	if (InitialDualContour)
		RebuildMesh();
}

void ADualContourMeshActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromDualContour();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ADualContourMeshActor::PostLoad()
{
	Super::PostLoad();
	bRebuildInitialDualContourAfterLoad = InitialDualContour != nullptr;
}

void ADualContourMeshActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!PropertyChangedEvent.MemberProperty)
	{
		RefreshCollisionSettings();
		return;
	}

	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty->GetFName();
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, InitialDualContour))
	{
		RebuildMesh();
	}
	else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, Divisions)
	         || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, bAutoCalculateDivisions)
	         || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, MaxCellsPerDivision))
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
	TGuardValue<bool> RebuildingMeshGuard(bRebuildingMesh, true);
	if (InitialDualContour)
	{
		if (!DualContour)
		{
			UE_LOG(LogDualContourMesh, Warning,
				TEXT("Mesh rebuild aborted for %s because its target DualContour is missing."), *GetName());
			return;
		}

		if (!DualContour->CopyFrom(InitialDualContour))
		{
			UE_LOG(LogDualContourMesh, Error,
				TEXT("Mesh rebuild aborted for %s because InitialDualContour is missing current generated data."), *GetName());
			return;
		}

		RecreateMeshComponents();
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

	UnbindFromDualContour();
	DualContour = InDualContour;
	BindToDualContour();

	RecreateMeshComponents();
	return true;
}

void ADualContourMeshActor::BindToDualContour()
{
	if (!DualContour || DualContourCellsRebuiltHandle.IsValid())
		return;

	DualContourCellsRebuiltHandle = DualContour->OnCellsRebuilt.AddUObject(
		this, &ADualContourMeshActor::OnDualContourCellsRebuilt);
}

void ADualContourMeshActor::UnbindFromDualContour()
{
	if (DualContour && DualContourCellsRebuiltHandle.IsValid())
	{
		DualContour->OnCellsRebuilt.Remove(DualContourCellsRebuiltHandle);
		DualContourCellsRebuiltHandle.Reset();
	}
}

void ADualContourMeshActor::OnDualContourCellsRebuilt(FVectorInt AffectedCellMin, FVectorInt AffectedCellMax)
{
	if (bRebuildingMesh)
		return;
	if (MeshCellCount.X != DualContour->CellCount.X || MeshCellCount.Y != DualContour->CellCount.Y
	    || MeshCellCount.Z != DualContour->CellCount.Z || MeshCellSize != DualContour->CellSize)
	{
		RecreateMeshComponents();
		return;
	}

#if WITH_EDITOR
	RefreshDebugComponent();
#endif
	PartialUpdateComponents(AffectedCellMin, AffectedCellMax);
}

void ADualContourMeshActor::RecreateMeshComponents()
{
	if (!DualContour || !DualContour->HasCurrentGeneratedData())
		return;
	UpdateAutoDivisions();
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
	MeshCellCount = DualContour->CellCount;
	MeshCellSize = DualContour->CellSize;

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

void ADualContourMeshActor::UpdateAutoDivisions()
{
	if (!bAutoCalculateDivisions || !DualContour)
		return;

	const int32 SafeMaxCells = FMath::Max(MaxCellsPerDivision, 1);
	const auto CalculateAxisDivisions = [SafeMaxCells](int32 CellCount)
	{
		return CellCount > 0 ? FMath::DivideAndRoundUp(CellCount, SafeMaxCells) : 1;
	};
	Divisions = FVectorInt(
		CalculateAxisDivisions(DualContour->CellCount.X),
		CalculateAxisDivisions(DualContour->CellCount.Y),
		CalculateAxisDivisions(DualContour->CellCount.Z));
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

bool ADualContourMeshActor::ValidateDivisions(FString& OutStatus) const
{
	if (!DualContour)
	{
		OutStatus = TEXT("Invalid: DualContour is missing.");
		return false;
	}

	TArray<FString> Errors;
	const auto ValidateAxis = [&Errors](const TCHAR* Axis, int32 CellCount, int32 DivisionCount)
	{
		if (DivisionCount <= 0)
			Errors.Add(FString::Printf(TEXT("%s must be greater than zero"), Axis));
		else if (CellCount <= 0)
			Errors.Add(FString::Printf(TEXT("CellCount.%s must be greater than zero"), Axis));
		else if (DivisionCount > CellCount)
			Errors.Add(FString::Printf(TEXT("Divisions.%s (%d) cannot exceed CellCount.%s (%d)"),
				Axis, DivisionCount, Axis, CellCount));
	};

	ValidateAxis(TEXT("X"), DualContour->CellCount.X, Divisions.X);
	ValidateAxis(TEXT("Y"), DualContour->CellCount.Y, Divisions.Y);
	ValidateAxis(TEXT("Z"), DualContour->CellCount.Z, Divisions.Z);

	if (Errors.IsEmpty())
	{
		OutStatus = FString::Printf(TEXT("Valid - maximum cells per component: (%d, %d, %d)"),
			FMath::DivideAndRoundUp(DualContour->CellCount.X, Divisions.X),
			FMath::DivideAndRoundUp(DualContour->CellCount.Y, Divisions.Y),
			FMath::DivideAndRoundUp(DualContour->CellCount.Z, Divisions.Z));
		return true;
	}

	OutStatus = FString::Printf(TEXT("Invalid - %s"), *FString::Join(Errors, TEXT("; ")));
	return false;
}

bool ADualContourMeshActor::HasValidDivisions() const
{
	FString Status;
	if (ValidateDivisions(Status))
		return true;

	UE_LOG(LogDualContourMesh, Error, TEXT("Mesh component generation aborted for %s: %s"), *GetName(), *Status);
	return false;
}

int32 ADualContourMeshActor::DivisionIndex(int32 DivX, int32 DivY, int32 DivZ) const
{
	return DivX + DivY * Divisions.X + DivZ * Divisions.X * Divisions.Y;
}

FVectorInt ADualContourMeshActor::DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	const auto GetAxisDivision = [](int32 Cell, int32 DivisionCount, int32 CellCount)
	{
		const int64 Numerator = (static_cast<int64>(Cell) + 1) * DivisionCount - 1;
		return FMath::Clamp(static_cast<int32>(Numerator / CellCount), 0, DivisionCount - 1);
	};
	return FVectorInt(
		GetAxisDivision(CellX, Divisions.X, DualContour->CellCount.X),
		GetAxisDivision(CellY, Divisions.Y, DualContour->CellCount.Y),
		GetAxisDivision(CellZ, Divisions.Z, DualContour->CellCount.Z));
}

FVectorInt ADualContourMeshActor::DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const
{
	return FVectorInt(
		static_cast<int32>(static_cast<int64>(DivX) * DualContour->CellCount.X / Divisions.X),
		static_cast<int32>(static_cast<int64>(DivY) * DualContour->CellCount.Y / Divisions.Y),
		static_cast<int32>(static_cast<int64>(DivZ) * DualContour->CellCount.Z / Divisions.Z));
}

FVectorInt ADualContourMeshActor::DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const
{
	return FVectorInt(
		static_cast<int32>((static_cast<int64>(DivX) + 1) * DualContour->CellCount.X / Divisions.X),
		static_cast<int32>((static_cast<int64>(DivY) + 1) * DualContour->CellCount.Y / Divisions.Y),
		static_cast<int32>((static_cast<int64>(DivZ) + 1) * DualContour->CellCount.Z / Divisions.Z));
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

bool ADualContourMeshActor::ModifyDensityWithSampler(const FVector& WorldHitPos, const FVector& WorldHitNormal, UVolumeSampler* Sampler,
	float UniformScale, bool bExcavate)
{
	if (!DualContour || !DualContour->HasCurrentGeneratedData())
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("Density edit ignored for %s because generation settings changed. Call RebuildMesh first."), *GetName());
		return false;
	}
	if (!Sampler || !FMath::IsFinite(UniformScale) || UniformScale <= UE_SMALL_NUMBER)
	{
		UE_LOG(LogDualContourMesh, Warning, TEXT("Density edit ignored for %s because its sampler or scale is invalid."),
			*GetName());
		return false;
	}

	const FTransform& ActorTransform = GetActorTransform();
	const FVector LocalHitPosition = ActorTransform.InverseTransformPosition(WorldHitPos);
	const FVector LocalHitNormal = ActorTransform.InverseTransformVectorNoScale(WorldHitNormal).GetSafeNormal();
	if (!HasValidDivisions() || LocalHitNormal.IsNearlyZero())
		return false;
	const FQuat SamplerRotation = FQuat::FindBetweenNormals(FVector::UpVector, LocalHitNormal);
	const FVector SamplerPivotPosition = Sampler->Pivot * Sampler->VolumeSize;
	const FTransform SamplerTransform(
		SamplerRotation, LocalHitPosition - SamplerPivotPosition, FVector(UniformScale));

	FVectorInt AffectedCellMin;
	FVectorInt AffectedCellMax;
	FText Error;
	if (!Sampler->ModifyDualContour(
		DualContour, SamplerTransform, bExcavate, AffectedCellMin, AffectedCellMax, Error))
	{
		if (!Error.IsEmpty())
			UE_LOG(LogDualContourMesh, Warning, TEXT("Density edit failed for %s: %s"), *GetName(), *Error.ToString());
		return false;
	}
	return true;
}
