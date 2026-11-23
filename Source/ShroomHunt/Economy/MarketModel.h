#pragma once

#include "CoreMinimal.h"
#include "Economy/EconomyTypes.h"

struct FBasketItem;
struct FMushroomSpeciesRow;

/**
 * How the buyer values a basket (M3, Docs/Progression.md section 4): price per kg of the
 * species x weight x quality (age at picking, freshness since, worms) x demand of the day x
 * reputation. Pure and deterministic (worms are decided by the specimen's seed), unit-tested.
 */
namespace MarketModel
{
	/** 1 when just picked, halving every HalfLifeBioDays of biological time. */
	SHROOMHUNT_API float Freshness(double PickGameSeconds, double NowGameSeconds, float BiologicalDaysPerGameDay, float HalfLifeBioDays);

	/** Young and mature specimens are worth the most; old ones much less (0.3 at the end of life). */
	SHROOMHUNT_API float AgeQuality(float AgeAtPick);

	/** Whether a specimen has worms (fixed by its seed; older specimens more often). */
	SHROOMHUNT_API bool IsWormy(uint32 Seed, float WormyChance, float AgeAtPick);

	/**
	 * Mean price factor over the kilograms of this item when SoldTodayKg of the species were
	 * already sold today: 1 / (1 + sold / demand), integrated over the item.
	 */
	SHROOMHUNT_API float DemandFactor(float SoldTodayKg, float ItemKg, float DemandKgPerDay);

	/** 0.7 at reputation 0, 1 at 50, 1.3 at 100. */
	SHROOMHUNT_API float ReputationFactor(float Reputation);

	/** True for species the buyer must never receive. */
	SHROOMHUNT_API bool IsToxic(const FMushroomSpeciesRow& Species);

	/**
	 * The buyer's look at one specimen. bLabelled: the player knows the species (Identified).
	 * SoldTodayKg: of this species, before this item.
	 */
	SHROOMHUNT_API FSpecimenAppraisal Appraise(const FMushroomSpeciesRow& Species, const FBasketItem& Item, bool bLabelled,
		double NowGameSeconds, float BiologicalDaysPerGameDay, const FMarketSettings& Settings, float SoldTodayKg, float Reputation, bool bBanned);
}
