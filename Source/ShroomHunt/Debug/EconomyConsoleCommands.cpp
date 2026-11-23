// Economy debug console commands: sh.Economy.Status / AddMoney / Sell / Buy / Reset.

#include "Core/ShroomLog.h"
#include "Economy/EconomySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace EconomyConsoleCommandsDetail
{
	UEconomySubsystem* FindEconomy(UWorld* World)
	{
		UEconomySubsystem* Economy = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UEconomySubsystem>() : nullptr;
		if (!Economy)
		{
			UE_LOG(LogShroomDebug, Warning, TEXT("Economy commands need a game world (Play)"));
		}
		return Economy;
	}

	FAutoConsoleCommandWithWorld CmdEconomyStatus(
		TEXT("sh.Economy.Status"),
		TEXT("sh.Economy.Status: money, reputation, ban, owned items and the buyer's offer for each specimen of the basket."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			const UEconomySubsystem* Economy = FindEconomy(World);
			if (!Economy)
			{
				return;
			}
			const FEconomyState& State = Economy->GetState();
			UE_LOG(LogShroomDebug, Display, TEXT("Money %.2f EUR, reputation %.0f, banned %.1f more game day(s); earned %.2f, fines %.2f"),
				State.Money, State.Reputation, Economy->GetBanRemainingGameDays(), State.TotalEarned, State.TotalFines);
			for (const TPair<FName, int32>& Item : State.Items)
			{
				UE_LOG(LogShroomDebug, Display, TEXT("  owns %s x%d"), *Item.Key.ToString(), Item.Value);
			}
			for (const FSpecimenAppraisal& Quote : Economy->QuoteBasket())
			{
				UE_LOG(LogShroomDebug, Display, TEXT("  %-26s %4.0f g  fresh %.2f  age %.2f%s  quality %.2f  -> %s %.2f EUR"),
					*Quote.SpeciesId.ToString(), Quote.WeightKg * 1000.0f, Quote.Freshness, Quote.AgeQuality, Quote.bWormy ? TEXT(" wormy") : TEXT(""),
					Quote.Quality, *StaticEnum<ESaleVerdict>()->GetNameStringByValue(static_cast<int64>(Quote.Verdict)), Quote.PriceEuros);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdEconomyAddMoney(
		TEXT("sh.Economy.AddMoney"),
		TEXT("sh.Economy.AddMoney <Euros>: adds (or removes, if negative) money."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UEconomySubsystem* Economy = FindEconomy(World))
			{
				Economy->AddMoney(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 100.0f);
				UE_LOG(LogShroomDebug, Display, TEXT("Money: %.2f EUR"), Economy->GetMoney());
			}
		}));

	FAutoConsoleCommandWithWorld CmdEconomySell(
		TEXT("sh.Economy.Sell"),
		TEXT("sh.Economy.Sell: hands the basket to the buyer, as the Sell button of the counter does."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UEconomySubsystem* Economy = FindEconomy(World))
			{
				const FSaleReport Report = Economy->SellBasket();
				UE_LOG(LogShroomDebug, Display, TEXT("Sold %d (%.2f kg) for %.2f EUR, %d returned; toxic %d deadly %d fine %.0f, reputation %+.0f"),
					Report.SoldCount, Report.SoldKg, Report.Earned, Report.ReturnedCount, Report.bRefusedToxic ? 1 : 0, Report.bDeadly ? 1 : 0,
					Report.Fine, Report.ReputationChange);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdEconomyBuy(
		TEXT("sh.Economy.Buy"),
		TEXT("sh.Economy.Buy <ItemId>: buys a shop item (ids in Data/Economy/Shop.json, e.g. BookBoletes)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UEconomySubsystem* Economy = FindEconomy(World);
			if (!Economy || Args.Num() == 0)
			{
				return;
			}
			FText Message;
			Economy->Buy(FName(*Args[0]), Message);
			UE_LOG(LogShroomDebug, Display, TEXT("%s"), *Message.ToString());
		}));

	FAutoConsoleCommandWithWorld CmdEconomyReset(
		TEXT("sh.Economy.Reset"),
		TEXT("sh.Economy.Reset: back to the start money and reputation, no items (knowledge from books stays)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UEconomySubsystem* Economy = FindEconomy(World))
			{
				Economy->ResetEconomy();
				UE_LOG(LogShroomDebug, Display, TEXT("Economy reset"));
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
