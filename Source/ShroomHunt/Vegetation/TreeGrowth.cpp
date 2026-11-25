#include "Vegetation/TreeGrowth.h"

#include "Core/ShroomHash.h"
#include "Math/RandomStream.h"

double FTreeBranch::GetLength() const
{
	double Length = 0.0;
	for (int32 K = 1; K < Points.Num(); ++K)
	{
		Length += (Points[K] - Points[K - 1]).Size();
	}
	return Length;
}

int32 FTreeSkeleton::CountBranches(int32 Level) const
{
	int32 Count = 0;
	for (const FTreeBranch& Branch : Branches)
	{
		Count += Branch.Level == Level ? 1 : 0;
	}
	return Count;
}

namespace TreeGrowthDetail
{
	const FVector3d SkyUp(0.0, 0.0, 1.0);
	constexpr double GoldenAngle = 2.39996323;
	/** Safety cap on the number of branches of one tree. */
	constexpr int32 MaxBranches = 6000;
	/** Thinnest radius per level (m): trunk, main branches, secondary branches, twigs. */
	constexpr double MinRadius[4] = { 0.03, 0.012, 0.006, 0.0035 };

	FVector3d RandomUnit(FRandomStream& Rng)
	{
		return FVector3d(Rng.VRand());
	}

	/** Point, direction and radius of a branch at an arc length. */
	struct FBranchSample
	{
		FVector3d Position = FVector3d::ZeroVector;
		FVector3d Direction = FVector3d::UnitZ();
		double Radius = 0.01;
	};

	FBranchSample SampleBranch(const FTreeBranch& Branch, double Along)
	{
		FBranchSample S;
		double Walked = 0.0;
		for (int32 K = 1; K < Branch.Points.Num(); ++K)
		{
			const FVector3d Segment = Branch.Points[K] - Branch.Points[K - 1];
			const double Length = Segment.Size();
			if (Walked + Length >= Along || K == Branch.Points.Num() - 1)
			{
				const double T = Length > 1e-9 ? FMath::Clamp((Along - Walked) / Length, 0.0, 1.0) : 0.0;
				S.Position = Branch.Points[K - 1] + Segment * T;
				S.Direction = Length > 1e-9 ? Segment / Length : FVector3d::UnitZ();
				S.Radius = FMath::Lerp(Branch.Radii[K - 1], Branch.Radii[K], T);
				return S;
			}
			Walked += Length;
		}
		if (Branch.Points.Num() > 0)
		{
			S.Position = Branch.Points[0];
			S.Radius = Branch.Radii[0];
		}
		return S;
	}

	/** A horizontal direction perpendicular to Dir (left of it seen from above), or any perpendicular when Dir is vertical. */
	FVector3d LateralOf(const FVector3d& Dir, double Roll)
	{
		const FVector3d Lateral = FVector3d::CrossProduct(SkyUp, Dir);
		if (Lateral.SizeSquared() > 0.04)
		{
			return Lateral.GetSafeNormal();
		}
		const FVector3d A = FVector3d::CrossProduct(Dir, FVector3d::UnitX()).GetSafeNormal();
		const FVector3d B = FVector3d::CrossProduct(Dir, A);
		return A * FMath::Cos(Roll) + B * FMath::Sin(Roll);
	}

	class FGrower
	{
	public:
		FGrower(const FTreeArchetype& InArch, const FTreeFormDefinition& InForm, const FTreeShape& InShape, uint32 InSeed, FTreeSkeleton& InOut)
			: Arch(InArch), Form(InForm), Shape(InShape), Seed(InSeed), Out(InOut)
			, Rng(static_cast<int32>(ShroomHash::Hash(InSeed, 0x6A0Bu)))
		{
			H = Shape.HeightM;
			CrownBase = Shape.ClearBoleM;
			Step = FMath::Clamp(H / 45.0, 0.1, 0.6);
			bExcurrent = Arch.Excurrent >= 0.5f;
			bMultiStem = Arch.Stems > 1;
			bSapling = Form.Kind == EVegetationKind::Sapling;
			TwigLevel = bSapling ? 2 : 3;
			LumpSeed = ShroomHash::Hash(Seed, 0x1B9u);
		}

		void Grow()
		{
			GrowTrunk();
			// Breadth-first: every branch spawns its children once (the list grows while walking it).
			for (int32 Index = 0; Index < Out.Branches.Num() && Out.Branches.Num() < MaxBranches; ++Index)
			{
				if (Out.Branches[Index].Level >= 1 && Out.Branches[Index].Level < TwigLevel)
				{
					GrowChildren(Index);
				}
			}
			GrowLeaves();
			GrowFills();
		}

