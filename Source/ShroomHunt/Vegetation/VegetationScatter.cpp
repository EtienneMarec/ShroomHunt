#include "Vegetation/VegetationScatter.h"

#include "Async/ParallelFor.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "Forest/ForestField.h"
#include "Forest/ForestPaths.h"
#include "Vegetation/VegetationObstacles.h"

FVegetationMeshSpec FVegetationInstance::GetMeshSpec() const
{
	FVegetationMeshSpec Spec;
	Spec.Kind = Kind;
	Spec.Genus = Genus;
	Spec.Variant = Variant;
	return Spec;
}

namespace VegetationScatterDetail
{
	struct FCandidate
	{
		FVector2d Position = FVector2d::ZeroVector;
		uint32 CellHash = 0;
		uint32 Priority = 0;
		float RadiusM = 0.0f;
		bool bPassed = false;
		EVegetationKind Kind = EVegetationKind::Mature;
		FName Genus;
	};

	/** Picks an index from non-negative weights with a uniform value U in [0, 1). INDEX_NONE if all are 0. */
	int32 PickWeighted(TConstArrayView<float> Weights, float U)
	{
		float Total = 0.0f;
		for (const float W : Weights)
		{
			Total += FMath::Max(W, 0.0f);
		}
		if (Total <= 0.0f)
		{
			return INDEX_NONE;
		}
		float Target = U * Total;
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			Target -= FMath::Max(Weights[Index], 0.0f);
			if (Target < 0.0f)
			{
				return Index;
			}
		}
		return Weights.Num() - 1;
	}

	/** True when a plant at this point would stand on a path (its surface plus Clearance). */
	bool IsOnPath(const FForestField& Field, const FForestSample& Sample, float Clearance)
	{
		return Sample.PathRank >= 0
			&& Sample.PathDistanceM < ForestPaths::HalfWidthM(static_cast<EForestPathRank>(Sample.PathRank), Field.GetDefinition()) + Clearance;
	}

	/**
	 * Candidate cell size so that the keep probability stays below ~0.5 at the highest density.
	 * Thinned processes keep cells of 1 m or more (bounded neighborhood search); unthinned ones
	 * (SpacingFactor 0: fallen leaves, tens per square meter) go down to 10 cm.
	 */
	double CellSizeFor(float MaxPerHa, bool bThinned = true)
	{
		return FMath::Clamp(0.7 * FMath::Sqrt(10000.0 / FMath::Max(MaxPerHa, 1.0f)), bThinned ? 1.0 : 0.1, 20.0);
	}

	/**
	 * Runs one point process over Bounds and appends the kept candidates that lie inside it.
	 * Evaluate(Position, CellHash, OutDensityPerHa, Candidate) returns false to reject the cell
	 * (it may also fill the candidate payload).
	 */
	template <typename TEvaluate>
	void RunProcess(const FForestField& Field, uint32 Seed, double CellM, double SpacingFactor, const FBox2d& Bounds, TEvaluate&& Evaluate, TArray<FCandidate>& OutKept)
	{
		const double SizeM = Field.GetSizeM();
		// No thinning: candidates are independent, no neighborhood to look at.
		const bool bThinned = SpacingFactor > 0.0;
		const int32 Margin = bThinned ? FMath::CeilToInt(VegetationScatter::MaxSpacingM / CellM) : 0;
		const int32 MaxCell = FMath::CeilToInt(SizeM / CellM);
		const int32 X0 = FMath::Max(FMath::FloorToInt(Bounds.Min.X / CellM) - Margin, 0);
		const int32 Y0 = FMath::Max(FMath::FloorToInt(Bounds.Min.Y / CellM) - Margin, 0);
		const int32 X1 = FMath::Min(FMath::FloorToInt(Bounds.Max.X / CellM) + Margin, MaxCell);
		const int32 Y1 = FMath::Min(FMath::FloorToInt(Bounds.Max.Y / CellM) + Margin, MaxCell);
		if (X1 < X0 || Y1 < Y0)
		{
			return;
		}
		const int32 Width = X1 - X0 + 1;
		const int32 Height = Y1 - Y0 + 1;

		TArray<FCandidate> Grid;
		Grid.SetNum(Width * Height);
		ParallelFor(Height, [&](int32 Row)
		{
			const int32 CY = Y0 + Row;
			for (int32 Column = 0; Column < Width; ++Column)
			{
				const int32 CX = X0 + Column;
				FCandidate& C = Grid[Row * Width + Column];
				C.CellHash = ShroomHash::Hash(Seed, static_cast<uint32>(CX), static_cast<uint32>(CY));
				C.Position = FVector2d((CX + ShroomHash::ToUnitFloat(ShroomHash::Hash(C.CellHash, 1u))) * CellM,
					(CY + ShroomHash::ToUnitFloat(ShroomHash::Hash(C.CellHash, 2u))) * CellM);
				if (C.Position.X < 0.0 || C.Position.Y < 0.0 || C.Position.X >= SizeM || C.Position.Y >= SizeM)
				{
					continue;
				}
				float Density = 0.0f;
				if (!Evaluate(C.Position, C.CellHash, Density, C) || Density <= 0.0f)
				{
					continue;
				}
				const double Probability = Density * CellM * CellM / 10000.0;
				if (ShroomHash::ToUnitFloat(ShroomHash::Hash(C.CellHash, 3u)) >= Probability)
				{
					continue;
				}
				C.bPassed = true;
				C.Priority = ShroomHash::Hash(C.CellHash, 4u);
				C.RadiusM = static_cast<float>(FMath::Min(SpacingFactor * FMath::Sqrt(10000.0 / Density), VegetationScatter::MaxSpacingM));
			}
		});

		for (int32 Row = 0; Row < Height; ++Row)
		{
			for (int32 Column = 0; Column < Width; ++Column)
			{
				const int32 Index = Row * Width + Column;
				const FCandidate& C = Grid[Index];
				if (!C.bPassed || C.Position.X < Bounds.Min.X || C.Position.Y < Bounds.Min.Y || C.Position.X >= Bounds.Max.X || C.Position.Y >= Bounds.Max.Y)
				{
					continue;
				}
				bool bKeep = true;
				for (int32 DY = -Margin; DY <= Margin && bKeep && bThinned; ++DY)
				{
					const int32 NRow = Row + DY;
					if (NRow < 0 || NRow >= Height)
					{
						continue;
					}
					for (int32 DX = -Margin; DX <= Margin; ++DX)
					{
						const int32 NColumn = Column + DX;
						if ((DX == 0 && DY == 0) || NColumn < 0 || NColumn >= Width)
						{
							continue;
						}
						const int32 NIndex = NRow * Width + NColumn;
						const FCandidate& N = Grid[NIndex];
						if (!N.bPassed)
						{
							continue;
						}
						const double Radius = FMath::Max(C.RadiusM, N.RadiusM);
						if (FVector2d::DistSquared(C.Position, N.Position) >= Radius * Radius)
						{
							continue;
						}
						// Higher priority wins; ties broken by the global cell order (same in every region).
						const bool bNeighborWins = N.Priority > C.Priority || (N.Priority == C.Priority && (DY > 0 || (DY == 0 && DX > 0)));
						if (bNeighborWins)
						{
							bKeep = false;
							break;
						}
					}
				}
				if (bKeep)
				{
					OutKept.Add(C);
				}
			}
		}
	}

	/** Steepest ground slope (rise over run) around a point, on the 1 m heightmap. */
	float FineSlope(const FForestField& Field, const FVector2d& P)
	{
		const FFieldRaster& H = Field.GetHeights();
		const float X = static_cast<float>(P.X);
		const float Y = static_cast<float>(P.Y);
		const float GX = (H.Sample(X + 1.0f, Y) - H.Sample(X - 1.0f, Y)) * 0.5f;
		const float GY = (H.Sample(X, Y + 1.0f) - H.Sample(X, Y - 1.0f)) * 0.5f;
		return FMath::Sqrt(GX * GX + GY * GY);
	}

	/**
	 * SinkM is how deep the origin goes below the ground at the center; SlopeSinkM adds that much
	 * per unit of slope so the downhill side of a trunk or a log does not float on steep banks.
	 */
	FVegetationInstance MakeInstance(const FForestField& Field, const FVegetationDefinition& Definition, const FCandidate& C, float ScaleJitter, float SinkM, float MaxTiltDeg, float SlopeSinkM = 0.0f)
	{
		FVegetationInstance Out;
		Out.Kind = C.Kind;
		Out.Genus = C.Genus;
		const int32 Variants = FMath::Max(Definition.GetVariantCount(C.Kind), 1);
		Out.Variant = static_cast<int32>(ShroomHash::Hash(C.CellHash, 5u) % static_cast<uint32>(Variants));
		Out.YawDeg = 360.0f * ShroomHash::ToUnitFloat(ShroomHash::Hash(C.CellHash, 6u));
		Out.PitchDeg = MaxTiltDeg * ShroomHash::ToSignedFloat(ShroomHash::Hash(C.CellHash, 7u));
		Out.RollDeg = MaxTiltDeg * ShroomHash::ToSignedFloat(ShroomHash::Hash(C.CellHash, 8u));
		Out.Scale = 1.0f + ScaleJitter * ShroomHash::ToSignedFloat(ShroomHash::Hash(C.CellHash, 9u));
		const float Sink = SinkM + SlopeSinkM * FineSlope(Field, C.Position) * Out.Scale;
		Out.PositionM = FVector3d(C.Position.X, C.Position.Y, Field.GetHeights().Sample(static_cast<float>(C.Position.X), static_cast<float>(C.Position.Y)) - Sink);
		return Out;
	}

	/** Tilts a flat piece of ground debris onto the terrain slope (keeps its yaw around the normal). */
	void AlignToGround(const FForestField& Field, FVegetationInstance& Instance)
	{
		const FFieldRaster& H = Field.GetHeights();
		const float X = static_cast<float>(Instance.PositionM.X);
		const float Y = static_cast<float>(Instance.PositionM.Y);
		const double GX = (H.Sample(X + 1.0f, Y) - H.Sample(X - 1.0f, Y)) * 0.5;
		const double GY = (H.Sample(X, Y + 1.0f) - H.Sample(X, Y - 1.0f)) * 0.5;
		const FVector Normal = FVector(-GX, -GY, 1.0).GetSafeNormal();
		const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, Normal);
		const FQuat Yaw(FVector::UpVector, FMath::DegreesToRadians(Instance.YawDeg));
		const FRotator Rotation = (Tilt * Yaw).Rotator();
		Instance.PitchDeg = static_cast<float>(Rotation.Pitch);
		Instance.YawDeg = static_cast<float>(Rotation.Yaw);
		Instance.RollDeg = static_cast<float>(Rotation.Roll);
	}

	/** Density multiplier of a rule from the local slope and the distance to water (stones, moss). */
	float HabitatBoost(const FUnderstoryRule& Rule, const FForestSample& Sample, float Slope)
	{
		const float Bank = 1.0f - ShroomMath::SmoothStep01(1.0f, 6.0f, Sample.WaterDistanceM);
		return (1.0f + Rule.SlopeBoost * Slope) * (1.0f + Rule.BankBoost * Bank);
	}
}

