#include "DualContourMeshComponent.h"
#include "DualContourMeshActor.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "Engine/Engine.h"
#include "RenderUtils.h"
#include "StaticMeshResources.h"
#include "LocalVertexFactory.h"
#include "DynamicMeshBuilder.h"
#include "PrimitiveDrawingUtils.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "SceneView.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"

// ---------------------------------------------------------------------------
// Scene proxy: one mesh per component
// ---------------------------------------------------------------------------

class FDualContourMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FDualContourMeshSceneProxy(UDualContourMeshComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
		  , VertexFactory(nullptr)
		  , NumPrimitives(0)
		  , Material(nullptr)
		  , MaterialRelevance(InComponent->GetMaterialRelevance(GetScene().GetShaderPlatform()))
	{
		if (InComponent->Positions.IsEmpty())
			return;

		const int32 VertexCount = InComponent->Positions.Num();
		TArray<FDynamicMeshVertex> Vertices;
		Vertices.SetNumUninitialized(VertexCount);
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
		{
			FDynamicMeshVertex& Vertex = Vertices[VertexIndex];
			Vertex.Position = FVector3f(InComponent->Positions[VertexIndex]);
			FVector3f Normal = FVector3f(InComponent->Normals[VertexIndex]).GetSafeNormal();
			FVector3f TangentX, TangentY;
			Normal.FindBestAxisVectors(TangentX, TangentY);
			Vertex.SetTangents(TangentX, TangentY, Normal);
			Vertex.TextureCoordinate[0] = InComponent->UVs[VertexIndex];
			Vertex.Color = FColor::White;
		}

		VertexFactory = new FLocalVertexFactory(GetScene().GetFeatureLevel(), "FDualContourMeshSceneProxy");
		IndexBuffer.Indices = InComponent->Indices;
		VertexBuffers.InitFromDynamicVertex(VertexFactory, Vertices);
		BeginInitResource(&VertexBuffers.PositionVertexBuffer);
		BeginInitResource(&VertexBuffers.StaticMeshVertexBuffer);
		BeginInitResource(&VertexBuffers.ColorVertexBuffer);
		BeginInitResource(&IndexBuffer);
		BeginInitResource(VertexFactory);

		NumPrimitives = InComponent->Indices.Num() / 3;
		Material = InComponent->GetMaterial(0);
		if (!Material)
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	virtual ~FDualContourMeshSceneProxy() override
	{
		VertexBuffers.PositionVertexBuffer.ReleaseResource();
		VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
		VertexBuffers.ColorVertexBuffer.ReleaseResource();
		IndexBuffer.ReleaseResource();
		if (VertexFactory)
		{
			VertexFactory->ReleaseResource();
			delete VertexFactory;
		}
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		if (!VertexFactory || NumPrimitives == 0)
			return;

		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		FColoredMaterialRenderProxy* WireframeMaterialProxy = nullptr;
		if (bWireframe)
		{
			WireframeMaterialProxy = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr, FLinearColor(0.f, 0.5f, 1.f));
			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialProxy);
		}

		FMaterialRenderProxy* MaterialProxy = bWireframe ? WireframeMaterialProxy : Material->GetRenderProxy();

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (!(VisibilityMap & (1u << ViewIndex)))
				continue;

			FMeshBatch& Mesh = Collector.AllocateMesh();
			FMeshBatchElement& BatchElement = Mesh.Elements[0];
			BatchElement.IndexBuffer = &IndexBuffer;
			Mesh.bWireframe = bWireframe;
			Mesh.VertexFactory = VertexFactory;
			Mesh.MaterialRenderProxy = MaterialProxy;

			FDynamicPrimitiveUniformBuffer& DynamicUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			FPrimitiveUniformShaderParametersBuilder Builder;
			BuildUniformShaderParameters(Builder);
			DynamicUniformBuffer.Set(Collector.GetRHICommandList(), Builder);
			BatchElement.PrimitiveUniformBufferResource = &DynamicUniformBuffer.UniformBuffer;

			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = NumPrimitives;
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;
			Collector.AddMesh(ViewIndex, Mesh);
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance ViewRelevance;
		ViewRelevance.bDrawRelevance = IsShown(View);
		ViewRelevance.bShadowRelevance = IsShadowCast(View);
		ViewRelevance.bDynamicRelevance = true;
		ViewRelevance.bRenderInMainPass = ShouldRenderInMainPass();
		ViewRelevance.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		ViewRelevance.bRenderCustomDepth = ShouldRenderCustomDepth();
		MaterialRelevance.SetPrimitiveViewRelevance(ViewRelevance);
		ViewRelevance.bVelocityRelevance = DrawsVelocity() && ViewRelevance.bOpaque && ViewRelevance.bRenderInMainPass;
		return ViewRelevance;
	}

	virtual bool CanBeOccluded() const override { return !MaterialRelevance.bDisableDepthTest; }
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

	virtual SIZE_T GetTypeHash() const override
	{
		static SIZE_T UniquePointer;
		return reinterpret_cast<SIZE_T>(&UniquePointer);
	}

