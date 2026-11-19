#include "Knowledge/KnowledgeSubsystem.h"

#include "Core/ShroomLog.h"
#include "Knowledge/KnowledgeText.h"

void UKnowledgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TArray<FString> Errors;
	if (!KnowledgeText::LoadSources(Sources, Errors))
	{
		UE_LOG(LogShroomKnowledge, Warning, TEXT("Knowledge: %s"), *FString::Join(Errors, TEXT("; ")));
	}
	State.SetSources(&Sources);
	UE_LOG(LogShroomKnowledge, Log, TEXT("Knowledge: %d sources loaded"), Sources.Sources.Num());
}

TArray<FName> UKnowledgeSubsystem::AcquireSource(FName SourceId)
{
	TArray<FName> Identified;
	if (State.AcquireSource(SourceId, Identified))
	{
		UE_LOG(LogShroomKnowledge, Log, TEXT("Knowledge: source %s acquired (%d species identified now)"), *SourceId.ToString(), Identified.Num());
		OnKnowledgeChanged.Broadcast();
	}
	return Identified;
}

bool UKnowledgeSubsystem::RecordObservation(FName SpeciesId, double GameSeconds)
{
	const bool bIdentified = State.RecordObservation(SpeciesId, GameSeconds);
	if (bIdentified)
	{
		UE_LOG(LogShroomKnowledge, Log, TEXT("Knowledge: %s identified"), *SpeciesId.ToString());
	}
	OnKnowledgeChanged.Broadcast();
	return bIdentified;
}

void UKnowledgeSubsystem::RecordPick(FName SpeciesId)
{
	State.RecordPick(SpeciesId);
	OnKnowledgeChanged.Broadcast();
}

void UKnowledgeSubsystem::UnlockAll(const TArray<FName>& SpeciesIds, double GameSeconds)
{
	TArray<FName> Ignored;
	for (const FKnowledgeSource& Source : Sources.Sources)
	{
		State.AcquireSource(Source.Id, Ignored);
	}
	for (const FName Id : SpeciesIds)
	{
		if (State.GetLevel(Id) == EKnowledgeLevel::Unknown)
		{
			State.RecordObservation(Id, GameSeconds);
		}
	}
	OnKnowledgeChanged.Broadcast();
}

void UKnowledgeSubsystem::ResetKnowledge()
{
	State.Reset();
	OnKnowledgeChanged.Broadcast();
}
