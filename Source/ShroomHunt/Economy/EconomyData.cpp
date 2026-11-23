#include "Economy/EconomyData.h"

#include "JsonObjectConverter.h"
#include "Knowledge/KnowledgeTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace EconomyData
{
	bool ParseShop(const FString& JsonText, FShopData& Out, TArray<FString>& OutErrors)
	{
		Out = FShopData();
		FText FailReason;
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &Out, 0, 0, false, &FailReason))
		{
			OutErrors.Add(FString::Printf(TEXT("Shop.json: invalid JSON: %s"), *FailReason.ToString()));
			return false;
		}
		return true;
	}

	bool LoadShop(FShopData& Out, TArray<FString>& OutErrors)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Economy"), TEXT("Shop.json")));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cannot read file"), *Path));
			return false;
		}
		return ParseShop(Text, Out, OutErrors);
	}

	bool ValidateShop(const FShopData& Shop, const FKnowledgeSourceData& Sources, TArray<FString>& OutErrors)
	{
		const int32 ErrorsBefore = OutErrors.Num();
		const FMarketSettings& M = Shop.Market;
		if (M.StartMoney < 0.0f || M.StartReputation < 0.0f || M.StartReputation > 100.0f || M.FreshnessHalfLifeBioDays <= 0.0f
			|| M.MinQuality < 0.0f || M.MinQuality >= 1.0f || M.WormyQuality <= 0.0f || M.WormyQuality > 1.0f || M.DemandKgPerDay <= 0.0f)
		{
			OutErrors.Add(TEXT("Shop.json: Market: StartMoney >= 0, StartReputation in [0, 100], FreshnessHalfLifeBioDays > 0, MinQuality in [0, 1), WormyQuality in (0, 1], DemandKgPerDay > 0 required"));
		}
		if (M.FineToxic < 0.0f || M.FineDeadly < M.FineToxic || M.ReputationDeadly < M.ReputationToxic || M.BanGameDaysToxic < 0.0f || M.BanGameDaysDeadly < 0.0f)
		{
			OutErrors.Add(TEXT("Shop.json: Market: penalties must be >= 0 and the deadly ones at least the toxic ones"));
		}
		TSet<FName> Ids;
		for (const FShopItem& Item : Shop.Items)
		{
			const FString Name = Item.Id.ToString();
			if (Item.Id.IsNone() || Ids.Contains(Item.Id))
			{
				OutErrors.Add(FString::Printf(TEXT("Shop.json: missing or duplicate Id '%s'"), *Name));
			}
			Ids.Add(Item.Id);
			if (Item.Title.IsEmpty() || Item.Price < 0.0f)
			{
				OutErrors.Add(FString::Printf(TEXT("Shop.json: %s needs a Title and a Price >= 0"), *Name));
			}
			if (Item.Kind == EShopItemKind::Kit && Item.Uses < 1)
			{
				OutErrors.Add(FString::Printf(TEXT("Shop.json: kit %s needs Uses >= 1"), *Name));
			}
			if (Item.Kind == EShopItemKind::Book)
			{
				const FKnowledgeSource* Source = Sources.Find(Item.Source);
				if (!Source || Source->Kind != EKnowledgeSourceKind::Book)
				{
					OutErrors.Add(FString::Printf(TEXT("Shop.json: book %s: Source '%s' is not a Book in Sources.json"), *Name, *Item.Source.ToString()));
				}
			}
		}
		return OutErrors.Num() == ErrorsBefore;
	}
}
