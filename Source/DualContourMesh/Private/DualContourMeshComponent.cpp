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
// SceneProxy â€” one mesh per component, no inner sections array
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

		const int32 NumVerts = InComponent->Positions.Num();
		TArray<FDynamicMeshVertex> Verts;
		Verts.SetNumUninitialized(NumVerts);
		for (int32 i = 0; i < NumVerts; i++)
		{
			FDynamicMeshVertex& V = Verts[i];
			V.Position = FVector3f(InComponent->Positions[i]);
			FVector3f N = FVector3f(InComponent->Normals[i]).GetSafeNormal();
			FVector3f TX, TY;
			N.FindBestAxisVectors(TX, TY);
			V.SetTangents(TX, TY, N);
			V.TextureCoordinate[0] = InComponent->UVs[i];
			V.Color = FColor::White;
		}

		VertexFactory = new FLocalVertexFactory(GetScene().GetFeatureLevel(), "FDualContourMeshSceneProxy");
		IndexBuffer.Indices = InComponent->Indices;
		VertexBuffers.InitFromDynamicVertex(VertexFactory, Verts);
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
		FColoredMaterialRenderProxy* WireframeMat = nullptr;
		if (bWireframe)
		{
			WireframeMat = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr, FLinearColor(0.f, 0.5f, 1.f));
			Collector.RegisterOneFrameMaterialProxy(WireframeMat);
		}

		FMaterialRenderProxy* MatProxy = bWireframe ? WireframeMat : Material->GetRenderProxy();

		for (int32 ViewIdx = 0; ViewIdx < Views.Num(); ViewIdx++)
		{
			if (!(VisibilityMap & (1u << ViewIdx)))
				continue;

			FMeshBatch& Mesh = Collector.AllocateMesh();
			FMeshBatchElement& BE = Mesh.Elements[0];
			BE.IndexBuffer = &IndexBuffer;
			Mesh.bWireframe = bWireframe;
			Mesh.VertexFactory = VertexFactory;
			Mesh.MaterialRenderProxy = MatProxy;

			FDynamicPrimitiveUniformBuffer& UB = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			FPrimitiveUniformShaderParametersBuilder Builder;
			BuildUniformShaderParameters(Builder);
			UB.Set(Collector.GetRHICommandList(), Builder);
			BE.PrimitiveUniformBufferResource = &UB.UniformBuffer;

			BE.FirstIndex = 0;
			BE.NumPrimitives = NumPrimitives;
			BE.MinVertexIndex = 0;
			BE.MaxVertexIndex = VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = false;
			Collector.AddMesh(ViewIdx, Mesh);
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance R;
		R.bDrawRelevance = IsShown(View);
		R.bShadowRelevance = IsShadowCast(View);
		R.bDynamicRelevance = true;
		R.bRenderInMainPass = ShouldRenderInMainPass();
		R.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		R.bRenderCustomDepth = ShouldRenderCustomDepth();
		MaterialRelevance.SetPrimitiveViewRelevance(R);
		R.bVelocityRelevance = DrawsVelocity() && R.bOpaque && R.bRenderInMainPass;
		return R;
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

UDualContourMeshComponent::UDualContourMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
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
	UMaterialInterface* Mat = Super::GetMaterial(ElementIndex);
	return Mat ? Mat : UMaterial::GetDefaultMaterial(MD_Surface);
}

int32 UDualContourMeshComponent::GetNumMaterials() const
{
	return 1;
}

FBoxSphereBounds UDualContourMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox Box = LocalBounds.IsValid ? LocalBounds : FBox(FVector::ZeroVector, FVector::ZeroVector);
	return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
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
		const uint32 I0 = Indices[TriangleIndex * 3];
		const uint32 I1 = Indices[TriangleIndex * 3 + 1];
		const uint32 I2 = Indices[TriangleIndex * 3 + 2];
		const uint32 VertexCount = static_cast<uint32>(Positions.Num());

		if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount || I0 == I1 || I0 == I2 || I1 == I2)
			continue;

		const FVector Edge01 = Positions[I1] - Positions[I0];
		const FVector Edge02 = Positions[I2] - Positions[I0];
		if (FVector::CrossProduct(Edge01, Edge02).IsNearlyZero())
			continue;

		FTriIndices Triangle;
		Triangle.v0 = static_cast<int32>(I0);
		Triangle.v1 = static_cast<int32>(I1);
		Triangle.v2 = static_cast<int32>(I2);
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
// Cell (CX,CY,CZ) owns the edge where it is the MINIMUM-index of the 4 sharing
// cells, so it only borrows the +X/+Y/+Z adjacent ring â€” no duplication across
// adjacent components.
// ---------------------------------------------------------------------------

void UDualContourMeshComponent::BuildMesh()
{
	Positions.Reset();
	Normals.Reset();
	UVs.Reset();
	Indices.Reset();
	LocalBounds = FBox(ForceInit);

	ADualContourMeshActor* Owner = Cast<ADualContourMeshActor>(GetOwner());
	if (!Owner || Owner->DualContourGrid.IsEmpty())
		return;

	for (int32 cz = CellRangeMin.Z; cz < CellRangeMax.Z; cz++)
		for (int32 cy = CellRangeMin.Y; cy < CellRangeMax.Y; cy++)
			for (int32 cx = CellRangeMin.X; cx < CellRangeMax.X; cx++)
				GenerateQuadsForCell(cx, cy, cz);

	// Bounds covers owned + borrowed +XYZ ring cell centers used as vertices
	for (const FVector& P : Positions)
		LocalBounds += P;
	if (!LocalBounds.IsValid)
		LocalBounds = FBox(FVector::ZeroVector, FVector::ZeroVector);
}

