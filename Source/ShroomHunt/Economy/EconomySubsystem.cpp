#include "Economy/EconomySubsystem.h"

#include "Core/ShroomLog.h"
#include "Economy/EconomyData.h"
#include "Economy/MarketModel.h"
#include "Knowledge/KnowledgeSubsystem.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Player/ForagingSubsystem.h"
#include "Time/CalendarSubsystem.h"

#define LOCTEXT_NAMESPACE "ShroomHunt.Economy"

void UEconomySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCalendarSubsystem>();
	Collection.InitializeDependency<USpeciesDatabaseSubsystem>();
	Collection.InitializeDependency<UKnowledgeSubsystem>();
	Collection.InitializeDependency<UForagingSubsystem>();

	TArray<FString> Errors;
	if (!EconomyData::LoadShop(Shop, Errors))
	{
		UE_LOG(LogShroomEconomy, Warning, TEXT("Economy: %s"), *FString::Join(Errors, TEXT("; ")));
	}
	State.Reset(Shop.Market);
	UE_LOG(LogShroomEconomy, Log, TEXT("Economy: %d shop items, %.0f euros to start"), Shop.Items.Num(), State.Money);
}

void UEconomySubsystem::SetState(const FEconomyState& NewState)
{
	State = NewState;
	OnEconomyChanged.Broadcast();
}

double UEconomySubsystem::GetNow() const
{
	const UCalendarSubsystem* Calendar = GetGameInstance()->GetSubsystem<UCalendarSubsystem>();
	return Calendar ? Calendar->GetTotalGameSeconds() : 0.0;
}

float UEconomySubsystem::GetBiologicalDaysPerGameDay() const
{
	const UCalendarSubsystem* Calendar = GetGameInstance()->GetSubsystem<UCalendarSubsystem>();
	return Calendar ? Calendar->GetCalendarConfig().BiologicalDaysPerGameDay : 4.0f;
}

bool UEconomySubsystem::IsBanned() const
{
	return State.IsBanned(GetNow());
}

float UEconomySubsystem::GetBanRemainingGameDays() const
{
	return static_cast<float>(FMath::Max(0.0, State.BannedUntilGameSeconds - GetNow()) / GameSecondsPerDay);
}

TArray<FSpecimenAppraisal> UEconomySubsystem::QuoteBasket() const
{
	TArray<FSpecimenAppraisal> Out;
	const UGameInstance* GameInstance = GetGameInstance();
	const UForagingSubsystem* Foraging = GameInstance->GetSubsystem<UForagingSubsystem>();
	const USpeciesDatabaseSubsystem* Database = GameInstance->GetSubsystem<USpeciesDatabaseSubsystem>();
	const UKnowledgeSubsystem* Knowledge = GameInstance->GetSubsystem<UKnowledgeSubsystem>();
	if (!Foraging || !Database || !Knowledge)
	{
		return Out;
	}
	const double Now = GetNow();
	const int32 Day = FMath::FloorToInt(Now / GameSecondsPerDay);
	const bool bBanned = State.IsBanned(Now);
	const float BioDays = GetBiologicalDaysPerGameDay();
	// Demand drops as the basket fills the buyer's needs, item after item.
	TMap<FName, float> Sold;
	for (const FBasketItem& Item : Foraging->GetBasket())
	{
		const FMushroomSpeciesRow* Row = Database->FindSpecies(Item.SpeciesId);
		if (!Row)
		{
			continue;
		}
		float& SoldKg = Sold.FindOrAdd(Item.SpeciesId, State.GetSoldTodayKg(Item.SpeciesId, Day));
		const bool bLabelled = Knowledge->GetLevel(Item.SpeciesId) == EKnowledgeLevel::Identified;
		const FSpecimenAppraisal Appraisal = MarketModel::Appraise(*Row, Item, bLabelled, Now, BioDays, Shop.Market, SoldKg, State.Reputation, bBanned);
		if (Appraisal.Verdict == ESaleVerdict::Sold)
		{
			SoldKg += Appraisal.WeightKg;
		}
		Out.Add(Appraisal);
	}
	return Out;
}

