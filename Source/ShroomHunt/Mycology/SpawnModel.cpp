#include "Mycology/SpawnModel.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "Forest/ForestField.h"
#include "Forest/ForestPaths.h"
#include "Time/CalendarMath.h"

using namespace ShroomHash;

EMushroomStage FFruitBody::StageAt(double NowGameSeconds, const FMushroomSpeciesRow& Species) const
{
	const float Age = AgeFraction(NowGameSeconds);
	if (Age < Species.YoungUntil)
	{
		return EMushroomStage::Young;
	}
	return Age < Species.MatureUntil ? EMushroomStage::Mature : EMushroomStage::Old;
}

namespace SpawnModelDetail
{
	/** A trapezoid that was never set (all zero) does not constrain. */
	float Respond(const FTrapezoid& Range, float X)
	{
		if (Range.Min == 0.0f && Range.OptLow == 0.0f && Range.OptHigh == 0.0f && Range.Max == 0.0f)
		{
			return 1.0f;
		}
		return Range.Evaluate(X);
	}

	float AgeSubstrate(EStandAgeClass Age)
	{
		switch (Age)
		{
		case EStandAgeClass::Old: return 1.0f;
		case EStandAgeClass::Mature: return 0.75f;
		case EStandAgeClass::Pole: return 0.5f;
		case EStandAgeClass::Regeneration: return 0.35f;
		case EStandAgeClass::ClearCut: return 0.7f; // fresh stumps
		}
		return 0.5f;
	}

	FVector2f DiscPoint(float Radius, float U1, float U2)
	{
		const float R = Radius * FMath::Sqrt(U1);
		const float A = UE_TWO_PI * U2;
		return FVector2f(R * FMath::Cos(A), R * FMath::Sin(A));
	}

	/** Position of fruit Index of a flush, relative to the patch center, by clustering pattern. */
	FVector2f FruitOffset(const FMushroomSpeciesRow& Species, const FMyceliumPatch& Patch, int32 Day, int32 Index, int32 Count)
	{
		const uint32 Flush = Hash(Patch.Id, static_cast<uint32>(Day), 0xC1u);
		const uint32 Fruit = Hash(Flush, static_cast<uint32>(Index));
		const float U1 = ToUnitFloat(Hash(Fruit, 1u));
		const float U2 = ToUnitFloat(Hash(Fruit, 2u));
		switch (Species.Clustering)
		{
		case EClustering::Solitary:
			return DiscPoint(Patch.RadiusM * 0.5f, U1, U2);
		case EClustering::Troop:
		{
			// One or two groups inside the patch.
			const int32 Group = Index % (Count > 6 ? 2 : 1);
			const uint32 G = Hash(Flush, 1000u + Group);
			const FVector2f Center = DiscPoint(Patch.RadiusM * 0.6f, ToUnitFloat(Hash(G, 1u)), ToUnitFloat(Hash(G, 2u)));
			return Center + DiscPoint(Patch.RadiusM * 0.35f, U1, U2);
		}
		case EClustering::Caespitose:
		{
			// Tufts of about five.
			const int32 Tuft = Index / 5;
			const uint32 T = Hash(Flush, 2000u + Tuft);
			const FVector2f Center = DiscPoint(Patch.RadiusM * 0.8f, ToUnitFloat(Hash(T, 1u)), ToUnitFloat(Hash(T, 2u)));
			return Center + DiscPoint(0.2f, U1, U2);
		}
		case EClustering::FairyRing:
		{
			// The ring keeps its radius from flush to flush (the mycelium grows outward slowly).
			const float Ring = Patch.RadiusM * (0.7f + 0.3f * ToUnitFloat(Hash(Patch.Id, 5u)));
			const float Angle = UE_TWO_PI * U1;
			const float Jitter = 0.35f * (U2 * 2.0f - 1.0f);
			return FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * (Ring + Jitter);
		}
		default:
			return DiscPoint(Patch.RadiusM, U1, U2);
		}
	}

