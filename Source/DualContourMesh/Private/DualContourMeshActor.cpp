#include "DualContourMeshActor.h"
#include "DualContourMeshBuilder.h"
#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "VolumeSampler/VolumeSampler.h"

DEFINE_LOG_CATEGORY_STATIC(LogDualContourMesh, Log, All);

namespace
{
struct FMeshBuildRequest
{
	int32 DivisionIndex = INDEX_NONE;
	FVectorInt CellMin;
	FVectorInt CellMax;
	FDualContourMeshData MeshData;
};

void BuildMeshRequests(const UDualContour& DualContour, TArray<FMeshBuildRequest>& Requests)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_BuildMeshRequests);
	check(IsInGameThread());
	ParallelFor(TEXT("DualContourMesh.BuildDivisions"), Requests.Num(), 1,
		[&DualContour, &Requests](int32 RequestIndex)
		{
			FMeshBuildRequest& Request = Requests[RequestIndex];
			FDualContourMeshBuilder::Build(DualContour, Request.CellMin, Request.CellMax, Request.MeshData);
		}, EParallelForFlags::Unbalanced);

	int64 ProcessedCellCount = 0;
	int64 VertexCount = 0;
	int64 TriangleCount = 0;
	for (const FMeshBuildRequest& Request : Requests)
	{
		ProcessedCellCount += static_cast<int64>(FMath::Max(0, Request.CellMax.X - Request.CellMin.X))
			* FMath::Max(0, Request.CellMax.Y - Request.CellMin.Y)
			* FMath::Max(0, Request.CellMax.Z - Request.CellMin.Z);
		VertexCount += Request.MeshData.Positions.Num();
		TriangleCount += Request.MeshData.Indices.Num() / 3;
	}
}
}

ADualContourMeshActor::ADualContourMeshActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
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

void ADualContourMeshActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyQueuedMeshData();
#if WITH_EDITOR
	ProcessPendingDebugComponentRefresh();
#endif
}

void ADualContourMeshActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	++MeshQueueRevision;
	DivisionUpdateSerials.Reset();
	bMeshUpdateCompletionPending = false;
#if WITH_EDITOR
	bDebugRefreshPending = false;
	bDebugRefreshImmediatelyAfterMeshUpdate = false;
#endif
	ResetQueuedMeshData();
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
	if (!DebugComponent || !DualContour || !UDualContourDebugComponent::IsDrawEnabled())
		return;

	DebugComponent->UpdateFromMeshComponents(MeshComponents, DualContour->CellCount, DualContour->CellSize, Divisions);
	DebugComponent->MarkRenderStateDirty();
}

void ADualContourMeshActor::RefreshDebugVisualization()
{
	RequestDebugComponentRefresh(true);
}

void ADualContourMeshActor::RequestDebugComponentRefresh(bool bImmediate)
{
	if (!UDualContourDebugComponent::IsDrawEnabled())
	{
		bDebugRefreshPending = false;
		UpdateActorTickEnabled();
		return;
	}

	if (bImmediate)
	{
		bDebugRefreshPending = false;
		RefreshDebugComponent();
		UpdateActorTickEnabled();
		return;
	}

	// A brush can produce many small contour rebuilds per second. The debug proxy is a full
	// snapshot, so coalesce them and refresh only after edits have been quiet briefly.
	bDebugRefreshPending = true;
	DebugRefreshDeadline = FPlatformTime::Seconds() + 0.15;
	UpdateActorTickEnabled();
}

