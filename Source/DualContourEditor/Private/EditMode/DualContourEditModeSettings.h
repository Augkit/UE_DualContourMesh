#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "DualContourTypes.h"
#include "DualContourEditModeSettings.generated.h"

class UVolumeSampledDualContour;

UENUM()
enum class EDualContourEditTool : uint8
{
	Sculpt,
	Erase,
	Smooth,
	Brush,
	PaintMaterial,
};

/** Per-user settings shared by the active edit mode and its brush tool. */
UCLASS(Transient, Config = EditorPerProjectUserSettings)
class UDualContourEditModeSettings final : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Selected from the mode toolbar; intentionally hidden from the details panel. */
	UPROPERTY(Config)
	EDualContourEditTool ActiveTool = EDualContourEditTool::Sculpt;

	/** Blend strength for sculpting tools. Volume stamps always use an exact boolean operation. */
	UPROPERTY(EditAnywhere, Config, Category = "Tool Settings",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0",
			EditCondition = "ActiveTool != EDualContourEditTool::Brush && ActiveTool != EDualContourEditTool::PaintMaterial", EditConditionHides))
	float ToolStrength = 0.3f;

	UPROPERTY(EditAnywhere, Config, Category = "Material Paint",
		meta = (ClampMin = "0", ClampMax = "255", EditCondition = "ActiveTool == EDualContourEditTool::PaintMaterial", EditConditionHides))
	int32 PaintMaterialId = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Material Paint",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "ActiveTool == EDualContourEditTool::PaintMaterial", EditConditionHides))
	float MaterialPaintThreshold = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Material Paint",
		meta = (EditCondition = "ActiveTool == EDualContourEditTool::PaintMaterial", EditConditionHides))
	bool bPaintSolidSamplesOnly = true;

	UPROPERTY(EditAnywhere, Config, Category = "Brush Settings", meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "8192.0", Delta = "1.0"))
	float BrushSize = 200.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Brush Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float BrushFalloff = 0.5f;

	/** Selected with icon buttons above the details panel. */
	UPROPERTY(Config)
	EDualContourBrushShape BrushType = EDualContourBrushShape::Sphere;

	/** Selected with icon buttons above the details panel. */
	UPROPERTY(Config)
	EDualContourBrushFalloff BrushFalloffType = EDualContourBrushFalloff::Smooth;

	UPROPERTY(EditAnywhere, Config, Category = "Tool Settings",
		meta = (EditCondition = "ActiveTool == EDualContourEditTool::Sculpt || ActiveTool == EDualContourEditTool::Erase", EditConditionHides))
	bool bUseClayBrush = false;

	UPROPERTY(EditAnywhere, Config, Category = "Tool Settings",
		meta = (EditCondition = "ActiveTool != EDualContourEditTool::Brush && ActiveTool != EDualContourEditTool::PaintMaterial", EditConditionHides))
	bool bApplyWithoutMoving = true;

	/** Seconds between synchronous preview rebuilds while dragging. The final result always flushes immediately. */
	UPROPERTY(EditAnywhere, Config, Category = "Performance", meta = (ClampMin = "0.033", ClampMax = "0.5", UIMin = "0.033", UIMax = "0.2"))
	float PreviewUpdateInterval = 0.08f;

	UPROPERTY(EditAnywhere, Config, Category = "Brush Stamp",
		meta = (EditCondition = "ActiveTool == EDualContourEditTool::Brush", EditConditionHides))
	TSoftObjectPtr<UVolumeSampledDualContour> VolumeBrush;

	UPROPERTY(EditAnywhere, Config, Category = "Brush Stamp",
		meta = (EditCondition = "ActiveTool == EDualContourEditTool::Brush", EditConditionHides))
	bool bAlignVolumeBrushToSurface = true;

	UPROPERTY(EditAnywhere, Config, Category = "Brush Stamp",
		meta = (ClampMin = "0.001", EditCondition = "ActiveTool == EDualContourEditTool::Brush", EditConditionHides))
	float VolumeBrushScale = 1.0f;
};
