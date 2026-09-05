#include "DualContourMaterialBrushVolume.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "DualContourMeshActor.h"

ADualContourMaterialBrushVolume::ADualContourMaterialBrushVolume()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	BoxPreview = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxPreview"));
	BoxPreview->SetupAttachment(SceneRoot);
	BoxPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoxPreview->SetGenerateOverlapEvents(false);
	BoxPreview->ShapeColor = FColor(40, 180, 255);
	BoxPreview->bDrawOnlyIfSelected = false;

	SpherePreview = CreateDefaultSubobject<USphereComponent>(TEXT("SpherePreview"));
	SpherePreview->SetupAttachment(SceneRoot);
	SpherePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpherePreview->SetGenerateOverlapEvents(false);
	SpherePreview->ShapeColor = FColor(40, 180, 255);
	SpherePreview->bDrawOnlyIfSelected = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	Spline->SetupAttachment(SceneRoot);
	Spline->SetClosedLoop(true);
	Spline->EditorUnselectedSplineSegmentColor = FLinearColor(0.05f, 0.55f, 1.0f);
	Spline->EditorSelectedSplineSegmentColor = FLinearColor(0.2f, 0.8f, 1.0f);

	bIsEditorOnlyActor = true;
	SetActorEnableCollision(false);
	UpdatePreviewComponents();
}

void ADualContourMaterialBrushVolume::Initialize(
	ADualContourMeshActor* InTargetActor,
	EDualContourMaterialBrushVolumeShape InShape,
	const FVector& InHalfSize)
{
	TargetActor = InTargetActor;
	Shape = InShape;
	BoxExtent = InHalfSize.ComponentMax(FVector(1.0));
	SphereRadius = FMath::Max(1.0, InHalfSize.GetMin());
	SplineHeight = FMath::Max(1.0, InHalfSize.Z * 2.0);

	if (Spline)
	{
		Spline->ClearSplinePoints(false);
		const FVector2D HalfSize(FMath::Max(1.0, InHalfSize.X), FMath::Max(1.0, InHalfSize.Y));
		Spline->AddSplinePoint(FVector(-HalfSize.X, -HalfSize.Y, 0.0), ESplineCoordinateSpace::Local, false);
		Spline->AddSplinePoint(FVector(HalfSize.X, -HalfSize.Y, 0.0), ESplineCoordinateSpace::Local, false);
		Spline->AddSplinePoint(FVector(HalfSize.X, HalfSize.Y, 0.0), ESplineCoordinateSpace::Local, false);
		Spline->AddSplinePoint(FVector(-HalfSize.X, HalfSize.Y, 0.0), ESplineCoordinateSpace::Local, false);
		for (int32 PointIndex = 0; PointIndex < Spline->GetNumberOfSplinePoints(); ++PointIndex)
			Spline->SetSplinePointType(PointIndex, ESplinePointType::Linear, false);
		Spline->SetClosedLoop(true, false);
		Spline->UpdateSpline();
	}
	UpdatePreviewComponents();
}

void ADualContourMaterialBrushVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePreviewComponents();
}

#if WITH_EDITOR
void ADualContourMaterialBrushVolume::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	BoxExtent = BoxExtent.ComponentMax(FVector(1.0));
	SphereRadius = FMath::Max(1.0f, SphereRadius);
	SplineHeight = FMath::Max(1.0f, SplineHeight);
	UpdatePreviewComponents();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ADualContourMaterialBrushVolume::PostEditUndo()
{
	Super::PostEditUndo();
	UpdatePreviewComponents();
}
#endif

void ADualContourMaterialBrushVolume::UpdatePreviewComponents()
{
	if (!BoxPreview || !SpherePreview || !Spline)
		return;

	BoxPreview->SetBoxExtent(BoxExtent.ComponentMax(FVector(1.0)));
	SpherePreview->SetSphereRadius(FMath::Max(1.0f, SphereRadius));
	BoxPreview->SetVisibility(Shape == EDualContourMaterialBrushVolumeShape::Box);
	SpherePreview->SetVisibility(Shape == EDualContourMaterialBrushVolumeShape::Sphere);
	Spline->SetVisibility(Shape == EDualContourMaterialBrushVolumeShape::SplinePrism);
	// The editor spline visualizer uses bDrawDebug, independently of visibility.
	Spline->SetDrawDebug(Shape == EDualContourMaterialBrushVolumeShape::SplinePrism);
	BoxPreview->SetHiddenInGame(Shape != EDualContourMaterialBrushVolumeShape::Box);
	SpherePreview->SetHiddenInGame(Shape != EDualContourMaterialBrushVolumeShape::Sphere);
	Spline->SetHiddenInGame(Shape != EDualContourMaterialBrushVolumeShape::SplinePrism);
#if WITH_EDITOR
	BoxPreview->bSelectable = Shape == EDualContourMaterialBrushVolumeShape::Box;
	SpherePreview->bSelectable = Shape == EDualContourMaterialBrushVolumeShape::Sphere;
	Spline->bSelectable = Shape == EDualContourMaterialBrushVolumeShape::SplinePrism;
#endif
	CachedSplinePolygon.Reset();
}

