#include "DualContourDebugComponent.h"

#if WITH_EDITOR
#include "Async/Async.h"
#include "DualContourMeshActor.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"
#include "UObject/UObjectIterator.h"

namespace
{
void RefreshAllDualContourDebugVisualizations(IConsoleVariable* Variable);
}

static TAutoConsoleVariable<int32> CVarDrawDualContourMeshComponents(
	TEXT("DualContour.Debug.DrawMeshComponents"), 0,
	TEXT("Draw generated dual-contour mesh-component bounds with stable random colors (editor only).\n"
		"  0: off\n"
		"  1: draw mesh-component bounds"),
	ECVF_Default);

namespace
{
void RefreshAllDualContourDebugVisualizations(IConsoleVariable* Variable)
{
	if (!IsInGameThread())
	{
		AsyncTask(ENamedThreads::GameThread, [Variable]() { RefreshAllDualContourDebugVisualizations(Variable); });
		return;
	}

	for (TObjectIterator<UDualContourDebugComponent> It; It; ++It)
	{
		if (ADualContourMeshActor* Owner = Cast<ADualContourMeshActor>(It->GetOwner()))
			Owner->RefreshDebugVisualization();
	}
}

struct FDebugVisualizationConsoleHook
{
	FDebugVisualizationConsoleHook()
	{
		CVarDrawDualContourMeshComponents.AsVariable()->SetOnChangedCallback(
			FConsoleVariableDelegate::CreateStatic(&RefreshAllDualContourDebugVisualizations));
	}
};

FDebugVisualizationConsoleHook DebugVisualizationConsoleHook;

uint32 AvalancheHash(uint32 Value)
{
	Value ^= Value >> 16;
	Value *= 0x7feb352dU;
	Value ^= Value >> 15;
	Value *= 0x846ca68bU;
	Value ^= Value >> 16;
	return Value;
}

FColor MakeStableDebugColor(int32 DivisionIndex)
{
	// Mix every bit of the flattened division index, rather than using only its low byte.
	// The three HSV channels consume different hash bits so matching hues still differ visibly.
	const uint32 Hash = AvalancheHash(static_cast<uint32>(DivisionIndex) ^ 0x9e3779b9U);
	const uint8 Hue = static_cast<uint8>(Hash);
	const uint8 Saturation = static_cast<uint8>(224U + ((Hash >> 8) & 0x1fU));
	const uint8 Value = static_cast<uint8>(192U + ((Hash >> 14) & 0x3fU));
	return FLinearColor::MakeFromHSV8(Hue, Saturation, Value).ToFColorSRGB();
}

void DrawLocalBox(FPrimitiveDrawInterface* PDI, const FMatrix& LocalToWorld, const FBox& LocalBox, const FLinearColor& Color)
{
	const FVector& Min = LocalBox.Min;
	const FVector& Max = LocalBox.Max;
	const FVector Corners[] = {
		{Min.X, Min.Y, Min.Z}, {Max.X, Min.Y, Min.Z}, {Max.X, Max.Y, Min.Z}, {Min.X, Max.Y, Min.Z},
		{Min.X, Min.Y, Max.Z}, {Max.X, Min.Y, Max.Z}, {Max.X, Max.Y, Max.Z}, {Min.X, Max.Y, Max.Z},
	};
	constexpr int32 EdgeIndices[][2] = {
		{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
	};

	for (const int32* Edge : EdgeIndices)
	{
		PDI->DrawLine(
			LocalToWorld.TransformPosition(Corners[Edge[0]]),
			LocalToWorld.TransformPosition(Corners[Edge[1]]),
			Color, SDPG_World, 1.5f, 0.0f, true);
	}
}
}

class FDualContourDebugSceneProxy final : public FPrimitiveSceneProxy
{
public:
	explicit FDualContourDebugSceneProxy(const UDualContourDebugComponent* Component)
		: FPrimitiveSceneProxy(Component)
		  , Boxes(Component->MeshEntries) {}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!UDualContourDebugComponent::IsDrawEnabledOnAnyThread())
			return;

