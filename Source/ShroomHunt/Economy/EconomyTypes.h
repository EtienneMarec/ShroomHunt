#pragma once

#include "CoreMinimal.h"
#include "EconomyTypes.generated.h"

UENUM(BlueprintType)
enum class EShopItemKind : uint8
{
	/** A field guide: a knowledge source teaching the species of one group (D-030). */
	Book,
	/** A test kit with a number of uses (spore prints, reagents): used in M3 identification. */
	Kit,
};

/** Something sold at the forester's house (Data/Economy/Shop.json). */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FShopItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy")
	EShopItemKind Kind = EShopItemKind::Book;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy")
	FText Description;

	/** Euros. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy")
	float Price = 0.0f;

	/** Books: the knowledge source (Data/Knowledge/Sources.json) it gives. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy")
	FName Source;

	/** Kits: uses per purchase. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy")
	int32 Uses = 0;
};

/** Rules of the buyer at the forester's house (Docs/Progression.md sections 4 and 6, D-013). */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FMarketSettings
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float StartMoney = 20.0f;
	/** Reputation in [0, 100] at the start; it scales prices (0.7x at 0, 1.3x at 100). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float StartReputation = 50.0f;
	/** Freshness halves every this many biological days after picking (game days x4). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float FreshnessHalfLifeBioDays = 3.0f;
	/** Below this quality the buyer refuses a specimen (rotten, slimy). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float MinQuality = 0.25f;
	/** Quality of a wormy specimen relative to a sound one. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float WormyQuality = 0.5f;
	/** Kilograms of one species the buyer takes per day at full price; prices drop beyond. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float DemandKgPerDay = 1.5f;
	/** Reputation gained per kilogram of good mushrooms sold. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float ReputationPerKg = 2.0f;
	/** Reputation lost when the buyer finds an inedible specimen in the basket. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float ReputationInedible = 3.0f;
	/** A toxic specimen in the basket: the whole lot is refused, reputation lost, fine, buyer ban. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float ReputationToxic = 20.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float FineToxic = 30.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float BanGameDaysToxic = 1.0f;
	/** A deadly one: police report, big fine, longer ban. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float ReputationDeadly = 45.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float FineDeadly = 250.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float BanGameDaysDeadly = 3.0f;
};

/** Data/Economy/Shop.json. */
USTRUCT()
struct SHROOMHUNT_API FShopData
{
	GENERATED_BODY()

	UPROPERTY()
	FMarketSettings Market;

	UPROPERTY()
	TArray<FShopItem> Items;

	const FShopItem* Find(FName Id) const
	{
		return Items.FindByPredicate([Id](const FShopItem& Item) { return Item.Id == Id; });
	}
};

/** Why the buyer takes or refuses a specimen. */
UENUM(BlueprintType)
enum class ESaleVerdict : uint8
{
	Sold,
	/** The player does not know the species: the buyer only takes labelled mushrooms. */
	Unlabelled,
	/** Too old or kept too long. */
	Rotten,
	/** Not worth anything (bitter, tough): refused, a little reputation lost. */
	Inedible,
	/** Toxic or deadly: the whole basket is refused (see FMarketSettings). */
	Toxic,
	/** The buyer refuses to deal with the player for now. */
	Banned,
};

/** The buyer's look at one specimen of the basket. */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FSpecimenAppraisal
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") FName SpeciesId;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") int64 Seed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float WeightKg = 0.0f;
	/** 1 when just picked, halves every FreshnessHalfLifeBioDays. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float Freshness = 1.0f;
	/** 1 for young and mature specimens, down to 0.3 for an old one. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float AgeQuality = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") bool bWormy = false;
	/** Overall quality in [0, 1]. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float Quality = 1.0f;
	/** Euros offered (0 unless sold). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float PriceEuros = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") ESaleVerdict Verdict = ESaleVerdict::Sold;
};

/** Result of handing the basket to the buyer. */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FSaleReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") int32 SoldCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float SoldKg = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float Earned = 0.0f;
	/** A toxic specimen was found: nothing sold, penalties applied. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") bool bRefusedToxic = false;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") bool bDeadly = false;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float Fine = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float ReputationChange = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") bool bBanned = false;
	/** Specimens the buyer gave back (unlabelled, rotten, inedible). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") int32 ReturnedCount = 0;
};

/**
 * The player's money and standing with the buyer. Pure data + logic (unit-tested), held by
 * UEconomySubsystem and saved as is (M4).
 */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FEconomyState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float Money = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float Reputation = 50.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") double BannedUntilGameSeconds = 0.0;
	/** Game day of SoldTodayKg. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") int32 SalesDay = -1;
	/** Kilograms sold today per species (demand). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") TMap<FName, float> SoldTodayKg;
	/** Owned shop items: books (1) and remaining kit uses. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") TMap<FName, int32> Items;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float TotalEarned = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Economy") float TotalFines = 0.0f;

	void Reset(const FMarketSettings& Settings);
	bool IsBanned(double NowGameSeconds) const { return NowGameSeconds < BannedUntilGameSeconds; }
	float GetSoldTodayKg(FName SpeciesId, int32 Day) const;
	void RecordSale(FName SpeciesId, float Kg, float Euros, int32 Day, const FMarketSettings& Settings);
	/** Toxic lot: fine (money may go negative: a debt), reputation, ban. Fills the report. */
	void ApplyToxicLot(bool bDeadly, double NowGameSeconds, const FMarketSettings& Settings, FSaleReport& Report);
	void ApplyInedible(int32 Count, const FMarketSettings& Settings, FSaleReport& Report);
	int32 GetItemCount(FName ItemId) const { return Items.FindRef(ItemId); }
	/** False if too expensive, or a book already owned. */
	bool CanBuy(const FShopItem& Item) const;
	bool Buy(const FShopItem& Item);
	/** Uses one charge of a kit; false if none left. */
	bool UseKit(FName ItemId);
};
