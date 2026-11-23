#pragma once

#include "CoreMinimal.h"
#include "Economy/EconomyTypes.h"

struct FKnowledgeSourceData;

/** Data/Economy/Shop.json: parsing and validation (shared by the game, the tests and the debug commands). */
namespace EconomyData
{
	SHROOMHUNT_API bool ParseShop(const FString& JsonText, FShopData& Out, TArray<FString>& OutErrors);

	/** Loads <Project>/Data/Economy/Shop.json. */
	SHROOMHUNT_API bool LoadShop(FShopData& Out, TArray<FString>& OutErrors);

	/** Unique ids, prices >= 0, kits with uses, books pointing at a Book knowledge source. */
	SHROOMHUNT_API bool ValidateShop(const FShopData& Shop, const FKnowledgeSourceData& Sources, TArray<FString>& OutErrors);
}
