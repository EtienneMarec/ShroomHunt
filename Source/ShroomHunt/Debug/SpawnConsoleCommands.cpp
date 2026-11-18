// Mushroom spawn debug console commands: sh.Spawn.Stats / Goto / Refresh / Map.

#include "Core/ShroomLog.h"
#include "Engine/World.h"
#include "Forest/ForestBake.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Mycology/MushroomSpawner.h"
#include "Mycology/SpawnModel.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Time/CalendarMath.h"
#include "Time/CalendarSubsystem.h"
#include "Time/TimeSettings.h"
#include "Weather/WeatherModel.h"
#include "Weather/WeatherSettings.h"

#if !UE_BUILD_SHIPPING

namespace SpawnConsoleCommandsDetail
{
	AMushroomSpawner* FindSpawner(UWorld* World)
	{
		AMushroomSpawner* Spawner = AMushroomSpawner::Find(World);
		if (!Spawner)
		{
			UE_LOG(LogShroomDebug, Warning, TEXT("No Mushroom Spawner in this world (place one in the forest map, then Play)"));
		}
		return Spawner;
	}

	FAutoConsoleCommandWithWorld CmdSpawnStats(
		TEXT("sh.Spawn.Stats"),
		TEXT("sh.Spawn.Stats: mycelium patches, live mushrooms per stage and today's weather trigger per species."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			AMushroomSpawner* Spawner = FindSpawner(World);
			if (!Spawner)
			{
				return;
			}
			const UCalendarSubsystem* Calendar = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCalendarSubsystem>() : nullptr;
			const double Now = Calendar ? Calendar->GetTotalGameSeconds() : 0.0;
			for (const FName Id : Spawner->GetSpeciesIds())
			{
				const FMushroomSpeciesRow* Row = Spawner->FindSpecies(Id);
				const int32 PatchCount = Spawner->GetPatches().FilterByPredicate([Id](const FMyceliumPatch& P) { return P.SpeciesId == Id; }).Num();
				int32 Stages[3] = { 0, 0, 0 };
				for (const FFruitBody& Fruit : Spawner->GetFruitBodies())
				{
					if (Fruit.SpeciesId == Id && Row)
					{
						++Stages[static_cast<int32>(Fruit.StageAt(Now, *Row))];
					}
				}
				UE_LOG(LogShroomDebug, Display, TEXT("%-22s %3d patches | today's trigger %.2f | mushrooms: %d young, %d mature, %d old"),
					*Id.ToString(), PatchCount, Spawner->GetTodayTrigger(Id), Stages[0], Stages[1], Stages[2]);
			}
			UE_LOG(LogShroomDebug, Display, TEXT("%d mushrooms in the forest"), Spawner->GetFruitBodies().Num());
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSpawnGoto(
		TEXT("sh.Spawn.Goto"),
		TEXT("sh.Spawn.Goto [SpeciesId]: teleports the player next to the nearest live mushroom (of that species)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AMushroomSpawner* Spawner = FindSpawner(World);
			APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
			APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
			if (!Spawner || !Pawn)
			{
				return;
			}
			const FName Filter = Args.Num() > 0 ? FName(*Args[0]) : NAME_None;
			const FVector From = Pawn->GetActorLocation();
			const FFruitBody* Best = nullptr;
			double BestDistance = TNumericLimits<double>::Max();
			for (const FFruitBody& Fruit : Spawner->GetFruitBodies())
			{
				const double Distance = FVector::DistSquared(From, Spawner->GetWorldLocation(Fruit));
				if ((Filter.IsNone() || Fruit.SpeciesId == Filter) && Distance > FMath::Square(300.0) && Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = &Fruit;
				}
			}
			if (!Best)
			{
				UE_LOG(LogShroomDebug, Display, TEXT("No mushroom %s right now (try rain: sh.Weather.Force Rain 24, then sh.Time.SkipDays 3)"), *Filter.ToString());
				return;
			}
			const FVector Target = Spawner->GetWorldLocation(*Best);
			const FVector Offset = (From - Target).GetSafeNormal2D() * 200.0;
			Pawn->TeleportTo(Target + Offset + FVector(0.0, 0.0, 120.0), (-Offset).Rotation());
			Controller->SetControlRotation((Target - (Target + Offset + FVector(0, 0, 160))).Rotation());
			UE_LOG(LogShroomDebug, Display, TEXT("Teleported next to a %s"), *Best->SpeciesId.ToString());
		}));

	FAutoConsoleCommandWithWorld CmdSpawnRefresh(
		TEXT("sh.Spawn.Refresh"),
		TEXT("sh.Spawn.Refresh: recomputes the mushrooms now (after changing the weather by hand)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (AMushroomSpawner* Spawner = FindSpawner(World))
			{
				Spawner->Refresh();
				UE_LOG(LogShroomDebug, Display, TEXT("%d mushrooms in the forest"), Spawner->GetFruitBodies().Num());
			}
		}));

