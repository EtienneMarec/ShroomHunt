#include "Player/ForagingSubsystem.h"

#include "Core/ShroomLog.h"
#include "Player/ForagingSettings.h"
#include "Time/TimeTypes.h"

namespace ForagingMath
{
	float Exhaustion(TConstArrayView<double> PickGameSeconds, double NowGameSeconds, float PerPick, float RecoveryGameDays)
	{
		const double Tau = FMath::Max(RecoveryGameDays, 0.01f) * GameSecondsPerDay;
		double Sum = 0.0;
		for (const double Pick : PickGameSeconds)
		{
			if (Pick <= NowGameSeconds)
			{
				Sum += PerPick * FMath::Exp(-(NowGameSeconds - Pick) / Tau);
			}
		}
		return static_cast<float>(FMath::Min(Sum, 1.0));
	}
}

void UForagingSubsystem::RecordPick(const FBasketItem& Item)
{
	Basket.Add(Item);
	PickedSeeds.Add(static_cast<uint32>(Item.Seed));
	PatchPicks.FindOrAdd(static_cast<uint32>(Item.PatchId)).Add(Item.PickGameSeconds);
	UE_LOG(LogShroomSpawn, Log, TEXT("Picked %s (%.0f g, age %.2f); basket: %d"), *Item.SpeciesId.ToString(), Item.WeightGrams, Item.AgeAtPick, Basket.Num());
}

float UForagingSubsystem::GetExhaustion(uint32 PatchId, double GameSeconds) const
{
	const TArray<double>* Picks = PatchPicks.Find(PatchId);
	if (!Picks)
	{
		return 0.0f;
	}
	const UForagingSettings* Settings = GetDefault<UForagingSettings>();
	return ForagingMath::Exhaustion(*Picks, GameSeconds, Settings->ExhaustionPerPick, Settings->ExhaustionRecoveryGameDays);
}

float UForagingSubsystem::GetBasketWeightGrams() const
{
	float Total = 0.0f;
	for (const FBasketItem& Item : Basket)
	{
		Total += Item.WeightGrams;
	}
	return Total;
}

int32 UForagingSubsystem::RemoveFromBasket(const TSet<int64>& Seeds)
{
	return Basket.RemoveAll([&Seeds](const FBasketItem& Item) { return Seeds.Contains(Item.Seed); });
}