void ADualContourMeshActor::ProcessPendingDebugComponentRefresh()
{
	if (!bDebugRefreshPending)
		return;

	if (!UDualContourDebugComponent::IsDrawEnabled() || FPlatformTime::Seconds() >= DebugRefreshDeadline)
	{
		bDebugRefreshPending = false;
		RefreshDebugComponent();
		UpdateActorTickEnabled();
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
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_OnDualContourCellsRebuilt);
	if (bRebuildingMesh)
		return;
	if (MeshCellCount.X != DualContour->CellCount.X || MeshCellCount.Y != DualContour->CellCount.Y
	    || MeshCellCount.Z != DualContour->CellCount.Z || MeshCellSize != DualContour->CellSize)
	{
		RecreateMeshComponents();
		return;
	}

#if WITH_EDITOR
	const bool bFullGridRebuild = AffectedCellMin.X == 0 && AffectedCellMin.Y == 0 && AffectedCellMin.Z == 0
	                              && AffectedCellMax.X == DualContour->CellCount.X
	                              && AffectedCellMax.Y == DualContour->CellCount.Y
	                              && AffectedCellMax.Z == DualContour->CellCount.Z;
	bDebugRefreshImmediatelyAfterMeshUpdate |= bFullGridRebuild;
#endif
	PartialUpdateComponents(AffectedCellMin, AffectedCellMax);
}

void ADualContourMeshActor::RecreateMeshComponents()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_RecreateMeshComponents);
	if (!DualContour || !DualContour->HasCurrentGeneratedData())
		return;
	UpdateAutoDivisions();
	if (!HasValidDivisions())
	{
		++MeshQueueRevision;
		DivisionUpdateSerials.Reset();
		bMeshUpdateCompletionPending = false;
		ResetQueuedMeshData();
		for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
			if (Pair.Value)
				Pair.Value->DestroyComponent();
		MeshComponents.Reset();
#if WITH_EDITOR
		RequestDebugComponentRefresh(true);
#endif
		return;
	}

#if WITH_EDITOR
	bDebugRefreshImmediatelyAfterMeshUpdate = true;
#endif

	TArray<FMeshBuildRequest> Requests;
	{
		Requests.Reserve(Divisions.X * Divisions.Y * Divisions.Z);
		for (int32 DivisionZ = 0; DivisionZ < Divisions.Z; ++DivisionZ)
			for (int32 DivisionY = 0; DivisionY < Divisions.Y; ++DivisionY)
				for (int32 DivisionX = 0; DivisionX < Divisions.X; ++DivisionX)
				{
					const FVectorInt CellMin = DivisionCellMin(DivisionX, DivisionY, DivisionZ);
					const FVectorInt CellMax = DivisionCellMax(DivisionX, DivisionY, DivisionZ);
					if (!DualContour->HasActiveCellInRange(CellMin, CellMax))
						continue;

					FMeshBuildRequest& Request = Requests.AddDefaulted_GetRef();
					Request.DivisionIndex = DivisionIndex(DivisionX, DivisionY, DivisionZ);
					Request.CellMin = CellMin;
					Request.CellMax = CellMax;
				}
	}
	BuildMeshRequests(*DualContour, Requests);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_ReplaceFullComponents);
		++MeshQueueRevision;
		DivisionUpdateSerials.Reset();
		bMeshUpdateCompletionPending = true;
		ResetQueuedMeshData();
		for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
			if (Pair.Value)
				Pair.Value->DestroyComponent();
		MeshComponents.Reset();
		MeshCellCount = DualContour->CellCount;
		MeshCellSize = DualContour->CellSize;

		for (FMeshBuildRequest& Request : Requests)
			QueueMeshData(Request.DivisionIndex, MoveTemp(Request.MeshData));
		SortQueuedMeshDataByViewDistance();
		NotifyMeshComponentsUpdatedIfReady();
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

UDualContourMeshComponent* ADualContourMeshActor::CreateMeshComponent()
{
	UDualContourMeshComponent* NewComponent = NewObject<UDualContourMeshComponent>(this, NAME_None, RF_Transactional);
	NewComponent->SetMaterial(0, MeshMaterial);
	ApplyCollisionSettings(NewComponent);
	NewComponent->RegisterComponent();
	NewComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	return NewComponent;
}

