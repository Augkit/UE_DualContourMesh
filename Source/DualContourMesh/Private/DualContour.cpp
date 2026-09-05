#include "DualContour.h"
#include "DualContourUtils.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/ObjectSaveContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogDualContour, Log, All);

namespace
{
constexpr int32 EdgeCorners[12][2][3] = {
	{{0, 0, 0}, {1, 0, 0}}, {{0, 1, 0}, {1, 1, 0}}, {{0, 0, 1}, {1, 0, 1}}, {{0, 1, 1}, {1, 1, 1}},
	{{0, 0, 0}, {0, 1, 0}}, {{1, 0, 0}, {1, 1, 0}}, {{0, 0, 1}, {0, 1, 1}}, {{1, 0, 1}, {1, 1, 1}},
	{{0, 0, 0}, {0, 0, 1}}, {{1, 0, 0}, {1, 0, 1}}, {{0, 1, 0}, {0, 1, 1}}, {{1, 1, 0}, {1, 1, 1}}
};

bool Solve3x3(const double Matrix[3][3], const double RightHandSide[3], double Solution[3])
{
	const double C00 = Matrix[1][1] * Matrix[2][2] - Matrix[1][2] * Matrix[2][1];
	const double C01 = -(Matrix[1][0] * Matrix[2][2] - Matrix[1][2] * Matrix[2][0]);
	const double C02 = Matrix[1][0] * Matrix[2][1] - Matrix[1][1] * Matrix[2][0];
	const double Determinant = Matrix[0][0] * C00 + Matrix[0][1] * C01 + Matrix[0][2] * C02;
	if (FMath::Abs(Determinant) < 1e-10)
		return false;

	const double C10 = -(Matrix[0][1] * Matrix[2][2] - Matrix[0][2] * Matrix[2][1]);
	const double C11 = Matrix[0][0] * Matrix[2][2] - Matrix[0][2] * Matrix[2][0];
	const double C12 = -(Matrix[0][0] * Matrix[2][1] - Matrix[0][1] * Matrix[2][0]);
	const double C20 = Matrix[0][1] * Matrix[1][2] - Matrix[0][2] * Matrix[1][1];
	const double C21 = -(Matrix[0][0] * Matrix[1][2] - Matrix[0][2] * Matrix[1][0]);
	const double C22 = Matrix[0][0] * Matrix[1][1] - Matrix[0][1] * Matrix[1][0];
	const double InverseDeterminant = 1.0 / Determinant;

	Solution[0] = InverseDeterminant * (C00 * RightHandSide[0] + C10 * RightHandSide[1] + C20 * RightHandSide[2]);
	Solution[1] = InverseDeterminant * (C01 * RightHandSide[0] + C11 * RightHandSide[1] + C21 * RightHandSide[2]);
	Solution[2] = InverseDeterminant * (C02 * RightHandSide[0] + C12 * RightHandSide[1] + C22 * RightHandSide[2]);
	return true;
}

bool IsValidSampleBounds(const FIntVector& FullDimensions, const FIntVector& SampleMin, const FIntVector& SampleDimensions)
{
	if (SampleMin.X < 0 || SampleMin.Y < 0 || SampleMin.Z < 0
	    || SampleDimensions.X < 0 || SampleDimensions.Y < 0 || SampleDimensions.Z < 0)
		return false;

	const bool bEmpty = SampleDimensions.X == 0 || SampleDimensions.Y == 0 || SampleDimensions.Z == 0;
	if (bEmpty)
	{
		return SampleDimensions.X == 0 && SampleDimensions.Y == 0 && SampleDimensions.Z == 0
		       && SampleMin.X <= FullDimensions.X && SampleMin.Y <= FullDimensions.Y && SampleMin.Z <= FullDimensions.Z;
	}

	return SampleMin.X < FullDimensions.X && SampleMin.Y < FullDimensions.Y && SampleMin.Z < FullDimensions.Z
	       && SampleDimensions.X <= FullDimensions.X - SampleMin.X
	       && SampleDimensions.Y <= FullDimensions.Y - SampleMin.Y
	       && SampleDimensions.Z <= FullDimensions.Z - SampleMin.Z;
}

bool IsValidSampledRegion(const FIntVector& FullDimensions, const FDualContourSampledRegion& Region)
{
	if (!IsValidSampleBounds(FullDimensions, Region.SampleMin, Region.SampleDimensions))
		return false;
	if (Region.SampleDimensions == FIntVector::ZeroValue)
		return Region.Chunks.IsEmpty();

	const FIntVector SampleMax = Region.SampleMin + Region.SampleDimensions;
	const FIntVector ChunkMin(Region.SampleMin.X / GDualContourChunkSize,
		Region.SampleMin.Y / GDualContourChunkSize, Region.SampleMin.Z / GDualContourChunkSize);
	const FIntVector ChunkMaxExclusive(FMath::DivideAndRoundUp(SampleMax.X, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleMax.Y, GDualContourChunkSize),
		FMath::DivideAndRoundUp(SampleMax.Z, GDualContourChunkSize));
	TSet<FIntVector> UniqueChunkCoords;
	UniqueChunkCoords.Reserve(Region.Chunks.Num());
	const int32 ExpandedChunkSize = GDualContourChunkSize * GDualContourChunkSize * GDualContourChunkSize;
	for (const FDualContourSampledChunk& SampledChunk : Region.Chunks)
	{
		const FIntVector& Coord = SampledChunk.ChunkCoord;
		if (Coord.X < ChunkMin.X || Coord.Y < ChunkMin.Y || Coord.Z < ChunkMin.Z
		    || Coord.X >= ChunkMaxExclusive.X || Coord.Y >= ChunkMaxExclusive.Y || Coord.Z >= ChunkMaxExclusive.Z
		    || UniqueChunkCoords.Contains(Coord)
		    || (!SampledChunk.Density.IsUniform() && SampledChunk.Density.DensitySamples.Num() != ExpandedChunkSize))
		{
			return false;
		}
		UniqueChunkCoords.Add(Coord);
	}
	return true;
}
}

void UDualContour::EnsureRebuildComplete() const
{
	if (!IsInGameThread())
		return;
	if (PendingRebuildFuture.IsValid())
	{
		PendingRebuildFuture.Get();
	}
}

void UDualContour::PostLoad()
{
	Super::PostLoad();
	EnsureRebuildComplete();
	CompactAllDensityChunks();
	CompactAllMaterialChunks();
	ModifiedDensityChunks.Reset();
	ModifiedMaterialChunks.Reset();

	// CellChunks is derived entirely from the persistent density grid. Keeping it transient
	// avoids serializing the large nested map while preserving the existing runtime query API.
	CellChunks.Reset();
	if (HasCurrentGeneratedData() && !DensityChunks.IsEmpty())
	{
		AsyncRebuildCells(false);
	}
}

void UDualContour::PreSave(FObjectPreSaveContext SaveContext)
{
	// Public mutation paths already compact their affected chunks. This full pass is a
	// safety net for legacy data and any future mutation path added without a flush.
	CompactAllDensityChunks();
	CompactAllMaterialChunks();
	Super::PreSave(SaveContext);
}

void UDualContour::BeginDestroy()
{
	EnsureRebuildComplete();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UDualContour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty
		                                 ? PropertyChangedEvent.MemberProperty->GetFName()
		                                 : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellCount)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, CellSize)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, VertexSolveMode)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, VertexRelaxation)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, RelaxationNormalCosine))
	{
		bRebuildRequired = true;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// UVs are generated from the existing cells, so changing these settings must
	// refresh mesh components without rebuilding the density/cell cache.
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, UVMode)
	    || MemberPropertyName == GET_MEMBER_NAME_CHECKED(UDualContour, UVWorldSize))
	{
		if (HasCurrentGeneratedData())
			OnCellsRebuilt.Broadcast(FIntVector::ZeroValue, CellCount);
	}
}

