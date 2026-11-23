#include "Kismet/GameplayStatics.h"
#include "Knowledge/KnowledgeTypes.h"
#include "Misc/AutomationTest.h"
#include "Save/ShroomSaveGame.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveRoundTripTest, "ShroomHunt.Save.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSaveRoundTripTest::RunTest(const FString& Parameters)
{
	// Every domain written, serialized like a save file, read back: nothing lost.
	UShroomSaveGame* Save = Cast<UShroomSaveGame>(UGameplayStatics::CreateSaveGameObject(UShroomSaveGame::StaticClass()));
	Save->TotalGameSeconds = 12.75 * 86400.0;
	FWeatherOverride Storm;
	Storm.State = EWeatherState::Storm;
	Storm.StartGameSeconds = 1000.0;
	Storm.EndGameSeconds = 5000.0;
	Save->WeatherOverrides.Add(Storm);
	Save->MapName = TEXT("/Game/ShroomHunt/Forests/F01/Lvl_F01_LowlandOakBeech");
	Save->bHasPlayerTransform = true;
	Save->PlayerTransform = FTransform(FRotator(0.0, 37.0, 0.0), FVector(1234.5, -987.0, 210.0));
	FBasketItem Item;
	Item.SpeciesId = TEXT("BoletusEdulis");
	Item.Seed = 0xFFFFFFF0; // above int32: stored as int64
	Item.PatchId = 42;
	Item.AgeAtPick = 0.4f;
	Item.PickGameSeconds = 11.5 * 86400.0;
	Item.WeightGrams = 183.0f;
	Save->Foraging.Basket.Add(Item);
	Save->Foraging.PickedSeeds = { 0xFFFFFFF0, 17 };
	FPatchPickTimes& Patch = Save->Foraging.Patches.AddDefaulted_GetRef();
	Patch.PatchId = 42;
	Patch.PickGameSeconds = { 11.5 * 86400.0 };
	Save->Knowledge.OwnedSources = { TEXT("PosterOurWoods"), TEXT("BookBoletes") };
	FSpeciesKnowledge Known;
	Known.Observations = 3;
	Known.Picks = 2;
	Known.FirstSeenGameSeconds = 5.0 * 86400.0;
	Save->Knowledge.Species.Add(TEXT("BoletusEdulis"), Known);
	Save->Economy.Money = 87.25f;
	Save->Economy.Reputation = 63.0f;
	Save->Economy.BannedUntilGameSeconds = 14.0 * 86400.0;
	Save->Economy.Items.Add(TEXT("BookBoletes"), 1);
	Save->Economy.SoldTodayKg.Add(TEXT("BoletusEdulis"), 0.6f);

	TArray<uint8> Bytes;
	if (!TestTrue(TEXT("Serialized"), UGameplayStatics::SaveGameToMemory(Save, Bytes)))
	{
		return false;
	}
	const UShroomSaveGame* Loaded = Cast<UShroomSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Read back as a ShroomHunt save"), Loaded))
	{
		return false;
	}
	TestEqual(TEXT("Version"), Loaded->SaveVersion, UShroomSaveGame::CurrentVersion);
	TestEqual(TEXT("Time"), Loaded->TotalGameSeconds, Save->TotalGameSeconds);
	TestTrue(TEXT("Weather overrides"), Loaded->WeatherOverrides.Num() == 1 && Loaded->WeatherOverrides[0].State == EWeatherState::Storm
		&& Loaded->WeatherOverrides[0].EndGameSeconds == 5000.0);
	TestEqual(TEXT("Map"), Loaded->MapName, Save->MapName);
	TestTrue(TEXT("Player transform"), Loaded->bHasPlayerTransform && Loaded->PlayerTransform.Equals(Save->PlayerTransform));
	TestTrue(TEXT("Basket"), Loaded->Foraging.Basket.Num() == 1 && Loaded->Foraging.Basket[0].Seed == Item.Seed
		&& Loaded->Foraging.Basket[0].SpeciesId == Item.SpeciesId && Loaded->Foraging.Basket[0].WeightGrams == Item.WeightGrams);
	TestTrue(TEXT("Picked seeds and patches"), Loaded->Foraging.PickedSeeds == Save->Foraging.PickedSeeds
		&& Loaded->Foraging.Patches.Num() == 1 && Loaded->Foraging.Patches[0].PickGameSeconds == Patch.PickGameSeconds);
	TestTrue(TEXT("Knowledge"), Loaded->Knowledge.OwnedSources == Save->Knowledge.OwnedSources
		&& Loaded->Knowledge.Species.FindRef(TEXT("BoletusEdulis")).Observations == 3);
	TestTrue(TEXT("Economy"), Loaded->Economy.Money == 87.25f && Loaded->Economy.Reputation == 63.0f
		&& Loaded->Economy.BannedUntilGameSeconds == Save->Economy.BannedUntilGameSeconds
		&& Loaded->Economy.GetItemCount(TEXT("BookBoletes")) == 1 && Loaded->Economy.SoldTodayKg.FindRef(TEXT("BoletusEdulis")) == 0.6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveKnowledgeTest, "ShroomHunt.Save.Knowledge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSaveKnowledgeTest::RunTest(const FString& Parameters)
{
	// Export -> import gives the same levels (the identification rule is recomputed, not stored).
	FKnowledgeSourceData Sources;
	FKnowledgeSource Poster;
	Poster.Id = TEXT("Poster");
	Poster.Species = { TEXT("A"), TEXT("B") };
	Sources.Sources.Add(Poster);

	FKnowledgeState State(&Sources);
	TArray<FName> Identified;
	State.AcquireSource(TEXT("Poster"), Identified);
	State.RecordObservation(TEXT("A"), 100.0);
	State.RecordObservation(TEXT("C"), 200.0);
	const FKnowledgeSaveData Data = State.ExportSave();

	FKnowledgeState Restored(&Sources);
	Restored.ImportSave(Data);
	TestTrue(TEXT("A identified again"), Restored.GetLevel(TEXT("A")) == EKnowledgeLevel::Identified);
	TestTrue(TEXT("B taught but never examined: unknown"), Restored.GetLevel(TEXT("B")) == EKnowledgeLevel::Unknown);
	TestTrue(TEXT("C observed"), Restored.GetLevel(TEXT("C")) == EKnowledgeLevel::Observed);
	TestTrue(TEXT("Source owned"), Restored.HasSource(TEXT("Poster")));
	TestEqual(TEXT("Observation dates"), Restored.Find(TEXT("C"))->FirstSeenGameSeconds, 200.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