	FAutoConsoleCommand CmdSpawnSimulate(
		TEXT("sh.Spawn.Simulate"),
		TEXT("sh.Spawn.Simulate [Years = 1]: offline simulation with the default weather: mushrooms standing each game day at 15:00, per species and season (to tune densities)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const int32 Years = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 10) : 1;
			TMap<FName, FMushroomSpeciesRow> AllSpecies;
			FString Source;
			TArray<FString> Errors;
			const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(TEXT("F01_LowlandOakBeech"), Errors);
			if (!Field || !USpeciesDatabaseSubsystem::LoadSpeciesForTools(AllSpecies, Source))
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("sh.Spawn.Simulate: no field or species data"));
				return;
			}
			const UWeatherSettings* WeatherSettings = GetDefault<UWeatherSettings>();
			const FCalendarConfig Calendar = GetDefault<UTimeSettings>()->MakeCalendarConfig();
			const TSharedRef<FWeatherModel> Weather = MakeShared<FWeatherModel>(static_cast<uint32>(WeatherSettings->DefaultSeed), WeatherSettings->DefaultClimate, Calendar);
			TMap<uint64, float> Cache;
			FSpawnContext Context;
			Context.Field = Field.Get();
			Context.Calendar = Calendar;
			Context.TriggerCache = &Cache;
			Context.Weather.RainMm = [Weather](double From, double To) { return Weather->IntegrateRainMm(From, To, {}); };
			Context.Weather.MeanTemperatureC = [Weather](double From, double To) { return Weather->MeanTemperatureC(From, To, {}); };