void UDualContour::PostEditUndo()
{
	Super::PostEditUndo();
	if (HasCurrentGeneratedData())
	{
		OnCellsRebuilt.Broadcast(FIntVector::ZeroValue, CellCount);
		OnMaterialsChanged.Broadcast(FIntVector::ZeroValue, CellCount);
	}
}
#endif

bool UDualContour::ValidateGenerationSettings() const
{
	if (CellCount.X <= 0 || CellCount.Y <= 0 || CellCount.Z <= 0)
	{
		UE_LOG(LogDualContour, Error, TEXT("Rebuild aborted for %s: CellCount must be positive (current: %d, %d, %d)."),
			*GetNameSafe(GetOuter()), CellCount.X, CellCount.Y, CellCount.Z);
		return false;
	}
	if (CellSize <= 0.f)
	{
		UE_LOG(LogDualContour, Error, TEXT("Rebuild aborted for %s: CellSize must be greater than zero (current: %g)."),
			*GetNameSafe(GetOuter()), CellSize);
		return false;
	}

	// Each axis needs one additional density sample. Chunk-native samplers do not require
	// the complete grid to fit in one TArray, but the boundary addition must remain valid.
	if (CellCount.X >= MAX_int32 || CellCount.Y >= MAX_int32 || CellCount.Z >= MAX_int32)
	{
		UE_LOG(LogDualContour, Error, TEXT("Rebuild aborted for %s: CellCount is too large to add boundary samples."),
			*GetNameSafe(GetOuter()));
		return false;
	}
	return true;
}

bool UDualContour::HasCurrentGeneratedData() const
{
	return !bRebuildRequired && LastBuiltCellCount.X == CellCount.X && LastBuiltCellCount.Y == CellCount.Y && LastBuiltCellCount.Z == CellCount.Z;
}

uint16 UDualContour::GetDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const
{
	const FIntVector SampleDimensions = GetSampleDimensions();
	if (!DualContourUtils::IsValidCoordinate(SampleDimensions, SampleX, SampleY, SampleZ))
		return 0;

	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(SampleX, SampleY, SampleZ);
	const FDensityChunk* Chunk = DensityChunks.Find(ChunkCoord);
	if (!Chunk)
		return 0;
	if (Chunk->IsUniform())
		return Chunk->UniformValue;

	return Chunk->DensitySamples[DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ)];
}

float UDualContour::GetLinearDensity(int32 SampleX, int32 SampleY, int32 SampleZ) const
{
	return FDensityChunk::DecodeLinearDensity(GetDensity(SampleX, SampleY, SampleZ));
}

uint8 UDualContour::GetMaterialId(int32 SampleX, int32 SampleY, int32 SampleZ) const
{
	if (!DualContourUtils::IsValidCoordinate(GetSampleDimensions(), SampleX, SampleY, SampleZ))
		return 0;
	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(SampleX, SampleY, SampleZ);
	const FMaterialIdChunk* Chunk = MaterialChunks.Find(ChunkCoord);
	return Chunk ? Chunk->GetMaterialId(DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ)) : 0;
}

const FDualContourCell* UDualContour::GetCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	EnsureRebuildComplete();
	if (!DualContourUtils::IsValidCoordinate(CellCount, CellX, CellY, CellZ))
		return nullptr;

	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(CellX, CellY, CellZ);
	const FCellChunk* Chunk = CellChunks.Find(ChunkCoord);
	return Chunk ? Chunk->ActiveCells.Find(DualContourUtils::ChunkLocalIndex(CellX, CellY, CellZ)) : nullptr;
}

bool UDualContour::HasActiveCellInRange(FIntVector CellMin, FIntVector CellMax) const
{
	EnsureRebuildComplete();
	const int32 ChunkMinX = CellMin.X / GDualContourChunkSize;
	const int32 ChunkMinY = CellMin.Y / GDualContourChunkSize;
	const int32 ChunkMinZ = CellMin.Z / GDualContourChunkSize;
	const int32 ChunkMaxX = (CellMax.X - 1) / GDualContourChunkSize;
	const int32 ChunkMaxY = (CellMax.Y - 1) / GDualContourChunkSize;
	const int32 ChunkMaxZ = (CellMax.Z - 1) / GDualContourChunkSize;

	for (int32 ChunkZ = ChunkMinZ; ChunkZ <= ChunkMaxZ; ++ChunkZ)
		for (int32 ChunkY = ChunkMinY; ChunkY <= ChunkMaxY; ++ChunkY)
			for (int32 ChunkX = ChunkMinX; ChunkX <= ChunkMaxX; ++ChunkX)
			{
				const FCellChunk* Chunk = CellChunks.Find(FIntVector(ChunkX, ChunkY, ChunkZ));
				if (!Chunk)
					continue;
				for (const TPair<uint16, FDualContourCell>& Pair : Chunk->ActiveCells)
				{
					const FIntVector CellCoord = DualContourUtils::ChunkOrigin(FIntVector(ChunkX, ChunkY, ChunkZ))
					                             + DualContourUtils::ChunkLocalCoord(Pair.Key);
					if (CellCoord.X >= CellMin.X && CellCoord.X < CellMax.X && CellCoord.Y >= CellMin.Y && CellCoord.Y < CellMax.Y
					    && CellCoord.Z >= CellMin.Z && CellCoord.Z < CellMax.Z)
						return true;
				}
			}
	return false;
}

bool UDualContour::Initialize(const UDualContour* InitialDualContour,
	const FDualContourDensityChunks* InModifiedDensityChunks, const FDualContourMaterialChunks* InModifiedMaterialChunks)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_Initialize);
	if (!CopyFrom(InitialDualContour, false))
		return false;

	// CopyFrom transfers the already-built cell cache. Applying the sparse overlay then
	// rebuilds only the cell chunks whose density inputs may have changed.
	if (InModifiedDensityChunks && !ApplyModifiedDensityChunks(*InModifiedDensityChunks))
		return false;
	if (InModifiedMaterialChunks && !ApplyModifiedMaterialChunks(*InModifiedMaterialChunks))
		return false;

	EnsureRebuildComplete();
	OnCellsRebuilt.Broadcast(FIntVector::ZeroValue, CellCount);
	return true;
}

bool UDualContour::CopyFrom(const UDualContour* Source, bool bBroadcastCellsRebuilt)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_CopyFrom);
	if (!Source || !Source->HasCurrentGeneratedData())
		return false;
	if (Source == this)
		return true;

	EnsureRebuildComplete();
	Source->EnsureRebuildComplete();

	Modify();
	CellCount = Source->CellCount;
	CellSize = Source->CellSize;
	VertexSolveMode = Source->VertexSolveMode;
	VertexRelaxation = Source->VertexRelaxation;
	RelaxationNormalCosine = Source->RelaxationNormalCosine;
	UVMode = Source->UVMode;
	UVWorldSize = Source->UVWorldSize;
	bRebuildRequired = Source->bRebuildRequired;
	DensityChunks = Source->DensityChunks;
	ModifiedDensityChunks.Reset();
	MaterialChunks = Source->MaterialChunks;
	ModifiedMaterialChunks.Reset();
	CellChunks = Source->CellChunks;
	LastBuiltCellCount = Source->LastBuiltCellCount;
	if (bBroadcastCellsRebuilt)
		OnCellsRebuilt.Broadcast(FIntVector::ZeroValue, CellCount);
	return true;
}

