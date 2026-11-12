#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SpeciesDataLibrary.generated.h"

class UDataTable;

/**
 * Editor entry points for the species data pipeline, called from
 * Tools/Python/import_data.py (Python sees it as unreal.SpeciesDataLibrary).
 */
UCLASS()
class SHROOMHUNT_API USpeciesDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Parses every species JSON in Folder and, if there is no error, replaces the rows of
	 * Table (row struct must be MushroomSpeciesRow). The table is marked dirty, not saved.
	 * Returns a report starting with "OK" or "FAILED". Editor only.
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Data")
	static FString ImportSpeciesFolderIntoTable(UDataTable* Table, const FString& Folder);

	/** Parses and validates the folder without touching any asset. Returns the report. */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Data")
	static FString ValidateSpeciesFolder(const FString& Folder);

	/** Absolute path of <Project>/Data/Species. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Data")
	static FString GetDefaultSpeciesFolder();
};