	private:
		const FTreeArchetype& Arch;
		const FTreeFormDefinition& Form;
		const FTreeShape& Shape;
		uint32 Seed;
		FTreeSkeleton& Out;
		FRandomStream Rng;
		double H = 20.0;
		double CrownBase = 8.0;
		double Step = 0.4;
		bool bExcurrent = true;
		bool bMultiStem = false;
		bool bSapling = false;
		int32 TwigLevel = 3;
		uint32 LumpSeed = 0;
		double TrunkTop = 18.0;

		FVector3d Axis(double Z) const { return TreeGrowth::TrunkAxis(Shape, Seed, Z); }

		/** Horizontal reach of the crown at a height and azimuth (the envelope, made irregular by Lumpiness). */
		double Reach(double Z, double Azimuth) const
		{
			// Below the crown, the crown base width: coppice stems and low branches start there.
			const double ZInCrown = FMath::Max(Z, CrownBase);
			const double T = (ZInCrown - CrownBase) / FMath::Max(H - CrownBase, 0.5);
			const float Noise = ShroomHash::GradientNoise2D(LumpSeed, FMath::Cos(Azimuth) * 1.4 + T * 2.2, FMath::Sin(Azimuth) * 1.4 - T * 1.7);
			return TreeGrowth::CrownRadiusAt(Arch, Shape, ZInCrown) * (1.0 + Arch.Lumpiness * 0.8 * Noise);
		}

		bool IsOutside(const FVector3d& P, double Slack) const
		{
			if (P.Z > H * (1.0 + 0.03 * Slack) || P.Z < 0.3)
			{
				return true;
			}
			const FVector3d A = Axis(P.Z);
			const FVector2d Offset(P.X - A.X, P.Y - A.Y);
			const double Distance = Offset.Size();
			return Distance > Reach(P.Z, FMath::Atan2(Offset.Y, Offset.X)) * Slack + 0.05;
		}

		/** Taper and root flare of the trunk (breast-height radius = Shape.TrunkRadiusM). */
		double TrunkRadius(double Z, double TaperHeight) const
		{
			const double R0 = Shape.TrunkRadiusM;
			const double FlareLength = 2.5 * R0;
			const double BreastZ = FMath::Min(1.3, TaperHeight * 0.3);
			const double Norm = (1.08 - 0.96 * BreastZ / TaperHeight) * (1.0 + 0.5 * FMath::Exp(-BreastZ / FlareLength));
			const double Taper = 1.08 - 0.96 * FMath::Clamp(Z / TaperHeight, 0.0, 1.0);
			const double Flare = 1.0 + 0.5 * FMath::Exp(-FMath::Max(Z, 0.0) / FlareLength);
			return FMath::Max(R0 * Taper * Flare / Norm, 0.004);
		}

		/** Grows one branch from Start; returns its index or INDEX_NONE when it ends at once outside the crown. */
		int32 GrowBranch(const FVector3d& Start, const FVector3d& Direction, double Length, double BaseRadius, int32 Level, int32 Parent)
		{
			if (Out.Branches.Num() >= MaxBranches || Length < 0.05)
			{
				return INDEX_NONE;
			}
			const double SegmentTarget = FMath::Max(Step * (Level == 1 ? 1.2 : (Level == 2 ? 0.9 : 0.6)), 0.06);
			const int32 Segments = FMath::Clamp(FMath::CeilToInt(Length / SegmentTarget), 2, Level == TwigLevel ? 3 : 30);
			const double SegmentLength = Length / Segments;
			const double Slack = Level == TwigLevel ? 1.12 : 1.04;
			// Bending per meter, so that a branch bends by about the same angle whatever its length.
			const double Up = Arch.UpCurve * 1.4 / FMath::Max(Length, 1.0);
			const double Down = Level >= 2 ? Arch.Droop * 2.2 / FMath::Max(Length, 0.6) : Arch.Droop * 0.5 / FMath::Max(Length, 1.0);
			const double Kink = Arch.Tortuosity * 0.55 * FMath::Sqrt(SegmentLength);

			FTreeBranch Branch;
			Branch.Level = Level;
			Branch.Parent = Parent;
			Branch.Points.Add(Start);
			FVector3d Position = Start;
			FVector3d Dir = Direction.GetSafeNormal();
			for (int32 K = 1; K <= Segments; ++K)
			{
				const double S = static_cast<double>(K) / Segments;
				Dir += SkyUp * (Up * SegmentLength) - SkyUp * (Down * SegmentLength * S) + RandomUnit(Rng) * Kink;
				Dir = Dir.GetSafeNormal();
				Position += Dir * SegmentLength;
				if (K > 1 && IsOutside(Position, Slack))
				{
					break;
				}
				Branch.Points.Add(Position);
			}
			if (Branch.Points.Num() < 2)
			{
				return INDEX_NONE;
			}
			const double TipRadius = FMath::Max(BaseRadius * 0.18, MinRadius[FMath::Clamp(Level, 0, 3)]);
			const int32 Count = Branch.Points.Num();
			for (int32 K = 0; K < Count; ++K)
			{
				const double S = static_cast<double>(K) / (Count - 1);
				Branch.Radii.Add(FMath::Lerp(BaseRadius, FMath::Min(TipRadius, BaseRadius), FMath::Pow(S, 0.85)));
			}
			return Out.Branches.Add(MoveTemp(Branch));
		}