void ADualContourMaterialBrushVolume::CacheBrushGeometry() const
{
	if (Shape == EDualContourMaterialBrushVolumeShape::SplinePrism)
		GetSplinePolygon(CachedSplinePolygon);
}

void ADualContourMaterialBrushVolume::GetSplinePolygon(TArray<FVector2D>& OutPolygon) const
{
	OutPolygon.Reset();
	if (!Spline || Spline->GetNumberOfSplinePoints() < 3)
		return;

	const int32 SegmentCount = Spline->IsClosedLoop()
		? Spline->GetNumberOfSplinePoints()
		: Spline->GetNumberOfSplinePoints() - 1;
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const float StartKey = static_cast<float>(SegmentIndex);
		const float EndKey = static_cast<float>(SegmentIndex + 1);
		constexpr int32 SamplesPerSegment = 8;
		for (int32 SampleIndex = 0; SampleIndex < SamplesPerSegment; ++SampleIndex)
		{
			const float Key = FMath::Lerp(StartKey, EndKey,
				static_cast<float>(SampleIndex) / static_cast<float>(SamplesPerSegment));
			const FVector Position = Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::Local);
			OutPolygon.Emplace(Position.X, Position.Y);
		}
	}
}

bool ADualContourMaterialBrushVolume::IsInsidePolygon(const FVector2D& Point, TConstArrayView<FVector2D> Polygon)
{
	bool bInside = false;
	for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[Previous];
		const bool bCrosses = (A.Y > Point.Y) != (B.Y > Point.Y);
		if (bCrosses && Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
			bInside = !bInside;
	}
	return bInside;
}

bool ADualContourMaterialBrushVolume::EncompassesWorldPosition(const FVector& WorldPosition) const
{
	switch (Shape)
	{
		case EDualContourMaterialBrushVolumeShape::Box:
		{
			const FVector Local = BoxPreview->GetComponentTransform().InverseTransformPosition(WorldPosition);
			return FMath::Abs(Local.X) <= BoxExtent.X && FMath::Abs(Local.Y) <= BoxExtent.Y
				&& FMath::Abs(Local.Z) <= BoxExtent.Z;
		}
		case EDualContourMaterialBrushVolumeShape::Sphere:
		{
			const FVector Local = SpherePreview->GetComponentTransform().InverseTransformPosition(WorldPosition);
			return Local.SizeSquared() <= FMath::Square(SphereRadius);
		}
		case EDualContourMaterialBrushVolumeShape::SplinePrism:
		{
			const FVector Local = Spline->GetComponentTransform().InverseTransformPosition(WorldPosition);
			if (FMath::Abs(Local.Z) > SplineHeight * 0.5f)
				return false;
			if (CachedSplinePolygon.IsEmpty())
				GetSplinePolygon(CachedSplinePolygon);
			return CachedSplinePolygon.Num() >= 3
				&& IsInsidePolygon(FVector2D(Local.X, Local.Y), CachedSplinePolygon);
		}
		default:
			return false;
	}
}

FBox ADualContourMaterialBrushVolume::GetBrushWorldBounds() const
{
	if (Shape == EDualContourMaterialBrushVolumeShape::Box && BoxPreview)
		return BoxPreview->Bounds.GetBox();
	if (Shape == EDualContourMaterialBrushVolumeShape::Sphere && SpherePreview)
		return SpherePreview->Bounds.GetBox();

	if (CachedSplinePolygon.IsEmpty())
		GetSplinePolygon(CachedSplinePolygon);
	FBox Bounds(ForceInit);
	if (!Spline)
		return Bounds;
	for (const FVector2D& Point : CachedSplinePolygon)
	{
		Bounds += Spline->GetComponentTransform().TransformPosition(FVector(Point.X, Point.Y, -SplineHeight * 0.5f));
		Bounds += Spline->GetComponentTransform().TransformPosition(FVector(Point.X, Point.Y, SplineHeight * 0.5f));
	}
	return Bounds;
}
