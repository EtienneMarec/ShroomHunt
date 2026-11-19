#pragma once

#include "CoreMinimal.h"
#include "Knowledge/KnowledgeTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KnowledgeSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnKnowledgeChanged);

/**
 * The player's knowledge for the whole game session: owned sources (poster, pages, later books)
 * and what was seen of each species. Logic in FKnowledgeState; sources from
 * Data/Knowledge/Sources.json. Saved with the game later (M4).
 */
UCLASS()
class SHROOMHUNT_API UKnowledgeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	const FKnowledgeSourceData& GetSources() const { return Sources; }
	const FKnowledgeSource* FindSource(FName SourceId) const { return Sources.Find(SourceId); }
	const FKnowledgeState& GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Knowledge")
	EKnowledgeLevel GetLevel(FName SpeciesId) const { return State.GetLevel(SpeciesId); }

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Knowledge")
	bool HasSource(FName SourceId) const { return State.HasSource(SourceId); }

	/** Reads / takes a source. Returns the species identified by it now (already examined ones). */
	TArray<FName> AcquireSource(FName SourceId);

	/** A specimen examined; true if this identifies its species. */
	bool RecordObservation(FName SpeciesId, double GameSeconds);

	void RecordPick(FName SpeciesId);

	/** Debug: every source owned, every species examined once (sh.Knowledge.UnlockAll). */
	void UnlockAll(const TArray<FName>& SpeciesIds, double GameSeconds);

	/** Debug: back to the start of the game (sh.Knowledge.Reset). */
	void ResetKnowledge();

	/** Broadcast after any change (UI, landmark pages). */
	FOnKnowledgeChanged OnKnowledgeChanged;

private:
	FKnowledgeSourceData Sources;
	FKnowledgeState State;
};