		/** Coppice (hazel): a short stool and several stems leaning out from it, each branching like a trunk. */
		void GrowStems()
		{
			{
				FTreeBranch Stool;
				Stool.Level = 0;
				for (int32 K = 0; K <= 2; ++K)
				{
					const double Z = -0.1 + 0.25 * K;
					Stool.Points.Add(Axis(Z));
					Stool.Radii.Add(Shape.TrunkRadiusM * (2.2 - 0.4 * K));
				}
				Out.Branches.Add(MoveTemp(Stool));
			}
			const double Azimuth0 = Rng.FRandRange(0.0f, UE_TWO_PI);
			for (int32 S = 0; S < Arch.Stems; ++S)
			{
				const double Az = Azimuth0 + S * GoldenAngle + Rng.FRandRange(-0.3f, 0.3f);
				const double Lean = FMath::DegreesToRadians(Rng.FRandRange(6.0f, 28.0f));
				const FVector3d Dir(FMath::Cos(Az) * FMath::Sin(Lean), FMath::Sin(Az) * FMath::Sin(Lean), FMath::Cos(Lean));
				const FVector3d Start = Axis(0.1) + FVector3d(FMath::Cos(Az), FMath::Sin(Az), 0.0) * Shape.TrunkRadiusM;
				GrowBranch(Start, Dir, H * Rng.FRandRange(0.75f, 1.0f), Shape.TrunkRadiusM * Rng.FRandRange(0.7f, 1.0f), 1, 0);
			}
		}

