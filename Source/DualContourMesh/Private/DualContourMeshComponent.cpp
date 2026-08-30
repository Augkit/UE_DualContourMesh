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
#include "ProfilingDebugging/CpuProfilerTrace.h"

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
		const FDualContourMeshData& MeshData = InComponent->GetMeshData();
		if (MeshData.Positions.IsEmpty())
			return;

		const int32 VertexCount = MeshData.Positions.Num();
		TArray<FDynamicMeshVertex> Vertices;
		Vertices.SetNumUninitialized(VertexCount);
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex++)
		{
			FDynamicMeshVertex& Vertex = Vertices[VertexIndex];
			Vertex.Position = FVector3f(MeshData.Positions[VertexIndex]);
			FVector3f Normal = FVector3f(MeshData.Normals[VertexIndex]).GetSafeNormal();
			FVector3f TangentX, TangentY;
			Normal.FindBestAxisVectors(TangentX, TangentY);
			Vertex.SetTangents(TangentX, TangentY, Normal);
			Vertex.TextureCoordinate[0] = MeshData.UVs[VertexIndex];
			Vertex.Color = FColor::White;
		}

		VertexFactory = new FLocalVertexFactory(GetScene().GetFeatureLevel(), "FDualContourMeshSceneProxy");
		IndexBuffer.Indices = MeshData.Indices;
		VertexBuffers.InitFromDynamicVertex(VertexFactory, Vertices);
		BeginInitResource(&VertexBuffers.PositionVertexBuffer);
		BeginInitResource(&VertexBuffers.StaticMeshVertexBuffer);
		BeginInitResource(&VertexBuffers.ColorVertexBuffer);
		BeginInitResource(&IndexBuffer);
		BeginInitResource(VertexFactory);

		NumPrimitives = MeshData.Indices.Num() / 3;
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

void UDualContourMeshComponent::ApplyMeshData(FDualContourMeshData&& InMeshData, bool bUpdateCollision)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_ApplyMeshData);
	check(IsInGameThread());
	MeshData = MoveTemp(InMeshData);
	UpdateBounds();
	if (bUpdateCollision)
		UpdateCollision();
	MarkRenderStateDirty();
}

void UDualContourMeshComponent::RefreshCollision()
{
	UpdateCollision();
}

FPrimitiveSceneProxy* UDualContourMeshComponent::CreateSceneProxy()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_CreateSceneProxy);
	if (MeshData.Positions.IsEmpty())
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
	FBox EffectiveLocalBounds = MeshData.LocalBounds.IsValid
		                            ? MeshData.LocalBounds
		                            : FBox(FVector::ZeroVector, FVector::ZeroVector);
	return FBoxSphereBounds(EffectiveLocalBounds).TransformBy(LocalToWorld);
}

bool UDualContourMeshComponent::GetTriMeshSizeEstimates(FTriMeshCollisionDataEstimates& OutTriMeshEstimates, bool bInUseAllTriData) const
{
	OutTriMeshEstimates.VerticeCount += MeshData.Positions.Num();
	return true;
}

bool UDualContourMeshComponent::GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData, bool bInUseAllTriData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_GetPhysicsTriMeshData);
	if (!CollisionData || !ContainsPhysicsTriMeshData(bInUseAllTriData))
		return false;

	CollisionData->Vertices.Reserve(MeshData.Positions.Num());
	for (const FVector& Position : MeshData.Positions)
	{
		if (Position.ContainsNaN())
			return false;
		CollisionData->Vertices.Add(FVector3f(Position));
	}

	const int32 TriangleCount = MeshData.Indices.Num() / 3;
	CollisionData->Indices.Reserve(TriangleCount);
	CollisionData->MaterialIndices.Reserve(TriangleCount);

	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const uint32 VertexIndex0 = MeshData.Indices[TriangleIndex * 3];
		const uint32 VertexIndex1 = MeshData.Indices[TriangleIndex * 3 + 1];
		const uint32 VertexIndex2 = MeshData.Indices[TriangleIndex * 3 + 2];
		const uint32 VertexCount = static_cast<uint32>(MeshData.Positions.Num());

		if (VertexIndex0 >= VertexCount || VertexIndex1 >= VertexCount || VertexIndex2 >= VertexCount || VertexIndex0 == VertexIndex1
		    || VertexIndex0 == VertexIndex2 || VertexIndex1 == VertexIndex2)
			continue;

		const FVector Edge01 = MeshData.Positions[VertexIndex1] - MeshData.Positions[VertexIndex0];
		const FVector Edge02 = MeshData.Positions[VertexIndex2] - MeshData.Positions[VertexIndex0];
		if (FVector::CrossProduct(Edge01, Edge02).IsNearlyZero())
			continue;

		FTriIndices Triangle;
		Triangle.v0 = static_cast<int32>(VertexIndex0);
		Triangle.v1 = static_cast<int32>(VertexIndex1);
		Triangle.v2 = static_cast<int32>(VertexIndex2);
		CollisionData->Indices.Add(Triangle);
		CollisionData->MaterialIndices.Add(0);
	}

	if (UPhysicsSettings::Get()->bSupportUVFromHitResults && MeshData.UVs.Num() == MeshData.Positions.Num())
	{
		TArray<FVector2D>& CollisionUVs = CollisionData->UVs.AddDefaulted_GetRef();
		CollisionUVs.Reserve(MeshData.UVs.Num());
		for (const FVector2f& UV : MeshData.UVs)
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
	return MeshData.Positions.Num() >= 3 && MeshData.Indices.Num() >= 3;
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
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContourMesh_UpdateCollision);
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
