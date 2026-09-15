#include "DualContourMeshActor.h"
#include "DualContourMeshBuilder.h"
#include "DualContourRuntimeSaveGame.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"
#include "DualContourEditContext.h"
#include "VolumeSampler/VolumeSampler.h"

DEFINE_LOG_CATEGORY_STATIC(LogDualContourMesh, Log, All);

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

void ADualContourMeshActor::RefreshMeshMaterial()
{
	for (TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
		if (Pair.Value)
			Pair.Value->SetMaterial(0, MeshMaterial);
}

void ADualContourMeshActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	BindToDualContour();
	RefreshCollisionSettings();

#if WITH_EDITOR
	if (bRestoreMeshAfterLoad && GetWorld() && GetWorld()->WorldType == EWorldType::Editor && !IsTemplate())
	{
		bRestoreMeshAfterLoad = false;
		if (DualContour && DualContour->HasCurrentGeneratedData())
		{
			// DualContour is an instanced default subobject, so its serialized density grid is
			// already restored at this point. Recreate only the transient render components;
			// rebuilding from InitialDualContour here would discard saved editor strokes.
			RecreateMeshComponents();
		}
		else if (InitialDualContour)
		{
			// Older or newly placed actors may not contain a valid saved contour yet.
			ResetDualContour();
		}
	}

	if (DualContour && DualContour->HasCurrentGeneratedData())
		RefreshDebugComponent();
#endif
}

void ADualContourMeshActor::BeginPlay()
{
	Super::BeginPlay();

	// A placed actor can contain an editor-saved density grid. Preserve it when entering play;
	// InitialDualContour is only the fallback for actors without valid serialized contour data.
	if (DualContour && DualContour->HasCurrentGeneratedData())
		RecreateMeshComponents();
	else if (InitialDualContour)
		ResetDualContour();
}

void ADualContourMeshActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ProcessPendingMeshUpdates();
#if WITH_EDITOR
	ProcessPendingDebugComponentRefresh();
#endif
}

void ADualContourMeshActor::ProcessPendingMeshUpdates()
{
	// This is intentionally a game-thread-only entry point. It is also called by the
	// editor toolkit ticker because an inactive editor viewport may stop ticking its
	// preview world while background generation is still running.
	check(IsInGameThread());
	ApplyQueuedMeshData();
}

void ADualContourMeshActor::BuildMeshRequests(const UDualContour& InDualContour, TArray<FMeshBuildRequest>& Requests,
	const std::atomic<bool>* bAbortFlag)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_BuildMeshRequests);
	ParallelFor(TEXT("DualContourMesh.BuildDivisions"), Requests.Num(), 1,
		[&InDualContour, &Requests, bAbortFlag](int32 RequestIndex)
		{
			// Aborted builds keep remaining divisions empty; the revision check discards them later.
			if (bAbortFlag && bAbortFlag->load(std::memory_order_relaxed))
				return;

			FMeshBuildRequest& Request = Requests[RequestIndex];
			FDualContourMeshBuilder::Build(InDualContour, Request.CellMin, Request.CellMax, Request.MeshData);
		}, EParallelForFlags::Unbalanced);
}

bool ADualContourMeshActor::IsMeshInitializationPending() const
{
	return (DualContour && DualContour->IsCellRebuildPending()) || ActiveMeshBuild.IsValid() || bMeshUpdateCompletionPending;
}

void ADualContourMeshActor::FlushPendingMeshWork()
{
	AbortActiveMeshBuild();
}

void ADualContourMeshActor::AbortActiveMeshBuild()
{
	if (ActiveMeshBuild.IsValid())
		ActiveMeshBuild->bAborted.store(true, std::memory_order_relaxed);

	if (PendingMeshBuildFuture.IsValid())
	{
		// Workers observe the abort flag between requests, so this join only waits for in-flight chunks.
		PendingMeshBuildFuture.Get();
		PendingMeshBuildFuture = TFuture<void>();
	}
	ActiveMeshBuild.Reset();
}

void ADualContourMeshActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AbortActiveMeshBuild();
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
	bRestoreMeshAfterLoad = true;
}

void ADualContourMeshActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!PropertyChangedEvent.MemberProperty)
	{
		// Undo/redo restores serialized properties only; re-sync chunk overrides with MeshMaterial.
		RefreshMeshMaterial();
		RefreshCollisionSettings();
		return;
	}

	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty->GetFName();
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, InitialDualContour))
	{
		ResetDualContour();
	}
	else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, Divisions)
	         || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, bAutoCalculateDivisions)
	         || MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, MaxCellsPerDivision))
	{
		RecreateMeshComponents();
	}
	else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(ADualContourMeshActor, MeshMaterial))
	{
		RefreshMeshMaterial();
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
	AbortActiveMeshBuild();
	if (!DualContour || !DualContour->Rebuild())
		return;
	RecreateMeshComponents();
}

void ADualContourMeshActor::ResetDualContour()
{
	TGuardValue<bool> RebuildingMeshGuard(bRebuildingMesh, true);
	if (!InitialDualContour)
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("DualContour reset aborted for %s because InitialDualContour is missing."), *GetName());
		return;
	}

	if (!DualContour)
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("DualContour reset aborted for %s because its target DualContour is missing."), *GetName());
		return;
	}

	AbortActiveMeshBuild();
	if (!DualContour->Initialize(InitialDualContour))
	{
		UE_LOG(LogDualContourMesh, Error,
			TEXT("DualContour reset aborted for %s because InitialDualContour is missing current generated data."), *GetName());
		return;
	}

	RecreateMeshComponents();
}

bool ADualContourMeshActor::SaveRuntimeDensityIncrement(const FString& SlotName, int32 UserIndex) const
{
	if (SlotName.IsEmpty() || UserIndex < 0 || !InitialDualContour || !DualContour
	    || !InitialDualContour->HasCurrentGeneratedData() || !DualContour->HasCurrentGeneratedData())
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("Runtime density save failed for %s because its slot, user index, InitialDualContour, or runtime data is invalid."),
			*GetName());
		return false;
	}
	if (InitialDualContour->CellCount != DualContour->CellCount)
	{
		UE_LOG(LogDualContourMesh, Warning, TEXT("Runtime density save failed for %s because InitialDualContour and DualContour dimensions differ."),
			*GetName());
		return false;
	}

	TStrongObjectPtr<UDualContourRuntimeSaveGame> SaveGame(
		Cast<UDualContourRuntimeSaveGame>(UGameplayStatics::CreateSaveGameObject(UDualContourRuntimeSaveGame::StaticClass())));
	if (!SaveGame.IsValid())
		return false;

	SaveGame->BaseDualContourPath = FSoftObjectPath(InitialDualContour);
	SaveGame->BaseCellCount = InitialDualContour->CellCount;
	SaveGame->DensityChunks = DualContour->GetModifiedDensityChunks();
	SaveGame->MaterialChunks = DualContour->GetModifiedMaterialChunks();

	const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame.Get(), SlotName, UserIndex);
	if (bSaved)
	{
		UE_LOG(LogDualContourMesh, Log, TEXT("Runtime density save completed for %s: slot '%s', user %d, %d modified chunks."),
			*GetName(), *SlotName, UserIndex, SaveGame->DensityChunks.Num());
	}
	else
	{
		UE_LOG(LogDualContourMesh, Error, TEXT("Runtime density save failed for %s: slot '%s', user %d."),
			*GetName(), *SlotName, UserIndex);
	}
	return bSaved;
}

