#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "Mycology/MycologySettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Mycology/SpeciesJsonParser.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrapezoidTest, "ShroomHunt.Mycology.Trapezoid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTrapezoidTest::RunTest(const FString& Parameters)
{
	const FTrapezoid Canopy(0.30f, 0.50f, 0.80f, 0.95f);
	TestEqual(TEXT("Below min"), Canopy.Evaluate(0.2f), 0.0f);
	TestEqual(TEXT("Optimum"), Canopy.Evaluate(0.6f), 1.0f);
	TestTrue(TEXT("Rising edge"), FMath::IsNearlyEqual(Canopy.Evaluate(0.40f), 0.5f, 1e-4f));
	TestTrue(TEXT("Falling edge (spawn model example: 0.85 -> 0.667)"), FMath::IsNearlyEqual(Canopy.Evaluate(0.85f), 0.6667f, 1e-3f));
	TestEqual(TEXT("Above max"), Canopy.Evaluate(1.0f), 0.0f);

	const FTrapezoid Vertical(0.0f, 0.0f, 1600.0f, 2100.0f);
	TestEqual(TEXT("Zero-width rising edge is a step"), Vertical.Evaluate(0.0f), 1.0f);

	// Winter species: November to early March, written past 13.
	const FTrapezoid Winter(11.0f, 12.0f, 14.0f, 15.5f);
	TestEqual(TEXT("Periodic: December"), Winter.EvaluatePeriodic(12.5f, 12.0f), 1.0f);
	TestEqual(TEXT("Periodic: January (13 - 12 = 1)"), Winter.EvaluatePeriodic(1.5f, 12.0f), 1.0f);
	TestEqual(TEXT("Periodic: July"), Winter.EvaluatePeriodic(7.0f, 12.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeciesDataFilesTest, "ShroomHunt.Mycology.SpeciesData.Files",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSpeciesDataFilesTest::RunTest(const FString& Parameters)
{
	// The real data files must parse without errors or warnings.
	TMap<FName, FMushroomSpeciesRow> Species;
	SpeciesJson::FParseReport Report;
	const int32 Count = SpeciesJson::LoadFolder(SpeciesJson::GetDefaultFolder(), Species, Report);
	if (Report.HasErrors() || Report.Warnings.Num() > 0)
	{
		AddError(FString::Printf(TEXT("Data/Species has problems:\n%s"), *Report.ToString()));
		return false;
	}
	TestTrue(TEXT("At least the three M1 species"), Count >= 3);

	const FMushroomSpeciesRow* Cep = Species.Find(TEXT("BoletusEdulis"));
	const FMushroomSpeciesRow* Chanterelle = Species.Find(TEXT("CantharellusCibarius"));
	const FMushroomSpeciesRow* FlyAgaric = Species.Find(TEXT("AmanitaMuscaria"));
	if (!TestNotNull(TEXT("Cep"), Cep) || !TestNotNull(TEXT("Chanterelle"), Chanterelle) || !TestNotNull(TEXT("Fly agaric"), FlyAgaric))
	{
		return false;
	}

	TestEqual(TEXT("Cep beech affinity"), Cep->GetHostAffinity(TEXT("Fagus")), 1.0f);
	TestEqual(TEXT("Cep has no alder host"), Cep->GetHostAffinity(TEXT("Alnus")), 0.0f);
	TestTrue(TEXT("Cep has pores"), Cep->Underside.Type == EUndersideType::Pores);
	TestTrue(TEXT("Cep stem network"), Cep->Stem.Reticulation.bPresent);
	TestTrue(TEXT("Cep is excellent"), Cep->Edibility == EEdibility::Excellent);
	TestTrue(TEXT("Cep phenology: mid October in peak"), Cep->FruitingMonths.EvaluatePeriodic(10.3f, 12.0f) == 1.0f);
	TestTrue(TEXT("Cep stage split"), Cep->GetStageAt(0.1f) == EMushroomStage::Young && Cep->GetStageAt(0.5f) == EMushroomStage::Mature
		&& Cep->GetStageAt(0.9f) == EMushroomStage::Old);
	TestEqual(TEXT("Cep French name"), Cep->FrenchName, FString(TEXT("Cèpe de Bordeaux")));

	TestTrue(TEXT("Chanterelle has ridges"), Chanterelle->Underside.Type == EUndersideType::Ridges);
	TestTrue(TEXT("Chanterelle ridges are decurrent"), Chanterelle->Underside.Attachment == EGillAttachment::Decurrent);
	TestTrue(TEXT("Chanterelle funnel when old"), Chanterelle->Cap.OldShapes.Num() > 0 && Chanterelle->Cap.OldShapes[0].Shape == ECapShape::Infundibuliform);

	TestTrue(TEXT("Fly agaric is toxic"), FlyAgaric->Edibility == EEdibility::Toxic);
	TestTrue(TEXT("Fly agaric ring"), FlyAgaric->Stem.Ring == ERingType::Skirt);
	TestTrue(TEXT("Fly agaric volva"), FlyAgaric->Stem.Volva == EVolvaType::Friable);
	TestTrue(TEXT("Fly agaric warts"), FlyAgaric->Cap.Warts.bPresent && FlyAgaric->Cap.Warts.bWashOffByRain);
	TestTrue(TEXT("Fly agaric favours birch"), FlyAgaric->GetHostAffinity(TEXT("Betula")) > FlyAgaric->GetHostAffinity(TEXT("Fagus")));
	TestTrue(TEXT("Fly agaric red cap"), FlyAgaric->Cap.Colors.A.R > 200 && FlyAgaric->Cap.Colors.A.G < 100);
	TestFalse(TEXT("Localizable description"), FlyAgaric->Description.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeciesDataErrorsTest, "ShroomHunt.Mycology.SpeciesData.ErrorDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSpeciesDataErrorsTest::RunTest(const FString& Parameters)
{
	FString Original;
	if (!FFileHelper::LoadFileToString(Original, *FPaths::Combine(SpeciesJson::GetDefaultFolder(), TEXT("BoletusEdulis.json"))))
	{
		AddError(TEXT("Cannot read BoletusEdulis.json"));
		return false;
	}

	auto Parse = [](const FString& Json, SpeciesJson::FParseReport& Report)
	{
		FName Id;
		FMushroomSpeciesRow Row;
		return SpeciesJson::ParseSpecies(Json, TEXT("Test"), Id, Row, Report);
	};

	{
		SpeciesJson::FParseReport Report;
		TestTrue(TEXT("Unmodified file parses"), Parse(Original, Report));
	}
	{
		SpeciesJson::FParseReport Report;
		const FString Broken = Original.Replace(TEXT("\"Type\": \"Pores\""), TEXT("\"Type\": \"Porez\""));
		TestFalse(TEXT("Unknown enum value is an error"), Parse(Broken, Report));
		TestTrue(TEXT("Error names the field"), Report.ToString().Contains(TEXT("Underside.Type")));
	}
	{
		SpeciesJson::FParseReport Report;
		const FString Broken = Original.Replace(TEXT("\"DiameterCm\": [6, 22]"), TEXT("\"DiameterCm\": [22, 6]"));
		TestFalse(TEXT("Inverted range is an error"), Parse(Broken, Report));
		TestTrue(TEXT("Error names the range"), Report.ToString().Contains(TEXT("Cap.DiameterCm")));
	}
	{
		SpeciesJson::FParseReport Report;
		const FString Broken = Original.Replace(TEXT("\"pH\": [3.8, 4.5, 6.0, 7.2]"), TEXT("\"pH\": [3.8, 6.5, 6.0, 7.2]"));
		TestFalse(TEXT("Unordered trapezoid is an error"), Parse(Broken, Report));
	}
	{
		SpeciesJson::FParseReport Report;
		const FString Typo = Original.Replace(TEXT("\"EdgeAffinity\""), TEXT("\"EdgeAffinty\": 0.0, \"EdgeAffinity\""));
		TestTrue(TEXT("Unknown key still parses"), Parse(Typo, Report));
		TestTrue(TEXT("Unknown key is a warning"), Report.Warnings.Num() == 1 && Report.Warnings[0].Contains(TEXT("EdgeAffinty")));
	}
	{
		SpeciesJson::FParseReport Report;
		TestFalse(TEXT("Invalid JSON is an error"), Parse(TEXT("{ \"Name\": "), Report));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpeciesDataTableSyncTest, "ShroomHunt.Mycology.SpeciesData.DataTableInSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSpeciesDataTableSyncTest::RunTest(const FString& Parameters)
{
	// The generated DataTable must match Data/Species: fails when a JSON was edited without re-import.
	const UDataTable* Table = GetDefault<UMycologySettings>()->SpeciesTable.LoadSynchronous();
	if (!Table)
	{
		AddError(TEXT("DT_Species not found: run Tools/Python/import_data.py in the editor."));
		return false;
	}

	TMap<FName, FMushroomSpeciesRow> Json;
	SpeciesJson::FParseReport Report;
	SpeciesJson::LoadFolder(SpeciesJson::GetDefaultFolder(), Json, Report);

	TArray<FName> TableIds = Table->GetRowNames();
	TArray<FName> JsonIds;
	Json.GetKeys(JsonIds);
	TableIds.Sort(FNameLexicalLess());
	JsonIds.Sort(FNameLexicalLess());
	if (TableIds != JsonIds)
	{
		AddError(TEXT("DT_Species rows differ from Data/Species: re-run Tools/Python/import_data.py."));
		return false;
	}

	for (const TPair<FName, FMushroomSpeciesRow>& Pair : Json)
	{
		const FMushroomSpeciesRow* Row = Table->FindRow<FMushroomSpeciesRow>(Pair.Key, TEXT("SyncTest"), false);
		const bool bSame = Row && Row->ScientificName == Pair.Value.ScientificName && Row->PricePerKg == Pair.Value.PricePerKg
			&& Row->PH.OptLow == Pair.Value.PH.OptLow && Row->Cap.DiameterCm.Max == Pair.Value.Cap.DiameterCm.Max
			&& Row->Hosts.Num() == Pair.Value.Hosts.Num() && Row->Lookalikes.Num() == Pair.Value.Lookalikes.Num();
		TestTrue(FString::Printf(TEXT("%s matches its JSON (re-import if not)"), *Pair.Key.ToString()), bSame);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
