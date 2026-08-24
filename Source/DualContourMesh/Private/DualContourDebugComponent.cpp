#include "DualContourDebugComponent.h"

#if WITH_EDITOR
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"

static TAutoConsoleVariable<int32> CVarDrawDualContourCells(
	TEXT("DualContour.Debug.DrawCells"), 0,
	TEXT("Draw dual contour cell debug boxes (editor only).\n"
		"  0: off\n"
		"  1: active cells only (green)\n"
		"  2: all cells (green=active, red=inactive)"),
	ECVF_Default);

enum class EDebugEdgeAxis : uint8
{
	X,
	Y,
	Z,
};

struct FDebugEdgeKey
{
	FIntVector GridStart;
	EDebugEdgeAxis Axis = EDebugEdgeAxis::X;

	friend bool operator==(const FDebugEdgeKey& A, const FDebugEdgeKey& B)
	{
		return A.GridStart == B.GridStart && A.Axis == B.Axis;
	}

	friend uint32 GetTypeHash(const FDebugEdgeKey& Key)
	{
		return HashCombineFast(GetTypeHash(Key.GridStart), GetTypeHash(static_cast<uint8>(Key.Axis)));
	}
};

struct FDebugEdgeState
{
	FVector3f Start = FVector3f::ZeroVector;
	FVector3f End = FVector3f::ZeroVector;
	bool bTouchesActiveCell = false;
};

static void AddUniqueEdge(TMap<FDebugEdgeKey, FDebugEdgeState>& Edges, const FDebugEdgeKey& Key,
	const FVector3f& Start, const FVector3f& End, bool bActive)
{
	if (FDebugEdgeState* Existing = Edges.Find(Key))
	{
		Existing->bTouchesActiveCell |= bActive;
		return;
	}

	Edges.Add(Key, {Start, End, bActive});
}

static void AddUniqueBoxEdges(const UDualContourDebugComponent::FCellEntry& Cell,
	TMap<FDebugEdgeKey, FDebugEdgeState>& Edges)
{
	const FIntVector& C = Cell.GridCoordinate;
	const FVector3f Lo(Cell.LocalBox.Min);
	const FVector3f Hi(Cell.LocalBox.Max);

	AddUniqueEdge(Edges, {C + FIntVector(0, 0, 0), EDebugEdgeAxis::X}, {Lo.X, Lo.Y, Lo.Z}, {Hi.X, Lo.Y, Lo.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(0, 1, 0), EDebugEdgeAxis::X}, {Lo.X, Hi.Y, Lo.Z}, {Hi.X, Hi.Y, Lo.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(0, 0, 1), EDebugEdgeAxis::X}, {Lo.X, Lo.Y, Hi.Z}, {Hi.X, Lo.Y, Hi.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(0, 1, 1), EDebugEdgeAxis::X}, {Lo.X, Hi.Y, Hi.Z}, {Hi.X, Hi.Y, Hi.Z}, Cell.bActive);

	AddUniqueEdge(Edges, {C + FIntVector(0, 0, 0), EDebugEdgeAxis::Y}, {Lo.X, Lo.Y, Lo.Z}, {Lo.X, Hi.Y, Lo.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(1, 0, 0), EDebugEdgeAxis::Y}, {Hi.X, Lo.Y, Lo.Z}, {Hi.X, Hi.Y, Lo.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(0, 0, 1), EDebugEdgeAxis::Y}, {Lo.X, Lo.Y, Hi.Z}, {Lo.X, Hi.Y, Hi.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(1, 0, 1), EDebugEdgeAxis::Y}, {Hi.X, Lo.Y, Hi.Z}, {Hi.X, Hi.Y, Hi.Z}, Cell.bActive);

	AddUniqueEdge(Edges, {C + FIntVector(0, 0, 0), EDebugEdgeAxis::Z}, {Lo.X, Lo.Y, Lo.Z}, {Lo.X, Lo.Y, Hi.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(1, 0, 0), EDebugEdgeAxis::Z}, {Hi.X, Lo.Y, Lo.Z}, {Hi.X, Lo.Y, Hi.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(0, 1, 0), EDebugEdgeAxis::Z}, {Lo.X, Hi.Y, Lo.Z}, {Lo.X, Hi.Y, Hi.Z}, Cell.bActive);
	AddUniqueEdge(Edges, {C + FIntVector(1, 1, 0), EDebugEdgeAxis::Z}, {Hi.X, Hi.Y, Lo.Z}, {Hi.X, Hi.Y, Hi.Z}, Cell.bActive);
}

