#include "Knowledge/KnowledgeText.h"

#include "Forest/ForestLandmarks.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Mushroom/MushroomMorphology.h"
#include "Mycology/ObservableCriteria.h"
#include "Time/TimeTypes.h"

#define LOCTEXT_NAMESPACE "ShroomHunt.Knowledge"

namespace KnowledgeText
{
	bool ParseSources(const FString& JsonText, FKnowledgeSourceData& Out, TArray<FString>& OutErrors)
	{
		Out = FKnowledgeSourceData();
		FText FailReason;
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &Out, 0, 0, false, &FailReason))
		{
			OutErrors.Add(FString::Printf(TEXT("Sources.json: invalid JSON: %s"), *FailReason.ToString()));
			return false;
		}
		return true;
	}

	bool LoadSources(FKnowledgeSourceData& Out, TArray<FString>& OutErrors)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Knowledge"), TEXT("Sources.json")));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cannot read file"), *Path));
			return false;
		}
		return ParseSources(Text, Out, OutErrors);
	}

	bool ValidateSources(const FKnowledgeSourceData& Data, const TMap<FName, FMushroomSpeciesRow>& Species, TArray<FString>& OutErrors)
	{
		const int32 ErrorsBefore = OutErrors.Num();
		TSet<FName> Ids;
		for (const FKnowledgeSource& Source : Data.Sources)
		{
			const FString Name = Source.Id.ToString();
			if (Source.Id.IsNone() || Ids.Contains(Source.Id))
			{
				OutErrors.Add(FString::Printf(TEXT("Sources.json: missing or duplicate Id '%s'"), *Name));
			}
			Ids.Add(Source.Id);
			if (Source.Title.IsEmpty() || Source.Species.Num() == 0)
			{
				OutErrors.Add(FString::Printf(TEXT("Sources.json: %s needs a Title and Species"), *Name));
			}
			bool bLandmarkFound = false;
			for (int32 Kind = 0; Kind <= static_cast<int32>(EForestLandmarkKind::LogPile); ++Kind)
			{
				bLandmarkFound |= Source.Landmark == ForestLandmarks::KindName(static_cast<EForestLandmarkKind>(Kind));
			}
			if (!bLandmarkFound)
			{
				OutErrors.Add(FString::Printf(TEXT("Sources.json: %s: unknown Landmark '%s'"), *Name, *Source.Landmark));
			}
			for (const FName Id : Source.Species)
			{
				if (!Species.Contains(Id))
				{
					OutErrors.Add(FString::Printf(TEXT("Sources.json: %s: unknown species '%s'"), *Name, *Id.ToString()));
				}
			}
		}
		return OutErrors.Num() == ErrorsBefore;
	}

	FText EdibilityName(EEdibility Edibility)
	{
		switch (Edibility)
		{
		case EEdibility::Excellent:      return LOCTEXT("EdibleExcellent", "edible, excellent");
		case EEdibility::Edible:         return LOCTEXT("Edible", "edible");
		case EEdibility::EdibleWithCare: return LOCTEXT("EdibleCare", "edible with care (well cooked)");
		case EEdibility::Inedible:       return LOCTEXT("Inedible", "inedible");
		case EEdibility::Toxic:          return LOCTEXT("Toxic", "TOXIC");
		case EEdibility::Deadly:         return LOCTEXT("Deadly", "DEADLY");
		}
		return FText::GetEmpty();
	}

	FString SpeciesIdToScientific(FName SpeciesId)
	{
		const FString Id = SpeciesId.ToString();
		FString Out;
		for (int32 Index = 0; Index < Id.Len(); ++Index)
		{
			const TCHAR C = Id[Index];
			if (Index > 0 && FChar::IsUpper(C))
			{
				Out += TEXT(' ');
				Out += FChar::ToLower(C);
			}
			else
			{
				Out += C;
			}
		}
		return Out;
	}

	FText SpecimenLabel(EKnowledgeLevel Level, const FMushroomSpeciesRow& Species, const FMushroomMorphology& Morphology)
	{
		if (Level == EKnowledgeLevel::Identified)
		{
			return Species.CommonName;
		}
		return ObservableCriteria::VagueLabel(Morphology);
	}

	TArray<FText> SourceLines(const FKnowledgeSource& Source, TFunctionRef<const FMushroomSpeciesRow*(FName)> FindSpecies)
	{
		TArray<FText> Lines;
		if (!Source.Intro.IsEmpty())
		{
			Lines.Add(Source.Intro);
			Lines.Add(FText::GetEmpty());
		}
		for (const FName Id : Source.Species)
		{
			const FMushroomSpeciesRow* Row = FindSpecies(Id);
			if (!Row)
			{
				continue;
			}
			Lines.Add(FText::Format(LOCTEXT("SourceSpecies", "{0} ({1}): {2}"),
				Row->CommonName, FText::FromString(Row->ScientificName), EdibilityName(Row->Edibility)));
			Lines.Add(Row->Description);
			Lines.Add(FText::GetEmpty());
		}
		return Lines;
	}

	FEntry EncyclopediaEntry(FName SpeciesId, const FMushroomSpeciesRow& Species, const FKnowledgeState& State, int32 UnknownNumber)
	{
		FEntry Entry;
		const EKnowledgeLevel Level = State.GetLevel(SpeciesId);
		const FSpeciesKnowledge* Seen = State.Find(SpeciesId);
		const bool bTaught = State.IsTaught(SpeciesId);

		if (Level == EKnowledgeLevel::Identified || bTaught)
		{
			Entry.Title = FText::Format(LOCTEXT("EntryTitle", "{0}  ({1})"), Species.CommonName, FText::FromString(Species.ScientificName));
			Entry.Lines.Add(FText::Format(LOCTEXT("EntryEdibility", "Edibility: {0}"), EdibilityName(Species.Edibility)));
			Entry.Lines.Add(Species.Description);
			Entry.Lines.Add(FText::Format(LOCTEXT("EntryHabitat", "Habitat: {0}"), Species.HabitatText));
			for (const FLookalike& Lookalike : Species.Lookalikes)
			{
				Entry.Lines.Add(FText::Format(LOCTEXT("EntryLookalike", "Do not confuse with {0} ({1}): {2}"),
					FText::FromString(SpeciesIdToScientific(Lookalike.Species)), EdibilityName(Lookalike.Danger),
					FText::Join(FText::FromString(TEXT("; ")), Lookalike.KeyDifferences)));
			}
			TArray<FText> Titles;
			for (const FKnowledgeSource* Source : State.SourcesTeaching(SpeciesId))
			{
				Titles.Add(Source->Title);
			}
			Entry.Lines.Add(FText::Format(LOCTEXT("EntrySources", "Learned from: {0}"), FText::Join(FText::FromString(TEXT(", ")), Titles)));
		}
		else
		{
			// Observed only: what the player saw, no name.
			Entry.Title = FText::Format(LOCTEXT("EntryUnknown", "Unknown species #{0}"), FText::AsNumber(UnknownNumber));
			const FMushroomMorphology Typical = MushroomMorphology::Sample(Species, SpeciesId, 0u, EMushroomStage::Mature);
			Entry.Lines.Add(ObservableCriteria::VagueLabel(Typical));
			for (const FText& Line : ObservableCriteria::Describe(Typical, Species))
			{
				if (!Line.ToString().StartsWith(TEXT("Age")))
				{
					Entry.Lines.Add(Line);
				}
			}
			Entry.Lines.Add(LOCTEXT("EntryNoSource", "No guide names it yet."));
		}

		if (Seen && Seen->Observations > 0)
		{
			Entry.Lines.Add(FText::Format(LOCTEXT("EntryFinds", "Examined {0} {0}|plural(one=time,other=times), picked {1}; first found on day {2}"),
				FText::AsNumber(Seen->Observations), FText::AsNumber(Seen->Picks),
				FText::AsNumber(FMath::FloorToInt(Seen->FirstSeenGameSeconds / GameSecondsPerDay) + 1)));
		}
		else
		{
			Entry.Lines.Add(LOCTEXT("EntryNotFound", "Not found yet: examine one to confirm it."));
		}
		return Entry;
	}
}

#undef LOCTEXT_NAMESPACE