bool ADualContourMeshActor::LoadRuntimeDensityIncrement(const FString& SlotName, int32 UserIndex)
{
	if (SlotName.IsEmpty() || UserIndex < 0 || !InitialDualContour || !DualContour
	    || !InitialDualContour->HasCurrentGeneratedData())
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("Runtime density load failed for %s because its slot, user index, InitialDualContour, or runtime target is invalid."),
			*GetName());
		return false;
	}

	TStrongObjectPtr<USaveGame> LoadedObject(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	const UDualContourRuntimeSaveGame* SaveGame = Cast<UDualContourRuntimeSaveGame>(LoadedObject.Get());
	if (!SaveGame)
	{
		UE_LOG(LogDualContourMesh, Warning, TEXT("Runtime density load failed for %s: slot '%s' is missing or incompatible."),
			*GetName(), *SlotName);
		return false;
	}
	if (SaveGame->BaseCellCount != InitialDualContour->CellCount
	    || (!SaveGame->BaseDualContourPath.IsNull() && SaveGame->BaseDualContourPath != FSoftObjectPath(InitialDualContour)))
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("Runtime density load failed for %s because the save was created from a different InitialDualContour."), *GetName());
		return false;
	}

	TGuardValue<bool> RebuildingMeshGuard(bRebuildingMesh, true);
	AbortActiveMeshBuild();
	if (!DualContour->Initialize(InitialDualContour, &SaveGame->DensityChunks, &SaveGame->MaterialChunks))
	{
		RecreateMeshComponents();
		UE_LOG(LogDualContourMesh, Error,
			TEXT("Runtime density load failed for %s while initializing from InitialDualContour and slot '%s'."), *GetName(), *SlotName);
		return false;
	}
	RecreateMeshComponents();

	UE_LOG(LogDualContourMesh, Log,
		TEXT("Runtime density load completed for %s: slot '%s', user %d, %d modified chunks."),
		*GetName(), *SlotName, UserIndex, SaveGame->DensityChunks.Num());
	return true;
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
	if (!DualContour)
		return;
	if (!DualContourCellsRebuiltHandle.IsValid())
		DualContourCellsRebuiltHandle = DualContour->OnCellsRebuilt.AddUObject(this, &ADualContourMeshActor::OnDualContourCellsRebuilt);
	if (!DualContourMaterialsChangedHandle.IsValid())
		DualContourMaterialsChangedHandle = DualContour->OnMaterialsChanged.AddUObject(this, &ADualContourMeshActor::OnDualContourMaterialsChanged);
}

void ADualContourMeshActor::UnbindFromDualContour()
{
	if (DualContour && DualContourCellsRebuiltHandle.IsValid())
	{
		DualContour->OnCellsRebuilt.Remove(DualContourCellsRebuiltHandle);
		DualContourCellsRebuiltHandle.Reset();
	}
	if (DualContour && DualContourMaterialsChangedHandle.IsValid())
	{
		DualContour->OnMaterialsChanged.Remove(DualContourMaterialsChangedHandle);
		DualContourMaterialsChangedHandle.Reset();
	}
}

void ADualContourMeshActor::OnDualContourCellsRebuilt(FIntVector AffectedCellMin, FIntVector AffectedCellMax)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_OnDualContourCellsRebuilt);
	if (bRebuildingMesh)
		return;
	const bool bFullGridRebuild = AffectedCellMin == FIntVector::ZeroValue && AffectedCellMax == DualContour->CellCount;
	if (bFullGridRebuild || MeshCellCount.X != DualContour->CellCount.X || MeshCellCount.Y != DualContour->CellCount.Y
	    || MeshCellCount.Z != DualContour->CellCount.Z || MeshCellSize != DualContour->CellSize)
	{
		RecreateMeshComponents();
		return;
	}

	PartialUpdateComponents(AffectedCellMin, AffectedCellMax);
}

void ADualContourMeshActor::OnDualContourMaterialsChanged(FIntVector AffectedCellMin, FIntVector AffectedCellMax)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_OnDualContourMaterialsChanged);
	if (!bRebuildingMesh)
		PartialUpdateComponents(AffectedCellMin, AffectedCellMax, false);
}

