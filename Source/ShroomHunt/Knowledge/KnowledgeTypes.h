#pragma once

#include "CoreMinimal.h"
#include "KnowledgeTypes.generated.h"

/** How well the player knows a species (Docs/Progression.md section 2). */
UENUM(BlueprintType)
enum class EKnowledgeLevel : uint8
{
	/** Never examined: specimens show only their observable criteria. */
	Unknown,
	/** Examined at least once, but no source names it yet. */
	Observed,
	/** A source teaches it and a specimen was examined: real name on specimens. */
	Identified,
};

UENUM(BlueprintType)
enum class EKnowledgeSourceKind : uint8
{
	/** Stays where it hangs (read it as often as you like). */
	Poster,
	/** Taken when read (it leaves the world). */
	Page,
	/** A field guide bought at the forester's house (no landmark). */
	Book,
};

/** Something that teaches species: a poster, a guide page, later books. Data/Knowledge/Sources.json. */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FKnowledgeSource
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	EKnowledgeSourceKind Kind = EKnowledgeSourceKind::Poster;

	/** Landmark kind it hangs on (ForesterHouse, HuntingHut...), see ForestLandmarks::KindName; empty for books. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	FString Landmark;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	FText Intro;

	/** Species ids it teaches. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	TArray<FName> Species;
};

USTRUCT()
struct SHROOMHUNT_API FKnowledgeSourceData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FKnowledgeSource> Sources;

	const FKnowledgeSource* Find(FName Id) const
	{
		return Sources.FindByPredicate([Id](const FKnowledgeSource& S) { return S.Id == Id; });
	}
};

/** What the player has seen of one species. */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FSpeciesKnowledge
{
	GENERATED_BODY()

	/** Specimens examined. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	int32 Observations = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	int32 Picks = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	double FirstSeenGameSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Knowledge")
	double LastSeenGameSeconds = 0.0;
};

/**
 * The player's knowledge, pure logic (the subsystem wraps it): owned sources and what was seen
 * of each species. Identified = taught by an owned source AND at least one specimen examined
 * (the comparison with the source; dichotomous keys replace it with the books, M3).
 */
class SHROOMHUNT_API FKnowledgeState
{
public:
	explicit FKnowledgeState(const FKnowledgeSourceData* InSources = nullptr) : Sources(InSources) {}

	void SetSources(const FKnowledgeSourceData* InSources) { Sources = InSources; }

	EKnowledgeLevel GetLevel(FName SpeciesId) const;
	bool HasSource(FName SourceId) const { return OwnedSources.Contains(SourceId); }
	bool IsTaught(FName SpeciesId) const;
	const FSpeciesKnowledge* Find(FName SpeciesId) const { return Species.Find(SpeciesId); }
	const TMap<FName, FSpeciesKnowledge>& GetAll() const { return Species; }
	const TSet<FName>& GetOwnedSources() const { return OwnedSources; }

	/** Owned sources teaching a species (titles for the encyclopedia). */
	TArray<const FKnowledgeSource*> SourcesTeaching(FName SpeciesId) const;

	/** Takes a source; returns the species that become Identified now. False if already owned or unknown. */
	bool AcquireSource(FName SourceId, TArray<FName>& OutNewlyIdentified);

	/** One specimen examined; returns true if the species becomes Identified now. */
	bool RecordObservation(FName SpeciesId, double GameSeconds);

	void RecordPick(FName SpeciesId) { ++Species.FindOrAdd(SpeciesId).Picks; }

	void Reset();

private:
	const FKnowledgeSourceData* Sources = nullptr;
	TSet<FName> OwnedSources;
	TMap<FName, FSpeciesKnowledge> Species;
};