private:
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;
	FLocalVertexFactory* VertexFactory;
	int32 NumPrimitives;
	UMaterialInterface* Material;
	FMaterialRelevance MaterialRelevance;
};

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

UDualContourMeshComponent::UDualContourMeshComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Static);
}

void UDualContourMeshComponent::RebuildMesh()
{
	if (ADualContourMeshActor* Owner = Cast<ADualContourMeshActor>(GetOwner()))
		Owner->RebuildMesh();
}

void UDualContourMeshComponent::BuildAndRefreshMesh()
{
	BuildMesh();
	UpdateBounds();
	UpdateCollision();
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UDualContourMeshComponent::CreateSceneProxy()
{
	if (Positions.IsEmpty())
		return nullptr;
	return new FDualContourMeshSceneProxy(this);
}

UMaterialInterface* UDualContourMeshComponent::GetMaterial(int32 ElementIndex) const
{
	UMaterialInterface* Material = Super::GetMaterial(ElementIndex);
	return Material ? Material : UMaterial::GetDefaultMaterial(MD_Surface);
}

int32 UDualContourMeshComponent::GetNumMaterials() const
{
	return 1;
}

FBoxSphereBounds UDualContourMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox EffectiveLocalBounds = LocalBounds.IsValid ? LocalBounds : FBox(FVector::ZeroVector, FVector::ZeroVector);
	return FBoxSphereBounds(EffectiveLocalBounds).TransformBy(LocalToWorld);
}

bool UDualContourMeshComponent::GetTriMeshSizeEstimates(FTriMeshCollisionDataEstimates& OutTriMeshEstimates, bool bInUseAllTriData) const
{
	OutTriMeshEstimates.VerticeCount += Positions.Num();
	return true;
}

bool UDualContourMeshComponent::GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData, bool bInUseAllTriData)
{
	if (!CollisionData || !ContainsPhysicsTriMeshData(bInUseAllTriData))
		return false;

	CollisionData->Vertices.Reserve(Positions.Num());
	for (const FVector& Position : Positions)
	{
		if (Position.ContainsNaN())
			return false;
		CollisionData->Vertices.Add(FVector3f(Position));
	}

	const int32 TriangleCount = Indices.Num() / 3;
	CollisionData->Indices.Reserve(TriangleCount);
	CollisionData->MaterialIndices.Reserve(TriangleCount);

	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const uint32 VertexIndex0 = Indices[TriangleIndex * 3];
		const uint32 VertexIndex1 = Indices[TriangleIndex * 3 + 1];
		const uint32 VertexIndex2 = Indices[TriangleIndex * 3 + 2];
		const uint32 VertexCount = static_cast<uint32>(Positions.Num());

		if (VertexIndex0 >= VertexCount || VertexIndex1 >= VertexCount || VertexIndex2 >= VertexCount || VertexIndex0 == VertexIndex1
		    || VertexIndex0 == VertexIndex2 || VertexIndex1 == VertexIndex2)
			continue;

		const FVector Edge01 = Positions[VertexIndex1] - Positions[VertexIndex0];
		const FVector Edge02 = Positions[VertexIndex2] - Positions[VertexIndex0];
		if (FVector::CrossProduct(Edge01, Edge02).IsNearlyZero())
			continue;

		FTriIndices Triangle;
		Triangle.v0 = static_cast<int32>(VertexIndex0);
		Triangle.v1 = static_cast<int32>(VertexIndex1);
		Triangle.v2 = static_cast<int32>(VertexIndex2);
		CollisionData->Indices.Add(Triangle);
		CollisionData->MaterialIndices.Add(0);
	}

	if (UPhysicsSettings::Get()->bSupportUVFromHitResults && UVs.Num() == Positions.Num())
	{
		TArray<FVector2D>& CollisionUVs = CollisionData->UVs.AddDefaulted_GetRef();
		CollisionUVs.Reserve(UVs.Num());
		for (const FVector2f& UV : UVs)
			CollisionUVs.Add(FVector2D(UV));
	}

	// These match the engine's StaticMesh/ProceduralMesh collision winding convention.
	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = false;
	CollisionData->bFastCook = false;

	return !CollisionData->Indices.IsEmpty();
}

