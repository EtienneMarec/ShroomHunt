// Species debug console commands: sh.Species.List / Show / Reload / Validate.

#include "Core/ShroomLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Mycology/SpeciesDataLibrary.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace
{
	USpeciesDatabaseSubsystem* FindDatabase(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		USpeciesDatabaseSubsystem* Database = GameInstance ? GameInstance->GetSubsystem<USpeciesDatabaseSubsystem>() : nullptr;
		if (!Database)
		{
			UE_LOG(LogShroomDebug, Warning, TEXT("No species database: run this command while playing (PIE)."));
		}
		return Database;
	}

	template <typename TEnum>
	FString EnumName(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	FString TrapezoidText(const FTrapezoid& T)
	{
		return FString::Printf(TEXT("[%g, %g, %g, %g]"), T.Min, T.OptLow, T.OptHigh, T.Max);
	}

	FAutoConsoleCommandWithWorldAndArgs CmdSpeciesList(
		TEXT("sh.Species.List"),
		TEXT("sh.Species.List: lists the loaded species and where they come from."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const USpeciesDatabaseSubsystem* Database = FindDatabase(World);
			if (!Database)
			{
				return;
			}
			UE_LOG(LogShroomDebug, Display, TEXT("%d species (%s):"), Database->GetSpeciesCount(), *Database->GetSourceDescription());
			for (const FName Id : Database->GetAllSpeciesIds())
			{
				const FMushroomSpeciesRow* Row = Database->FindSpecies(Id);
				UE_LOG(LogShroomDebug, Display, TEXT("  %-24s %-26s %-22s %s"), *Id.ToString(), *Row->ScientificName,
					*Row->CommonName.ToString(), *EnumName(Row->Edibility));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSpeciesShow(
		TEXT("sh.Species.Show"),
		TEXT("sh.Species.Show <SpeciesId>: prints the main data of a species, e.g. sh.Species.Show BoletusEdulis"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const USpeciesDatabaseSubsystem* Database = FindDatabase(World);
			if (!Database || Args.Num() == 0)
			{
				return;
			}
			const FMushroomSpeciesRow* Row = Database->FindSpecies(FName(*Args[0]));
			if (!Row)
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("Unknown species '%s' (see sh.Species.List)"), *Args[0]);
				return;
			}
			TArray<FString> Hosts;
			for (const FHostAffinity& Host : Row->Hosts)
			{
				Hosts.Add(FString::Printf(TEXT("%s %.2f"), *Host.Genus.ToString(), Host.Affinity));
			}
			UE_LOG(LogShroomDebug, Display, TEXT("%s - %s (%s / %s)"), *Args[0], *Row->ScientificName, *Row->CommonName.ToString(), *Row->FrenchName);
			UE_LOG(LogShroomDebug, Display, TEXT("  %s, %s, guide %s, %.0f EUR/kg"), *EnumName(Row->Edibility), *EnumName(Row->Trophic), *EnumName(Row->GuideGroup), Row->PricePerKg);
			UE_LOG(LogShroomDebug, Display, TEXT("  Hosts: %s"), *FString::Join(Hosts, TEXT(", ")));
			UE_LOG(LogShroomDebug, Display, TEXT("  pH %s  moisture %s  canopy %s  altitude %s"),
				*TrapezoidText(Row->PH), *TrapezoidText(Row->Moisture), *TrapezoidText(Row->Canopy), *TrapezoidText(Row->AltitudeM));
			UE_LOG(LogShroomDebug, Display, TEXT("  Fruiting months %s, rain %g-%g mm over %g days, temp %s, lag %g-%g days"),
				*TrapezoidText(Row->FruitingMonths), Row->RainMm.Min, Row->RainMm.Max, Row->RainWindowDays,
				*TrapezoidText(Row->TempC), Row->LagDays.Min, Row->LagDays.Max);
			UE_LOG(LogShroomDebug, Display, TEXT("  Cap %g-%g cm %s, underside %s, stem %s, ring %s, volva %s, %d lookalike(s)"),
				Row->Cap.DiameterCm.Min, Row->Cap.DiameterCm.Max, *EnumName(Row->Cap.Surface), *EnumName(Row->Underside.Type),
				*EnumName(Row->Stem.Shape), *EnumName(Row->Stem.Ring), *EnumName(Row->Stem.Volva), Row->Lookalikes.Num());
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSpeciesReload(
		TEXT("sh.Species.Reload"),
		TEXT("sh.Species.Reload: reloads Data/Species/*.json into the running game (the DataTable is not changed)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USpeciesDatabaseSubsystem* Database = FindDatabase(World))
			{
				FString Report;
				const bool bOk = Database->ReloadFromJson(Report);
				UE_LOG(LogShroomDebug, Display, TEXT("Species reload %s (%d species)%s%s"), bOk ? TEXT("OK") : TEXT("FAILED"),
					Database->GetSpeciesCount(), Report.IsEmpty() ? TEXT("") : TEXT("\n"), *Report);
			}
		}));

	FAutoConsoleCommand CmdSpeciesValidate(
		TEXT("sh.Species.Validate"),
		TEXT("sh.Species.Validate: checks Data/Species/*.json and prints errors and warnings (works in the editor without playing)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UE_LOG(LogShroomDebug, Display, TEXT("%s"), *USpeciesDataLibrary::ValidateSpeciesFolder(FString()));
		}));
}

#endif // !UE_BUILD_SHIPPING