void ADualContourMeshActor::QueueMeshData(int32 DivisionIndex, FDualContourMeshData&& MeshData)
{
	check(IsInGameThread());
	bMeshUpdateCompletionPending = true;
	const uint64 UpdateSerial = ++NextMeshUpdateSerial;
	DivisionUpdateSerials.Add(DivisionIndex, UpdateSerial);
	for (int32 PendingIndex = NextPendingMeshApplyIndex; PendingIndex < PendingMeshApplies.Num(); ++PendingIndex)
	{
		FPendingMeshApply& PendingApply = PendingMeshApplies[PendingIndex];
		if (PendingApply.DivisionIndex == DivisionIndex)
		{
			PendingApply.QueueRevision = MeshQueueRevision;
			PendingApply.UpdateSerial = UpdateSerial;
			PendingApply.MeshData = MoveTemp(MeshData);
			UpdateActorTickEnabled();
			return;
		}
	}

	FPendingMeshApply& PendingApply = PendingMeshApplies.AddDefaulted_GetRef();
	PendingApply.DivisionIndex = DivisionIndex;
	PendingApply.QueueRevision = MeshQueueRevision;
	PendingApply.UpdateSerial = UpdateSerial;
	PendingApply.MeshData = MoveTemp(MeshData);
	UpdateActorTickEnabled();
}

void ADualContourMeshActor::SortQueuedMeshDataByViewDistance()
{
	check(IsInGameThread());
	if (PendingMeshApplies.Num() - NextPendingMeshApplyIndex <= 1)
		return;

	// Discard entries already consumed by earlier frames before sorting the remaining work.
	if (NextPendingMeshApplyIndex > 0)
	{
		PendingMeshApplies.RemoveAt(0, NextPendingMeshApplyIndex);
		NextPendingMeshApplyIndex = 0;
	}

	UWorld* World = GetWorld();
	if (!World)
		return;

	TArray<FVector> ViewLocations = World->ViewLocationsRenderedLastFrame;
	if (ViewLocations.IsEmpty())
	{
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
				ViewLocations.Add(CameraManager->GetCameraLocation());
		}
	}
	if (ViewLocations.IsEmpty())
		return;

	const FTransform ActorTransform = GetActorTransform();
	for (FPendingMeshApply& PendingApply : PendingMeshApplies)
	{
		const FBox LocalBounds = PendingApply.MeshData.LocalBounds.IsValid
			                         ? PendingApply.MeshData.LocalBounds
			                         : FBox(FVector::ZeroVector, FVector::ZeroVector);
		const FBox WorldBounds = LocalBounds.TransformBy(ActorTransform);
		PendingApply.ViewDistanceSquared = MAX_dbl;
		for (const FVector& ViewLocation : ViewLocations)
		{
			PendingApply.ViewDistanceSquared = FMath::Min(
				PendingApply.ViewDistanceSquared, WorldBounds.ComputeSquaredDistanceToPoint(ViewLocation));
		}
	}

	PendingMeshApplies.Sort([](const FPendingMeshApply& Left, const FPendingMeshApply& Right)
	{
		if (Left.ViewDistanceSquared == Right.ViewDistanceSquared)
			return Left.DivisionIndex < Right.DivisionIndex;
		return Left.ViewDistanceSquared < Right.ViewDistanceSquared;
	});
}

void ADualContourMeshActor::CancelQueuedMeshData(int32 DivisionIndex)
{
	DivisionUpdateSerials.Add(DivisionIndex, ++NextMeshUpdateSerial);
	for (int32 PendingIndex = PendingMeshApplies.Num() - 1; PendingIndex >= NextPendingMeshApplyIndex; --PendingIndex)
	{
		if (PendingMeshApplies[PendingIndex].DivisionIndex == DivisionIndex)
			PendingMeshApplies.RemoveAt(PendingIndex);
	}

	if (NextPendingMeshApplyIndex >= PendingMeshApplies.Num())
		ResetQueuedMeshData();
}

void ADualContourMeshActor::NotifyMeshComponentsUpdatedIfReady()
{
	if (!bMeshUpdateCompletionPending || NextPendingMeshApplyIndex < PendingMeshApplies.Num())
		return;

	// Clear first so callbacks that enqueue another update start a new completion cycle.
	bMeshUpdateCompletionPending = false;
#if WITH_EDITOR
	const bool bImmediateDebugRefresh = bDebugRefreshImmediatelyAfterMeshUpdate;
	bDebugRefreshImmediatelyAfterMeshUpdate = false;
	RequestDebugComponentRefresh(bImmediateDebugRefresh);
#endif
	OnMeshComponentsUpdated.Broadcast();
}

