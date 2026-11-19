#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Misc/AutomationTest.h"
#include "Mushroom/MushroomMorphology.h"
#include "Mycology/ObservableCriteria.h"
#include "Mycology/SpawnModel.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Player/ForagingSubsystem.h"
#include "Time/CalendarMath.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForagingTest, "ShroomHunt.Player.Foraging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FForagingTest::RunTest(const FString& Parameters)
{
	// Exhaustion: 0.15 per pick, recovering with a 3-day time constant, capped at 1, only past picks.
	const double Day = GameSecondsPerDay;
	const TArray<double> One = { 10.0 * Day };
	TestTrue(TEXT("Exhaustion right after a pick"), FMath::IsNearlyEqual(ForagingMath::Exhaustion(One, 10.0 * Day, 0.15f, 3.0f), 0.15f, 1e-4f));
	TestTrue(TEXT("Exhaustion recovers"), FMath::IsNearlyEqual(ForagingMath::Exhaustion(One, 13.0 * Day, 0.15f, 3.0f), 0.15f * FMath::Exp(-1.0f), 1e-3f));
	TestEqual(TEXT("A future pick does not count"), ForagingMath::Exhaustion(One, 9.0 * Day, 0.15f, 3.0f), 0.0f);
	TArray<double> Many;
	Many.Init(10.0 * Day, 20);
	TestEqual(TEXT("Capped at 1"), ForagingMath::Exhaustion(Many, 10.0 * Day, 0.15f, 3.0f), 1.0f);

	// Spawn hooks: a picked mushroom is gone, an exhausted patch does not flush.
	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(TEXT("F01_LowlandOakBeech"), Errors);
	TMap<FName, FMushroomSpeciesRow> Species;
	FString Source;
	if (!Field || !USpeciesDatabaseSubsystem::LoadSpeciesForTools(Species, Source))
	{
		AddError(TEXT("Missing forest field or species data"));
		return false;
	}
	FMushroomSpeciesRow AllYear = Species[TEXT("CantharellusCibarius")];
	AllYear.FruitingMonths = FTrapezoid();
	AllYear.TempC = FTrapezoid();
	TArray<FMyceliumPatch> Patches;
	SpawnModel::GeneratePatches(AllYear, TEXT("CantharellusCibarius"), *Field, Patches);
	if (!TestTrue(TEXT("Chanterelle patches"), Patches.Num() > 0))
	{
		return false;
	}
	FMyceliumPatch Patch = Patches[0];
	Patch.Strength = 1.0f;
	FSpawnContext Context;
	Context.Field = Field.Get();
	Context.Weather.RainMm = [](double From, double To) { return static_cast<float>((To - From) / 3600.0); };
	Context.Weather.MeanTemperatureC = [](double, double) { return 15.0f; };
	const double Now = SpawnModel::GrowthTickSeconds(60) + 0.6 * Day;
	TArray<FFruitBody> Before;
	SpawnModel::FruitBodiesAt(AllYear, Patch, Context, Now, Before);
	if (!TestTrue(TEXT("Wet weather gives chanterelles"), Before.Num() > 0))
	{
		return false;
	}
	const uint32 Picked = Before[0].Seed;
	Context.IsPicked = [Picked](uint32 Seed) { return Seed == Picked; };
	TArray<FFruitBody> After;
	SpawnModel::FruitBodiesAt(AllYear, Patch, Context, Now, After);
	TestEqual(TEXT("The picked mushroom is gone, the others stay"), After.Num(), Before.Num() - 1);

	Context.Exhaustion = [Now](uint32, double Time) { return Time >= Now ? 1.0f : 0.0f; };
	int32 FlushesAfter = 0;
	for (int32 D = 61; D < 75; ++D)
	{
		FlushesAfter += SpawnModel::RawRoll(AllYear, Patch, Context, D).bFlush ? 1 : 0;
	}
	TestEqual(TEXT("A fully exhausted patch does not flush"), FlushesAfter, 0);
	TArray<FFruitBody> SameDay;
	SpawnModel::FruitBodiesAt(AllYear, Patch, Context, Now, SameDay);
	TestEqual(TEXT("Exhaustion does not change mushrooms already grown"), SameDay.Num(), After.Num());

	// Observable criteria.
	TestTrue(TEXT("Red"), ObservableCriteria::ColorName(FLinearColor(0.85f, 0.15f, 0.08f)).ToString() == TEXT("red"));
	TestTrue(TEXT("Brown"), ObservableCriteria::ColorName(FLinearColor(0.45f, 0.30f, 0.16f)).ToString() == TEXT("brown"));
	TestTrue(TEXT("White"), ObservableCriteria::ColorName(FLinearColor(0.93f, 0.92f, 0.90f)).ToString() == TEXT("white"));
	TestTrue(TEXT("Yellow"), ObservableCriteria::ColorName(FLinearColor(0.95f, 0.78f, 0.25f)).ToString() == TEXT("yellow"));
	for (const TPair<FName, FMushroomSpeciesRow>& Pair : Species)
	{
		const FMushroomMorphology M = MushroomMorphology::Sample(Pair.Value, Pair.Key, 5u, EMushroomStage::Mature);
		const TArray<FText> Lines = ObservableCriteria::Describe(M, Pair.Value);
		AddInfo(FString::Printf(TEXT("%s: %s | %s"), *Pair.Key.ToString(), *ObservableCriteria::VagueLabel(M).ToString(), *FText::Join(FText::FromString(TEXT(" / ")), Lines).ToString()));
		TestTrue(Pair.Key.ToString() + TEXT(": at least cap, underside, stem and age"), Lines.Num() >= 4);
		const float Grams = ObservableCriteria::EstimateWeightGrams(M);
		TestTrue(FString::Printf(TEXT("%s: plausible weight %.0f g"), *Pair.Key.ToString(), Grams), Grams > 5.0f && Grams < 1500.0f);
	}
	const FMushroomMorphology Cep = MushroomMorphology::Sample(Species[TEXT("BoletusEdulis")], TEXT("BoletusEdulis"), 5u, EMushroomStage::Mature);
	TestTrue(TEXT("A cep is recognized as having pores"), ObservableCriteria::VagueLabel(Cep).ToString().Contains(TEXT("pores")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