bool UDualContourMeshComponent::ContainsPhysicsTriMeshData(bool bInUseAllTriData) const
{
	return Positions.Num() >= 3 && Indices.Num() >= 3;
}

void UDualContourMeshComponent::CreateMeshBodySetup()
{
	if (MeshBodySetup)
		return;

	const EObjectFlags BodySetupFlags = IsTemplate() ? RF_Public | RF_ArchetypeObject : RF_NoFlags;
	MeshBodySetup = NewObject<UBodySetup>(this, NAME_None, BodySetupFlags);
	MeshBodySetup->BodySetupGuid = FGuid::NewGuid();
	MeshBodySetup->bGenerateMirroredCollision = false;
	MeshBodySetup->bDoubleSidedGeometry = false;
	MeshBodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
}

void UDualContourMeshComponent::UpdateCollision()
{
	CreateMeshBodySetup();
	MeshBodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	MeshBodySetup->bDoubleSidedGeometry = false;
	MeshBodySetup->bHasCookedCollisionData = true;
	MeshBodySetup->InvalidatePhysicsData();
	MeshBodySetup->CreatePhysicsMeshes();
	RecreatePhysicsState();
}

UBodySetup* UDualContourMeshComponent::GetBodySetup()
{
	CreateMeshBodySetup();
	return MeshBodySetup;
}

UMaterialInterface* UDualContourMeshComponent::GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const
{
	SectionIndex = FaceIndex >= 0 ? 0 : INDEX_NONE;
	return FaceIndex >= 0 ? GetMaterial(0) : nullptr;
}

// ---------------------------------------------------------------------------
// Mesh building
// The adjacent cell with the minimum coordinates owns each sign-changing grid edge.
// Quad construction only reads the +X/+Y/+Z neighbor ring, avoiding duplicate quads
// across adjacent components.
// ---------------------------------------------------------------------------

void UDualContourMeshComponent::BuildMesh()
{
	Positions.Reset();
	Normals.Reset();
	UVs.Reset();
	Indices.Reset();
	LocalBounds = FBox(ForceInit);

	ADualContourMeshActor* Owner = Cast<ADualContourMeshActor>(GetOwner());
	if (!Owner || !Owner->HasCurrentGeneratedData())
		return;

	for (int32 CellZ = CellRangeMin.Z; CellZ < CellRangeMax.Z; CellZ++)
		for (int32 CellY = CellRangeMin.Y; CellY < CellRangeMax.Y; CellY++)
			for (int32 CellX = CellRangeMin.X; CellX < CellRangeMax.X; CellX++)
				GenerateQuadsForCell(CellX, CellY, CellZ);

	// Bounds include every emitted vertex, including centers read from the positive-axis neighbor ring.
	for (const FVector& Position : Positions)
		LocalBounds += Position;
	if (!LocalBounds.IsValid)
		LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
}