void ADualContourMeshActor::ResetQueuedMeshData()
{
	PendingMeshApplies.Reset();
	NextPendingMeshApplyIndex = 0;
	UpdateActorTickEnabled();
}

void ADualContourMeshActor::UpdateActorTickEnabled()
{
	const bool bHasPendingMeshData = NextPendingMeshApplyIndex < PendingMeshApplies.Num();
#if WITH_EDITOR
	SetActorTickEnabled(bHasPendingMeshData || bDebugRefreshPending);
#else
	SetActorTickEnabled(bHasPendingMeshData);
#endif
}

void ADualContourMeshActor::ApplyQueuedMeshData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_ApplyQueuedMeshData);
	check(IsInGameThread());

	const int32 MaxApplyCount = FMath::Max(MeshComponentsPerFrame, 1);
	int32 AppliedCount = 0;
	while (AppliedCount < MaxApplyCount && NextPendingMeshApplyIndex < PendingMeshApplies.Num())
	{
		FPendingMeshApply PendingApply = MoveTemp(PendingMeshApplies[NextPendingMeshApplyIndex++]);
		TObjectPtr<UDualContourMeshComponent>* ExistingComponent = MeshComponents.Find(PendingApply.DivisionIndex);
		const bool bCreatedComponent = !ExistingComponent || !IsValid(ExistingComponent->Get());
		UDualContourMeshComponent* MeshComponent = bCreatedComponent ? CreateMeshComponent() : ExistingComponent->Get();
		MeshComponent->ApplyMeshData(MoveTemp(PendingApply.MeshData), !bDensityEditInProgress);
		++AppliedCount;

		const bool bIsStillCurrent = PendingApply.QueueRevision == MeshQueueRevision
		                             && DivisionUpdateSerials.FindRef(PendingApply.DivisionIndex) == PendingApply.UpdateSerial;
		if (bIsStillCurrent && IsValid(MeshComponent))
			MeshComponents.Add(PendingApply.DivisionIndex, MeshComponent);
		else if (bCreatedComponent && IsValid(MeshComponent))
			MeshComponent->DestroyComponent();
	}

	if (NextPendingMeshApplyIndex >= PendingMeshApplies.Num())
	{
		ResetQueuedMeshData();
		NotifyMeshComponentsUpdatedIfReady();
	}
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
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_PartialUpdateComponents);
	if (!DualContour || !HasValidDivisions())
		return;

	AffectedCellMin = FVectorInt(
		FMath::Clamp(AffectedCellMin.X, 0, DualContour->CellCount.X),
		FMath::Clamp(AffectedCellMin.Y, 0, DualContour->CellCount.Y),
		FMath::Clamp(AffectedCellMin.Z, 0, DualContour->CellCount.Z));
	AffectedCellMax = FVectorInt(
		FMath::Clamp(AffectedCellMax.X, 0, DualContour->CellCount.X),
		FMath::Clamp(AffectedCellMax.Y, 0, DualContour->CellCount.Y),
		FMath::Clamp(AffectedCellMax.Z, 0, DualContour->CellCount.Z));
	if (AffectedCellMin.X >= AffectedCellMax.X || AffectedCellMin.Y >= AffectedCellMax.Y
	    || AffectedCellMin.Z >= AffectedCellMax.Z)
		return;

	TArray<int32> AffectedDivisions;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_CollectAffectedDivisions);

		// A division owns [CellMin, CellMax), but mesh generation reads the positive-axis
		// neighbor ring. Expanding the changed range by one cell toward the negative axes
		// finds every possible owner without scanning every affected cell.
		const FVectorInt OwnerCellMin(
			FMath::Max(0, AffectedCellMin.X - 1),
			FMath::Max(0, AffectedCellMin.Y - 1),
			FMath::Max(0, AffectedCellMin.Z - 1));
		const FVectorInt LastAffectedCell(
			AffectedCellMax.X - 1,
			AffectedCellMax.Y - 1,
			AffectedCellMax.Z - 1);
		const FVectorInt DivisionMin = DivisionFromCell(OwnerCellMin.X, OwnerCellMin.Y, OwnerCellMin.Z);
		const FVectorInt DivisionMax = DivisionFromCell(LastAffectedCell.X, LastAffectedCell.Y, LastAffectedCell.Z);
		const int64 CandidateDivisionCount = static_cast<int64>(DivisionMax.X - DivisionMin.X + 1)
		                                     * (DivisionMax.Y - DivisionMin.Y + 1) * (DivisionMax.Z - DivisionMin.Z + 1);
		if (CandidateDivisionCount <= MAX_int32)
			AffectedDivisions.Reserve(static_cast<int32>(CandidateDivisionCount));

		for (int32 DivisionZ = DivisionMin.Z; DivisionZ <= DivisionMax.Z; ++DivisionZ)
			for (int32 DivisionY = DivisionMin.Y; DivisionY <= DivisionMax.Y; ++DivisionY)
				for (int32 DivisionX = DivisionMin.X; DivisionX <= DivisionMax.X; ++DivisionX)
					AffectedDivisions.Add(DivisionIndex(DivisionX, DivisionY, DivisionZ));
	}

	TArray<int32> DivisionsToRemove;
	TArray<FMeshBuildRequest> Requests;
	Requests.Reserve(AffectedDivisions.Num());
	for (const int32 AffectedDivisionIndex : AffectedDivisions)
	{
		const int32 DivisionX = AffectedDivisionIndex % Divisions.X;
		const int32 DivisionY = (AffectedDivisionIndex / Divisions.X) % Divisions.Y;
		const int32 DivisionZ = AffectedDivisionIndex / (Divisions.X * Divisions.Y);
		if (DivisionX < 0 || DivisionX >= Divisions.X || DivisionY < 0
		    || DivisionY >= Divisions.Y || DivisionZ < 0 || DivisionZ >= Divisions.Z)
			continue;

		const FVectorInt CellMin = DivisionCellMin(DivisionX, DivisionY, DivisionZ);
		const FVectorInt CellMax = DivisionCellMax(DivisionX, DivisionY, DivisionZ);
		if (!DualContour->HasActiveCellInRange(CellMin, CellMax))
		{
			DivisionsToRemove.Add(AffectedDivisionIndex);
			continue;
		}

		FMeshBuildRequest& Request = Requests.AddDefaulted_GetRef();
		Request.DivisionIndex = AffectedDivisionIndex;
		Request.CellMin = CellMin;
		Request.CellMax = CellMax;
	}
	BuildMeshRequests(*DualContour, Requests);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_QueuePartialComponents);
		if (!DivisionsToRemove.IsEmpty() || !Requests.IsEmpty())
			bMeshUpdateCompletionPending = true;
		for (const int32 DivisionIndexToRemove : DivisionsToRemove)
		{
			CancelQueuedMeshData(DivisionIndexToRemove);
			if (TObjectPtr<UDualContourMeshComponent>* ExistingComponent = MeshComponents.Find(DivisionIndexToRemove))
				if (*ExistingComponent)
					(*ExistingComponent)->DestroyComponent();
			MeshComponents.Remove(DivisionIndexToRemove);
		}

		for (FMeshBuildRequest& Request : Requests)
			QueueMeshData(Request.DivisionIndex, MoveTemp(Request.MeshData));
		SortQueuedMeshDataByViewDistance();
		NotifyMeshComponentsUpdatedIfReady();
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

void ADualContourMeshActor::SetDensityEditInProgress(bool bInProgress)
{
	if (bDensityEditInProgress == bInProgress)
		return;
	bDensityEditInProgress = bInProgress;
	if (!bDensityEditInProgress)
		for (const TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
			if (IsValid(Pair.Value))
				Pair.Value->RefreshCollision();
}
