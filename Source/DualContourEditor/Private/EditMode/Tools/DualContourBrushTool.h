#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "DualContourTypes.h"
#include "DualContourEditorTypes.h"
#include "EditMode/Editing/DualContourEditChange.h"
#include "DualContourBrushTool.generated.h"

class ADualContourMeshActor;
class ADualContourMaterialBrushVolume;
class FPrimitiveDrawInterface;
class UDualContourEditModeSettings;
class UMaterialInterface;

UCLASS(Transient)
class UDualContourBrushToolBuilder final : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	void Initialize(UDualContourEditModeSettings* InSettings, ADualContourMeshActor* InTargetActor);
	void SetTargetActor(ADualContourMeshActor* InTargetActor) { TargetActor = InTargetActor; }
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

private:
	TWeakObjectPtr<UDualContourEditModeSettings> Settings;
	TWeakObjectPtr<ADualContourMeshActor> TargetActor;
};

UCLASS(Transient)
class UDualContourBrushTool final : public UInteractiveTool, public IClickDragBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld, UDualContourEditModeSettings* InSettings, ADualContourMeshActor* InTargetActor);
	void SetTargetActor(ADualContourMeshActor* InTargetActor);
	void SetMaterialRegionTransformMode(bool bEnabled);

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override { bHasHit = false; }

	/** Applies selected placement volumes as one undoable material edit. Returns the changed sample count. */
	int32 ApplyMaterialBrushVolumes(TConstArrayView<ADualContourMaterialBrushVolume*> BrushVolumes);

private:
	bool UpdateHit(const FRay& WorldRay, float* OutDistance = nullptr);
	bool UpdateSculptHitAlongNormal(const FVector& WorldPosition, const FVector& WorldNormal);
	bool ProjectBrushPointToSurface(const FVector& PlanePoint, float ProjectionHalfDepth, FVector& OutSurfacePoint) const;
	void DrawSurfaceProjectedFalloff(IToolsContextRenderAPI* RenderAPI, float Radius) const;
	void DrawSurfaceProjectedRing(FPrimitiveDrawInterface* PDI, float Radius, const FLinearColor& Color, float Thickness) const;
	bool BeginPendingBatch();
	bool ApplyStampAt(const FVector& WorldPosition, const FVector& WorldNormal, float TimeScale);
	bool ApplyStationarySculptStamp(float WorldDistance, float TimeScale);
	bool ApplyBrushStamp(FDualContourPendingBatch& Batch, const FDualContourBrushStamp& Stamp) const;
	bool ApplyMaterialBrushStamp(FDualContourPendingMaterialBatch& Batch, const FDualContourBrushStamp& Stamp) const;
	void ApplyPathTo(const FVector& WorldPosition, const FVector& WorldNormal);
	void FlushStroke(bool bFinalFlush);
	void FinishStroke(bool bCancel);
	FDualContourBrushStamp MakeStamp(const FVector& WorldPosition, const FVector& WorldNormal, float TimeScale) const;

	UPROPERTY()
	TObjectPtr<UDualContourEditModeSettings> Settings;

	UPROPERTY()
	TObjectPtr<ADualContourMeshActor> TargetActor;

	UPROPERTY()
	TObjectPtr<UWorld> TargetWorld;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BrushFalloffMaterial;

	FDualContourPendingBatch ActiveBatch;
	FDualContourPendingMaterialBatch ActiveMaterialBatch;
	TMap<FIntVector, FDualContourSampleDelta> StrokeDeltas;
	TMap<FIntVector, FDualContourMaterialSampleDelta> MaterialStrokeDeltas;
	FVector HitPosition = FVector::ZeroVector;
	FVector HitNormal = FVector::UpVector;
	FVector LastStampPosition = FVector::ZeroVector;
	FVector LastStampNormal = FVector::UpVector;
	FVector ClayPlaneOrigin = FVector::ZeroVector;
	FVector ClayPlaneNormal = FVector::UpVector;
	FVector FlattenPlaneOrigin = FVector::ZeroVector;
	FVector FlattenPlaneNormal = FVector::UpVector;
	FVector ActiveRayOrigin = FVector::ZeroVector;
	FVector ActiveRayDirection = FVector::ForwardVector;
	FVector StrokeOrigin = FVector::ZeroVector;
	FVector StrokeNormal = FVector::UpVector;
	FVector StrokeGrowthDirection = FVector::UpVector;
	float FlattenWorldHeight = 0.0f;
	float StationarySculptDistance = 0.0f;
	float StationarySculptEmbedDepth = 0.0f;
	double LastPreviewFlushTime = 0.0;
	float StationaryAccumulator = 0.0f;
	bool bHasHit = false;
	bool bStrokeActive = false;
	bool bShiftDown = false;
	bool bFlattenHeightLocked = false;
	bool bHasActiveRay = false;
	bool bStationarySculptStroke = false;
	bool bStationarySculptSubtract = false;
	bool bStrokeMoved = false;
	bool bMaterialRegionTransformMode = false;

	static constexpr int32 ShiftModifierId = 1;
};
