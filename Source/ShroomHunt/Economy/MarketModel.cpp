#include "Economy/MarketModel.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Player/ForagingSubsystem.h"
#include "Time/TimeTypes.h"

namespace MarketModel
{
	float Freshness(double PickGameSeconds, double NowGameSeconds, float BiologicalDaysPerGameDay, float HalfLifeBioDays)
	{
		const double BioDays = FMath::Max(0.0, NowGameSeconds - PickGameSeconds) / GameSecondsPerDay * FMath::Max(BiologicalDaysPerGameDay, 0.01f);
		return static_cast<float>(FMath::Pow(0.5, BioDays / FMath::Max(HalfLifeBioDays, 0.01f)));
	}

	float AgeQuality(float AgeAtPick)
	{
		return 1.0f - 0.7f * ShroomMath::SmoothStep01(0.55f, 1.0f, AgeAtPick);
	}

	bool IsWormy(uint32 Seed, float WormyChance, float AgeAtPick)
	{
		const float Chance = FMath::Clamp(WormyChance, 0.0f, 1.0f) * (0.3f + 0.7f * FMath::Clamp(AgeAtPick, 0.0f, 1.0f));
		return ShroomHash::ToUnitFloat(ShroomHash::Hash(Seed, 0x3011u)) < Chance;
	}

	float DemandFactor(float SoldTodayKg, float ItemKg, float DemandKgPerDay)
	{
		const double D = FMath::Max(DemandKgPerDay, 0.01f);
		const double S = FMath::Max(SoldTodayKg, 0.0f);
		const double K = FMath::Max(ItemKg, 0.0f);
		if (K < 1e-6)
		{
			return static_cast<float>(1.0 / (1.0 + S / D));
		}
		// Integral of 1 / (1 + x / D) over [S, S + K], divided by K.
		return static_cast<float>(D / K * FMath::Loge((D + S + K) / (D + S)));
	}

	float ReputationFactor(float Reputation)
	{
		return 0.7f + 0.6f * FMath::Clamp(Reputation, 0.0f, 100.0f) / 100.0f;
	}

	bool IsToxic(const FMushroomSpeciesRow& Species)
	{
		return Species.Edibility == EEdibility::Toxic || Species.Edibility == EEdibility::Deadly;
	}

	FSpecimenAppraisal Appraise(const FMushroomSpeciesRow& Species, const FBasketItem& Item, bool bLabelled,
		double NowGameSeconds, float BiologicalDaysPerGameDay, const FMarketSettings& Settings, float SoldTodayKg, float Reputation, bool bBanned)
	{
		FSpecimenAppraisal Out;
		Out.SpeciesId = Item.SpeciesId;
		Out.Seed = Item.Seed;
		Out.WeightKg = FMath::Max(Item.WeightGrams, 0.0f) / 1000.0f;
		Out.Freshness = Freshness(Item.PickGameSeconds, NowGameSeconds, BiologicalDaysPerGameDay, Settings.FreshnessHalfLifeBioDays);
		Out.AgeQuality = AgeQuality(Item.AgeAtPick);
		Out.bWormy = IsWormy(static_cast<uint32>(Item.Seed), Species.WormyChance, Item.AgeAtPick);
		Out.Quality = FMath::Clamp(Out.AgeQuality * Out.Freshness * (Out.bWormy ? Settings.WormyQuality : 1.0f), 0.0f, 1.0f);

		// The buyer checks every mushroom: a toxic one is found whatever its label.
		if (IsToxic(Species))
		{
			Out.Verdict = ESaleVerdict::Toxic;
		}
		else if (bBanned)
		{
			Out.Verdict = ESaleVerdict::Banned;
		}
		else if (!bLabelled)
		{
			Out.Verdict = ESaleVerdict::Unlabelled;
		}
		else if (Species.Edibility == EEdibility::Inedible || Species.PricePerKg <= 0.0f)
		{
			Out.Verdict = ESaleVerdict::Inedible;
		}
		else if (Out.Quality < Settings.MinQuality)
		{
			Out.Verdict = ESaleVerdict::Rotten;
		}
		else
		{
			Out.Verdict = ESaleVerdict::Sold;
			Out.PriceEuros = Species.PricePerKg * Out.WeightKg * Out.Quality
				* DemandFactor(SoldTodayKg, Out.WeightKg, Settings.DemandKgPerDay) * ReputationFactor(Reputation);
		}
		return Out;
	}
}

// --- FEconomyState ------------------------------------------------------------------------------

void FEconomyState::Reset(const FMarketSettings& Settings)
{
	*this = FEconomyState();
	Money = Settings.StartMoney;
	Reputation = Settings.StartReputation;
}

float FEconomyState::GetSoldTodayKg(FName SpeciesId, int32 Day) const
{
	return Day == SalesDay ? SoldTodayKg.FindRef(SpeciesId) : 0.0f;
}

void FEconomyState::RecordSale(FName SpeciesId, float Kg, float Euros, int32 Day, const FMarketSettings& Settings)
{
	if (Day != SalesDay)
	{
		SalesDay = Day;
		SoldTodayKg.Reset();
	}
	SoldTodayKg.FindOrAdd(SpeciesId) += Kg;
	Money += Euros;
	TotalEarned += Euros;
	Reputation = FMath::Clamp(Reputation + Settings.ReputationPerKg * Kg, 0.0f, 100.0f);
}

void FEconomyState::ApplyToxicLot(bool bDeadly, double NowGameSeconds, const FMarketSettings& Settings, FSaleReport& Report)
{
	Report.bRefusedToxic = true;
	Report.bDeadly = bDeadly;
	Report.Fine = bDeadly ? Settings.FineDeadly : Settings.FineToxic;
	const float Loss = bDeadly ? Settings.ReputationDeadly : Settings.ReputationToxic;
	const float Before = Reputation;
	Reputation = FMath::Clamp(Reputation - Loss, 0.0f, 100.0f);
	Report.ReputationChange += Reputation - Before;
	Money -= Report.Fine;
	TotalFines += Report.Fine;
	const double Ban = (bDeadly ? Settings.BanGameDaysDeadly : Settings.BanGameDaysToxic) * GameSecondsPerDay;
	BannedUntilGameSeconds = FMath::Max(BannedUntilGameSeconds, NowGameSeconds + Ban);
	Report.bBanned = Ban > 0.0;
}

void FEconomyState::ApplyInedible(int32 Count, const FMarketSettings& Settings, FSaleReport& Report)
{
	if (Count <= 0)
	{
		return;
	}
	const float Before = Reputation;
	Reputation = FMath::Clamp(Reputation - Settings.ReputationInedible * Count, 0.0f, 100.0f);
	Report.ReputationChange += Reputation - Before;
}

bool FEconomyState::CanBuy(const FShopItem& Item) const
{
	if (Money + KINDA_SMALL_NUMBER < Item.Price)
	{
		return false;
	}
	return Item.Kind != EShopItemKind::Book || GetItemCount(Item.Id) == 0;
}

bool FEconomyState::Buy(const FShopItem& Item)
{
	if (!CanBuy(Item))
	{
		return false;
	}
	Money -= Item.Price;
	Items.FindOrAdd(Item.Id) += Item.Kind == EShopItemKind::Kit ? FMath::Max(Item.Uses, 1) : 1;
	return true;
}

bool FEconomyState::UseKit(FName ItemId)
{
	int32* Count = Items.Find(ItemId);
	if (!Count || *Count <= 0)
	{
		return false;
	}
	--*Count;
	return true;
}