void UDualContourMeshComponent::GenerateQuadsForCell(int32 CX, int32 CY, int32 CZ)
{
	const int32 IsoValue = 127;
	ADualContourMeshActor* Owner = Cast<ADualContourMeshActor>(GetOwner());
	if (!Owner)
		return;
	const FVectorInt& CC = Owner->CellCount;

	auto GetCell = [&](int32 cx, int32 cy, int32 cz) -> const FDualContourCell*
	{
		if (!CC.IsValid(cx, cy, cz))
			return nullptr;
		const FDualContourCell& C = Owner->DualContourGrid[CC.LinearIndex(cx, cy, cz)];
		return C.bActive ? &C : nullptr;
	};

	// Reversed winding â€” (0,2,1)+(0,3,2) â€” makes faces visible from the outward side in UE
	auto AddQuad = [&](const FDualContourCell* V0, FVector2f UV0, const FDualContourCell* V1, FVector2f UV1, const FDualContourCell* V2,
					   FVector2f UV2, const FDualContourCell* V3, FVector2f UV3)
	{
		if (!V0 || !V1 || !V2 || !V3)
			return;
		uint32 Base = (uint32)Positions.Num();
		Positions.Add(V0->Center);
		Normals.Add(V0->Normal);
		UVs.Add(UV0);
		Positions.Add(V1->Center);
		Normals.Add(V1->Normal);
		UVs.Add(UV1);
		Positions.Add(V2->Center);
		Normals.Add(V2->Normal);
		UVs.Add(UV2);
		Positions.Add(V3->Center);
		Normals.Add(V3->Normal);
		UVs.Add(UV3);
		Indices.Append({Base, Base + 2, Base + 1, Base, Base + 3, Base + 2});
	};

	// X-axis edge: edge at sample (CX, CY+1, CZ+1)â†’(CX+1, CY+1, CZ+1)
	// 4 cells in Y-Z plane; borrows CY+1 and CZ+1 rings
	if (CY + 1 < CC.Y && CZ + 1 < CC.Z)
	{
		int32 dA = Owner->GetSample(CX, CY + 1, CZ + 1);
		int32 dB = Owner->GetSample(CX + 1, CY + 1, CZ + 1);
		if (dA != IsoValue && dB != IsoValue && ((dA < IsoValue) != (dB < IsoValue)))
		{
			const FDualContourCell* C00 = GetCell(CX, CY, CZ);
			const FDualContourCell* C10 = GetCell(CX, CY + 1, CZ);
			const FDualContourCell* C11 = GetCell(CX, CY + 1, CZ + 1);
			const FDualContourCell* C01 = GetCell(CX, CY, CZ + 1);
			if (dA > IsoValue)  // outward = +X
				AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
			else  // outward = -X
				AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
		}
	}

	// Y-axis edge: edge at sample (CX+1, CY, CZ+1)â†’(CX+1, CY+1, CZ+1)
	// 4 cells in X-Z plane; borrows CX+1 and CZ+1 rings
	if (CX + 1 < CC.X && CZ + 1 < CC.Z)
	{
		int32 dA = Owner->GetSample(CX + 1, CY, CZ + 1);
		int32 dB = Owner->GetSample(CX + 1, CY + 1, CZ + 1);
		if (dA != IsoValue && dB != IsoValue && ((dA < IsoValue) != (dB < IsoValue)))
		{
			const FDualContourCell* C00 = GetCell(CX, CY, CZ);
			const FDualContourCell* C10 = GetCell(CX + 1, CY, CZ);
			const FDualContourCell* C11 = GetCell(CX + 1, CY, CZ + 1);
			const FDualContourCell* C01 = GetCell(CX, CY, CZ + 1);
			if (dA > IsoValue)  // outward = +Y
				AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
			else  // outward = -Y
				AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
		}
	}

	// Z-axis edge: edge at sample (CX+1, CY+1, CZ)â†’(CX+1, CY+1, CZ+1)
	// 4 cells in X-Y plane; borrows CX+1 and CY+1 rings
	if (CX + 1 < CC.X && CY + 1 < CC.Y)
	{
		int32 dA = Owner->GetSample(CX + 1, CY + 1, CZ);
		int32 dB = Owner->GetSample(CX + 1, CY + 1, CZ + 1);
		if (dA != IsoValue && dB != IsoValue && ((dA < IsoValue) != (dB < IsoValue)))
		{
			const FDualContourCell* C00 = GetCell(CX, CY, CZ);
			const FDualContourCell* C10 = GetCell(CX + 1, CY, CZ);
			const FDualContourCell* C11 = GetCell(CX + 1, CY + 1, CZ);
			const FDualContourCell* C01 = GetCell(CX, CY + 1, CZ);
			if (dA > IsoValue)  // outward = +Z
				AddQuad(C00, {0, 0}, C10, {1, 0}, C11, {1, 1}, C01, {0, 1});
			else  // outward = -Z
				AddQuad(C00, {0, 0}, C01, {0, 1}, C11, {1, 1}, C10, {1, 0});
		}
	}
}
