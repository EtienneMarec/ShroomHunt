#include "Economy/EconomyData.h"
#include "Economy/MarketModel.h"
#include "Knowledge/KnowledgeText.h"
#include "Misc/AutomationTest.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Player/ForagingSubsystem.h"
#include "Time/TimeTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace EconomyTestsDetail
{
	FMushroomSpeciesRow MakeSpecies(EEdibility Edibility, float PricePerKg, float WormyChance = 0.0f)
	{
		FMushroomSpeciesRow Row;
		Row.Edibility = Edibility;
		Row.PricePerKg = PricePerKg;
		Row.WormyChance = WormyChance;
		return Row;
	}

	FBasketItem MakeItem(int64 Seed, float Grams, float Age, double PickSeconds)
	{
		FBasketItem Item;
		Item.SpeciesId = TEXT("Test");
		Item.Seed = Seed;
		Item.WeightGrams = Grams;
		Item.AgeAtPick = Age;
		Item.PickGameSeconds = PickSeconds;
		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarketModelTest, "ShroomHunt.Economy.Market",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMarketModelTest::RunTest(const FString& Parameters)
{
	using namespace EconomyTestsDetail;
	const FMarketSettings Settings;
	const double Day = GameSecondsPerDay;

	// Freshness: biological time (4 bio days per game day), half-life in bio days.
	TestEqual(TEXT("Just picked: fresh"), MarketModel::Freshness(100.0, 100.0, 4.0f, 3.0f), 1.0f);
	TestTrue(TEXT("Half-life: 3 bio days = 0.75 game day"), FMath::IsNearlyEqual(MarketModel::Freshness(0.0, 0.75 * Day, 4.0f, 3.0f), 0.5f, 0.001f));

	// Age: young and mature full value, old less.
	TestEqual(TEXT("Young specimen: full quality"), MarketModel::AgeQuality(0.2f), 1.0f);
	TestTrue(TEXT("Old specimen: low quality"), MarketModel::AgeQuality(1.0f) < 0.35f && MarketModel::AgeQuality(0.8f) < MarketModel::AgeQuality(0.5f));

	// Worms: deterministic per seed, frequency close to the chance (older -> more).
	int32 Wormy = 0;
	for (uint32 Seed = 1; Seed <= 4000; ++Seed)
	{
		Wormy += MarketModel::IsWormy(Seed, 0.5f, 1.0f) ? 1 : 0;
		TestTrue(TEXT("Worms are fixed by the seed"), MarketModel::IsWormy(Seed, 0.5f, 1.0f) == MarketModel::IsWormy(Seed, 0.5f, 1.0f));
	}
	TestTrue(FString::Printf(TEXT("Old specimens wormy at the species rate (%d / 4000)"), Wormy), Wormy > 1800 && Wormy < 2200);
	TestFalse(TEXT("A species without worms has none"), MarketModel::IsWormy(7u, 0.0f, 1.0f));

	// Demand: full price at first, lower when the day's demand is filled; additive over splits.
	TestTrue(TEXT("First grams at full price"), FMath::IsNearlyEqual(MarketModel::DemandFactor(0.0f, 0.001f, 1.5f), 1.0f, 0.001f));
	TestTrue(TEXT("Half price once the demand is sold"), FMath::IsNearlyEqual(MarketModel::DemandFactor(1.5f, 0.0f, 1.5f), 0.5f, 0.001f));
	const float Whole = 1.0f * MarketModel::DemandFactor(0.0f, 1.0f, 1.5f);
	const float Split = 0.4f * MarketModel::DemandFactor(0.0f, 0.4f, 1.5f) + 0.6f * MarketModel::DemandFactor(0.4f, 0.6f, 1.5f);
	TestTrue(TEXT("Selling in one go or in parts pays the same"), FMath::IsNearlyEqual(Whole, Split, 0.001f));
	TestTrue(TEXT("Reputation factor 0.7 / 1 / 1.3"), FMath::IsNearlyEqual(MarketModel::ReputationFactor(0.0f), 0.7f)
		&& FMath::IsNearlyEqual(MarketModel::ReputationFactor(50.0f), 1.0f) && FMath::IsNearlyEqual(MarketModel::ReputationFactor(100.0f), 1.3f));

	// Verdicts.
	const FMushroomSpeciesRow Cep = MakeSpecies(EEdibility::Excellent, 30.0f);
	const FBasketItem Fresh = MakeItem(11, 200.0f, 0.3f, 0.0);
	const FSpecimenAppraisal Sold = MarketModel::Appraise(Cep, Fresh, true, 0.0, 4.0f, Settings, 0.0f, 50.0f, false);
	TestTrue(TEXT("A fresh labelled cep is sold"), Sold.Verdict == ESaleVerdict::Sold);
	const float Expected = 30.0f * 0.2f * MarketModel::DemandFactor(0.0f, 0.2f, Settings.DemandKgPerDay);
	TestTrue(FString::Printf(TEXT("200 g of fresh cep at 30 EUR/kg: %.2f EUR (expected %.2f)"), Sold.PriceEuros, Expected), FMath::IsNearlyEqual(Sold.PriceEuros, Expected, 0.01f));
	TestTrue(TEXT("Unlabelled: refused"), MarketModel::Appraise(Cep, Fresh, false, 0.0, 4.0f, Settings, 0.0f, 50.0f, false).Verdict == ESaleVerdict::Unlabelled);
	TestTrue(TEXT("Banned: refused"), MarketModel::Appraise(Cep, Fresh, true, 0.0, 4.0f, Settings, 0.0f, 50.0f, true).Verdict == ESaleVerdict::Banned);
	TestTrue(TEXT("Kept three game days: rotten"), MarketModel::Appraise(Cep, Fresh, true, 3.0 * Day, 4.0f, Settings, 0.0f, 50.0f, false).Verdict == ESaleVerdict::Rotten);
	const FMushroomSpeciesRow Bitter = MakeSpecies(EEdibility::Inedible, 0.0f);
	TestTrue(TEXT("Inedible: refused"), MarketModel::Appraise(Bitter, Fresh, true, 0.0, 4.0f, Settings, 0.0f, 50.0f, false).Verdict == ESaleVerdict::Inedible);
	const FMushroomSpeciesRow DeathCap = MakeSpecies(EEdibility::Deadly, 0.0f);
	TestTrue(TEXT("Deadly: found even unlabelled and while banned"),
		MarketModel::Appraise(DeathCap, Fresh, false, 0.0, 4.0f, Settings, 0.0f, 50.0f, true).Verdict == ESaleVerdict::Toxic);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyStateTest, "ShroomHunt.Economy.State",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEconomyStateTest::RunTest(const FString& Parameters)
{
	FMarketSettings Settings;
	FEconomyState State;
	State.Reset(Settings);
	TestEqual(TEXT("Start money"), State.Money, Settings.StartMoney);

	State.RecordSale(TEXT("Cep"), 0.5f, 10.0f, 3, Settings);
	TestEqual(TEXT("Money earned"), State.Money, Settings.StartMoney + 10.0f);
	TestEqual(TEXT("Demand of the day"), State.GetSoldTodayKg(TEXT("Cep"), 3), 0.5f);
	TestEqual(TEXT("A new day resets the demand"), State.GetSoldTodayKg(TEXT("Cep"), 4), 0.0f);
	TestTrue(TEXT("Good sales raise the reputation"), State.Reputation > Settings.StartReputation);

	FSaleReport Report;
	const double Now = 5.0 * GameSecondsPerDay;
	const float MoneyBefore = State.Money;
	State.ApplyToxicLot(true, Now, Settings, Report);
	TestEqual(TEXT("Deadly fine"), MoneyBefore - State.Money, Settings.FineDeadly);
	TestTrue(TEXT("Banned for days"), State.IsBanned(Now + 0.5 * GameSecondsPerDay) && !State.IsBanned(Now + (Settings.BanGameDaysDeadly + 0.1) * GameSecondsPerDay));
	TestTrue(TEXT("Reputation crash"), Report.ReputationChange <= -Settings.ReputationDeadly + 2.0f * 0.5f + 0.01f);
	TestTrue(TEXT("The fine can leave a debt"), State.Money < 0.0f);

	FShopItem Book;
	Book.Id = TEXT("BookTest");
	Book.Kind = EShopItemKind::Book;
	Book.Price = 40.0f;
	FShopItem Kit;
	Kit.Id = TEXT("KitTest");
	Kit.Kind = EShopItemKind::Kit;
	Kit.Price = 2.0f;
	Kit.Uses = 5;
	TestFalse(TEXT("No buying with a debt"), State.Buy(Book));
	State.Money = 100.0f;
	TestTrue(TEXT("Buy a book"), State.Buy(Book));
	TestFalse(TEXT("A book is bought once"), State.Buy(Book));
	TestTrue(TEXT("Buy kits twice"), State.Buy(Kit) && State.Buy(Kit));
	TestEqual(TEXT("Kit uses add up"), State.GetItemCount(Kit.Id), 10);
	TestTrue(TEXT("Use a kit"), State.UseKit(Kit.Id) && State.GetItemCount(Kit.Id) == 9);
	TestEqual(TEXT("Money spent"), State.Money, 100.0f - 40.0f - 4.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEconomyDataTest, "ShroomHunt.Economy.ShopData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEconomyDataTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	FShopData Shop;
	FKnowledgeSourceData Sources;
	if (!EconomyData::LoadShop(Shop, Errors) || !KnowledgeText::LoadSources(Sources, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	if (!EconomyData::ValidateShop(Shop, Sources, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
	}
	TestTrue(TEXT("Shop sells books and kits"), Shop.Items.ContainsByPredicate([](const FShopItem& I) { return I.Kind == EShopItemKind::Book; })
		&& Shop.Items.ContainsByPredicate([](const FShopItem& I) { return I.Kind == EShopItemKind::Kit; }));
	// Every book of Sources.json is for sale.
	for (const FKnowledgeSource& Source : Sources.Sources)
	{
		if (Source.Kind == EKnowledgeSourceKind::Book)
		{
			TestTrue(FString::Printf(TEXT("%s is sold"), *Source.Id.ToString()),
				Shop.Items.ContainsByPredicate([&Source](const FShopItem& I) { return I.Source == Source.Id; }));
		}
	}

	// The validator catches mistakes.
	FShopData Broken = Shop;
	const FShopItem Duplicate = Broken.Items[0]; // never Add() an element of the same array
	Broken.Items.Add(Duplicate);
	FShopItem BadBook;
	BadBook.Id = TEXT("BookNowhere");
	BadBook.Title = FText::FromString(TEXT("x"));
	BadBook.Source = TEXT("PosterOurWoods");
	Broken.Items.Add(BadBook);
	TArray<FString> BrokenErrors;
	TestFalse(TEXT("Duplicate and non-book source rejected"), EconomyData::ValidateShop(Broken, Sources, BrokenErrors));
	TestEqual(TEXT("Two errors reported"), BrokenErrors.Num(), 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