	bool CanGrowAt(const FForestField& Field, const FVector2f& P)
	{
		if (P.X < 0.0f || P.Y < 0.0f || P.X > Field.GetSizeM() || P.Y > Field.GetSizeM())
		{
			return false;
		}
		const FForestSample S = Field.Sample(P.X, P.Y);
		const bool bOnPath = S.PathRank >= 0 && S.PathDistanceM < ForestPaths::HalfWidthM(static_cast<EForestPathRank>(S.PathRank), Field.GetDefinition()) + 0.2f;
		return !S.bWater && !S.bNearLandmark && !bOnPath && S.WaterDistanceM > 2.0f;
	}
}

namespace SpawnModel
{
	using namespace SpawnModelDetail;

	float HostFactor(const FMushroomSpeciesRow& Species, const FForestField& Field, const FForestSample& Sample)
	{
		switch (Species.Trophic)
		{
		case ETrophicMode::Saprotrophic:
			return AgeSubstrate(Sample.AgeClass);
		case ETrophicMode::Grassland:
			return 1.0f - Sample.Canopy;
		default:
		{
			float H = 0.0f;
			for (const FHostAffinity& Host : Species.Hosts)
			{
				const int32 Genus = Field.FindGenus(Host.Genus);
				if (Sample.StandWeights.IsValidIndex(Genus))
				{
					H += Host.Affinity * Sample.StandWeights[Genus];
				}
			}
			return FMath::Clamp(H, 0.0f, 1.0f);
		}
		}
	}

	float Suitability(const FMushroomSpeciesRow& Species, FName SpeciesId, const FForestField& Field, float XM, float YM)
	{
		if (!CanGrowAt(Field, FVector2f(XM, YM)))
		{
			return 0.0f;
		}
		const FForestSample S = Field.Sample(XM, YM);
		float Soil = Species.Soils.Num() > 0 ? 0.1f : 1.0f;
		for (const FSoilPreference& Preference : Species.Soils)
		{
			if (Preference.Soil == S.Soil)
			{
				Soil = Preference.Weight;
			}
		}
		const float Edge = FMath::Clamp(1.0f + Species.EdgeAffinity * (S.Edgeness - 0.5f), 0.0f, 1.5f);
		// Real mycelia are patchy even in a uniform habitat: low-frequency noise per species.
		const uint32 NoiseSeed = Hash(static_cast<uint32>(Field.GetDefinition().Seed), HashString(SpeciesId.ToString()), 0x0153u);
		const float Noise = FMath::Clamp(0.9f + 0.4f * GradientFbm2D(NoiseSeed, XM / 150.0, YM / 150.0, 2), 0.6f, 1.2f);
		const float Value = HostFactor(Species, Field, S) * Respond(Species.PH, S.PH) * Respond(Species.AltitudeM, S.AltitudeM)
			* Respond(Species.Moisture, S.Moisture) * Respond(Species.Canopy, S.Canopy) * Soil * Edge * Noise;
		return FMath::Clamp(Value, 0.0f, 1.0f);
	}

	void GeneratePatches(const FMushroomSpeciesRow& Species, FName SpeciesId, const FForestField& Field, TArray<FMyceliumPatch>& Out)
	{
		const float Density = FMath::Max(Species.PatchDensityPerHa, 0.01f);
		const float CellM = FMath::Clamp(0.7f * FMath::Sqrt(10000.0f / Density), 2.0f, 60.0f);
		const float Keep = Density * CellM * CellM / 10000.0f;
		const uint32 Seed = Hash(static_cast<uint32>(Field.GetDefinition().Seed), HashString(SpeciesId.ToString()), 0x9A7Cu);
		const int32 Cells = FMath::CeilToInt(Field.GetSizeM() / CellM);
		for (int32 CY = 0; CY < Cells; ++CY)
		{
			for (int32 CX = 0; CX < Cells; ++CX)
			{
				const uint32 H = Hash(Seed, static_cast<uint32>(CX), static_cast<uint32>(CY));
				if (ToUnitFloat(Hash(H, 1u)) >= Keep)
				{
					continue;
				}
				const FVector2f P((CX + ToUnitFloat(Hash(H, 2u))) * CellM, (CY + ToUnitFloat(Hash(H, 3u))) * CellM);
				const float S = Suitability(Species, SpeciesId, Field, P.X, P.Y);
				// Kept with probability S^1.5: the best places get most patches.
				if (S <= 0.0f || ToUnitFloat(Hash(H, 4u)) >= FMath::Pow(S, 1.5f))
				{
					continue;
				}
				FMyceliumPatch& Patch = Out.AddDefaulted_GetRef();
				Patch.SpeciesId = SpeciesId;
				Patch.Id = H;
				Patch.PositionM = P;
				Patch.RadiusM = FMath::Lerp(Species.PatchRadiusM.Min, Species.PatchRadiusM.Max, ToUnitFloat(Hash(H, 5u)));
				Patch.Strength = S;
			}
		}
	}

