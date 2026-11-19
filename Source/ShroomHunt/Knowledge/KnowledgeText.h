#pragma once

#include "CoreMinimal.h"
#include "Knowledge/KnowledgeTypes.h"
#include "Mycology/MushroomSpeciesTypes.h"

struct FMushroomMorphology;

/** Data/Knowledge/Sources.json loading, and the player-facing texts that depend on knowledge. */
namespace KnowledgeText
{
	SHROOMHUNT_API bool ParseSources(const FString& JsonText, FKnowledgeSourceData& Out, TArray<FString>& OutErrors);
	SHROOMHUNT_API bool LoadSources(FKnowledgeSourceData& Out, TArray<FString>& OutErrors);

	/** Every source names existing species and a landmark kind; ids are unique. */
	SHROOMHUNT_API bool ValidateSources(const FKnowledgeSourceData& Data, const TMap<FName, FMushroomSpeciesRow>& Species, TArray<FString>& OutErrors);

	SHROOMHUNT_API FText EdibilityName(EEdibility Edibility);

	/** "Omphalotus olearius" from the id "OmphalotusOlearius" (lookalikes not in the game yet). */
	SHROOMHUNT_API FString SpeciesIdToScientific(FName SpeciesId);

	/** What a specimen is called: its common name once identified, else its observable look. */
	SHROOMHUNT_API FText SpecimenLabel(EKnowledgeLevel Level, const FMushroomSpeciesRow& Species, const FMushroomMorphology& Morphology);

	/** Lines a source shows when read: per species, its name, edibility and key features. */
	SHROOMHUNT_API TArray<FText> SourceLines(const FKnowledgeSource& Source, TFunctionRef<const FMushroomSpeciesRow*(FName)> FindSpecies);

	/** Encyclopedia page of a species at a knowledge level. */
	struct FEntry
	{
		FText Title;
		TArray<FText> Lines;
	};
	SHROOMHUNT_API FEntry EncyclopediaEntry(FName SpeciesId, const FMushroomSpeciesRow& Species, const FKnowledgeState& State, int32 UnknownNumber);
}