void ADualContourMeshActor::RecreateMeshComponents()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_RecreateMeshComponents);
	if (!DualContour || !DualContour->HasCurrentGeneratedData())
		return;

	// Cells may still be rebuilding in the background (e.g. dispatched by PostLoad after loading).
	// Game worlds resume through OnCellsRebuilt; editor worlds keep the previous blocking
	// behavior because their world Tick is not guaranteed to run.
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && DualContour->IsCellRebuildPending())
		return;

	// Supersede any in-flight build before touching shared contour state.
	AbortActiveMeshBuild();
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
	Requests.Reserve(Divisions.X * Divisions.Y * Divisions.Z);
	for (int32 DivisionZ = 0; DivisionZ < Divisions.Z; ++DivisionZ)
		for (int32 DivisionY = 0; DivisionY < Divisions.Y; ++DivisionY)
			for (int32 DivisionX = 0; DivisionX < Divisions.X; ++DivisionX)
			{
				const FIntVector CellMin = DivisionCellMin(DivisionX, DivisionY, DivisionZ);
				const FIntVector CellMax = DivisionCellMax(DivisionX, DivisionY, DivisionZ);
				if (!DualContour->HasActiveCellInRange(CellMin, CellMax))
					continue;

				FMeshBuildRequest& Request = Requests.AddDefaulted_GetRef();
				Request.DivisionIndex = DivisionIndex(DivisionX, DivisionY, DivisionZ);
				Request.CellMin = CellMin;
				Request.CellMax = CellMax;
			}

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

		const TSharedPtr<FAsyncMeshBuild> Build = MakeShared<FAsyncMeshBuild>();
		Build->DualContour = TStrongObjectPtr<UDualContour>(DualContour.Get());
		Build->Requests = MoveTemp(Requests);
		Build->Revision = MeshQueueRevision;
		ActiveMeshBuild = Build;

		TWeakObjectPtr<ADualContourMeshActor> WeakThis(this);
		PendingMeshBuildFuture = Async(EAsyncExecution::ThreadPool, [WeakThis, Build]()
		{
			BuildMeshRequests(*Build->DualContour, Build->Requests, &Build->bAborted);

			AsyncTask(ENamedThreads::GameThread, [WeakThis, Build]()
			{
				ADualContourMeshActor* Actor = WeakThis.Get();
				if (!Actor || Build != Actor->ActiveMeshBuild)
					return;

				// Leave PendingMeshBuildFuture alone: the worker may still be unwinding after enqueuing this
				// callback, and a completed future is a no-op for the next AbortActiveMeshBuild join.
				Actor->ActiveMeshBuild.Reset();
				if (Build->bAborted.load(std::memory_order_relaxed) || Build->Revision != Actor->MeshQueueRevision)
					return;

				for (FMeshBuildRequest& Request : Build->Requests)
					Actor->QueueMeshData(Request.DivisionIndex, MoveTemp(Request.MeshData));
				Actor->SortQueuedMeshDataByViewDistance();
				Actor->NotifyMeshComponentsUpdatedIfReady();
			});
		});
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
	Divisions = FIntVector(
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

void ADualContourMeshActor::QueueMeshData(int32 DivisionIndex, FDualContourMeshData&& MeshData, bool bUpdateCollision)
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
			PendingApply.bUpdateCollision |= bUpdateCollision;
			UpdateActorTickEnabled();
			return;
		}
	}

	FPendingMeshApply& PendingApply = PendingMeshApplies.AddDefaulted_GetRef();
	PendingApply.DivisionIndex = DivisionIndex;
	PendingApply.QueueRevision = MeshQueueRevision;
	PendingApply.UpdateSerial = UpdateSerial;
	PendingApply.MeshData = MoveTemp(MeshData);
	PendingApply.bUpdateCollision = bUpdateCollision;
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
	// An async build still in flight means its queued data has not been produced yet.
	if (!bMeshUpdateCompletionPending || ActiveMeshBuild.IsValid() || NextPendingMeshApplyIndex < PendingMeshApplies.Num())
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
		MeshComponent->ApplyMeshData(MoveTemp(PendingApply.MeshData),
			(bCreatedComponent || PendingApply.bUpdateCollision)
			&& (!bDensityEditInProgress || bUpdateCollisionDuringDensityEdit));
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