	double GrowthTickSeconds(int32 Day)
	{
		return (static_cast<double>(Day) + GrowthTickHour / 24.0) * GameSecondsPerDay;
	}

	float Trigger(const FMushroomSpeciesRow& Species, const FSpawnContext& Context, double TickGameSeconds, float LagRealDays)
	{
		const double B = FMath::Max(Context.Calendar.BiologicalDaysPerGameDay, 0.01f);
		const double Lag = LagRealDays / B * GameSecondsPerDay;
		const double Window = FMath::Max(Species.RainWindowDays, 0.5f) / B * GameSecondsPerDay;
		const double From = TickGameSeconds - Lag - Window;
		const double To = TickGameSeconds - Lag;

		const FTrapezoid& Months = Species.FruitingMonths;
		const bool bAllYear = Months.Min == 0.0f && Months.OptLow == 0.0f && Months.OptHigh == 0.0f && Months.Max == 0.0f;
		const float Phenology = bAllYear ? 1.0f : Months.EvaluatePeriodic(CalendarMath::MonthAt(Context.Calendar, TickGameSeconds), 12.0f);
		if (Phenology <= 0.0f)
		{
			return 0.0f;
		}
		const float RainEquivalent = static_cast<float>(B) * (Context.Weather.RainMm ? Context.Weather.RainMm(From, To) : 0.0f);
		const float Rain = ShroomMath::SmoothStep01(Species.RainMm.Min, Species.RainMm.Max, RainEquivalent);
		if (Rain <= 0.0f)
		{
			return 0.0f;
		}
		const float Temperature = Context.Weather.MeanTemperatureC ? Context.Weather.MeanTemperatureC(From, To) : 15.0f;
		return Phenology * Rain * Respond(Species.TempC, Temperature);
	}

	float FlushProbability(const FMushroomSpeciesRow& Species, float Strength, float TriggerValue, float Exhaustion)
	{
		return (1.0f - FMath::Exp(-Species.FlushRate * Strength * TriggerValue)) * FMath::Clamp(1.0f - Exhaustion, 0.0f, 1.0f);
	}

	int32 PoissonFromUniform(float Lambda, float U)
	{
		if (Lambda <= 0.0f)
		{
			return 0;
		}
		double P = FMath::Exp(-static_cast<double>(Lambda));
		double Cumulative = P;
		int32 K = 0;
		while (U > Cumulative && K < 200)
		{
			++K;
			P *= Lambda / K;
			Cumulative += P;
		}
		return K;
	}

	FFlushRoll RawRoll(const FMushroomSpeciesRow& Species, const FMyceliumPatch& Patch, const FSpawnContext& Context, int32 Day)
	{
		FFlushRoll Roll;
		Roll.Day = Day;
		const uint32 H = Hash(Patch.Id, static_cast<uint32>(Day), 0xF1u);
		// Lag drawn per patch and flush, quantized to 0.25 day so patches share trigger values.
		const float LagRaw = FMath::Lerp(Species.LagDays.Min, Species.LagDays.Max, ToUnitFloat(Hash(H, 1u)));
		const int32 LagBucket = FMath::RoundToInt(LagRaw * 4.0f);
		const double Tick = GrowthTickSeconds(Day);
		if (Context.TriggerCache)
		{
			const uint64 Key = (static_cast<uint64>(HashString(Patch.SpeciesId.ToString())) << 32) ^ (static_cast<uint64>(static_cast<uint32>(Day)) << 8) ^ static_cast<uint64>(LagBucket);
			if (const float* Found = Context.TriggerCache->Find(Key))
			{
				Roll.Trigger = *Found;
			}
			else
			{
				Roll.Trigger = Trigger(Species, Context, Tick, LagBucket / 4.0f);
				Context.TriggerCache->Add(Key, Roll.Trigger);
			}
		}
		else
		{
			Roll.Trigger = Trigger(Species, Context, Tick, LagBucket / 4.0f);
		}
		Roll.Probability = FlushProbability(Species, Patch.Strength, Roll.Trigger, 0.0f);
		Roll.bFlush = Roll.Probability > 0.0f && ToUnitFloat(Hash(H, 2u)) < Roll.Probability;
		if (Roll.bFlush)
		{
			Roll.Fruits = FMath::Max(1, PoissonFromUniform(Species.FruitsPerFlush * Patch.Strength * Roll.Trigger, ToUnitFloat(Hash(H, 3u))));
			if (Species.Clustering == EClustering::Solitary)
			{
				Roll.Fruits = FMath::Min(Roll.Fruits, 2);
			}
		}
		return Roll;
	}

