#include "Mycology/SpeciesDatabaseSubsystem.h"

#include "Core/ShroomLog.h"
#include "Engine/DataTable.h"
#include "Mycology/MycologySettings.h"
#include "Mycology/SpeciesJsonParser.h"

void USpeciesDatabaseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (LoadFromDataTable())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (GetDefault<UMycologySettings>()->bFallbackToJsonInDevelopment)
	{
		FString Report;
		if (LoadFromJsonFolder(Report))
		{
			UE_LOG(LogShroomMycology, Warning, TEXT("Species DataTable missing or empty: loaded Data/Species JSON directly (run Tools/Python/import_data.py)."));
			return;
		}
		UE_LOG(LogShroomMycology, Error, TEXT("Species JSON fallback failed:\n%s"), *Report);
	}
#endif

	UE_LOG(LogShroomMycology, Error, TEXT("No species data loaded. Check Project Settings > ShroomHunt > Mycology > Species Table."));
}

bool USpeciesDatabaseSubsystem::LoadFromDataTable()
{
	const TSoftObjectPtr<UDataTable>& TableRef = GetDefault<UMycologySettings>()->SpeciesTable;
	if (TableRef.IsNull())
	{
		return false;
	}
	const UDataTable* Table = TableRef.LoadSynchronous();
	if (!Table || Table->GetRowStruct() != FMushroomSpeciesRow::StaticStruct() || Table->GetRowMap().Num() == 0)
	{
		return false;
	}

	Species.Reset();
	Table->ForeachRow<FMushroomSpeciesRow>(TEXT("SpeciesDatabase"), [this](const FName& Key, const FMushroomSpeciesRow& Row)
	{
		Species.Add(Key, Row);
	});
	SourceDescription = FString::Printf(TEXT("DataTable %s"), *Table->GetPathName());
	UE_LOG(LogShroomMycology, Log, TEXT("Loaded %d species from %s"), Species.Num(), *SourceDescription);
	return true;
}

bool USpeciesDatabaseSubsystem::LoadFromJsonFolder(FString& OutReport)
{
	TMap<FName, FMushroomSpeciesRow> Loaded;
	SpeciesJson::FParseReport Report;
	const FString Folder = SpeciesJson::GetDefaultFolder();
	SpeciesJson::LoadFolder(Folder, Loaded, Report);
	OutReport = Report.ToString();
	if (Report.HasErrors() || Loaded.Num() == 0)
	{
		return false;
	}
	Species = MoveTemp(Loaded);
	SourceDescription = FString::Printf(TEXT("JSON %s"), *Folder);
	UE_LOG(LogShroomMycology, Log, TEXT("Loaded %d species from %s"), Species.Num(), *SourceDescription);
	return true;
}

bool USpeciesDatabaseSubsystem::ReloadFromJson(FString& OutReport)
{
#if UE_BUILD_SHIPPING
	OutReport = TEXT("Not available in shipping builds");
	return false;
#else
	return LoadFromJsonFolder(OutReport);
#endif
}

const FMushroomSpeciesRow* USpeciesDatabaseSubsystem::FindSpecies(FName SpeciesId) const
{
	return Species.Find(SpeciesId);
}

bool USpeciesDatabaseSubsystem::GetSpecies(FName SpeciesId, FMushroomSpeciesRow& OutSpecies) const
{
	if (const FMushroomSpeciesRow* Found = Species.Find(SpeciesId))
	{
		OutSpecies = *Found;
		return true;
	}
	return false;
}

TArray<FName> USpeciesDatabaseSubsystem::GetAllSpeciesIds() const
{
	TArray<FName> Ids;
	Species.GetKeys(Ids);
	Ids.Sort(FNameLexicalLess());
	return Ids;
}

TArray<FName> USpeciesDatabaseSubsystem::GetSpeciesInGuideGroup(EGuideGroup Group) const
{
	TArray<FName> Ids;
	for (const TPair<FName, FMushroomSpeciesRow>& Pair : Species)
	{
		if (Pair.Value.GuideGroup == Group)
		{
			Ids.Add(Pair.Key);
		}
	}
	Ids.Sort(FNameLexicalLess());
	return Ids;
}
