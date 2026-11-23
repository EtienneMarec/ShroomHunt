// Save debug console commands: sh.Save.Now / Load / Delete / Status.

#include "Core/ShroomLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Save/SaveSettings.h"
#include "Save/SaveSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace SaveConsoleCommandsDetail
{
	USaveSubsystem* FindSave(UWorld* World)
	{
		USaveSubsystem* Save = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USaveSubsystem>() : nullptr;
		if (!Save)
		{
			UE_LOG(LogShroomDebug, Warning, TEXT("Save commands need a game world (Play)"));
		}
		return Save;
	}

	FString SlotArg(const TArray<FString>& Args)
	{
		return Args.Num() > 0 ? Args[0] : FString();
	}

	FAutoConsoleCommandWithWorldAndArgs CmdSaveNow(
		TEXT("sh.Save.Now"),
		TEXT("sh.Save.Now [Slot]: saves the game now (default slot: Project Settings > ShroomHunt > Save)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USaveSubsystem* Save = FindSave(World))
			{
				Save->SaveGame(SlotArg(Args));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSaveLoad(
		TEXT("sh.Save.Load"),
		TEXT("sh.Save.Load [Slot]: loads a save into the running game (time, weather, player, basket, knowledge, money)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USaveSubsystem* Save = FindSave(World))
			{
				Save->LoadGame(SlotArg(Args));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSaveDelete(
		TEXT("sh.Save.Delete"),
		TEXT("sh.Save.Delete [Slot]: deletes a save. Stop and Play again for a new game (Stop saves again if Save On Quit is on: delete after Stop, or turn it off)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USaveSubsystem* Save = FindSave(World))
			{
				Save->DeleteSave(SlotArg(Args));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdSaveStatus(
		TEXT("sh.Save.Status"),
		TEXT("sh.Save.Status [Slot]: whether the slot exists, and the save settings."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (const USaveSubsystem* Save = FindSave(World))
			{
				const USaveSettings* Settings = GetDefault<USaveSettings>();
				UE_LOG(LogShroomDebug, Display, TEXT("Slot %s: %s; autoload %d, autosave each day %d, save on quit %d; last save/load %.0f s ago"),
					Args.Num() > 0 ? *Args[0] : *Settings->SlotName, Save->HasSave(SlotArg(Args)) ? TEXT("exists") : TEXT("empty"),
					Settings->bAutoLoad ? 1 : 0, Settings->bAutoSaveEachDay ? 1 : 0, Settings->bSaveOnQuit ? 1 : 0,
					Save->GetLastSaveTime() > 0.0 ? FPlatformTime::Seconds() - Save->GetLastSaveTime() : -1.0);
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
