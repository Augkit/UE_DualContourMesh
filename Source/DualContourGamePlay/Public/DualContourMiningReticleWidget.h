#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DualContourMiningReticleWidget.generated.h"

/**
 * First-person mining reticle drawn with Slate paint elements only, so no UMG assets are needed.
 *
 * A center dot is always visible; while the dig button is held, a ring progress bar around
 * the dot fills clockwise from 12 o'clock as the hold duration accumulates.
 */
UCLASS()
class DUALCONTOURGAMEPLAY_API UDualContourMiningReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UDualContourMiningReticleWidget(const FObjectInitializer& ObjectInitializer);

	/** Dig hold progress in [0, 1]; zero hides the ring and leaves only the center dot. */
	UFUNCTION(BlueprintCallable, Category = "DualContour|Mining")
	void SetProgress(float InProgress);

	UFUNCTION(BlueprintPure, Category = "DualContour|Mining")
	float GetProgress() const { return Progress; }

	/** Radius of the center dot in screen units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "0.0"))
	float DotRadius = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining")
	FLinearColor DotColor = FLinearColor::White;

	/** Ring radius measured from the reticle center to the ring centerline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "0.0"))
	float RingRadius = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining", meta = (ClampMin = "1.0"))
	float RingThickness = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour|Mining")
	FLinearColor RingProgressColor = FLinearColor::White;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TArray<FVector2f> MakeCirclePoints(const FVector2f& Center, float InRadius, float StartAngleRadians, float SweepAngleRadians,
		int32 NumSegments) const;

	float Progress = 0.0f;
};