FIntVector ADualContourMeshActor::DivisionFromCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	const auto GetAxisDivision = [](int32 Cell, int32 DivisionCount, int32 CellCount)
	{
		const int64 Numerator = (static_cast<int64>(Cell) + 1) * DivisionCount - 1;
		return FMath::Clamp(static_cast<int32>(Numerator / CellCount), 0, DivisionCount - 1);
	};
	return FIntVector(
		GetAxisDivision(CellX, Divisions.X, DualContour->CellCount.X),
		GetAxisDivision(CellY, Divisions.Y, DualContour->CellCount.Y),
		GetAxisDivision(CellZ, Divisions.Z, DualContour->CellCount.Z));
}

FIntVector ADualContourMeshActor::DivisionCellMin(int32 DivX, int32 DivY, int32 DivZ) const
{
	return FIntVector(
		static_cast<int32>(static_cast<int64>(DivX) * DualContour->CellCount.X / Divisions.X),
		static_cast<int32>(static_cast<int64>(DivY) * DualContour->CellCount.Y / Divisions.Y),
		static_cast<int32>(static_cast<int64>(DivZ) * DualContour->CellCount.Z / Divisions.Z));
}

FIntVector ADualContourMeshActor::DivisionCellMax(int32 DivX, int32 DivY, int32 DivZ) const
{
	return FIntVector(
		static_cast<int32>((static_cast<int64>(DivX) + 1) * DualContour->CellCount.X / Divisions.X),
		static_cast<int32>((static_cast<int64>(DivY) + 1) * DualContour->CellCount.Y / Divisions.Y),
		static_cast<int32>((static_cast<int64>(DivZ) + 1) * DualContour->CellCount.Z / Divisions.Z));
}

void ADualContourMeshActor::PartialUpdateComponents(FIntVector AffectedCellMin, FIntVector AffectedCellMax, bool bUpdateCollision)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_PartialUpdateComponents);
	if (!DualContour || !HasValidDivisions())
		return;

	AffectedCellMin = FIntVector(
		FMath::Clamp(AffectedCellMin.X, 0, DualContour->CellCount.X),
		FMath::Clamp(AffectedCellMin.Y, 0, DualContour->CellCount.Y),
		FMath::Clamp(AffectedCellMin.Z, 0, DualContour->CellCount.Z));
	AffectedCellMax = FIntVector(
		FMath::Clamp(AffectedCellMax.X, 0, DualContour->CellCount.X),
		FMath::Clamp(AffectedCellMax.Y, 0, DualContour->CellCount.Y),
		FMath::Clamp(AffectedCellMax.Z, 0, DualContour->CellCount.Z));
	if (AffectedCellMin.X >= AffectedCellMax.X || AffectedCellMin.Y >= AffectedCellMax.Y
	    || AffectedCellMin.Z >= AffectedCellMax.Z)
		return;

	TSet<int32> AffectedDivisions;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_CollectAffectedDivisions);

		// QEF normals also read incident quads across division boundaries.
		constexpr int32 NormalHalo = 1;
		const FIntVector OwnerCellMin(
			FMath::Max(0, AffectedCellMin.X - 1 - NormalHalo),
			FMath::Max(0, AffectedCellMin.Y - 1 - NormalHalo),
			FMath::Max(0, AffectedCellMin.Z - 1 - NormalHalo));
		const FIntVector LastAffectedCell(
			FMath::Min(DualContour->CellCount.X - 1, AffectedCellMax.X - 1 + NormalHalo),
			FMath::Min(DualContour->CellCount.Y - 1, AffectedCellMax.Y - 1 + NormalHalo),
			FMath::Min(DualContour->CellCount.Z - 1, AffectedCellMax.Z - 1 + NormalHalo));
		const FIntVector DivisionMin = DivisionFromCell(OwnerCellMin.X, OwnerCellMin.Y, OwnerCellMin.Z);
		const FIntVector DivisionMax = DivisionFromCell(LastAffectedCell.X, LastAffectedCell.Y, LastAffectedCell.Z);
		const int64 CandidateDivisionCount = static_cast<int64>(DivisionMax.X - DivisionMin.X + 1)
		                                     * (DivisionMax.Y - DivisionMin.Y + 1) * (DivisionMax.Z - DivisionMin.Z + 1);
		if (CandidateDivisionCount <= MAX_int32)
			AffectedDivisions.Reserve(static_cast<int32>(CandidateDivisionCount));

		for (int32 DivisionZ = DivisionMin.Z; DivisionZ <= DivisionMax.Z; ++DivisionZ)
			for (int32 DivisionY = DivisionMin.Y; DivisionY <= DivisionMax.Y; ++DivisionY)
				for (int32 DivisionX = DivisionMin.X; DivisionX <= DivisionMax.X; ++DivisionX)
					AffectedDivisions.Add(DivisionIndex(DivisionX, DivisionY, DivisionZ));
	}
	UpdateMeshDivisions(AffectedDivisions, bUpdateCollision);
}

