#pragma once

#include "CoreMinimal.h"
#include "Economy/EconomyTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EconomySubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnEconomyChanged);

/**
 * Money, reputation and the shop (M3). The buyer at the forester's house values the basket
 * through MarketModel; the shop sells field guides (knowledge sources) and test kits from
 * Data/Economy/Shop.json. Logic in FEconomyState / MarketModel; saved with the game (M4).
 */
UCLASS()
class SHROOMHUNT_API UEconomySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const FShopData& GetShop() const { return Shop; }
	const FMarketSettings& GetMarket() const { return Shop.Market; }
	const FEconomyState& GetState() const { return State; }

	/** Replaces the whole state (save game). */
	void SetState(const FEconomyState& NewState);

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Economy")
	float GetMoney() const { return State.Money; }

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Economy")
	float GetReputation() const { return State.Reputation; }

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Economy")
	bool IsBanned() const;

	/** Game days until the buyer deals with the player again (0 if not banned). */
	float GetBanRemainingGameDays() const;

	/** The buyer's look at each specimen of the basket, in basket order (nothing changes). */
	TArray<FSpecimenAppraisal> QuoteBasket() const;

	/**
	 * Hands the basket to the buyer: if a toxic specimen is in it, nothing is sold, toxic
	 * specimens are confiscated and the penalties apply (D-013 option B); otherwise every
	 * specimen with the verdict Sold is paid and leaves the basket, the others come back.
	 */
	FSaleReport SellBasket();

	/** Throws away every specimen the buyer would not take now (unknown, rotten, inedible, toxic). */
	int32 DiscardUnsellable();

	/** Buys a shop item (a book gives its knowledge source). OutMessage says what happened. */
	bool Buy(FName ItemId, FText& OutMessage);

	/** Debug: adds (or removes) money. */
	void AddMoney(float Euros);

	/** Debug: back to the start money and reputation, no items. */
	void ResetEconomy();

	FOnEconomyChanged OnEconomyChanged;

private:
	double GetNow() const;
	float GetBiologicalDaysPerGameDay() const;

	FShopData Shop;
	FEconomyState State;
};