			TArray<FName> Ids;
			AllSpecies.GetKeys(Ids);
			Ids.Sort(FNameLexicalLess());
			TMap<FName, TArray<FMyceliumPatch>> Patches;
			for (const FName Id : Ids)
			{
				SpawnModel::GeneratePatches(AllSpecies[Id], Id, *Field, Patches.Add(Id));
			}
			static const TCHAR* SeasonNames[4] = { TEXT("spring"), TEXT("summer"), TEXT("autumn"), TEXT("winter") };
			for (const FName Id : Ids)
			{
				double Sum[4] = { 0, 0, 0, 0 };
				int32 Max[4] = { 0, 0, 0, 0 };
				int32 Days[4] = { 0, 0, 0, 0 };
				int32 DaysWith[4] = { 0, 0, 0, 0 };
				for (int32 Day = 0; Day < Calendar.GetYearLengthDays() * Years; ++Day)
				{
					const double Now = (Day + 15.0 / 24.0) * GameSecondsPerDay;
					const int32 Season = static_cast<int32>(CalendarMath::DecodeDate(Calendar, Now).Season);
					TArray<FFruitBody> Fruits;
					for (const FMyceliumPatch& Patch : Patches[Id])
					{
						SpawnModel::FruitBodiesAt(AllSpecies[Id], Patch, Context, Now, Fruits);
					}
					Sum[Season] += Fruits.Num();
					Max[Season] = FMath::Max(Max[Season], Fruits.Num());
					++Days[Season];
					DaysWith[Season] += Fruits.Num() > 0 ? 1 : 0;
				}
				TArray<FString> Parts;
				for (int32 Season = 0; Season < 4; ++Season)
				{
					Parts.Add(FString::Printf(TEXT("%s mean %.0f max %d (%d/%d days)"), SeasonNames[Season], Sum[Season] / FMath::Max(Days[Season], 1), Max[Season], DaysWith[Season], Days[Season]));
				}
				UE_LOG(LogShroomDebug, Display, TEXT("%-22s %3d patches | %s"), *Id.ToString(), Patches[Id].Num(), *FString::Join(Parts, TEXT(" | ")));
			}
		}));

	FAutoConsoleCommand CmdSpawnMap(
		TEXT("sh.Spawn.Map"),
		TEXT("sh.Spawn.Map [SpeciesId] [ForestName = F01_LowlandOakBeech]: writes Saved/SpawnDebug/<Species>.png, the habitat suitability (black 0 - yellow 1) with the mycelium patches (red circles), north up."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			TMap<FName, FMushroomSpeciesRow> AllSpecies;
			FString Source;
			TArray<FString> Errors;
			const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(Args.Num() > 1 ? Args[1] : TEXT("F01_LowlandOakBeech"), Errors);
			if (!Field || !USpeciesDatabaseSubsystem::LoadSpeciesForTools(AllSpecies, Source))
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("sh.Spawn.Map: no field or species data %s"), *FString::Join(Errors, TEXT("; ")));
				return;
			}
			TArray<FName> Ids;
			if (Args.Num() > 0)
			{
				Ids.Add(FName(*Args[0]));
			}
			else
			{
				AllSpecies.GetKeys(Ids);
			}
			const int32 Size = 504; // 2 m per pixel
			const float Step = Field->GetSizeM() / Size;
			for (const FName Id : Ids)
			{
				const FMushroomSpeciesRow* Row = AllSpecies.Find(Id);
				if (!Row)
				{
					UE_LOG(LogShroomDebug, Warning, TEXT("Unknown species %s"), *Id.ToString());
					continue;
				}
				TArray<FColor> Pixels;
				Pixels.SetNumUninitialized(Size * Size);
				// North (+X) at the top, east (+Y) to the right, like the forest debug maps.
				for (int32 PY = 0; PY < Size; ++PY)
				{
					for (int32 PX = 0; PX < Size; ++PX)
					{
						const float XM = (Size - 1 - PY) * Step;
						const float YM = PX * Step;
						const float S = SpawnModel::Suitability(*Row, Id, *Field, XM, YM);
						const FForestSample Sample = Field->Sample(XM, YM);
						Pixels[PY * Size + PX] = Sample.bWater ? FColor(60, 100, 170)
							: FColor(static_cast<uint8>(30 + 225 * S), static_cast<uint8>(30 + 200 * S * S), 30);
					}
				}
				TArray<FMyceliumPatch> Patches;
				SpawnModel::GeneratePatches(*Row, Id, *Field, Patches);
				for (const FMyceliumPatch& Patch : Patches)
				{
					const float Radius = FMath::Max(Patch.RadiusM / Step, 2.0f);
					for (int32 A = 0; A < 64; ++A)
					{
						const float Angle = UE_TWO_PI * A / 64.0f;
						const int32 PX = FMath::RoundToInt(Patch.PositionM.Y / Step + Radius * FMath::Sin(Angle));
						const int32 PY = Size - 1 - FMath::RoundToInt(Patch.PositionM.X / Step + Radius * FMath::Cos(Angle));
						if (PX >= 0 && PY >= 0 && PX < Size && PY < Size)
						{
							Pixels[PY * Size + PX] = FColor(230, 30, 30);
						}
					}
				}
				const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SpawnDebug") / (Id.ToString() + TEXT(".png")));
				ForestBake::WriteColorPng(Path, Pixels, Size, Size);
				UE_LOG(LogShroomDebug, Display, TEXT("%s: %d patches, map written to %s"), *Id.ToString(), Patches.Num(), *Path);
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
