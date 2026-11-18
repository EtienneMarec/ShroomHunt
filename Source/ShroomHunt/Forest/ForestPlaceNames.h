#pragma once

#include "CoreMinimal.h"
#include "ForestPlaceNames.generated.h"

class FForestField;

USTRUCT()
struct FPlaceNamePrefixes
{
	GENERATED_BODY()

	UPROPERTY() FString Crossroads;
	/** Crossroads where five or more paths meet. */
	UPROPERTY() FString Star;
	UPROPERTY() FString Hub;
	UPROPERTY() FString Junction;
	UPROPERTY() FString Entrance;
	/** Full name of the base (the forester's house). */
	UPROPERTY() FString Base;
};

USTRUCT()
struct FPlaceNameList
{
	GENERATED_BODY()

	/** Generic, Pond, Open (clear-cut / regeneration) or a tree genus (Fagus, Quercus...). */
	UPROPERTY() FString Context;
	UPROPERTY() TArray<FString> Names;
};

/** Source of truth: Data/Names/PlaceNames.json. */
USTRUCT()
struct SHROOMHUNT_API FPlaceNameData
{
	GENERATED_BODY()

	UPROPERTY() FPlaceNamePrefixes Prefixes;
	UPROPERTY() TArray<FPlaceNameList> Lists;

	const TArray<FString>* Find(const FString& Context) const;
};

/**
 * Names of the forest's places (crossroads, hubs, junctions, entrances, base), chosen from
 * their surroundings: a crossroads by a pond becomes "Carrefour de la Mare", in an oak stand
 * "Carrefour du Gros Chêne". Deterministic and unique within a forest.
 */
namespace ForestPlaceNames
{
	SHROOMHUNT_API bool Parse(const FString& JsonText, FPlaceNameData& Out, TArray<FString>& OutErrors);
	SHROOMHUNT_API bool Load(FPlaceNameData& Out, TArray<FString>& OutErrors);

	/** One name per path node (index = node index); empty for nodes that are not named places. */
	SHROOMHUNT_API TArray<FString> NameNodes(const FForestField& Field, const FPlaceNameData& Data);
}
