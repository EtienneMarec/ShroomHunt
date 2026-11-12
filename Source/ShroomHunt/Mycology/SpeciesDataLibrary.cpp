#include "Mycology/SpeciesDataLibrary.h"

#include "Core/ShroomLog.h"
#include "Engine/DataTable.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Mycology/SpeciesJsonParser.h"

namespace
{
	FString BuildReport(bool bOk, int32 Count, const SpeciesJson::FParseReport& Report)
	{
		FString Text = FString::Printf(TEXT("%s: %d species, %d error(s), %d warning(s)"),
			bOk ? TEXT("OK") : TEXT("FAILED"), Count, Report.Errors.Num(), Report.Warnings.Num());
		const FString Details = Report.ToString();
		if (!Details.IsEmpty())
		{
			Text += TEXT("\n") + Details;
		}
		return Text;
	}
}

FString USpeciesDataLibrary::ImportSpeciesFolderIntoTable(UDataTable* Table, const FString& Folder)
{
#if WITH_EDITOR
	if (!Table)
	{
		return TEXT("FAILED: no DataTable given");
	}
	if (Table->GetRowStruct() != FMushroomSpeciesRow::StaticStruct())
	{
		return FString::Printf(TEXT("FAILED: %s does not use the MushroomSpeciesRow row structure"), *Table->GetPathName());
	}

	const FString SourceFolder = Folder.IsEmpty() ? SpeciesJson::GetDefaultFolder() : Folder;
	TMap<FName, FMushroomSpeciesRow> Species;
	SpeciesJson::FParseReport Report;
	const int32 Count = SpeciesJson::LoadFolder(SourceFolder, Species, Report);

	if (Report.HasErrors())
	{
		const FString Text = BuildReport(false, Count, Report);
		UE_LOG(LogShroomMycology, Error, TEXT("Species import aborted, table left unchanged:\n%s"), *Text);
		return Text;
	}

	// Deterministic row order (sorted ids) keeps the asset diff stable.
	Species.KeySort(FNameLexicalLess());

	Table->Modify();
	Table->EmptyTable();
	for (const TPair<FName, FMushroomSpeciesRow>& Pair : Species)
	{
		Table->AddRow(Pair.Key, Pair.Value);
	}
	Table->MarkPackageDirty();

	const FString Text = BuildReport(true, Count, Report);
	UE_LOG(LogShroomMycology, Log, TEXT("Species imported into %s from %s:\n%s"), *Table->GetPathName(), *SourceFolder, *Text);
	return Text;
#else
	return TEXT("FAILED: species import is only available in the editor");
#endif
}

FString USpeciesDataLibrary::ValidateSpeciesFolder(const FString& Folder)
{
	TMap<FName, FMushroomSpeciesRow> Species;
	SpeciesJson::FParseReport Report;
	const int32 Count = SpeciesJson::LoadFolder(Folder.IsEmpty() ? SpeciesJson::GetDefaultFolder() : Folder, Species, Report);
	return BuildReport(!Report.HasErrors(), Count, Report);
}

FString USpeciesDataLibrary::GetDefaultSpeciesFolder()
{
	return SpeciesJson::GetDefaultFolder();
}