bool UDualContour::Rebuild()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_Rebuild);
	EnsureRebuildComplete();
	bRebuildRequired = true;
	if (!ValidateGenerationSettings())
		return false;

	LastBuiltCellCount = CellCount;
	bRebuildRequired = false;
	CellChunks.Reset();
	AsyncRebuildCells();
	return true;
}

bool UDualContour::ReplaceDensityChunks(FDualContourSampledRegion&& SampledRegion, bool bBroadcastCellsRebuilt)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_ReplaceDensityChunks);
	EnsureRebuildComplete();
	bRebuildRequired = true;
	if (!ValidateGenerationSettings() || !IsValidSampledRegion(GetSampleDimensions(), SampledRegion))
	{
		UE_LOG(LogDualContour, Error, TEXT("ReplaceDensityChunks aborted for %s because the sampled region is invalid."),
			*GetNameSafe(GetOuter()));
		return false;
	}

	const FIntVector SampleMin = SampledRegion.SampleMin;
	const FIntVector SampleDimensions = SampledRegion.SampleDimensions;
	const FIntVector SampleMax = SampleMin + SampleDimensions;
	DensityChunks.Empty(SampledRegion.Chunks.Num());
	ModifiedDensityChunks.Reset();
	MaterialChunks.Reset();
	ModifiedMaterialChunks.Reset();
	for (FDualContourSampledChunk& SampledChunk : SampledRegion.Chunks)
	{
		SampledChunk.Density.TryCollapse();
		if (!SampledChunk.Density.IsUniform() || SampledChunk.Density.UniformValue != 0)
			DensityChunks.Add(SampledChunk.ChunkCoord, MoveTemp(SampledChunk.Density));
	}

	CellChunks.Reset();
	LastBuiltCellCount = CellCount;
	bRebuildRequired = false;
	if (SampleDimensions == FIntVector::ZeroValue)
	{
		if (bBroadcastCellsRebuilt)
			OnCellsRebuilt.Broadcast(FIntVector::ZeroValue, CellCount);
	}
	else
	{
		const FIntVector CellRangeMin(FMath::Max(0, SampleMin.X - 1), FMath::Max(0, SampleMin.Y - 1),
			FMath::Max(0, SampleMin.Z - 1));
		const FIntVector CellRangeMax(FMath::Min(CellCount.X, SampleMax.X), FMath::Min(CellCount.Y, SampleMax.Y),
			FMath::Min(CellCount.Z, SampleMax.Z));
		AsyncRebuildCellsInRange(CellRangeMin, CellRangeMax, bBroadcastCellsRebuilt);
	}
	return true;
}

bool UDualContour::ModifyDensityChunks(const FDualContourSampledRegion& SampledRegion, bool bExcavate,
	FIntVector& OutAffectedCellMin, FIntVector& OutAffectedCellMax)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_ModifyDensityChunks);
	OutAffectedCellMin = FIntVector::ZeroValue;
	OutAffectedCellMax = FIntVector::ZeroValue;
	const FIntVector FullSampleDimensions = GetSampleDimensions();
	if (!HasCurrentGeneratedData() || !IsValidSampledRegion(FullSampleDimensions, SampledRegion))
		return false;

	// Drain any previous rebuild before writing DensityChunks (the previous BG task reads them).
	EnsureRebuildComplete();

	TSet<FIntVector> ModifiedChunks;
	FIntVector ModifiedSampleMin = FullSampleDimensions;
	FIntVector ModifiedSampleMax(-1, -1, -1);
	const FIntVector SampleMax = SampledRegion.SampleMin + SampledRegion.SampleDimensions;
	for (const FDualContourSampledChunk& SampledChunk : SampledRegion.Chunks)
	{
		if (SampledChunk.Density.IsUniform()
		    && (SampledChunk.Density.UniformValue == 0
		        || (bExcavate && SampledChunk.Density.UniformValue < GDualContourIsoValue)))
		{
			continue;
		}

		const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(SampledChunk.ChunkCoord);
		const FIntVector BuildMin(
			FMath::Max(SampledRegion.SampleMin.X, ChunkOrigin.X),
			FMath::Max(SampledRegion.SampleMin.Y, ChunkOrigin.Y),
			FMath::Max(SampledRegion.SampleMin.Z, ChunkOrigin.Z));
		const FIntVector BuildMax(
			FMath::Min(SampleMax.X, ChunkOrigin.X + GDualContourChunkSize),
			FMath::Min(SampleMax.Y, ChunkOrigin.Y + GDualContourChunkSize),
			FMath::Min(SampleMax.Z, ChunkOrigin.Z + GDualContourChunkSize));
		FDensityChunk* TargetChunk = DensityChunks.Find(SampledChunk.ChunkCoord);
		for (int32 SampleZ = BuildMin.Z; SampleZ < BuildMax.Z; ++SampleZ)
			for (int32 SampleY = BuildMin.Y; SampleY < BuildMax.Y; ++SampleY)
				for (int32 SampleX = BuildMin.X; SampleX < BuildMax.X; ++SampleX)
				{
					const uint16 LocalIndex = DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ);
					const uint16 SamplerDensity = SampledChunk.Density.IsUniform()
						                              ? SampledChunk.Density.UniformValue
						                              : SampledChunk.Density.DensitySamples[LocalIndex];
					if (SamplerDensity == 0 || (bExcavate && SamplerDensity < GDualContourIsoValue))
						continue;

					const uint16 OldDensity =
						TargetChunk
							? (TargetChunk->IsUniform()
								   ? TargetChunk->UniformValue
								   : TargetChunk->DensitySamples[LocalIndex])
							: 0;
					uint16 NewDensity = FMath::Max(OldDensity, SamplerDensity);
					if (bExcavate)
					{
						const uint16 DifferenceDensity =
							static_cast<uint16>(2u * static_cast<uint32>(GDualContourIsoValue) - SamplerDensity);
						NewDensity = FMath::Min(OldDensity, DifferenceDensity);
					}
					if (NewDensity == OldDensity)
						continue;

					if (!TargetChunk)
						TargetChunk = &DensityChunks.FindOrAdd(SampledChunk.ChunkCoord);
					TargetChunk->SetDensitySample(LocalIndex, NewDensity);
					ModifiedChunks.Add(SampledChunk.ChunkCoord);
					ModifiedSampleMin.X = FMath::Min(ModifiedSampleMin.X, SampleX);
					ModifiedSampleMin.Y = FMath::Min(ModifiedSampleMin.Y, SampleY);
					ModifiedSampleMin.Z = FMath::Min(ModifiedSampleMin.Z, SampleZ);
					ModifiedSampleMax.X = FMath::Max(ModifiedSampleMax.X, SampleX);
					ModifiedSampleMax.Y = FMath::Max(ModifiedSampleMax.Y, SampleY);
					ModifiedSampleMax.Z = FMath::Max(ModifiedSampleMax.Z, SampleZ);
				}
	}
	if (ModifiedChunks.IsEmpty())
		return false;
	CompactDensityChunks(ModifiedChunks);
	RecordModifiedDensityChunks(ModifiedChunks);

	const FIntVector CellRangeMin(FMath::Max(0, ModifiedSampleMin.X - 1), FMath::Max(0, ModifiedSampleMin.Y - 1),
		FMath::Max(0, ModifiedSampleMin.Z - 1));
	const FIntVector CellRangeMax(FMath::Min(CellCount.X, ModifiedSampleMax.X + 1),
		FMath::Min(CellCount.Y, ModifiedSampleMax.Y + 1), FMath::Min(CellCount.Z, ModifiedSampleMax.Z + 1));
	AsyncRebuildCellsInRange(CellRangeMin, CellRangeMax);
	OutAffectedCellMin = CellRangeMin;
	OutAffectedCellMax = CellRangeMax;
	return true;
}

