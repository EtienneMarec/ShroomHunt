#pragma once

#include "CoreMinimal.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SpeciesDatabaseSubsystem.generated.h"

/**
 * Read-only access to all species for the whole game session.
 * Loads the species DataTable (UMycologySettings::SpeciesTable) at startup; in development
 * builds it can also (re)load Data/Species/*.json directly for fast iteration.
 */
UCLASS()
class SHROOMHUNT_API USpeciesDatabaseSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** C++ lookup; nullptr if unknown. */
	const FMushroomSpeciesRow* FindSpecies(FName SpeciesId) const;

	/** Blueprint lookup. */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Mycology", meta = (ExpandBoolAsExecs = "ReturnValue"))
	bool GetSpecies(FName SpeciesId, FMushroomSpeciesRow& OutSpecies) const;

	/** All species ids, sorted alphabetically. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Mycology")
	TArray<FName> GetAllSpeciesIds() const;

	/** Species taught by one field guide. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Mycology")
	TArray<FName> GetSpeciesInGuideGroup(EGuideGroup Group) const;

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Mycology")
	int32 GetSpeciesCount() const { return Species.Num(); }

	/** Where the current data came from ("DataTable /Game/..." or "JSON <folder>"). */
	const FString& GetSourceDescription() const { return SourceDescription; }

	/**
	 * Loads all species without a game instance (editor tools such as the specimen garden):
	 * the DataTable from the settings, else the JSON files in development builds.
	 */
	static bool LoadSpeciesForTools(TMap<FName, FMushroomSpeciesRow>& OutSpecies, FString& OutSource);

	/** Development: reloads Data/Species/*.json, replacing the current data if valid. */
	bool ReloadFromJson(FString& OutReport);

private:
	bool LoadFromDataTable();
	bool LoadFromJsonFolder(FString& OutReport);

	TMap<FName, FMushroomSpeciesRow> Species;
	FString SourceDescription;
};
