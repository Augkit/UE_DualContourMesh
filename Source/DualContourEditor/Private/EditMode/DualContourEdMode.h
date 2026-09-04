#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "DualContourEdMode.generated.h"

class ADualContourMeshActor;
class UDualContourBrushToolBuilder;
class UDualContourEditModeSettings;
enum class EDualContourEditTool : uint8;

UCLASS()
class UDualContourEdMode final : public UEdMode
{
	GENERATED_BODY()

public:
	static const FEditorModeID EM_DualContourEdModeId;
	static const FString BrushToolName;

	UDualContourEdMode();
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void ActorSelectionChangeNotify() override;
	virtual void CreateToolkit() override;

	UDualContourEditModeSettings* GetSettings() const { return Settings; }
	ADualContourMeshActor* GetTargetActor() const { return TargetActor; }
	FText GetTargetStatus() const { return TargetStatus; }
	bool HasValidTarget() const { return TargetActor != nullptr; }
	bool IsEditingPreviewActor() const { return bUseOverrideTarget; }
	void SetOverrideTargetActor(ADualContourMeshActor* InTargetActor);
	void SetActiveTool(EDualContourEditTool InTool);
	FSimpleMulticastDelegate& OnActiveToolChanged() { return ActiveToolChanged; }

private:
	void RefreshTarget();
	void EnsureBrushToolActive();

	UPROPERTY()
	TObjectPtr<UDualContourEditModeSettings> Settings;

	UPROPERTY()
	TObjectPtr<UDualContourBrushToolBuilder> BrushToolBuilder;

	UPROPERTY()
	TObjectPtr<ADualContourMeshActor> TargetActor;

	UPROPERTY()
	TObjectPtr<ADualContourMeshActor> OverrideTargetActor;

	bool bUseOverrideTarget = false;

	FText TargetStatus;
	FSimpleMulticastDelegate ActiveToolChanged;
};