void ADualContourMeshActor::UpdateMeshDivisions(const TSet<int32>& AffectedDivisions, bool bUpdateCollision)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_UpdateMeshDivisions);
	if (!DualContour || !HasValidDivisions() || AffectedDivisions.IsEmpty())
		return;

	if (bDensityEditInProgress && !bUpdateCollisionDuringDensityEdit && bUpdateCollision)
		DensityEditDirtyDivisions.Append(AffectedDivisions);

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

		const FIntVector CellMin = DivisionCellMin(DivisionX, DivisionY, DivisionZ);
		const FIntVector CellMax = DivisionCellMax(DivisionX, DivisionY, DivisionZ);
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
	BuildMeshRequests(*DualContour, Requests, nullptr);

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
			QueueMeshData(Request.DivisionIndex, MoveTemp(Request.MeshData), bUpdateCollision);
		SortQueuedMeshDataByViewDistance();
		NotifyMeshComponentsUpdatedIfReady();
	}
}

bool ADualContourMeshActor::ModifyDensityWithSampler(const FVector& WorldHitPos, const FVector& WorldHitNormal, UVolumeSampler* Sampler,
	const FVector& SamplingVolumeSize, bool bExcavate, const FVector& WorldEditDirection)
{
	if (!DualContour || !DualContour->HasCurrentGeneratedData())
	{
		UE_LOG(LogDualContourMesh, Warning,
			TEXT("Density edit ignored for %s because generation settings changed. Call RebuildMesh first."), *GetName());
		return false;
	}
	if (!Sampler || SamplingVolumeSize.ContainsNaN() || SamplingVolumeSize.GetMin() <= UE_SMALL_NUMBER)
	{
		UE_LOG(LogDualContourMesh, Warning, TEXT("Density edit ignored for %s because its sampler or sampling volume size is invalid."),
			*GetName());
		return false;
	}

	const FTransform& ActorTransform = GetActorTransform();
	const FVector LocalHitPosition = ActorTransform.InverseTransformPosition(WorldHitPos);
	const FVector LocalHitNormal = ActorTransform.InverseTransformVectorNoScale(WorldHitNormal).GetSafeNormal();
	if (!HasValidDivisions() || LocalHitNormal.IsNearlyZero())
		return false;
	FQuat SamplerRotation = FQuat::FindBetweenNormals(FVector::UpVector, LocalHitNormal);
	const FVector LocalEditDirection = ActorTransform.InverseTransformVectorNoScale(WorldEditDirection).GetSafeNormal();
	if (!LocalEditDirection.IsNearlyZero())
	{
		FVector LocalUp = LocalHitNormal;
		if (FMath::Abs(FVector::DotProduct(LocalEditDirection, LocalUp)) > 0.999f)
			LocalUp = FMath::Abs(LocalEditDirection.Z) < 0.999f ? FVector::UpVector : FVector::RightVector;
		SamplerRotation = FRotationMatrix::MakeFromXZ(LocalEditDirection, LocalUp).ToQuat();
	}
	const FTransform SamplerPivotTransform(SamplerRotation, LocalHitPosition);

	const EDualContourDensityOperation Operation = bExcavate
		                                               ? EDualContourDensityOperation::Difference
		                                               : EDualContourDensityOperation::Union;
	AbortActiveMeshBuild();
	FDualContourEditContext Edit(*DualContour);
	if (!Edit.ApplyDensity(Operation, *Sampler, SamplingVolumeSize, SamplerPivotTransform))
	{
		UE_LOG(LogDualContourMesh, Warning, TEXT("Density edit failed for %s."), *GetName());
		return false;
	}
	return Edit.Commit();
}