		void GrowTrunk()
		{
			if (bMultiStem)
			{
				GrowStems();
				return;
			}
			const double CrownDepth = FMath::Max(H - CrownBase, 0.5);
			TrunkTop = bExcurrent ? H * 0.9 : CrownBase + CrownDepth * FMath::Lerp(0.08, 0.35, Arch.Excurrent * 2.0);
			const double TaperHeight = bExcurrent ? TrunkTop : H * 0.93;
			{
				FTreeBranch Trunk;
				Trunk.Level = 0;
				const int32 Rings = FMath::Clamp(FMath::RoundToInt(TrunkTop / 0.6), 6, 48);
				for (int32 K = 0; K <= Rings; ++K)
				{
					const double Z = -0.15 + (TrunkTop + 0.15) * FMath::Pow(static_cast<double>(K) / Rings, 1.4);
					Trunk.Points.Add(Axis(Z));
					Trunk.Radii.Add(TrunkRadius(Z, TaperHeight));
				}
				Out.Branches.Add(MoveTemp(Trunk));
			}

			const double Azimuth0 = Rng.FRandRange(0.0f, UE_TWO_PI);
			if (!bExcurrent)
			{
				// Decurrent crown: a few strong limbs share the top (pipe model: r = r_trunk / sqrt(n)).
				const int32 Limbs = 2 + Rng.RandRange(0, 2);
				const double LimbRadius = TrunkRadius(TrunkTop, TaperHeight) * 1.1 / FMath::Sqrt(static_cast<double>(Limbs));
				for (int32 L = 0; L < Limbs; ++L)
				{
					const double Az = Azimuth0 + UE_DOUBLE_TWO_PI * L / Limbs + Rng.FRandRange(-0.35f, 0.35f);
					const FVector3d Start = Axis(TrunkTop - 0.3);
					const FVector3d Target = Axis(H * Rng.FRandRange(0.78f, 0.9f)) + FVector3d(FMath::Cos(Az), FMath::Sin(Az), 0.0) * Shape.CrownRadiusM * Rng.FRandRange(0.35f, 0.55f);
					GrowBranch(Start, Target - Start, (Target - Start).Size() * 1.05, LimbRadius, 1, 0);
				}
			}

			// Main branches along the trunk inside the crown.
			const double ZStart = FMath::Max(CrownBase * 0.97, 0.4);
			const double ZEnd = bExcurrent ? H * 0.93 : TrunkTop;
			const double Span = FMath::Max(ZEnd - ZStart, 0.2);
			const double PerM = Arch.BranchesPerM * (bSapling ? 2.2 : 1.0);
			const int32 Count = FMath::Max(bSapling ? 4 : 3, FMath::RoundToInt(Span * PerM));
			for (int32 I = 0; I < Count; ++I)
			{
				const double Z = ZStart + Span * (I + 0.5 + Rng.FRandRange(-0.35f, 0.35f)) / Count;
				const double T = FMath::Clamp((Z - CrownBase) / CrownDepth, 0.0, 1.0);
				const double Azimuth = Azimuth0 + I * GoldenAngle + Rng.FRandRange(-0.35f, 0.35f);
				const double Elevation = FMath::DegreesToRadians(FMath::Clamp(Arch.BranchAngleDeg + (T - 0.45) * 40.0 + Rng.FRandRange(-7.0f, 7.0f), 3.0, 82.0));
				const FVector3d Dir(FMath::Cos(Azimuth) * FMath::Cos(Elevation), FMath::Sin(Azimuth) * FMath::Cos(Elevation), FMath::Sin(Elevation));
				// Long enough to reach the widest part of the crown: the envelope clips it (IsOutside).
				const double Length = Shape.CrownRadiusM * 1.4 / FMath::Max(FMath::Cos(Elevation), 0.35) * Rng.FRandRange(0.9f, 1.1f);
				const double ParentRadius = TrunkRadius(Z, TaperHeight);
				const double Radius = FMath::Max(ParentRadius * FMath::Lerp(0.62, 0.3, T), MinRadius[1]);
				GrowBranch(Axis(Z), Dir, Length, Radius, 1, 0);
			}
		}

		void GrowChildren(int32 ParentIndex)
		{
			const int32 Level = Out.Branches[ParentIndex].Level + 1;
			const double ParentLength = Out.Branches[ParentIndex].GetLength();
			const bool bTwig = Level == TwigLevel;
			const double Spacing = bTwig ? FMath::Max(0.1, Step * 0.5) : FMath::Max(0.3, Step * 1.3);
			const double StartFraction = bTwig ? 0.22 : 0.18;
			const double TwigMax = FMath::Clamp(Step * 1.8, 0.25, 0.95);
			double Along = ParentLength * StartFraction + Spacing * Rng.FRandRange(0.3f, 1.0f);
			double Side = Rng.FRand() < 0.5f ? -1.0 : 1.0;
			int32 Child = 0;
			while (Along < ParentLength * 0.96 && Out.Branches.Num() < MaxBranches)
			{
				// Copy: GrowBranch adds to Out.Branches, which may move the parent in memory.
				const FBranchSample P = SampleBranch(Out.Branches[ParentIndex], Along);
				const FVector3d Lateral = LateralOf(P.Direction, Child * GoldenAngle) * Side;
				FVector3d SideDir = Lateral + SkyUp * Rng.FRandRange(-0.25f, 0.6f);
				SideDir = (SideDir - P.Direction * FVector3d::DotProduct(SideDir, P.Direction)).GetSafeNormal();
				const double Angle = FMath::DegreesToRadians(bTwig ? Rng.FRandRange(40.0f, 70.0f) : Rng.FRandRange(32.0f, 58.0f));
				const FVector3d Dir = P.Direction * FMath::Cos(Angle) + SideDir * FMath::Sin(Angle);
				const double Remaining = ParentLength - Along;
				const double Length = bTwig
					? FMath::Min(Remaining * 0.7 + 0.12, TwigMax) * Rng.FRandRange(0.7f, 1.1f)
					: FMath::Max(Remaining * Rng.FRandRange(0.42f, 0.68f), 0.3);
				const double Radius = FMath::Max(P.Radius * (bTwig ? 0.5 : 0.62), MinRadius[FMath::Clamp(Level, 0, 3)]);
				GrowBranch(P.Position, Dir, Length, Radius, Level, ParentIndex);
				Along += Spacing * Rng.FRandRange(0.7f, 1.3f);
				Side = -Side;
				++Child;
			}
		}

