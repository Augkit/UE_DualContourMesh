#include "DualContourPlaneFit.h"
#include "DualContour.h"

namespace DualContourPlaneFit
{
namespace
{
constexpr double Tolerance = 2.0;
// Quantization must imply less than 0.001 cell of positional uncertainty
// before neighbourhood planes can replace the local Hermite constraints.
// Texture SDF units can be much smaller than procedural distance units.
constexpr double MinimumPlaneGradient = Tolerance / 0.001;
// A curved surface never yields an affine block, so a cell beside a cylinder rim
// would see only the flat cap and no constraint at all from the wall. Accept a
// tangent plane from the central difference while the second difference stays a
// small fraction of the gradient: a stencil straddling the crease blends two
// faces and produces a second difference comparable to the gradient itself.
struct FPlane
{
	FVector Normal;
	double Distance;
	double DensityScale;
	// Second-difference allowance this tangent fit needs to explain values.
	double Slack = 0.0;
	double Predict(const FVector& P) const { return DensityScale * (Distance - FVector::DotProduct(Normal, P)); }
};

FIntVector CornerOffset(int32 Index)
{
	return FIntVector(Index & 1, (Index >> 1) & 1, (Index >> 2) & 1);
}

bool IsUnsaturated(double D)
{
	return D > GDualContourMinLinearDensity + Tolerance && D < GDualContourMaxLinearDensity - Tolerance;
}
}

bool Fit(const UDualContour& Grid, FIntVector Cell, double Matrix[3][3], double RHS[3],
		int32& ConstraintCount, FVector& OutNormal)
{
	const auto Read = [&Grid](FIntVector P) { return double(Grid.GetLinearDensity(P.X, P.Y, P.Z)); };
	double CellDensities[8];
	for (int32 C = 0; C < 8; ++C)
		CellDensities[C] = Read(Cell + CornerOffset(C));
	TArray<FPlane, TInlineAllocator<16>> Candidates;
	const auto AddAffineBlock = [&](FIntVector Origin)
	{
		if (Origin.X < 0 || Origin.Y < 0 || Origin.Z < 0 || Origin.X >= Grid.CellCount.X
		    || Origin.Y >= Grid.CellCount.Y || Origin.Z >= Grid.CellCount.Z)
			return false;
		double D[8];
		for (int32 C = 0; C < 8; ++C)
		{
			D[C] = Read(Origin + CornerOffset(C));
			if (!IsUnsaturated(D[C]))
				return false;
		}
		const FVector Gradient(D[1] - D[0], D[2] - D[0], D[4] - D[0]);
		const double Scale = Gradient.Length();
		if (Scale < MinimumPlaneGradient)
			return false;
		for (int32 C = 0; C < 8; ++C)
			if (FMath::Abs(D[C] - D[0] - FVector::DotProduct(Gradient, FVector(CornerOffset(C)))) > Tolerance)
				return false;
		const FVector N = -Gradient / Scale;
		const double Distance = FVector::DotProduct(N, FVector(Origin - Cell)) + D[0] / Scale;
		if (!Candidates.ContainsByPredicate([&](const FPlane& P)
		{
			return FVector::DotProduct(P.Normal, N) > 0.999 && FMath::Abs(P.Distance - Distance) < 0.01;
		}))
			Candidates.Add({N, Distance, Scale});
		return true;
	};
	// An eight-corner block needs one cell of unsaturated data, unlike a central
	// derivative stencil spanning two cells. This matters at 25-unit spacing when
	// the default procedural density saturates only 32 units from the surface.
	if (!AddAffineBlock(Cell))
		for (int32 Z = -1; Z <= 2; ++Z)
			for (int32 Y = -1; Y <= 2; ++Y)
				for (int32 X = -1; X <= 2; ++X)
				{
					AddAffineBlock(Cell + FIntVector(X, Y, Z));
					const FIntVector P = Cell + FIntVector(X, Y, Z);
					if (P.X < 1 || P.Y < 1 || P.Z < 1 || P.X >= Grid.CellCount.X
					    || P.Y >= Grid.CellCount.Y || P.Z >= Grid.CellCount.Z)
						continue;
					const double D = Read(P);
					FVector Gradient = FVector::ZeroVector;
					bool bUsable = IsUnsaturated(D);
					double Slack = 0.0;
					for (int32 Axis = 0; Axis < 3 && bUsable; ++Axis)
					{
						FIntVector Step = FIntVector::ZeroValue;
						Step[Axis] = 1;
						const double A = Read(P - Step), B = Read(P + Step);
						bUsable = IsUnsaturated(A) && IsUnsaturated(B);
						Slack = FMath::Max(Slack, FMath::Abs(A + B - 2.0 * D));
						Gradient[Axis] = 0.5 * (B - A);
					}
					const double Magnitude = Gradient.Length();
					if (!bUsable || Magnitude < MinimumPlaneGradient || Slack > Tolerance)
						continue;
					const FVector N = -Gradient / Magnitude;
					const double Distance = FVector::DotProduct(N, FVector(P - Cell)) + D / Magnitude;
					if (!Candidates.ContainsByPredicate([&](const FPlane& Plane)
					{
						return FVector::DotProduct(Plane.Normal, N) > 0.999 && FMath::Abs(Plane.Distance - Distance) < 0.01;
					}))
						Candidates.Add({N, Distance, Magnitude, Slack});
				}

	// A union's convex corner is a minimum of planes in positive-solid density.
	// Difference cavities require the dual (maximum). Normalize the sign for the
	// validation only; preserve the original plane orientation for render normals.
	for (double Sign : {1.0, -1.0})
	{
		TArray<int32, TInlineAllocator<16>> Supporting;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			bool bSupports = true;
			for (int32 C = 0; C < 8 && bSupports; ++C)
			{
				const double Actual = Sign * CellDensities[C];
				// Negative saturation is an upper bound on the unknown true value.
				if (Actual <= GDualContourMinLinearDensity + Tolerance)
					continue;
				bSupports = Actual <= Sign * Candidates[Index].Predict(FVector(CornerOffset(C)))
				            + 4.0 * Tolerance + Candidates[Index].Slack;
			}
			// A tangent plane fitted only to exterior/corner samples can support the
			// field without being an actual face. Require interior evidence spanning
			// a plane, otherwise this redundant constraint bends the true face.
			// Beside a crease only a narrow wedge of the field has this plane as its
			// nearest feature, and that wedge can lie outside the two-cell stencil
			// used for the affine blocks. Search wider so a real face still collects
			// its three witnesses instead of being dropped for lack of room.
			constexpr int32 WitnessMin = -2;
			constexpr int32 WitnessMax = 3;
			FVector WitnessA = FVector::ZeroVector, WitnessB = FVector::ZeroVector;
			int32 WitnessCount = 0;
			for (int32 Z = WitnessMin; Z <= WitnessMax && bSupports && WitnessCount < 3; ++Z)
				for (int32 Y = WitnessMin; Y <= WitnessMax && bSupports && WitnessCount < 3; ++Y)
					for (int32 X = WitnessMin; X <= WitnessMax && bSupports && WitnessCount < 3; ++X)
					{
						const FIntVector Sample = Cell + FIntVector(X, Y, Z);
						if (Sample.X < 0 || Sample.Y < 0 || Sample.Z < 0 || Sample.X >= Grid.CellCount.X
						    || Sample.Y >= Grid.CellCount.Y || Sample.Z >= Grid.CellCount.Z)
							continue;
						const FVector P(X, Y, Z);
						const double D = Sign * Read(Sample);
						if (D <= Tolerance || !IsUnsaturated(D)
						    || FMath::Abs(D - Sign * Candidates[Index].Predict(P)) > 4.0 * Tolerance + Candidates[Index].Slack)
							continue;
						if (WitnessCount == 0)
						{
							WitnessA = P;
							WitnessCount = 1;
						}
						else if (WitnessCount == 1)
						{
							WitnessB = P;
							WitnessCount = 2;
						}
						else if (FVector::CrossProduct(WitnessB - WitnessA, P - WitnessA).SizeSquared() > 0.5)
							WitnessCount = 3;
					}
			bSupports &= WitnessCount >= 3;
			if (bSupports)
				Supporting.Add(Index);
		}
		// A tangent plane on a curved face needs its own second-difference allowance
		// here too, otherwise the wall's constraint is dropped again at this step.
		double SupportingSlack = 0.0;
		for (int32 Index : Supporting)
			SupportingSlack = FMath::Max(SupportingSlack, Candidates[Index].Slack);
		bool bExplains = !Supporting.IsEmpty();
		for (int32 C = 0; C < 8 && bExplains; ++C)
		{
			const double Actual = Sign * CellDensities[C];
			double Predicted = TNumericLimits<double>::Max();
			for (int32 Index : Supporting)
				Predicted = FMath::Min(Predicted, Sign * Candidates[Index].Predict(FVector(CornerOffset(C))));
			if (Actual >= GDualContourMaxLinearDensity - Tolerance)
				bExplains = Predicted >= GDualContourMaxLinearDensity - 4.0 * Tolerance - SupportingSlack;
			else if (Actual >= 0.0)
				bExplains = FMath::Abs(Actual - Predicted) <= 4.0 * Tolerance + SupportingSlack;
			else
				bExplains = Predicted <= 4.0 * Tolerance + SupportingSlack;
		}
		if (!bExplains)
			continue;
		double NewMatrix[3][3] = {}, NewRHS[3] = {};
		int32 Count = 0;
		FVector NormalSum = FVector::ZeroVector, FirstNormal = FVector::ZeroVector;
		bool bSharp = false;
		for (int32 Index : Supporting)
		{
			const FPlane& P = Candidates[Index];
			const double Radius = 0.5 * (FMath::Abs(P.Normal.X) + FMath::Abs(P.Normal.Y) + FMath::Abs(P.Normal.Z));
			// Out-of-cell planes explain densities but must not position the vertex.
			if (FMath::Abs(P.Distance - FVector::DotProduct(P.Normal, FVector(0.5))) > Radius)
				continue;
			if (Count == 0)
				FirstNormal = P.Normal;
			else
				bSharp |= FVector::DotProduct(FirstNormal, P.Normal) < 0.8;
			++Count;
			NormalSum += P.Normal;
			for (int32 R = 0; R < 3; ++R)
			{
				NewRHS[R] += P.Normal[R] * P.Distance;
				for (int32 C = 0; C < 3; ++C)
					NewMatrix[R][C] += P.Normal[R] * P.Normal[C];
			}
		}
		// A smooth field already has local Hermite constraints. Replacing them
		// by a neighbourhood tangent plane makes its position depend on which
		// lattice samples happened to pass the fit, producing contour bands.
		// Neighbourhood reconstruction is only needed to recover a real crease.
		if (Count == 0 || !bSharp)
			continue;
		FMemory::Memcpy(Matrix, NewMatrix, sizeof(NewMatrix));
		FMemory::Memcpy(RHS, NewRHS, sizeof(NewRHS));
		ConstraintCount = Count;
		OutNormal = NormalSum.GetSafeNormal();
		return true;
	}
	return false;
}
}