	void FruitBodiesAt(const FMushroomSpeciesRow& Species, const FMyceliumPatch& Patch, const FSpawnContext& Context, double NowGameSeconds,
		TArray<FFruitBody>& Out, TArray<FFlushRoll>* Rolls)
	{
		if (!Context.Field)
		{
			return;
		}
		const double B = FMath::Max(Context.Calendar.BiologicalDaysPerGameDay, 0.01f);
		const double Lifetime = FMath::Max(Species.LifetimeDays, 0.5f) / B * GameSecondsPerDay;
		const int32 Cooldown = FMath::Max(0, FMath::CeilToInt(Species.FlushCooldownDays / B));
		const int32 NowDay = FMath::FloorToInt(NowGameSeconds / GameSecondsPerDay);
		const int32 FirstDay = NowDay - FMath::CeilToInt(Lifetime / GameSecondsPerDay + 0.5) - 1;

		TMap<int32, FFlushRoll> Memo;
		auto RollOn = [&](int32 Day) -> const FFlushRoll&
		{
			if (const FFlushRoll* Found = Memo.Find(Day))
			{
				return *Found;
			}
			return Memo.Add(Day, RawRoll(Species, Patch, Context, Day));
		};

		for (int32 Day = FirstDay; Day <= NowDay; ++Day)
		{
			const double Tick = GrowthTickSeconds(Day);
			if (Tick > NowGameSeconds)
			{
				break;
			}
			const FFlushRoll Roll = RollOn(Day);
			if (!Roll.bFlush)
			{
				continue;
			}
			bool bCoolingDown = false;
			for (int32 Back = 1; Back <= Cooldown && !bCoolingDown; ++Back)
			{
				bCoolingDown = RollOn(Day - Back).bFlush;
			}
			if (bCoolingDown)
			{
				continue;
			}
			if (Rolls)
			{
				Rolls->Add(Roll);
			}
			for (int32 Index = 0; Index < Roll.Fruits; ++Index)
			{
				const uint32 Seed = Hash(Patch.Id, static_cast<uint32>(Day), 100u + static_cast<uint32>(Index));
				FFruitBody Fruit;
				Fruit.SpeciesId = Patch.SpeciesId;
				Fruit.PatchId = Patch.Id;
				Fruit.Seed = Seed;
				// Not all at once: births spread over half a game day after the tick.
				Fruit.BirthGameSeconds = Tick + 0.5 * GameSecondsPerDay * ToUnitFloat(Hash(Seed, 1u));
				Fruit.LifetimeGameSeconds = Lifetime * (0.85 + 0.3 * ToUnitFloat(Hash(Seed, 2u)));
				if (!Fruit.IsAlive(NowGameSeconds))
				{
					continue;
				}
				Fruit.PositionM = Patch.PositionM + FruitOffset(Species, Patch, Day, Index, Roll.Fruits);
				if (!CanGrowAt(*Context.Field, Fruit.PositionM))
				{
					continue;
				}
				Fruit.YawDeg = 360.0f * ToUnitFloat(Hash(Seed, 3u));
				Fruit.Scale = 0.85f + 0.3f * ToUnitFloat(Hash(Seed, 4u));
				Out.Add(Fruit);
			}
		}
	}
}