bool ADualContourMeshActor::ModifyMaterialWithSampler(const FVector& WorldHitPos, const FVector& WorldHitNormal,
	UVolumeSampler* Sampler, const FVector& SamplingVolumeSize, uint8 MaterialId, const FVector& WorldEditDirection)
{
	if (!DualContour || !DualContour->HasCurrentGeneratedData() || !Sampler
	    || SamplingVolumeSize.ContainsNaN() || SamplingVolumeSize.GetMin() <= UE_SMALL_NUMBER)
		return false;

	const FTransform& ActorTransform = GetActorTransform();
	const FVector LocalHitPosition = ActorTransform.InverseTransformPosition(WorldHitPos);
	const FVector LocalHitNormal = ActorTransform.InverseTransformVectorNoScale(WorldHitNormal).GetSafeNormal();
	if (!HasValidDivisions() || LocalHitNormal.IsNearlyZero())
		return false;

	FQuat SamplerRotation = FQuat::FindBetweenNormals(FVector::UpVector, LocalHitNormal);
	const FVector LocalEditDirection = ActorTransform.InverseTransformVectorNoScale(WorldEditDirection).GetSafeNormal();
	if (!LocalEditDirection.IsNearlyZero())
	{
		FVector LocalUp = LocalHitNormal;
		if (FMath::Abs(FVector::DotProduct(LocalEditDirection, LocalUp)) > 0.999f)
			LocalUp = FMath::Abs(LocalEditDirection.Z) < 0.999f ? FVector::UpVector : FVector::RightVector;
		SamplerRotation = FRotationMatrix::MakeFromXZ(LocalEditDirection, LocalUp).ToQuat();
	}
	const FTransform SamplerPivotTransform(SamplerRotation, LocalHitPosition);

	AbortActiveMeshBuild();
	FDualContourEditContext Edit(*DualContour);
	if (!Edit.ApplyMaterial(*Sampler, SamplingVolumeSize, MaterialId, SamplerPivotTransform))
		return false;
	return Edit.Commit();
}

void ADualContourMeshActor::SetDensityEditInProgress(bool bInProgress, bool bUpdateCollisionDuringEdit)
{
	if (bDensityEditInProgress == bInProgress)
	{
		if (!bInProgress || bUpdateCollisionDuringDensityEdit == bUpdateCollisionDuringEdit)
			return;

		bUpdateCollisionDuringDensityEdit = bUpdateCollisionDuringEdit;
		if (bUpdateCollisionDuringDensityEdit)
		{
			for (const int32 DivisionIndex : DensityEditDirtyDivisions)
				if (const TObjectPtr<UDualContourMeshComponent>* Component = MeshComponents.Find(DivisionIndex))
					if (IsValid(*Component))
						(*Component)->RefreshCollision();
			DensityEditDirtyDivisions.Reset();
		}
		return;
	}
	if (bInProgress)
		DensityEditDirtyDivisions.Reset();
	bDensityEditInProgress = bInProgress;
	bUpdateCollisionDuringDensityEdit = bInProgress && bUpdateCollisionDuringEdit;
	if (!bDensityEditInProgress)
	{
		for (const int32 DivisionIndex : DensityEditDirtyDivisions)
			if (const TObjectPtr<UDualContourMeshComponent>* Component = MeshComponents.Find(DivisionIndex))
				if (IsValid(*Component))
					(*Component)->RefreshCollision();
		DensityEditDirtyDivisions.Reset();
	}
}