class FDualContourDebugSceneProxy final : public FPrimitiveSceneProxy
{
public:
	explicit FDualContourDebugSceneProxy(const UDualContourDebugComponent* Component)
		: FPrimitiveSceneProxy(Component)
	{
		TMap<FDebugEdgeKey, FDebugEdgeState> Edges;
		Edges.Reserve(Component->CellEntries.Num() * 3);

		for (const UDualContourDebugComponent::FCellEntry& Cell : Component->CellEntries)
			AddUniqueBoxEdges(Cell, Edges);

		GreenEdges.Reserve(Edges.Num());
		RedEdges.Reserve(Edges.Num());
		for (const TPair<FDebugEdgeKey, FDebugEdgeState>& Pair : Edges)
			(Pair.Value.bTouchesActiveCell ? GreenEdges : RedEdges).Add(Pair.Value);

	}

	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		const int32 DrawMode = CVarDrawDualContourCells.GetValueOnAnyThread();
		if (DrawMode == 0)
			return;

		const FMatrix& DebugLocalToWorld = GetLocalToWorld();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (!(VisibilityMap & (1u << ViewIndex)))
				continue;

			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
			PDI->AddReserveLines(SDPG_World, GreenEdges.Num(), false, true);
			DrawEdges(PDI, DebugLocalToWorld, GreenEdges, FLinearColor::Green);

			if (DrawMode >= 2)
			{
				PDI->AddReserveLines(SDPG_World, RedEdges.Num(), false, true);
				DrawEdges(PDI, DebugLocalToWorld, RedEdges, FLinearColor::Red);
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View) && CVarDrawDualContourCells.GetValueOnAnyThread() != 0;
		Result.bDynamicRelevance = true;
		return Result;
	}

	virtual SIZE_T GetTypeHash() const override { return (SIZE_T)this; }
	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this) + GreenEdges.GetAllocatedSize() + RedEdges.GetAllocatedSize();
	}

private:
	static void DrawEdges(FPrimitiveDrawInterface* PDI, const FMatrix& LocalToWorld,
		const TArray<FDebugEdgeState>& Edges, const FLinearColor& Color)
	{
		for (const FDebugEdgeState& Edge : Edges)
		{
			PDI->DrawLine(
				LocalToWorld.TransformPosition(FVector(Edge.Start)),
				LocalToWorld.TransformPosition(FVector(Edge.End)),
				Color, SDPG_World, 1.0f, 0.0f, true);
		}
	}

	TArray<FDebugEdgeState> GreenEdges;
	TArray<FDebugEdgeState> RedEdges;
};

void UDualContourDebugComponent::UpdateFromGrid(
	const TArray<FDualContourCell>& Grid, FVectorInt InCellCount, float CellSize)
{
	CellEntries.Reset(Grid.Num());
	const FVector HalfExtent(CellSize * 0.5f);
	FBox TotalBox(ForceInit);

	for (int32 CellZ = 0; CellZ < InCellCount.Z; CellZ++)
		for (int32 CellY = 0; CellY < InCellCount.Y; CellY++)
			for (int32 CellX = 0; CellX < InCellCount.X; CellX++)
			{
				const int32 Idx = InCellCount.LinearIndex(CellX, CellY, CellZ);
				if (!Grid.IsValidIndex(Idx))
					continue;
				const FVector Center(
					(CellX + 0.5f) * CellSize,
					(CellY + 0.5f) * CellSize,
					(CellZ + 0.5f) * CellSize);
				const FBox Box(Center - HalfExtent, Center + HalfExtent);
				const bool bActive = Grid[Idx].bActive;
				CellEntries.Add({FIntVector(CellX, CellY, CellZ), Box, bActive});
				TotalBox += Box;
			}

	CachedLocalBounds = CellEntries.IsEmpty()
		? FBoxSphereBounds(FVector::ZeroVector, FVector::ZeroVector, 0.f)
		: FBoxSphereBounds(TotalBox);
}

#endif // WITH_EDITOR

FPrimitiveSceneProxy* UDualContourDebugComponent::CreateSceneProxy()
{
#if WITH_EDITOR
	if (CellEntries.IsEmpty())
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
