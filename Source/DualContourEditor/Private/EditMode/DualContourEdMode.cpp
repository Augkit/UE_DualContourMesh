#include "EditMode/DualContourEdMode.h"

#include "EditMode/DualContourEdModeToolkit.h"
#include "EditMode/DualContourEditModeSettings.h"
#include "EditMode/Tools/DualContourBrushTool.h"
#include "DualContourMeshActor.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "InteractiveToolManager.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "DualContourEdMode"

const FEditorModeID UDualContourEdMode::EM_DualContourEdModeId = TEXT("EM_DualContourEdMode");
const FString UDualContourEdMode::BrushToolName = TEXT("DualContourBrushTool");

UDualContourEdMode::UDualContourEdMode()
{
	Settings = CreateDefaultSubobject<UDualContourEditModeSettings>(TEXT("DualContourEditModeSettings"));
	Settings->LoadConfig();
	Info = FEditorModeInfo(EM_DualContourEdModeId, LOCTEXT("ModeName", "Dual Contour"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.LandscapeMode"), TEXT("LevelEditor.LandscapeMode")), true);
}

void UDualContourEdMode::Enter()
{
	Super::Enter();
	BrushToolBuilder = NewObject<UDualContourBrushToolBuilder>(this);
	RefreshTarget();
	BrushToolBuilder->Initialize(Settings, TargetActor);
	GetToolManager()->RegisterToolType(BrushToolName, BrushToolBuilder);
	EnsureBrushToolActive();
}

void UDualContourEdMode::Exit()
{
	if (GetToolManager() && GetToolManager()->HasActiveTool(EToolSide::Left))
		GetToolManager()->DeactivateTool(EToolSide::Left, EToolShutdownType::Accept);
	if (Settings)
		Settings->SaveConfig();
	TargetActor = nullptr;
	BrushToolBuilder = nullptr;
	Super::Exit();
}

void UDualContourEdMode::SetActiveTool(EDualContourEditTool InTool)
{
	if (!Settings || Settings->ActiveTool == InTool)
		return;
	Settings->ActiveTool = InTool;
	Settings->SaveConfig();
	ActiveToolChanged.Broadcast();
}

void UDualContourEdMode::ActorSelectionChangeNotify()
{
	RefreshTarget();
	if (BrushToolBuilder)
		BrushToolBuilder->SetTargetActor(TargetActor);
	if (UDualContourBrushTool* Tool = GetToolManager() ? Cast<UDualContourBrushTool>(GetToolManager()->GetActiveTool(EToolSide::Left)) : nullptr)
		Tool->SetTargetActor(TargetActor);
	EnsureBrushToolActive();
}

void UDualContourEdMode::CreateToolkit()
{
	Toolkit = MakeShared<FDualContourEdModeToolkit>();
}

void UDualContourEdMode::EnsureBrushToolActive()
{
	UInteractiveToolManager* ToolManager = GetToolManager();
	if (!ToolManager || ToolManager->HasActiveTool(EToolSide::Left) || !TargetActor)
		return;
	if (ToolManager->SelectActiveToolType(EToolSide::Left, BrushToolName))
		ToolManager->ActivateTool(EToolSide::Left);
}

void UDualContourEdMode::RefreshTarget()
{
	TargetActor = nullptr;
	if (!GEditor)
	{
		TargetStatus = LOCTEXT("NoEditor", "Editor context is unavailable.");
		return;
	}
	if (GEditor->PlayWorld || GEditor->bIsSimulatingInEditor)
	{
		TargetStatus = LOCTEXT("PIE", "Editing is disabled during PIE or SIE.");
		return;
	}

	TArray<ADualContourMeshActor*> SelectedActors;
	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
		if (ADualContourMeshActor* Actor = Cast<ADualContourMeshActor>(*It))
			SelectedActors.Add(Actor);
	if (SelectedActors.IsEmpty())
	{
		TargetStatus = LOCTEXT("NoTarget", "Select one DualContourMeshActor.");
		return;
	}
	if (SelectedActors.Num() != 1)
	{
		TargetStatus = LOCTEXT("MultipleTargets", "Select only one DualContourMeshActor.");
		return;
	}

	ADualContourMeshActor* Candidate = SelectedActors[0];
	if (!Candidate->DualContour)
	{
		TargetStatus = LOCTEXT("NoData", "The selected actor has no runtime DualContour instance.");
		return;
	}
	if (!Candidate->DualContour->HasCurrentGeneratedData())
	{
		TargetStatus = LOCTEXT("Rebuild", "Generation settings changed. Run Rebuild Mesh before editing.");
		return;
	}
	FString DivisionStatus;
	if (!Candidate->ValidateDivisions(DivisionStatus))
	{
		TargetStatus = FText::FromString(DivisionStatus);
		return;
	}
	const FVector Scale = Candidate->GetActorScale3D().GetAbs();
	if (!FMath::IsNearlyEqual(Scale.X, Scale.Y) || !FMath::IsNearlyEqual(Scale.X, Scale.Z))
	{
		TargetStatus = LOCTEXT("NonUniformScale", "Non-uniform actor scale is not supported. Use a uniform scale before editing.");
		return;
	}
	TargetActor = Candidate;
	TargetStatus = FText::Format(LOCTEXT("Ready", "Ready: {0}  |  Rebuild Mesh replaces unsaved instance edits."),
		FText::FromString(Candidate->GetActorLabel()));
}

#undef LOCTEXT_NAMESPACE