bool UDualContour::ApplyPendingBatch(FDualContourPendingBatch& Batch, TFunctionRef<void(const FIntVector&, uint16, uint16)> OnSampleChanged)
{
	if (!Batch.bOpen || Batch.Owner != this || !HasCurrentGeneratedData())
		return false;
	Batch.bOpen = false;
	Batch.Owner = nullptr;
	EnsureRebuildComplete();

	TSet<FIntVector> ActuallyDirtyChunks;
	const FIntVector SampleDims = GetSampleDimensions();
	for (const TPair<FIntVector, TMap<uint16, FDualContourPendingSample>>& ChunkPair : Batch.ChunkSamples)
	{
		const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkPair.Key);
		for (const TPair<uint16, FDualContourPendingSample>& SamplePair : ChunkPair.Value)
		{
			const FIntVector SampleCoord = ChunkOrigin + DualContourUtils::ChunkLocalCoord(SamplePair.Key);
			if (!DualContourUtils::IsValidCoordinate(SampleDims, SampleCoord.X, SampleCoord.Y, SampleCoord.Z))
				continue;
			const uint16 Before = GetDensity(SampleCoord.X, SampleCoord.Y, SampleCoord.Z);
			const uint16 After = FDensityChunk::EncodeDensity(SamplePair.Value.WorkingValue);
			if (Before == After)
				continue;
			WriteDirtyDensitySample(SampleCoord.X, SampleCoord.Y, SampleCoord.Z, After, ActuallyDirtyChunks);
			OnSampleChanged(SampleCoord, Before, After);
		}
	}
	Batch.ChunkSamples.Reset();
	if (ActuallyDirtyChunks.IsEmpty())
		return false;

	CompactDensityChunks(ActuallyDirtyChunks);
	RecordModifiedDensityChunks(ActuallyDirtyChunks);
	AsyncRebuildDirtyCellChunks(MoveTemp(ActuallyDirtyChunks));
	return true;
}

void UDualContour::WriteDirtyDensitySample(int32 SampleX, int32 SampleY, int32 SampleZ, uint16 Density, TSet<FIntVector>& DirtyChunks)
{
	const FIntVector SampleDims = GetSampleDimensions();
	if (!DualContourUtils::IsValidCoordinate(SampleDims, SampleX, SampleY, SampleZ) || GetDensity(SampleX, SampleY, SampleZ) == Density)
		return;
	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(SampleX, SampleY, SampleZ);
	FDensityChunk& Chunk = DensityChunks.FindOrAdd(ChunkCoord);
	Chunk.SetDensitySample(DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ), Density);
	DirtyChunks.Add(ChunkCoord);
}

void UDualContour::CompactAllDensityChunks()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_CompactAllDensityChunks);
	for (auto ChunkIt = DensityChunks.CreateIterator(); ChunkIt; ++ChunkIt)
	{
		FDensityChunk& Chunk = ChunkIt.Value();
		if (Chunk.TryCollapse() && Chunk.UniformValue == 0)
			ChunkIt.RemoveCurrent();
	}
	DensityChunks.Compact();
}

void UDualContour::CompactDensityChunks(const TSet<FIntVector>& ChunkCoords)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_CompactDensityChunks);
	for (const FIntVector& ChunkCoord : ChunkCoords)
	{
		FDensityChunk* Chunk = DensityChunks.Find(ChunkCoord);
		if (Chunk && Chunk->TryCollapse() && Chunk->UniformValue == 0)
			DensityChunks.Remove(ChunkCoord);
	}
}

bool UDualContour::ApplyPendingMaterialBatch(FDualContourPendingMaterialBatch& Batch, FDualContourMaterialEditResult& OutResult)
{
	OutResult = FDualContourMaterialEditResult();
	if (!HasCurrentGeneratedData() || !Batch.bOpen || Batch.Owner != this)
		return false;
	Batch.bOpen = false;
	Batch.Owner = nullptr;
	EnsureRebuildComplete();

	TSet<FIntVector> DirtyChunks;
	FIntVector SampleMin = GetSampleDimensions();
	FIntVector SampleMax(-1, -1, -1);
	for (const TPair<FIntVector, TMap<uint16, FDualContourPendingMaterialSample>>& ChunkPair : Batch.ChunkSamples)
	{
		const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkPair.Key);
		for (const TPair<uint16, FDualContourPendingMaterialSample>& SamplePair : ChunkPair.Value)
		{
			if (SamplePair.Value.Before == SamplePair.Value.WorkingId)
				continue;
			const FIntVector SampleCoord = ChunkOrigin + DualContourUtils::ChunkLocalCoord(SamplePair.Key);
			WriteDirtyMaterialSample(SampleCoord.X, SampleCoord.Y, SampleCoord.Z, SamplePair.Value.WorkingId, DirtyChunks);
			FDualContourMaterialSampleDelta& Delta = OutResult.Deltas.AddDefaulted_GetRef();
			Delta.SampleCoord = SampleCoord;
			Delta.Before = SamplePair.Value.Before;
			Delta.After = SamplePair.Value.WorkingId;
			SampleMin.X = FMath::Min(SampleMin.X, SampleCoord.X);
			SampleMin.Y = FMath::Min(SampleMin.Y, SampleCoord.Y);
			SampleMin.Z = FMath::Min(SampleMin.Z, SampleCoord.Z);
			SampleMax.X = FMath::Max(SampleMax.X, SampleCoord.X);
			SampleMax.Y = FMath::Max(SampleMax.Y, SampleCoord.Y);
			SampleMax.Z = FMath::Max(SampleMax.Z, SampleCoord.Z);
		}
	}
	Batch.ChunkSamples.Reset();
	if (OutResult.IsEmpty())
		return false;
	CompactMaterialChunks(DirtyChunks);
	RecordModifiedMaterialChunks(DirtyChunks);
	BroadcastMaterialSampleRange(SampleMin, SampleMax);
	return true;
}

bool UDualContour::ApplyMaterialEditDeltas(TConstArrayView<FDualContourMaterialSampleDelta> Deltas, bool bUseAfterValues,
	FDualContourMaterialEditResult* OutResult)
{
	if (!HasCurrentGeneratedData() || Deltas.IsEmpty())
		return false;
	EnsureRebuildComplete();
	TSet<FIntVector> DirtyChunks;
	FIntVector SampleMin = GetSampleDimensions();
	FIntVector SampleMax(-1, -1, -1);
	for (const FDualContourMaterialSampleDelta& Delta : Deltas)
	{
		WriteDirtyMaterialSample(Delta.SampleCoord.X, Delta.SampleCoord.Y, Delta.SampleCoord.Z,
			bUseAfterValues ? Delta.After : Delta.Before, DirtyChunks);
		SampleMin.X = FMath::Min(SampleMin.X, Delta.SampleCoord.X);
		SampleMin.Y = FMath::Min(SampleMin.Y, Delta.SampleCoord.Y);
		SampleMin.Z = FMath::Min(SampleMin.Z, Delta.SampleCoord.Z);
		SampleMax.X = FMath::Max(SampleMax.X, Delta.SampleCoord.X);
		SampleMax.Y = FMath::Max(SampleMax.Y, Delta.SampleCoord.Y);
		SampleMax.Z = FMath::Max(SampleMax.Z, Delta.SampleCoord.Z);
	}
	if (DirtyChunks.IsEmpty())
		return false;
	CompactMaterialChunks(DirtyChunks);
	RecordModifiedMaterialChunks(DirtyChunks);
	BroadcastMaterialSampleRange(SampleMin, SampleMax);
	if (OutResult)
		OutResult->Deltas = TArray<FDualContourMaterialSampleDelta>(Deltas);
	return true;
}