		const FMatrix& DebugLocalToWorld = GetLocalToWorld();
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if (!(VisibilityMap & (1u << ViewIndex)))
				continue;

			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
			PDI->AddReserveLines(SDPG_World, Boxes.Num() * 12, false, true);
			for (const UDualContourDebugComponent::FMeshEntry& Entry : Boxes)
				DrawLocalBox(PDI, DebugLocalToWorld, Entry.LocalBounds, FLinearColor(Entry.Color));
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View) && UDualContourDebugComponent::IsDrawEnabledOnAnyThread();
		Result.bDynamicRelevance = true;
		return Result;
	}

	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + Boxes.GetAllocatedSize(); }

	virtual SIZE_T GetTypeHash() const override
	{
		static SIZE_T UniquePointer;
		return reinterpret_cast<SIZE_T>(&UniquePointer);
	}

private:
	TArray<UDualContourDebugComponent::FMeshEntry> Boxes;
};

bool UDualContourDebugComponent::IsDrawEnabled()
{
	return CVarDrawDualContourMeshComponents.GetValueOnGameThread() != 0;
}

bool UDualContourDebugComponent::IsDrawEnabledOnAnyThread()
{
	return CVarDrawDualContourMeshComponents.GetValueOnAnyThread() != 0;
}

void UDualContourDebugComponent::UpdateFromMeshComponents(
	const TMap<int32, TObjectPtr<UDualContourMeshComponent>>& MeshComponents,
	FVectorInt CellCount, float CellSize, FVectorInt Divisions)
{
	MeshEntries.Reset();
	FBox TotalBox(ForceInit);
	if (CellSize <= 0.0f || Divisions.X <= 0 || Divisions.Y <= 0 || Divisions.Z <= 0)
	{
		CachedLocalBounds = FBoxSphereBounds(FVector::ZeroVector, FVector::ZeroVector, 0.f);
		return;
	}

	for (const TPair<int32, TObjectPtr<UDualContourMeshComponent>>& Pair : MeshComponents)
	{
		const UDualContourMeshComponent* MeshComponent = Pair.Value;
		if (!MeshComponent)
			continue;

		if (MeshComponent->GetMeshData().Positions.IsEmpty())
			continue;
		const int32 DivisionIndex = Pair.Key;
		const int32 DivisionX = DivisionIndex % Divisions.X;
		const int32 DivisionY = (DivisionIndex / Divisions.X) % Divisions.Y;
		const int32 DivisionZ = DivisionIndex / (Divisions.X * Divisions.Y);
		if (DivisionX < 0 || DivisionY < 0 || DivisionZ < 0 || DivisionZ >= Divisions.Z)
			continue;

		const FVectorInt CellMin(
			static_cast<int32>(static_cast<int64>(DivisionX) * CellCount.X / Divisions.X),
			static_cast<int32>(static_cast<int64>(DivisionY) * CellCount.Y / Divisions.Y),
			static_cast<int32>(static_cast<int64>(DivisionZ) * CellCount.Z / Divisions.Z));
		const FVectorInt CellMax(
			static_cast<int32>((static_cast<int64>(DivisionX) + 1) * CellCount.X / Divisions.X),
			static_cast<int32>((static_cast<int64>(DivisionY) + 1) * CellCount.Y / Divisions.Y),
			static_cast<int32>((static_cast<int64>(DivisionZ) + 1) * CellCount.Z / Divisions.Z));

		FMeshEntry& Entry = MeshEntries.AddDefaulted_GetRef();
		Entry.LocalBounds = FBox(FVector(CellMin.X, CellMin.Y, CellMin.Z) * CellSize,
			FVector(CellMax.X, CellMax.Y, CellMax.Z) * CellSize);
		Entry.Color = MakeStableDebugColor(Pair.Key);
		TotalBox += Entry.LocalBounds;
	}

	CachedLocalBounds = MeshEntries.IsEmpty()
		                    ? FBoxSphereBounds(FVector::ZeroVector, FVector::ZeroVector, 0.f)
		                    : FBoxSphereBounds(TotalBox);
}

#endif // WITH_EDITOR

FPrimitiveSceneProxy* UDualContourDebugComponent::CreateSceneProxy()
{
#if WITH_EDITOR
	if (!IsDrawEnabled() || MeshEntries.IsEmpty())
		return nullptr;
	return new FDualContourDebugSceneProxy(this);
#else
	return nullptr;
#endif
}

FBoxSphereBounds UDualContourDebugComponent::CalcBounds(const FTransform& LocalToWorld) const
{
#if WITH_EDITOR
	return CachedLocalBounds.TransformBy(LocalToWorld);
#else
	return FBoxSphereBounds(GetComponentLocation(), FVector::ZeroVector, 0.f);
#endif
}