void UDualContourMeshComponent::GenerateQuadsForCell(int32 CellX, int32 CellY, int32 CellZ)
{
	ADualContourMeshActor* Owner = Cast<ADualContourMeshActor>(GetOwner());
	if (!Owner)
		return;
	const FVectorInt& CellCounts = Owner->CellCount;

	auto GetCell = [&](int32 QueryCellX, int32 QueryCellY, int32 QueryCellZ) -> const FDualContourCell*
	{
		if (!CellCounts.IsValid(QueryCellX, QueryCellY, QueryCellZ))
			return nullptr;
		return Owner->GetContourCell(QueryCellX, QueryCellY, QueryCellZ);
	};

	// Reversed winding, (0,2,1) + (0,3,2), makes faces visible from the outward side in UE.
	auto AddQuad = [&](const FDualContourCell* Cell0, FVector2f UV0, const FDualContourCell* Cell1, FVector2f UV1, const FDualContourCell* Cell2,
		FVector2f UV2, const FDualContourCell* Cell3, FVector2f UV3)
	{
		if (!Cell0 || !Cell1 || !Cell2 || !Cell3)
			return;
		uint32 BaseVertexIndex = (uint32)Positions.Num();
		Positions.Add(Cell0->Center);
		Normals.Add(Cell0->Normal);
		UVs.Add(UV0);
		Positions.Add(Cell1->Center);
		Normals.Add(Cell1->Normal);
		UVs.Add(UV1);
		Positions.Add(Cell2->Center);
		Normals.Add(Cell2->Normal);
		UVs.Add(UV2);
		Positions.Add(Cell3->Center);
		Normals.Add(Cell3->Normal);
		UVs.Add(UV3);
		Indices.Append({BaseVertexIndex, BaseVertexIndex + 2, BaseVertexIndex + 1, BaseVertexIndex, BaseVertexIndex + 3, BaseVertexIndex + 2});
	};

	// X-axis edge: (CellX, CellY+1, CellZ+1) -> (CellX+1, CellY+1, CellZ+1)
	// Four adjacent cells lie in the Y-Z plane; this reads the CellY+1 and CellZ+1 rings.
	if (CellY + 1 < CellCounts.Y && CellZ + 1 < CellCounts.Z)
	{
		const uint8 DensityA = Owner->GetSample(CellX, CellY + 1, CellZ + 1);
		const uint8 DensityB = Owner->GetSample(CellX + 1, CellY + 1, CellZ + 1);
		if ((DensityA < GDualContourIsoValue) != (DensityB < GDualContourIsoValue))
		{
			const FDualContourCell* C00 = GetCell(CellX, CellY, CellZ);
			const FDualContourCell* C10 = GetCell(CellX, CellY + 1, CellZ);
			const FDualContourCell* C11 = GetCell(CellX, CellY + 1, CellZ + 1);
			const FDualContourCell* C01 = GetCell(CellX, CellY, CellZ + 1);
			if (DensityA >= GDualContourIsoValue) // outward = +X
				AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
			else // outward = -X
				AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
		}
	}

	// Y-axis edge: (CellX+1, CellY, CellZ+1) -> (CellX+1, CellY+1, CellZ+1)
	// Four adjacent cells lie in the X-Z plane; this reads the CellX+1 and CellZ+1 rings.
	if (CellX + 1 < CellCounts.X && CellZ + 1 < CellCounts.Z)
	{
		const uint8 DensityA = Owner->GetSample(CellX + 1, CellY, CellZ + 1);
		const uint8 DensityB = Owner->GetSample(CellX + 1, CellY + 1, CellZ + 1);
		if ((DensityA < GDualContourIsoValue) != (DensityB < GDualContourIsoValue))
		{
			const FDualContourCell* C00 = GetCell(CellX, CellY, CellZ);
			const FDualContourCell* C10 = GetCell(CellX + 1, CellY, CellZ);
			const FDualContourCell* C11 = GetCell(CellX + 1, CellY, CellZ + 1);
			const FDualContourCell* C01 = GetCell(CellX, CellY, CellZ + 1);
			if (DensityA >= GDualContourIsoValue) // outward = +Y
				AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
			else // outward = -Y
				AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
		}
	}

	// Z-axis edge: (CellX+1, CellY+1, CellZ) -> (CellX+1, CellY+1, CellZ+1)
	// Four adjacent cells lie in the X-Y plane; this reads the CellX+1 and CellY+1 rings.
	if (CellX + 1 < CellCounts.X && CellY + 1 < CellCounts.Y)
	{
		const uint8 DensityA = Owner->GetSample(CellX + 1, CellY + 1, CellZ);
		const uint8 DensityB = Owner->GetSample(CellX + 1, CellY + 1, CellZ + 1);
		if ((DensityA < GDualContourIsoValue) != (DensityB < GDualContourIsoValue))
		{
			const FDualContourCell* C00 = GetCell(CellX, CellY, CellZ);
			const FDualContourCell* C10 = GetCell(CellX + 1, CellY, CellZ);
			const FDualContourCell* C11 = GetCell(CellX + 1, CellY + 1, CellZ);
			const FDualContourCell* C01 = GetCell(CellX, CellY + 1, CellZ);
			if (DensityA >= GDualContourIsoValue) // outward = +Z
				AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
			else // outward = -Z
				AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
		}
	}
}