void UDualContour::WriteDirtyMaterialSample(int32 SampleX, int32 SampleY, int32 SampleZ, uint8 MaterialId,
	TSet<FIntVector>& DirtyChunks)
{
	if (!DualContourUtils::IsValidCoordinate(GetSampleDimensions(), SampleX, SampleY, SampleZ)
	    || GetMaterialId(SampleX, SampleY, SampleZ) == MaterialId)
		return;
	const FIntVector ChunkCoord = DualContourUtils::ChunkCoord(SampleX, SampleY, SampleZ);
	FMaterialIdChunk& Chunk = MaterialChunks.FindOrAdd(ChunkCoord);
	Chunk.SetMaterialId(DualContourUtils::ChunkLocalIndex(SampleX, SampleY, SampleZ), MaterialId);
	DirtyChunks.Add(ChunkCoord);
}

void UDualContour::CompactAllMaterialChunks()
{
	for (auto ChunkIt = MaterialChunks.CreateIterator(); ChunkIt; ++ChunkIt)
		if (ChunkIt.Value().TryCollapse() && ChunkIt.Value().UniformId == 0)
			ChunkIt.RemoveCurrent();
	MaterialChunks.Compact();
}

void UDualContour::CompactMaterialChunks(const TSet<FIntVector>& ChunkCoords)
{
	for (const FIntVector& ChunkCoord : ChunkCoords)
		if (FMaterialIdChunk* Chunk = MaterialChunks.Find(ChunkCoord))
			if (Chunk->TryCollapse() && Chunk->UniformId == 0)
				MaterialChunks.Remove(ChunkCoord);
}

void UDualContour::BroadcastMaterialSampleRange(FIntVector SampleMin, FIntVector SampleMaxInclusive)
{
	const FIntVector CellMin(FMath::Max(0, SampleMin.X - 1), FMath::Max(0, SampleMin.Y - 1), FMath::Max(0, SampleMin.Z - 1));
	const FIntVector CellMax(FMath::Min(CellCount.X, SampleMaxInclusive.X + 1),
		FMath::Min(CellCount.Y, SampleMaxInclusive.Y + 1), FMath::Min(CellCount.Z, SampleMaxInclusive.Z + 1));
	if (CellMin.X < CellMax.X && CellMin.Y < CellMax.Y && CellMin.Z < CellMax.Z)
		OnMaterialsChanged.Broadcast(CellMin, CellMax);
}

float UDualContour::TrilinearDensity(const FVector& GridPos) const
{
	const FIntVector Dims = GetSampleDimensions();
	const float GridX = FMath::Clamp(GridPos.X, 0., static_cast<double>(Dims.X - 1));
	const float GridY = FMath::Clamp(GridPos.Y, 0., static_cast<double>(Dims.Y - 1));
	const float GridZ = FMath::Clamp(GridPos.Z, 0., static_cast<double>(Dims.Z - 1));
	const int32 LowerX = FMath::Clamp(FMath::FloorToInt(GridX), 0, Dims.X - 2);
	const int32 LowerY = FMath::Clamp(FMath::FloorToInt(GridY), 0, Dims.Y - 2);
	const int32 LowerZ = FMath::Clamp(FMath::FloorToInt(GridZ), 0, Dims.Z - 2);
	const int32 UpperX = LowerX + 1, UpperY = LowerY + 1, UpperZ = LowerZ + 1;
	const float BlendX = GridX - LowerX, BlendY = GridY - LowerY, BlendZ = GridZ - LowerZ;

	return FMath::Lerp(
		FMath::Lerp(
			FMath::Lerp(GetLinearDensity(LowerX, LowerY, LowerZ), GetLinearDensity(UpperX, LowerY, LowerZ), BlendX),
			FMath::Lerp(GetLinearDensity(LowerX, UpperY, LowerZ), GetLinearDensity(UpperX, UpperY, LowerZ), BlendX),
			BlendY),
		FMath::Lerp(
			FMath::Lerp(GetLinearDensity(LowerX, LowerY, UpperZ), GetLinearDensity(UpperX, LowerY, UpperZ), BlendX),
			FMath::Lerp(GetLinearDensity(LowerX, UpperY, UpperZ), GetLinearDensity(UpperX, UpperY, UpperZ), BlendX),
			BlendY),
		BlendZ);
}

FVector UDualContour::CalculateCentralDifferenceNormal(const FVector& GridPosition) const
{
	constexpr float Step = 0.125f;
	return (-FVector(
			TrilinearDensity(GridPosition + FVector(Step, 0, 0)) - TrilinearDensity(GridPosition - FVector(Step, 0, 0)),
			TrilinearDensity(GridPosition + FVector(0, Step, 0)) - TrilinearDensity(GridPosition - FVector(0, Step, 0)),
			TrilinearDensity(GridPosition + FVector(0, 0, Step)) - TrilinearDensity(GridPosition - FVector(0, 0, Step))))
		.GetSafeNormal();
}

bool UDualContour::ApplyModifiedDensityChunks(const FDualContourDensityChunks& InModifiedDensityChunks)
{
	if (!HasCurrentGeneratedData())
		return false;
	EnsureRebuildComplete();

	const FIntVector SampleDimensions = GetSampleDimensions();
	const int32 ExpandedChunkSize = GDualContourChunkSize * GDualContourChunkSize * GDualContourChunkSize;
	for (const TPair<FIntVector, FDensityChunk>& Pair : InModifiedDensityChunks)
	{
		const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(Pair.Key);
		if (ChunkOrigin.X < 0 || ChunkOrigin.Y < 0 || ChunkOrigin.Z < 0
		    || ChunkOrigin.X >= SampleDimensions.X || ChunkOrigin.Y >= SampleDimensions.Y || ChunkOrigin.Z >= SampleDimensions.Z
		    || (!Pair.Value.IsUniform() && Pair.Value.DensitySamples.Num() != ExpandedChunkSize))
		{
			return false;
		}
	}

	TSet<FIntVector> DirtyChunks;
	DirtyChunks.Reserve(InModifiedDensityChunks.Num());
	for (const TPair<FIntVector, FDensityChunk>& Pair : InModifiedDensityChunks)
	{
		FDensityChunk Chunk = Pair.Value;
		Chunk.TryCollapse();
		if (Chunk.IsUniform() && Chunk.UniformValue == 0)
			DensityChunks.Remove(Pair.Key);
		else
			DensityChunks.Add(Pair.Key, MoveTemp(Chunk));
		DirtyChunks.Add(Pair.Key);
	}
	ModifiedDensityChunks = InModifiedDensityChunks;

	if (!DirtyChunks.IsEmpty())
		AsyncRebuildDirtyCellChunks(MoveTemp(DirtyChunks));
	return true;
}