FSaleReport UEconomySubsystem::SellBasket()
{
	FSaleReport Report;
	UForagingSubsystem* Foraging = GetGameInstance()->GetSubsystem<UForagingSubsystem>();
	const USpeciesDatabaseSubsystem* Database = GetGameInstance()->GetSubsystem<USpeciesDatabaseSubsystem>();
	if (!Foraging || !Database)
	{
		return Report;
	}
	const double Now = GetNow();
	const int32 Day = FMath::FloorToInt(Now / GameSecondsPerDay);
	const TArray<FSpecimenAppraisal> Quotes = QuoteBasket();

	TSet<int64> Toxic;
	bool bDeadly = false;
	for (const FSpecimenAppraisal& Quote : Quotes)
	{
		if (Quote.Verdict == ESaleVerdict::Toxic)
		{
			Toxic.Add(Quote.Seed);
			const FMushroomSpeciesRow* Row = Database->FindSpecies(Quote.SpeciesId);
			bDeadly |= Row && Row->Edibility == EEdibility::Deadly;
		}
	}
	if (Toxic.Num() > 0)
	{
		// The buyer finds it, confiscates it and refuses the whole lot.
		State.ApplyToxicLot(bDeadly, Now, Shop.Market, Report);
		Foraging->RemoveFromBasket(Toxic);
		Report.ReturnedCount = Quotes.Num() - Toxic.Num();
		UE_LOG(LogShroomEconomy, Log, TEXT("Economy: toxic basket (%d specimens, deadly: %d): fine %.0f, reputation %.0f"),
			Toxic.Num(), bDeadly ? 1 : 0, Report.Fine, State.Reputation);
	}
	else
	{
		const float ReputationStart = State.Reputation;
		TSet<int64> SoldSeeds;
		int32 Inedible = 0;
		for (const FSpecimenAppraisal& Quote : Quotes)
		{
			if (Quote.Verdict == ESaleVerdict::Sold)
			{
				State.RecordSale(Quote.SpeciesId, Quote.WeightKg, Quote.PriceEuros, Day, Shop.Market);
				SoldSeeds.Add(Quote.Seed);
				++Report.SoldCount;
				Report.SoldKg += Quote.WeightKg;
				Report.Earned += Quote.PriceEuros;
			}
			else
			{
				Inedible += Quote.Verdict == ESaleVerdict::Inedible ? 1 : 0;
				++Report.ReturnedCount;
			}
		}
		State.ApplyInedible(Inedible, Shop.Market, Report);
		Report.ReputationChange = State.Reputation - ReputationStart;
		Foraging->RemoveFromBasket(SoldSeeds);
		UE_LOG(LogShroomEconomy, Log, TEXT("Economy: sold %d specimens (%.2f kg) for %.2f euros; %d returned"),
			Report.SoldCount, Report.SoldKg, Report.Earned, Report.ReturnedCount);
	}
	OnEconomyChanged.Broadcast();
	return Report;
}

int32 UEconomySubsystem::DiscardUnsellable()
{
	UForagingSubsystem* Foraging = GetGameInstance()->GetSubsystem<UForagingSubsystem>();
	if (!Foraging)
	{
		return 0;
	}
	TSet<int64> Discard;
	for (const FSpecimenAppraisal& Quote : QuoteBasket())
	{
		// While banned everything is refused: keep the good ones for later.
		if (Quote.Verdict != ESaleVerdict::Sold && Quote.Verdict != ESaleVerdict::Banned)
		{
			Discard.Add(Quote.Seed);
		}
	}
	const int32 Count = Foraging->RemoveFromBasket(Discard);
	OnEconomyChanged.Broadcast();
	return Count;
}

bool UEconomySubsystem::Buy(FName ItemId, FText& OutMessage)
{
	const FShopItem* Item = Shop.Find(ItemId);
	if (!Item)
	{
		OutMessage = LOCTEXT("BuyUnknown", "This item is not for sale.");
		return false;
	}
	if (Item->Kind == EShopItemKind::Book && State.GetItemCount(ItemId) > 0)
	{
		OutMessage = FText::Format(LOCTEXT("BuyOwned", "You already own \"{0}\"."), Item->Title);
		return false;
	}
	if (!State.Buy(*Item))
	{
		OutMessage = FText::Format(LOCTEXT("BuyTooExpensive", "\"{0}\" costs {1} euros: not enough money."), Item->Title, FText::AsNumber(Item->Price));
		return false;
	}
	OutMessage = FText::Format(LOCTEXT("Bought", "Bought \"{0}\"."), Item->Title);
	if (Item->Kind == EShopItemKind::Book)
	{
		if (UKnowledgeSubsystem* Knowledge = GetGameInstance()->GetSubsystem<UKnowledgeSubsystem>())
		{
			const TArray<FName> Identified = Knowledge->AcquireSource(Item->Source);
			if (Identified.Num() > 0)
			{
				OutMessage = FText::Format(LOCTEXT("BoughtBookIdentified", "Bought \"{0}\": {1} species you had examined are now identified."),
					Item->Title, FText::AsNumber(Identified.Num()));
			}
		}
	}
	UE_LOG(LogShroomEconomy, Log, TEXT("Economy: bought %s for %.2f euros (%.2f left)"), *ItemId.ToString(), Item->Price, State.Money);
	OnEconomyChanged.Broadcast();
	return true;
}

void UEconomySubsystem::AddMoney(float Euros)
{
	State.Money += Euros;
	OnEconomyChanged.Broadcast();
}

void UEconomySubsystem::ResetEconomy()
{
	State.Reset(Shop.Market);
	OnEconomyChanged.Broadcast();
}

#undef LOCTEXT_NAMESPACE
