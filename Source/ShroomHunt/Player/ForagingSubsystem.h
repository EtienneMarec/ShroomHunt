#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ForagingSubsystem.generated.h"

/** A mushroom in the basket. */
USTRUCT(BlueprintType)
struct FBasketItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Foraging")
	FName SpeciesId;

	/** Fruit body seed (identifies the individual). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Foraging")
	int64 Seed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Foraging")
	int64 PatchId = 0;

	/** Age in [0, 1] when picked (freshness, quality later). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Foraging")
	float AgeAtPick = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Foraging")
	double PickGameSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Foraging")
	float WeightGrams = 0.0f;
};

namespace ForagingMath
{
	/** Exhaustion of a patch at Now from its pick times: sum of PerPick x exp(-(Now - t) / tau), capped at 1. */
	SHROOMHUNT_API float Exhaustion(TConstArrayView<double> PickGameSeconds, double NowGameSeconds, float PerPick, float RecoveryGameDays);
}

/**
 * What the player has taken from the forests: the basket, the picked individuals (so they do
 * not grow back, the spawn model being stateless) and the picks per mycelium patch (exhaustion).
 * Game instance subsystem: survives map changes; saved with the game later (M4).
 */
UCLASS()
class SHROOMHUNT_API UForagingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	void RecordPick(const FBasketItem& Item);

	bool IsPicked(uint32 Seed) const { return PickedSeeds.Contains(Seed); }

	/** Exhaustion in [0, 1] of a patch at a game time (only picks before that time count). */
	float GetExhaustion(uint32 PatchId, double GameSeconds) const;

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Foraging")
	const TArray<FBasketItem>& GetBasket() const { return Basket; }

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Foraging")
	float GetBasketWeightGrams() const;

	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Foraging")
	void EmptyBasket() { Basket.Reset(); }

	/** Takes specimens out of the basket (sold, discarded), by fruit seed. Returns how many. */
	int32 RemoveFromBasket(const TSet<int64>& Seeds);

private:
	UPROPERTY()
	TArray<FBasketItem> Basket;

	TSet<uint32> PickedSeeds;
	TMap<uint32, TArray<double>> PatchPicks;
};