bool UDualContour::ApplyModifiedMaterialChunks(const FDualContourMaterialChunks& InModifiedMaterialChunks)
{
	if (!HasCurrentGeneratedData())
		return false;
	EnsureRebuildComplete();
	const FIntVector SampleDimensions = GetSampleDimensions();
	const int32 ExpandedChunkSize = GDualContourChunkSize * GDualContourChunkSize * GDualContourChunkSize;
	for (const TPair<FIntVector, FMaterialIdChunk>& Pair : InModifiedMaterialChunks)
	{
		const FIntVector Origin = DualContourUtils::ChunkOrigin(Pair.Key);
		if (Origin.X < 0 || Origin.Y < 0 || Origin.Z < 0 || Origin.X >= SampleDimensions.X
		    || Origin.Y >= SampleDimensions.Y || Origin.Z >= SampleDimensions.Z
		    || (!Pair.Value.IsUniform() && Pair.Value.MaterialIds.Num() != ExpandedChunkSize))
			return false;
	}
	for (const TPair<FIntVector, FMaterialIdChunk>& Pair : InModifiedMaterialChunks)
	{
		FMaterialIdChunk Chunk = Pair.Value;
		Chunk.TryCollapse();
		if (Chunk.IsUniform() && Chunk.UniformId == 0)
			MaterialChunks.Remove(Pair.Key);
		else
			MaterialChunks.Add(Pair.Key, MoveTemp(Chunk));
	}
	ModifiedMaterialChunks = InModifiedMaterialChunks;
	if (!InModifiedMaterialChunks.IsEmpty())
		OnMaterialsChanged.Broadcast(FIntVector::ZeroValue, CellCount);
	return true;
}

FDualContourCell UDualContour::CreateNewCell(int32 CellX, int32 CellY, int32 CellZ) const
{
	FDualContourCell Cell;
	const FVector CellMin = FVector(CellX, CellY, CellZ) * CellSize;
	const FVector CellMax = FVector(CellX + 1, CellY + 1, CellZ + 1) * CellSize;
	const FVector CellCenter = (CellMin + CellMax) * 0.5;
	Cell.Center = CellCenter;

	bool bHasInside = false, bHasOutside = false;
	for (int32 Z = 0; Z <= 1; ++Z)
		for (int32 Y = 0; Y <= 1; ++Y)
			for (int32 X = 0; X <= 1; ++X)
				(GetDensity(CellX + X, CellY + Y, CellZ + Z) >= GDualContourIsoValue
					 ? bHasInside
					 : bHasOutside) = true;
	Cell.bActive = bHasInside && bHasOutside;
	if (!Cell.bActive)
		return Cell;

	const bool bUseQEF = VertexSolveMode == EDualContourVertexSolveMode::QEF;
	constexpr double Lambda = 0.1;
	double Matrix[3][3] = {};
	double Vector[3] = {};
	FVector IntersectionSum = FVector::ZeroVector;
	double IntersectionWeightSum = 0.0;
	int32 NumIntersections = 0;
	for (int32 EdgeIndex = 0; EdgeIndex < 12; ++EdgeIndex)
	{
		const int32* A = EdgeCorners[EdgeIndex][0];
		const int32* B = EdgeCorners[EdgeIndex][1];
		const int32 AX = CellX + A[0], AY = CellY + A[1], AZ = CellZ + A[2];
		const int32 BX = CellX + B[0], BY = CellY + B[1], BZ = CellZ + B[2];
		const uint16 DensityA = GetDensity(AX, AY, AZ);
		const uint16 DensityB = GetDensity(BX, BY, BZ);
		if ((DensityA < GDualContourIsoValue) == (DensityB < GDualContourIsoValue))
			continue;
		const float LinearDensityA = FDensityChunk::DecodeLinearDensity(DensityA);
		const float LinearDensityB = FDensityChunk::DecodeLinearDensity(DensityB);

		const float Alpha = (GDualContourLinearIsoValue - LinearDensityA) / (LinearDensityB - LinearDensityA);
		const FVector GridPosition = FVector(AX, AY, AZ) + Alpha * (FVector(BX, BY, BZ) - FVector(AX, AY, AZ));
		const FVector WorldPosition = GridPosition * CellSize;
		const FVector Normal = CalculateCentralDifferenceNormal(GridPosition);
		if (Normal.IsNearlyZero())
			continue;
		const double IntersectionWeight = FMath::Pow(FMath::Max(FMath::Abs(LinearDensityB - LinearDensityA), UE_SMALL_NUMBER), 4.0f);

		if (bUseQEF)
		{
			const double NX = Normal.X, NY = Normal.Y, NZ = Normal.Z;
			const double Distance = FVector::DotProduct(Normal, WorldPosition);
			Matrix[0][0] += NX * NX;
			Matrix[0][1] += NX * NY;
			Matrix[0][2] += NX * NZ;
			Matrix[1][0] += NY * NX;
			Matrix[1][1] += NY * NY;
			Matrix[1][2] += NY * NZ;
			Matrix[2][0] += NZ * NX;
			Matrix[2][1] += NZ * NY;
			Matrix[2][2] += NZ * NZ;
			Vector[0] += NX * Distance;
			Vector[1] += NY * Distance;
			Vector[2] += NZ * Distance;
		}
		else
		{
			IntersectionSum += WorldPosition * IntersectionWeight;
			IntersectionWeightSum += IntersectionWeight;
		}
		++NumIntersections;
	}

	if (NumIntersections > 0)
	{
		if (bUseQEF)
		{
			Matrix[0][0] += Lambda;
			Matrix[1][1] += Lambda;
			Matrix[2][2] += Lambda;
			Vector[0] += Lambda * CellCenter.X;
			Vector[1] += Lambda * CellCenter.Y;
			Vector[2] += Lambda * CellCenter.Z;
			double Position[3];
			if (Solve3x3(Matrix, Vector, Position))
			{
				Cell.Center.X = FMath::Clamp(Position[0], CellMin.X, CellMax.X);
				Cell.Center.Y = FMath::Clamp(Position[1], CellMin.Y, CellMax.Y);
				Cell.Center.Z = FMath::Clamp(Position[2], CellMin.Z, CellMax.Z);
			}
		}
		else
		{
			Cell.Center = IntersectionWeightSum > UE_SMALL_NUMBER
				              ? IntersectionSum / IntersectionWeightSum
				              : CellCenter;
		}
		Cell.Normal = CalculateCentralDifferenceNormal(Cell.Center / CellSize);
		if (Cell.Normal.IsNearlyZero())
			Cell.Normal = FVector::UpVector;
	}
	return Cell;
}

void UDualContour::AsyncRebuildCells(bool bBroadcastCellsRebuilt)
{
	AsyncRebuildCellsInRange(FIntVector::ZeroValue, CellCount, bBroadcastCellsRebuilt);
}

