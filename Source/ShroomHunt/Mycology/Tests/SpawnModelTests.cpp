#include "Core/ShroomHash.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Misc/AutomationTest.h"
#include "Mycology/SpawnModel.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Time/CalendarMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SpawnModelTestsDetail
{
	/** Synthetic weather: RainMmPerHour between RainFrom and RainTo (game seconds), else dry; constant temperature. */
	FSpawnWeather MakeWeather(double RainFrom, double RainTo, float RainMmPerHour, float TemperatureC)
	{
		FSpawnWeather Weather;
		Weather.RainMm = [=](double From, double To)
		{
			const double Overlap = FMath::Max(0.0, FMath::Min(To, RainTo) - FMath::Max(From, RainFrom));
			return static_cast<float>(Overlap / 3600.0 * RainMmPerHour);
		};
		Weather.MeanTemperatureC = [=](double, double) { return TemperatureC; };
		return Weather;
	}

	bool LoadAll(FAutomationTestBase& Test, TSharedPtr<const FForestField>& OutField, TMap<FName, FMushroomSpeciesRow>& OutSpecies)
	{
		TArray<FString> Errors;
		OutField = ForestFieldCache::GetField(TEXT("F01_LowlandOakBeech"), Errors);
		FString Source;
		if (!OutField || !USpeciesDatabaseSubsystem::LoadSpeciesForTools(OutSpecies, Source) || !OutSpecies.Contains(TEXT("BoletusEdulis")))
		{
			Test.AddError(TEXT("Forest field or species data missing: ") + FString::Join(Errors, TEXT("; ")));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnSuitabilityTest, "ShroomHunt.Mycology.Spawn.Suitability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSpawnSuitabilityTest::RunTest(const FString& Parameters)
{
	using namespace SpawnModelTestsDetail;
	TSharedPtr<const FForestField> Field;
	TMap<FName, FMushroomSpeciesRow> Species;
	if (!LoadAll(*this, Field, Species))
	{
		return false;
	}
	for (const TPair<FName, FMushroomSpeciesRow>& Pair : Species)
	{
		const FString Name = Pair.Key.ToString();
		TArray<FMyceliumPatch> Patches;
		TArray<FMyceliumPatch> Again;
		SpawnModel::GeneratePatches(Pair.Value, Pair.Key, *Field, Patches);
		SpawnModel::GeneratePatches(Pair.Value, Pair.Key, *Field, Again);
		AddInfo(FString::Printf(TEXT("%s: %d patches"), *Name, Patches.Num()));
		TestTrue(Name + TEXT(": a plausible number of patches on 1 km2"), Patches.Num() >= 3 && Patches.Num() <= 250);
		TestEqual(Name + TEXT(": patches are deterministic"), Again.Num(), Patches.Num());

		// Patches sit on better ground than the forest average, never on water.
		double PatchMean = 0.0;
		bool bSame = true;
		for (int32 Index = 0; Index < Patches.Num(); ++Index)
		{
			PatchMean += Patches[Index].Strength;
			bSame &= Again.IsValidIndex(Index) && Again[Index].Id == Patches[Index].Id && Again[Index].PositionM == Patches[Index].PositionM;
			TestFalse(Name + TEXT(": no patch on water"), Field->Sample(Patches[Index].PositionM.X, Patches[Index].PositionM.Y).bWater);
		}
		TestTrue(Name + TEXT(": identical patches"), bSame);
		double ForestMean = 0.0;
		int32 Samples = 0;
		for (float Y = 4.0f; Y < Field->GetSizeM(); Y += 16.0f)
		{
			for (float X = 4.0f; X < Field->GetSizeM(); X += 16.0f)
			{
				const float S = SpawnModel::Suitability(Pair.Value, Pair.Key, *Field, X, Y);
				if (S < 0.0f || S > 1.0f)
				{
					AddError(FString::Printf(TEXT("%s: suitability %.3f out of [0, 1]"), *Name, S));
					return false;
				}
				ForestMean += S;
				++Samples;
			}
		}
		ForestMean /= FMath::Max(Samples, 1);
		if (Patches.Num() > 0)
		{
			TestTrue(FString::Printf(TEXT("%s: patches on better ground (%.2f) than the forest mean (%.2f)"), *Name, PatchMean / Patches.Num(), ForestMean),
				PatchMean / Patches.Num() > ForestMean);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpawnFlushTest, "ShroomHunt.Mycology.Spawn.Flush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSpawnFlushTest::RunTest(const FString& Parameters)
{
	using namespace SpawnModelTestsDetail;
	TSharedPtr<const FForestField> Field;
	TMap<FName, FMushroomSpeciesRow> Species;
	if (!LoadAll(*this, Field, Species))
	{
		return false;
	}
	const FMushroomSpeciesRow& Cep = Species[TEXT("BoletusEdulis")];
	TArray<FMyceliumPatch> Patches;
	SpawnModel::GeneratePatches(Cep, TEXT("BoletusEdulis"), *Field, Patches);
	if (!TestTrue(TEXT("Cep has patches"), Patches.Num() > 0))
	{
		return false;
	}
	FMyceliumPatch Patch = Patches[0];
	Patch.Strength = 1.0f; // a perfect spot, so the timing is what we test

	FSpawnContext Context;
	Context.Field = Field.Get();
	// The seasons are compressed (autumn: 8 game days for 3 months): rain at the start of
	// September so that the lag (2-3 game days, about a month) still falls in the cep season.
	int32 RainDay = 0;
	while (RainDay < 100 && CalendarMath::MonthAt(Context.Calendar, (RainDay + 0.5) * GameSecondsPerDay) < 9.0f)
	{
		++RainDay;
	}
	const double RainStart = RainDay * GameSecondsPerDay + 10.0 * 3600.0;
	const double RainEnd = RainStart + 6.0 * 3600.0;
	AddInfo(FString::Printf(TEXT("Rain on game day %d, month %.1f"), RainDay, CalendarMath::MonthAt(Context.Calendar, RainStart)));

	// Dry weather: no trigger, no mushrooms.
	Context.Weather = MakeWeather(0.0, 0.0, 0.0f, 14.0f);
	TestEqual(TEXT("No rain, no trigger"), SpawnModel::Trigger(Cep, Context, SpawnModel::GrowthTickSeconds(RainDay + 3), 8.0f), 0.0f);

	// 30 mm in one afternoon (= 120 mm biological): mushrooms after the lag, not before.
	Context.Weather = MakeWeather(RainStart, RainEnd, 5.0f, 14.0f);
	TestEqual(TEXT("No trigger the day after the rain (lag)"), SpawnModel::Trigger(Cep, Context, SpawnModel::GrowthTickSeconds(RainDay + 1), Cep.LagDays.Min), 0.0f);
	TestTrue(TEXT("Full trigger three days after the rain"), SpawnModel::Trigger(Cep, Context, SpawnModel::GrowthTickSeconds(RainDay + 3), 8.0f) > 0.9f);

	auto CountAt = [&](double Now)
	{
		TArray<FFruitBody> Fruits;
		SpawnModel::FruitBodiesAt(Cep, Patch, Context, Now, Fruits);
		return Fruits.Num();
	};
	const double EarliestPossible = SpawnModel::GrowthTickSeconds(RainDay + 1);
	TestEqual(TEXT("No mushroom before the lag"), CountAt(EarliestPossible), 0);
	int32 Peak = 0;
	int32 FirstDay = -1;
	for (int32 Day = RainDay; Day <= RainDay + 8; ++Day)
	{
		const int32 Count = CountAt(SpawnModel::GrowthTickSeconds(Day) + 0.6 * GameSecondsPerDay);
		Peak = FMath::Max(Peak, Count);
		if (Count > 0 && FirstDay < 0)
		{
			FirstDay = Day;
		}
	}
	AddInfo(FString::Printf(TEXT("First cep %d game days after the rain, peak %d"), FirstDay - RainDay, Peak));
	TestTrue(TEXT("Ceps appear 2-4 game days after the rain"), FirstDay >= RainDay + 2 && FirstDay <= RainDay + 4);
	TestTrue(TEXT("They are gone 8 game days later"), CountAt(SpawnModel::GrowthTickSeconds(RainDay + 12)) == 0);

	// Determinism and consistency over time: the survivors of an hour later are the same mushrooms.
	const double Now = SpawnModel::GrowthTickSeconds(FirstDay >= 0 ? FirstDay : RainDay + 3) + 0.6 * GameSecondsPerDay;
	TArray<FFruitBody> A, B, Later;
	SpawnModel::FruitBodiesAt(Cep, Patch, Context, Now, A);
	SpawnModel::FruitBodiesAt(Cep, Patch, Context, Now, B);
	SpawnModel::FruitBodiesAt(Cep, Patch, Context, Now + 3600.0, Later);
	TestEqual(TEXT("Deterministic"), A.Num(), B.Num());
	for (const FFruitBody& Fruit : A)
	{
		if (Fruit.IsAlive(Now + 3600.0))
		{
			TestTrue(TEXT("Still there an hour later"), Later.ContainsByPredicate([&](const FFruitBody& Other) { return Other.Seed == Fruit.Seed && Other.PositionM == Fruit.PositionM; }));
		}
	}

	// Out of season (spring): the same rain does nothing.
	const double Spring = CalendarMath::EncodeDate(Context.Calendar, 1, ESeason::Spring, 2, 12.0f);
	Context.Weather = MakeWeather(Spring - 2.0 * GameSecondsPerDay, Spring, 5.0f, 14.0f);
	TestEqual(TEXT("No cep in early spring"), SpawnModel::Trigger(Cep, Context, Spring, 8.0f), 0.0f);

	// Statistics: the empirical flush frequency matches P_flush; Poisson mean matches lambda.
	FMushroomSpeciesRow AllYear = Cep;
	AllYear.FruitingMonths = FTrapezoid();
	AllYear.TempC = FTrapezoid();
	Context.Weather = MakeWeather(0.0, 1e12, 0.4f, 14.0f); // drizzle every day
	FMyceliumPatch Half = Patches[0];
	Half.Strength = 0.5f;
	double ExpectedSum = 0.0;
	int32 Flushes = 0;
	constexpr int32 Days = 10000;
	for (int32 Day = 100; Day < 100 + Days; ++Day)
	{
		const FFlushRoll Roll = SpawnModel::RawRoll(AllYear, Half, Context, Day);
		ExpectedSum += Roll.Probability;
		Flushes += Roll.bFlush ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Flush frequency %.3f, expected %.3f"), static_cast<double>(Flushes) / Days, ExpectedSum / Days));
	TestTrue(TEXT("Flush frequency matches P_flush"), FMath::Abs(static_cast<double>(Flushes) / Days - ExpectedSum / Days) < 0.02);
	double PoissonSum = 0.0;
	for (int32 Index = 0; Index < 20000; ++Index)
	{
		PoissonSum += SpawnModel::PoissonFromUniform(3.7f, ShroomHash::ToUnitFloat(ShroomHash::Hash(77u, static_cast<uint32>(Index))));
	}
	TestTrue(FString::Printf(TEXT("Poisson mean %.2f close to 3.7"), PoissonSum / 20000.0), FMath::Abs(PoissonSum / 20000.0 - 3.7) < 0.1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
