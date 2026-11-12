#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MycologySettings.generated.h"

class UDataTable;

/** Mycology data settings. Edit in Project Settings > ShroomHunt > Mycology. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Mycology"))
class SHROOMHUNT_API UMycologySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** DataTable generated from Data/Species by Tools/Python/import_data.py. */
	UPROPERTY(Config, EditAnywhere, Category = "Data", meta = (RequiredAssetDataTags = "RowStructure=/Script/ShroomHunt.MushroomSpeciesRow"))
	TSoftObjectPtr<UDataTable> SpeciesTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Data/DT_Species.DT_Species")));

	/**
	 * In development builds, load Data/Species/*.json directly when the DataTable is missing
	 * (so the game works before the first import). Never used in shipping builds.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	bool bFallbackToJsonInDevelopment = true;

	virtual FName GetCategoryName() const override { return TEXT("ShroomHunt"); }
};