void UDualContour::RebuildCellsInRange(FIntVector RangeMin, FIntVector RangeMax, bool bBroadcastCellsRebuilt)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_RebuildCellsInRange);
	RangeMin = FIntVector(FMath::Max(0, RangeMin.X), FMath::Max(0, RangeMin.Y), FMath::Max(0, RangeMin.Z));
	RangeMax = FIntVector(FMath::Min(CellCount.X, RangeMax.X), FMath::Min(CellCount.Y, RangeMax.Y), FMath::Min(CellCount.Z, RangeMax.Z));
	const int64 ProcessedCellCount = static_cast<int64>(FMath::Max(0, RangeMax.X - RangeMin.X))
	                                 * FMath::Max(0, RangeMax.Y - RangeMin.Y) * FMath::Max(0, RangeMax.Z - RangeMin.Z);
	if (ProcessedCellCount == 0)
	{
		return;
	}

	// The game thread remains blocked in ParallelFor, so worker threads can safely perform
	// const queries on this object while writing only to their own output slots.
	const FIntVector ChunkMin(RangeMin.X / GDualContourChunkSize, RangeMin.Y / GDualContourChunkSize,
		RangeMin.Z / GDualContourChunkSize);
	const FIntVector ChunkMaxExclusive(FMath::DivideAndRoundUp(RangeMax.X, GDualContourChunkSize),
		FMath::DivideAndRoundUp(RangeMax.Y, GDualContourChunkSize),
		FMath::DivideAndRoundUp(RangeMax.Z, GDualContourChunkSize));

	TArray<FIntVector> ChunkCoords;
	for (int32 ChunkZ = ChunkMin.Z; ChunkZ < ChunkMaxExclusive.Z; ++ChunkZ)
		for (int32 ChunkY = ChunkMin.Y; ChunkY < ChunkMaxExclusive.Y; ++ChunkY)
			for (int32 ChunkX = ChunkMin.X; ChunkX < ChunkMaxExclusive.X; ++ChunkX)
				ChunkCoords.Emplace(ChunkX, ChunkY, ChunkZ);

	TArray<FCellChunk> BuiltChunks;
	BuiltChunks.SetNum(ChunkCoords.Num());

	ParallelFor(TEXT("DualContour.BuildCellChunks"), ChunkCoords.Num(), 1,
		[this, RangeMin, RangeMax, &ChunkCoords, &BuiltChunks](int32 Index)
		{
			const FIntVector ChunkCoord = ChunkCoords[Index];
			const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkCoord);
			const FIntVector BuildMin(FMath::Max(RangeMin.X, ChunkOrigin.X),
				FMath::Max(RangeMin.Y, ChunkOrigin.Y), FMath::Max(RangeMin.Z, ChunkOrigin.Z));
			const FIntVector BuildMax(FMath::Min(RangeMax.X, ChunkOrigin.X + GDualContourChunkSize),
				FMath::Min(RangeMax.Y, ChunkOrigin.Y + GDualContourChunkSize),
				FMath::Min(RangeMax.Z, ChunkOrigin.Z + GDualContourChunkSize));

			FCellChunk& BuiltChunk = BuiltChunks[Index];
			for (int32 CellZ = BuildMin.Z; CellZ < BuildMax.Z; ++CellZ)
				for (int32 CellY = BuildMin.Y; CellY < BuildMax.Y; ++CellY)
					for (int32 CellX = BuildMin.X; CellX < BuildMax.X; ++CellX)
					{
						FDualContourCell Cell = CreateNewCell(CellX, CellY, CellZ);
						if (!Cell.bActive)
							continue;
						const uint16 LocalKey = DualContourUtils::ChunkLocalIndex(CellX, CellY, CellZ);
						BuiltChunk.ActiveCells.Add(LocalKey, MoveTemp(Cell));
					}
		}, EParallelForFlags::Unbalanced);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_MergeCellChunks);
		for (int32 Index = 0; Index < ChunkCoords.Num(); ++Index)
		{
			const FIntVector ChunkCoord = ChunkCoords[Index];
			const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkCoord);

			if (FCellChunk* ExistingChunk = CellChunks.Find(ChunkCoord))
			{
				for (auto CellIt = ExistingChunk->ActiveCells.CreateIterator(); CellIt; ++CellIt)
				{
					const uint16 LocalKey = CellIt.Key();
					const FIntVector CellCoord = ChunkOrigin + DualContourUtils::ChunkLocalCoord(LocalKey);
					if (CellCoord.X >= RangeMin.X && CellCoord.X < RangeMax.X && CellCoord.Y >= RangeMin.Y && CellCoord.Y < RangeMax.Y
					    && CellCoord.Z >= RangeMin.Z && CellCoord.Z < RangeMax.Z)
						CellIt.RemoveCurrent();
				}
			}

			FCellChunk& BuiltChunk = BuiltChunks[Index];
			if (!BuiltChunk.ActiveCells.IsEmpty())
			{
				FCellChunk& TargetChunk = CellChunks.FindOrAdd(ChunkCoord);
				for (TPair<uint16, FDualContourCell>& CellPair : BuiltChunk.ActiveCells)
					TargetChunk.ActiveCells.Add(CellPair.Key, MoveTemp(CellPair.Value));
			}

			const FCellChunk* UpdatedChunk = CellChunks.Find(ChunkCoord);
			if (UpdatedChunk && UpdatedChunk->ActiveCells.IsEmpty())
				CellChunks.Remove(ChunkCoord);
		}
	}

	const float Relaxation = FMath::Clamp(VertexRelaxation, 0.0f, 1.0f);
	if (Relaxation > 0.0f && RangeMin.X == 0 && RangeMin.Y == 0 && RangeMin.Z == 0
	    && RangeMax.X == CellCount.X && RangeMax.Y == CellCount.Y && RangeMax.Z == CellCount.Z)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_RelaxeCellChunks);
		const float MinimumNormalCosine = FMath::Clamp(RelaxationNormalCosine, -1.0f, 1.0f);
		TMap<FIntVector, FVector> RelaxedCenters;
		for (const TPair<FIntVector, FCellChunk>& ChunkPair : CellChunks)
		{
			const FIntVector ChunkOrigin = DualContourUtils::ChunkOrigin(ChunkPair.Key);
			for (const TPair<uint16, FDualContourCell>& CellPair : ChunkPair.Value.ActiveCells)
			{
				const FIntVector CellCoord = ChunkOrigin + DualContourUtils::ChunkLocalCoord(CellPair.Key);
				FVector Sum = FVector::ZeroVector;
				int32 Count = 0;
				for (const FIntVector& Offset : {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0),
				                                 FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1)})
				{
					if (const FDualContourCell* Neighbor = GetCell(CellCoord.X + Offset.X, CellCoord.Y + Offset.Y, CellCoord.Z + Offset.Z))
					{
						if (FVector::DotProduct(CellPair.Value.Normal, Neighbor->Normal) < MinimumNormalCosine)
							continue;
						Sum += Neighbor->Center;
						++Count;
					}
				}
				if (Count > 0)
				{
					// Relaxing in 3D shrinks the surface and pulls vertices across cap/wall features.
					// Keep only the displacement tangent to this cell's Hermite normal so the pass
					// smooths neighbouring cells without moving the surface along its normal.
					FVector Delta = Sum / Count - CellPair.Value.Center;
					Delta -= CellPair.Value.Normal * FVector::DotProduct(Delta, CellPair.Value.Normal);
					RelaxedCenters.Add(CellCoord, CellPair.Value.Center + Delta * Relaxation);
				}
			}
		}
		for (const TPair<FIntVector, FVector>& Pair : RelaxedCenters)
			if (FDualContourCell* Cell = const_cast<FDualContourCell*>(GetCell(Pair.Key.X, Pair.Key.Y, Pair.Key.Z)))
				Cell->Center = Pair.Value;
	}
	if (bBroadcastCellsRebuilt && RangeMin.X < RangeMax.X && RangeMin.Y < RangeMax.Y && RangeMin.Z < RangeMax.Z)
	{
		if (IsInGameThread())
		{
			OnCellsRebuilt.Broadcast(RangeMin, RangeMax);
		}
		else
		{
			TWeakObjectPtr<UDualContour> WeakThis(this);
			AsyncTask(ENamedThreads::GameThread, [WeakThis, RangeMin, RangeMax]()
			{
				if (WeakThis.IsValid())
					WeakThis->OnCellsRebuilt.Broadcast(RangeMin, RangeMax);
			});
		}
	}
}

void UDualContour::AsyncRebuildCellsInRange(FIntVector RangeMin, FIntVector RangeMax, bool bBroadcastCellsRebuilt)
{
	PendingRebuildFuture = Async(EAsyncExecution::ThreadPool, [this, RangeMin, RangeMax, bBroadcastCellsRebuilt]()
	{
		RebuildCellsInRange(RangeMin, RangeMax, bBroadcastCellsRebuilt);
	});
}