		float LightAt(const FVector3d& P) const
		{
			const FVector3d A = Axis(P.Z);
			const double Distance = FVector2d(P.X - A.X, P.Y - A.Y).Size();
			const double Outward = Distance / FMath::Max(TreeGrowth::CrownRadiusAt(Arch, Shape, P.Z), 0.3);
			const double T = (P.Z - CrownBase) / FMath::Max(H - CrownBase, 0.5);
			return static_cast<float>(FMath::Clamp(0.15 + 0.5 * FMath::Min(Outward, 1.0) + 0.4 * T, 0.0, 1.0));
		}

		void GrowLeaves()
		{
			// Leaf-bearing stretches: whole twigs, the outer part of their parents, the top of an excurrent trunk.
			struct FStretch
			{
				int32 Branch;
				double From;
				double To;
			};
			TArray<FStretch> Stretches;
			double Total = 0.0;
			for (int32 Index = 0; Index < Out.Branches.Num(); ++Index)
			{
				const FTreeBranch& B = Out.Branches[Index];
				const double Length = B.GetLength();
				double From = -1.0;
				if (B.Level == TwigLevel)
				{
					From = Length * 0.12;
				}
				else if (B.Level == TwigLevel - 1)
				{
					From = Length * 0.6;
				}
				else if (B.Level == 0 && bExcurrent)
				{
					From = Length * 0.94;
				}
				if (From >= 0.0 && Length > From)
				{
					Stretches.Add({ Index, From, Length });
					Total += Length - From;
				}
			}
			// Lobed leaves cost twice the triangles: fewer of them.
			const int32 Budget = FMath::RoundToInt(TreeGrowth::GetLeafBudget(Form.Kind) * (Arch.LeafLobes > 0 ? 0.6 : 1.0));
			if (Total <= 0.0 || Budget <= 0)
			{
				return;
			}
			const double Spacing = FMath::Max(Total / Budget, 0.03);
			const double LeafLength = Arch.LeafSizeCm / 100.0 * Arch.LeafScale * (bSapling ? 0.7 : 1.0);
			double Carry = Spacing * Rng.FRand();
			double Side = 1.0;
			for (const FStretch& Stretch : Stretches)
			{
				const FTreeBranch& B = Out.Branches[Stretch.Branch];
				for (double Along = Stretch.From + Carry; Along < Stretch.To && Out.Leaves.Num() < Budget * 1.15; Along += Spacing * Rng.FRandRange(0.6f, 1.4f))
				{
					const FBranchSample P = SampleBranch(B, Along);
					const FVector3d Lateral = LateralOf(P.Direction, Out.Leaves.Num() * GoldenAngle) * Side;
					Side = -Side;
					FTreeLeaf& Leaf = Out.Leaves.AddDefaulted_GetRef();
					FVector3d LeafAxis = P.Direction * 0.45 + Lateral * 0.6 + SkyUp * (0.18 - 0.7 * Arch.Droop) + RandomUnit(Rng) * 0.65;
					LeafAxis = LeafAxis.GetSafeNormal();
					const FVector3d CrownAxis = Axis(P.Position.Z);
					const FVector3d Outward = FVector3d(P.Position.X - CrownAxis.X, P.Position.Y - CrownAxis.Y, 0.0).GetSafeNormal();
					FVector3d Normal = SkyUp + Outward * 0.45 + RandomUnit(Rng) * 0.35;
					Normal = (Normal - LeafAxis * FVector3d::DotProduct(Normal, LeafAxis)).GetSafeNormal();
					if (Normal.IsNearlyZero())
					{
						Normal = LateralOf(LeafAxis, 0.0);
					}
					Leaf.Axis = LeafAxis;
					Leaf.Normal = Normal;
					Leaf.Base = P.Position + LeafAxis * FMath::Max(P.Radius, 0.01);
					Leaf.Length = LeafLength * Rng.FRandRange(0.8f, 1.2f);
					Leaf.Width = Leaf.Length * Arch.LeafWidth * Rng.FRandRange(0.9f, 1.1f);
					Leaf.Random = Rng.FRand();
					Leaf.Light = LightAt(Leaf.Base);
				}
				Carry = 0.0;
			}
		}

