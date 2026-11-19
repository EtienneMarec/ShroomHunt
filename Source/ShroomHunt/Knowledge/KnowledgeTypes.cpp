#include "Knowledge/KnowledgeTypes.h"

bool FKnowledgeState::IsTaught(FName SpeciesId) const
{
	return SourcesTeaching(SpeciesId).Num() > 0;
}

TArray<const FKnowledgeSource*> FKnowledgeState::SourcesTeaching(FName SpeciesId) const
{
	TArray<const FKnowledgeSource*> Result;
	if (!Sources)
	{
		return Result;
	}
	for (const FKnowledgeSource& Source : Sources->Sources)
	{
		if (OwnedSources.Contains(Source.Id) && Source.Species.Contains(SpeciesId))
		{
			Result.Add(&Source);
		}
	}
	return Result;
}

EKnowledgeLevel FKnowledgeState::GetLevel(FName SpeciesId) const
{
	const FSpeciesKnowledge* Seen = Species.Find(SpeciesId);
	if (!Seen || Seen->Observations == 0)
	{
		return EKnowledgeLevel::Unknown;
	}
	return IsTaught(SpeciesId) ? EKnowledgeLevel::Identified : EKnowledgeLevel::Observed;
}

bool FKnowledgeState::AcquireSource(FName SourceId, TArray<FName>& OutNewlyIdentified)
{
	OutNewlyIdentified.Reset();
	const FKnowledgeSource* Source = Sources ? Sources->Find(SourceId) : nullptr;
	if (!Source || OwnedSources.Contains(SourceId))
	{
		return false;
	}
	TArray<EKnowledgeLevel> Before;
	for (const FName Id : Source->Species)
	{
		Before.Add(GetLevel(Id));
	}
	OwnedSources.Add(SourceId);
	for (int32 Index = 0; Index < Source->Species.Num(); ++Index)
	{
		if (Before[Index] != EKnowledgeLevel::Identified && GetLevel(Source->Species[Index]) == EKnowledgeLevel::Identified)
		{
			OutNewlyIdentified.Add(Source->Species[Index]);
		}
	}
	return true;
}

bool FKnowledgeState::RecordObservation(FName SpeciesId, double GameSeconds)
{
	const EKnowledgeLevel Before = GetLevel(SpeciesId);
	FSpeciesKnowledge& Seen = Species.FindOrAdd(SpeciesId);
	if (Seen.Observations == 0)
	{
		Seen.FirstSeenGameSeconds = GameSeconds;
	}
	++Seen.Observations;
	Seen.LastSeenGameSeconds = GameSeconds;
	return Before != EKnowledgeLevel::Identified && GetLevel(SpeciesId) == EKnowledgeLevel::Identified;
}

void FKnowledgeState::Reset()
{
	OwnedSources.Reset();
	Species.Reset();
}