void UDualContour::RebuildDirtyCellChunks(const TSet<FIntVector>& DirtyDensityChunks, bool bBroadcastCellsRebuilt)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_RebuildDirtyCellChunks);
	TSet<FIntVector> DirtyCellChunks;
	for (const FIntVector& DensityChunk : DirtyDensityChunks)
	{
		const FIntVector SampleMin = DualContourUtils::ChunkOrigin(DensityChunk);
		const FIntVector SampleMax(FMath::Min(CellCount.X, SampleMin.X + GDualContourChunkSize),
			FMath::Min(CellCount.Y, SampleMin.Y + GDualContourChunkSize), FMath::Min(CellCount.Z, SampleMin.Z + GDualContourChunkSize));
		const FIntVector CellMin(FMath::Max(0, SampleMin.X - 2), FMath::Max(0, SampleMin.Y - 2), FMath::Max(0, SampleMin.Z - 2));
		const FIntVector CellMax(FMath::Min(CellCount.X, SampleMax.X + 2), FMath::Min(CellCount.Y, SampleMax.Y + 2),
			FMath::Min(CellCount.Z, SampleMax.Z + 2));
		for (int32 ChunkZ = CellMin.Z / GDualContourChunkSize; ChunkZ <= (CellMax.Z - 1) / GDualContourChunkSize; ++ChunkZ)
			for (int32 ChunkY = CellMin.Y / GDualContourChunkSize; ChunkY <= (CellMax.Y - 1) / GDualContourChunkSize; ++ChunkY)
				for (int32 ChunkX = CellMin.X / GDualContourChunkSize; ChunkX <= (CellMax.X - 1) / GDualContourChunkSize; ++ChunkX)
					DirtyCellChunks.Add(FIntVector(ChunkX, ChunkY, ChunkZ));
	}

	if (DirtyCellChunks.IsEmpty())
		return;

	TArray<FIntVector> ChunkCoords = DirtyCellChunks.Array();
	TArray<FCellChunk> BuiltChunks;
	BuiltChunks.SetNum(ChunkCoords.Num());
	ParallelFor(TEXT("DualContour.BuildDirtyCellChunks"), ChunkCoords.Num(), 1,
		[this, &ChunkCoords, &BuiltChunks](int32 Index)
		{
			const FIntVector ChunkCoord = ChunkCoords[Index];
			const FIntVector BuildMin(ChunkCoord.X * GDualContourChunkSize,
				ChunkCoord.Y * GDualContourChunkSize, ChunkCoord.Z * GDualContourChunkSize);
			const FIntVector BuildMax(FMath::Min(CellCount.X, BuildMin.X + GDualContourChunkSize),
				FMath::Min(CellCount.Y, BuildMin.Y + GDualContourChunkSize),
				FMath::Min(CellCount.Z, BuildMin.Z + GDualContourChunkSize));
			FCellChunk& BuiltChunk = BuiltChunks[Index];
			for (int32 CellZ = BuildMin.Z; CellZ < BuildMax.Z; ++CellZ)
				for (int32 CellY = BuildMin.Y; CellY < BuildMax.Y; ++CellY)
					for (int32 CellX = BuildMin.X; CellX < BuildMax.X; ++CellX)
					{
						FDualContourCell Cell = CreateNewCell(CellX, CellY, CellZ);
						if (Cell.bActive)
							BuiltChunk.ActiveCells.Add(DualContourUtils::ChunkLocalIndex(CellX, CellY, CellZ), MoveTemp(Cell));
					}
		}, EParallelForFlags::Unbalanced);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DualContour_MergeDirtyCellChunks);
		for (int32 Index = 0; Index < ChunkCoords.Num(); ++Index)
		{
			if (BuiltChunks[Index].ActiveCells.IsEmpty())
				CellChunks.Remove(ChunkCoords[Index]);
			else
				CellChunks.Add(ChunkCoords[Index], MoveTemp(BuiltChunks[Index]));
		}
	}

	if (bBroadcastCellsRebuilt)
	{
		FIntVector AffectedCellMin = CellCount;
		FIntVector AffectedCellMax = FIntVector::ZeroValue;
		for (const FIntVector& ChunkCoord : ChunkCoords)
		{
			const FIntVector ChunkCellMin(ChunkCoord.X * GDualContourChunkSize,
				ChunkCoord.Y * GDualContourChunkSize, ChunkCoord.Z * GDualContourChunkSize);
			const FIntVector ChunkCellMax(FMath::Min(CellCount.X, ChunkCellMin.X + GDualContourChunkSize),
				FMath::Min(CellCount.Y, ChunkCellMin.Y + GDualContourChunkSize),
				FMath::Min(CellCount.Z, ChunkCellMin.Z + GDualContourChunkSize));
			AffectedCellMin.X = FMath::Min(AffectedCellMin.X, ChunkCellMin.X);
			AffectedCellMin.Y = FMath::Min(AffectedCellMin.Y, ChunkCellMin.Y);
			AffectedCellMin.Z = FMath::Min(AffectedCellMin.Z, ChunkCellMin.Z);
			AffectedCellMax.X = FMath::Max(AffectedCellMax.X, ChunkCellMax.X);
			AffectedCellMax.Y = FMath::Max(AffectedCellMax.Y, ChunkCellMax.Y);
			AffectedCellMax.Z = FMath::Max(AffectedCellMax.Z, ChunkCellMax.Z);
		}
		if (IsInGameThread())
		{
			OnCellsRebuilt.Broadcast(AffectedCellMin, AffectedCellMax);
		}
		else
		{
			TWeakObjectPtr<UDualContour> WeakThis(this);
			AsyncTask(ENamedThreads::GameThread, [WeakThis, AffectedCellMin, AffectedCellMax]()
			{
				if (WeakThis.IsValid())
					WeakThis->OnCellsRebuilt.Broadcast(AffectedCellMin, AffectedCellMax);
			});
		}
	}
}

void UDualContour::AsyncRebuildDirtyCellChunks(TSet<FIntVector>&& DirtyDensityChunks, bool bBroadcastCellsRebuilt)
{
	PendingRebuildFuture = Async(EAsyncExecution::ThreadPool, [this, Dirty = MoveTemp(DirtyDensityChunks), bBroadcastCellsRebuilt]()
	{
		RebuildDirtyCellChunks(Dirty, bBroadcastCellsRebuilt);
	});
}

void UDualContour::RecordModifiedDensityChunks(const TSet<FIntVector>& ChunkCoords)
{
	for (const FIntVector& ChunkCoord : ChunkCoords)
	{
		if (const FDensityChunk* Chunk = DensityChunks.Find(ChunkCoord))
		{
			ModifiedDensityChunks.Add(ChunkCoord, *Chunk);
		}
		else
		{
			// A missing chunk means uniform zero in the main sparse grid. Keep an explicit
			// zero override so loading can remove a non-zero chunk from the base contour.
			ModifiedDensityChunks.Add(ChunkCoord, FDensityChunk());
		}
	}
}

void UDualContour::RecordModifiedMaterialChunks(const TSet<FIntVector>& ChunkCoords)
{
	for (const FIntVector& ChunkCoord : ChunkCoords)
	{
		if (const FMaterialIdChunk* Chunk = MaterialChunks.Find(ChunkCoord))
			ModifiedMaterialChunks.Add(ChunkCoord, *Chunk);
		else
			ModifiedMaterialChunks.Add(ChunkCoord, FMaterialIdChunk());
	}
}
