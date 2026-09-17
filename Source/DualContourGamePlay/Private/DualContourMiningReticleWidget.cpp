#include "DualContourMiningReticleWidget.h"
#include "Rendering/DrawElements.h"

UDualContourMiningReticleWidget::UDualContourMiningReticleWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The reticle only draws; it must never eat mouse clicks aimed at the world.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UDualContourMiningReticleWidget::SetProgress(float InProgress)
{
	const float ClampedProgress = FMath::Clamp(InProgress, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(Progress, ClampedProgress))
	{
		Progress = ClampedProgress;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 UDualContourMiningReticleWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 PaintLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const FVector2f Center = FVector2f(AllottedGeometry.GetLocalSize()) * 0.5f;
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	if (DotRadius > 0.0f && DotColor.A > 0.0f)
	{
		// Overlapped closed polyline with a doubled thickness renders as a filled disc.
		TArray<FVector2f> DotPoints = MakeCirclePoints(Center, FMath::Max(DotRadius * 0.5f, 0.5f), 0.0f, 2.0f * PI, 12);
		FSlateDrawElement::MakeLines(OutDrawElements, PaintLayer, PaintGeometry, DotPoints,
			ESlateDrawEffect::None, DotColor, /*bAntialias=*/ true, /*Thickness=*/ DotRadius);
		++PaintLayer;
	}

	const float ClampedProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
	if (ClampedProgress > KINDA_SMALL_NUMBER && RingRadius > 0.0f && RingThickness > 0.0f)
	{
		constexpr int32 FullRingSegments = 48;

		const float SweepAngle = 2.0f * PI * ClampedProgress;
		const int32 ProgressSegments = FMath::Max(FMath::CeilToInt(FullRingSegments * ClampedProgress), 1);
		TArray<FVector2f> ProgressPoints = MakeCirclePoints(Center, RingRadius, -0.5f * PI, SweepAngle, ProgressSegments);
		FSlateDrawElement::MakeLines(OutDrawElements, PaintLayer, PaintGeometry, ProgressPoints,
			ESlateDrawEffect::None, RingProgressColor, /*bAntialias=*/ true, RingThickness);
		++PaintLayer;
	}

	return PaintLayer;
}

TArray<FVector2f> UDualContourMiningReticleWidget::MakeCirclePoints(const FVector2f& Center, float InRadius,
	float StartAngleRadians, float SweepAngleRadians, int32 NumSegments) const
{
	TArray<FVector2f> Points;
	if (NumSegments <= 0)
		return Points;

	Points.Reserve(NumSegments + 1);
	const float StepAngle = SweepAngleRadians / NumSegments;
	for (int32 Index = 0; Index <= NumSegments; ++Index)
	{
		const float Angle = StartAngleRadians + StepAngle * Index;
		Points.Emplace(Center.X + InRadius * FMath::Cos(Angle), Center.Y + InRadius * FMath::Sin(Angle));
	}
	return Points;
}