namespace VegetationScatter
{
	float TreeDensityPerHa(const FVegetationDefinition& Definition, const FForestSample& Sample)
	{
		const FTreeDensityDefinition& Trees = Definition.TreeDensity;
		float Density = Trees.PerHa.Get(Sample.AgeClass);
		const bool bClosedStand = Sample.AgeClass == EStandAgeClass::Pole || Sample.AgeClass == EStandAgeClass::Mature || Sample.AgeClass == EStandAgeClass::Old;
		if (bClosedStand && Trees.GapCanopy.Num() == 2)
		{
			Density *= ShroomMath::SmoothStep01(Trees.GapCanopy[0], Trees.GapCanopy[1], Sample.Canopy);
		}
		return Density;
	}

	float UnderstoryDensityPerHa(const FUnderstoryRule& Rule, const FForestSample& Sample)
	{
		return Rule.PerHa.Get(Sample.AgeClass)
			* ForestData::ToTrapezoid(Rule.Moisture).Evaluate(Sample.Moisture)
			* ForestData::ToTrapezoid(Rule.PH).Evaluate(Sample.PH)
			* ForestData::ToTrapezoid(Rule.Light).Evaluate(1.0f - Sample.Canopy);
	}

	void Scatter(const FForestField& Field, const FVegetationDefinition& Definition, EVegetationLayer Layer, const FBox2d& BoundsM, TArray<FVegetationInstance>& Out)
	{
		using namespace VegetationScatterDetail;
		if (!Field.IsGenerated())
		{
			return;
		}
		const uint32 ForestSeed = static_cast<uint32>(Field.GetDefinition().Seed);
		const TArray<FName>& Genera = Field.GetGenera();

		// Riparian genera (alder) may stand right at the water's edge.
		TArray<float> Riparian;
		for (const FName Genus : Genera)
		{
			const FStandSpecies* Stand = Field.GetDefinition().Stands.FindByPredicate([Genus](const FStandSpecies& S) { return S.Genus == Genus; });
			Riparian.Add(Stand ? Stand->Riparian : 0.0f);
		}
		auto PickGenus = [&Genera](const FForestSample& Sample, uint32 Hash)
		{
			const int32 Index = PickWeighted(Sample.StandWeights, ShroomHash::ToUnitFloat(Hash));
			return Index == INDEX_NONE ? INDEX_NONE : Index;
		};

		if (Layer == EVegetationLayer::Trees)
		{
			const FTreeDensityDefinition& Trees = Definition.TreeDensity;
			TArray<FCandidate> Kept;
			RunProcess(Field, ShroomHash::Hash(ForestSeed, 0x7EEu), CellSizeFor(Trees.PerHa.GetMax()), Trees.SpacingFactor, BoundsM,
				[&](const FVector2d& P, uint32 Hash, float& OutDensity, FCandidate& C)
				{
					const FForestSample Sample = Field.Sample(static_cast<float>(P.X), static_cast<float>(P.Y));
					if (Sample.bWater || Sample.bNearLandmark || IsOnPath(Field, Sample, 1.5f))
					{
						return false;
					}
					const int32 GenusIndex = PickGenus(Sample, ShroomHash::Hash(Hash, 10u));
					if (GenusIndex == INDEX_NONE)
					{
						return false;
					}
					const float Margin = Riparian[GenusIndex] > 0.5f ? Trees.RiparianMarginM : Trees.WaterMarginM;
					if (Sample.WaterDistanceM < Margin)
					{
						return false;
					}
					const FTreeFormMix& Mix = Trees.FormMix.Get(Sample.AgeClass);
					const float FormWeights[3] = { Mix.Pole, Mix.Mature, Mix.Old };
					const int32 Form = PickWeighted(FormWeights, ShroomHash::ToUnitFloat(ShroomHash::Hash(Hash, 11u)));
					if (Form == INDEX_NONE)
					{
						return false;
					}
					C.Kind = Form == 0 ? EVegetationKind::Pole : (Form == 1 ? EVegetationKind::Mature : EVegetationKind::Old);
					C.Genus = Genera[GenusIndex];
					OutDensity = TreeDensityPerHa(Definition, Sample);
					return true;
				}, Kept);
			for (const FCandidate& C : Kept)
			{
				// Old trunks reach ~0.7 m radius at the flare.
				Out.Add(MakeInstance(Field, Definition, C, Trees.ScaleJitter, 0.15f, 2.0f, C.Kind == EVegetationKind::Old ? 0.8f : 0.5f));
			}
			return;
		}

		// Trunks of the trees around the region: nothing grows inside them. The trees are a
		// global function of the forest, so a slightly larger region gives the same answer in
		// every PCG partition. Ground debris skips this (tiny, flat, hidden by the trunk anyway).
		const bool bGround = Layer == EVegetationLayer::Ground;
		FVegetationObstacleGrid Trunks;
		if (!bGround)
		{
			TArray<FVegetationInstance> Trees;
			Scatter(Field, Definition, EVegetationLayer::Trees, FBox2d(BoundsM.Min - FVector2d(6.0), BoundsM.Max + FVector2d(6.0)), Trees);
			Trunks.AddInstances(Definition, Trees);
		}

		for (const FUnderstoryRule* RulePtr : Definition.GetRules(Layer))
		{
			const FUnderstoryRule& Rule = *RulePtr;
			const bool bSapling = Rule.Kind == EVegetationKind::Sapling;
			const bool bByGenus = bSapling || Rule.Kind == EVegetationKind::Leaf;
			const float PathClearance = bGround ? 0.0f : ((Rule.Kind == EVegetationKind::Log || bSapling) ? 1.0f : 0.3f);
			const bool bBoosted = Rule.SlopeBoost > 0.0f || Rule.BankBoost > 0.0f;
			TArray<FCandidate> Kept;
			RunProcess(Field, ShroomHash::Hash(ForestSeed, 0x0DE5u, static_cast<uint32>(Rule.Kind)), CellSizeFor(Rule.PerHa.GetMax(), Rule.SpacingFactor > 0.0f), Rule.SpacingFactor, BoundsM,
				[&](const FVector2d& P, uint32 Hash, float& OutDensity, FCandidate& C)
				{
					const FForestSample Sample = Field.Sample(static_cast<float>(P.X), static_cast<float>(P.Y));
					if (Sample.bWater || Sample.bNearLandmark || Sample.WaterDistanceM < Rule.WaterMarginM)
					{
						return false;
					}
					// On a path: excluded, or thinned by the rule's factor for that path rank (leaves).
					float PathMultiplier = 1.0f;
					if (IsOnPath(Field, Sample, PathClearance))
					{
						PathMultiplier = Rule.PathFactor.Num() == 3 ? Rule.PathFactor[FMath::Clamp(static_cast<int32>(Sample.PathRank), 0, 2)] : 0.0f;
						if (PathMultiplier <= 0.0f)
						{
							return false;
						}
					}
					C.Kind = Rule.Kind;
					if (Rule.Kind == EVegetationKind::Log && FineSlope(Field, P) > 0.15f)
					{
						return false; // a horizontal log on a steep slope would float
					}
					if (bByGenus)
					{
						// Saplings and fallen leaves of the stand's trees.
						const int32 GenusIndex = PickGenus(Sample, ShroomHash::Hash(Hash, 10u));
						if (GenusIndex == INDEX_NONE)
						{
							return false;
						}
						C.Genus = Genera[GenusIndex];
					}
					OutDensity = UnderstoryDensityPerHa(Rule, Sample) * PathMultiplier;
					if (bBoosted)
					{
						OutDensity *= HabitatBoost(Rule, Sample, FineSlope(Field, P));
					}
					return true;
				}, Kept);
			if (bGround)
			{
				// Flat on the terrain, just above it (leaves, moss) or sunk in it (stones).
				const float Sink = Rule.Kind == EVegetationKind::Stones ? 0.03f : 0.005f;
				for (const FCandidate& C : Kept)
				{
					FVegetationInstance Instance = MakeInstance(Field, Definition, C, Rule.ScaleJitter, Sink, 0.0f);
					AlignToGround(Field, Instance);
					Out.Add(Instance);
				}
				continue;
			}
			const float MaxTilt = (Rule.Kind == EVegetationKind::Log || Rule.Kind == EVegetationKind::Stump) ? 3.0f : 8.0f;
			for (const FCandidate& C : Kept)
			{
				const float SlopeSink = Rule.Kind == EVegetationKind::Log ? 3.0f : (Rule.Kind == EVegetationKind::Stump ? 0.4f : 0.0f);
				const FVegetationInstance Instance = MakeInstance(Field, Definition, C, Rule.ScaleJitter, 0.05f, MaxTilt, SlopeSink);
				FVegetationObstacle Footprint;
				const bool bBlocked = FVegetationObstacleGrid::MakeObstacle(Definition, Instance, Footprint)
					? Trunks.OverlapsCapsule(FVegetationObstacle{ Footprint.A, Footprint.B, Footprint.RadiusM + 0.1f })
					: Trunks.Overlaps(FVector2f(static_cast<float>(Instance.PositionM.X), static_cast<float>(Instance.PositionM.Y)), 0.3f);
				if (!bBlocked)
				{
					Out.Add(Instance);
				}
			}
		}
	}
}