		void GrowFills()
		{
			const int32 FillLevel = FMath::Max(TwigLevel - 1, 1);
			TArray<int32> Candidates;
			for (int32 Index = 0; Index < Out.Branches.Num(); ++Index)
			{
				if (Out.Branches[Index].Level == FillLevel && Out.Branches[Index].GetLength() > 0.4)
				{
					Candidates.Add(Index);
				}
			}
			const int32 Count = FMath::Min(Shape.Clumps, Candidates.Num());
			const double Radius = FMath::Clamp(Shape.CrownRadiusM * 0.2 * Arch.ClumpScale, 0.2, 1.8);
			for (int32 I = 0; I < Count; ++I)
			{
				// One candidate per slice of the list, so that the fills spread over the whole crown.
				const int32 Slice = Candidates.Num() / Count;
				const FTreeBranch& B = Out.Branches[Candidates[I * Slice + Rng.RandRange(0, FMath::Max(Slice - 1, 0))]];
				const FBranchSample P = SampleBranch(B, B.GetLength() * Rng.FRandRange(0.55f, 0.8f));
				const FVector3d CrownAxis = Axis(P.Position.Z);
				const FVector3d Outward = FVector3d(P.Position.X - CrownAxis.X, P.Position.Y - CrownAxis.Y, 0.0).GetSafeNormal();
				FTreeFill& Fill = Out.Fills.AddDefaulted_GetRef();
				Fill.Radius = Radius * Rng.FRandRange(0.75f, 1.15f);
				Fill.Center = P.Position - Outward * Fill.Radius * 0.4;
				Fill.Random = Rng.FRand();
				Fill.Light = LightAt(Fill.Center) * 0.7f;
			}
		}
	};
}

namespace TreeGrowth
{
	FVector3d TrunkAxis(const FTreeShape& Shape, uint32 Seed, double Z)
	{
		const double H = FMath::Max(Shape.HeightM, 0.5);
		const uint32 NoiseSeed = ShroomHash::Hash(Seed, 0xBA4Cu);
		const double T = Z / H;
		const double Wobble = 0.006 * H;
		return FVector3d(
			Shape.LeanX * H * T * T + Wobble * ShroomHash::GradientNoise2D(NoiseSeed, Z / (0.25 * H), 0.5),
			Shape.LeanY * H * T * T + Wobble * ShroomHash::GradientNoise2D(NoiseSeed ^ 0x77u, Z / (0.25 * H), 3.5),
			Z);
	}

	double CrownRadiusAt(const FTreeArchetype& Archetype, const FTreeShape& Shape, double Z)
	{
		const double CrownDepth = FMath::Max(Shape.HeightM - Shape.ClearBoleM, 0.5);
		const double T = (Z - Shape.ClearBoleM) / CrownDepth;
		if (T < -0.04 || T > 1.0)
		{
			return 0.0;
		}
		const double Peak = FMath::Clamp(Archetype.CrownPeak, 0.05f, 0.95f);
		const double Exponent = FMath::Max(Archetype.CrownExponent, 0.5f);
		const double Clamped = FMath::Clamp(T, 0.0, 1.0);
		const double U = FMath::Clamp(Clamped < Peak ? (Peak - Clamped) / Peak : (Clamped - Peak) / (1.0 - Peak), 0.0, 1.0);
		// A small minimum keeps a little crown at the very base and top (no zero-length branches there).
		return Shape.CrownRadiusM * FMath::Max(FMath::Pow(FMath::Max(0.0, 1.0 - FMath::Pow(U, Exponent)), 1.0 / Exponent), 0.08);
	}

	int32 GetLeafBudget(EVegetationKind Kind)
	{
		switch (Kind)
		{
		case EVegetationKind::Sapling: return 600;
		case EVegetationKind::Young: return 2500;
		case EVegetationKind::Hazel: return 2200;
		case EVegetationKind::Holly: return 2600;
		case EVegetationKind::Pole: return 5000;
		case EVegetationKind::Mature: return 10000;
		case EVegetationKind::Old: return 13000;
		default: return 0;
		}
	}

	FTreeSkeleton Grow(const FTreeArchetype& Archetype, const FTreeFormDefinition& Form, const FTreeShape& Shape, uint32 Seed)
	{
		FTreeSkeleton Skeleton;
		TreeGrowthDetail::FGrower Grower(Archetype, Form, Shape, Seed, Skeleton);
		Grower.Grow();
		return Skeleton;
	}
}
