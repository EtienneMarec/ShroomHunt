#include "Save/SaveSubsystem.h"

#include "Core/ShroomLog.h"
#include "Economy/EconomySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Knowledge/KnowledgeSubsystem.h"
#include "Mycology/MushroomSpawner.h"
#include "Player/ForagingSubsystem.h"
#include "Save/SaveSettings.h"
#include "Save/ShroomSaveGame.h"
#include "Time/CalendarSubsystem.h"
#include "Weather/WeatherSubsystem.h"

void USaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCalendarSubsystem>();
	Collection.InitializeDependency<UForagingSubsystem>();
	Collection.InitializeDependency<UKnowledgeSubsystem>();
	Collection.InitializeDependency<UEconomySubsystem>();

	if (UCalendarSubsystem* Calendar = GetGameInstance()->GetSubsystem<UCalendarSubsystem>())
	{
		Calendar->OnDayStarted.AddDynamic(this, &USaveSubsystem::HandleDayStarted);
	}
	WorldInitializedHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &USaveSubsystem::HandleWorldInitializedActors);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &USaveSubsystem::HandleWorldCleanup);
}

void USaveSubsystem::Deinitialize()
{
	// Quitting without a world cleanup first (standalone game): save here, the other systems
	// are deinitialized after this one (they are its dependencies).
	if (bAutoLoadDone && !bQuitSaved && GetDefault<USaveSettings>()->bSaveOnQuit)
	{
		bQuitSaved = SaveGame();
	}
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitializedHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	if (PlayerTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlayerTickerHandle);
		PlayerTickerHandle.Reset();
	}
	Super::Deinitialize();
}

FString USaveSubsystem::ResolveSlot(const FString& Slot) const
{
	return Slot.IsEmpty() ? GetDefault<USaveSettings>()->SlotName : Slot;
}

UWorld* USaveSubsystem::GetGameWorld() const
{
	UWorld* World = GetGameInstance()->GetWorld();
	return World && World->IsGameWorld() ? World : nullptr;
}

bool USaveSubsystem::HasSave(const FString& Slot) const
{
	return UGameplayStatics::DoesSaveGameExist(ResolveSlot(Slot), 0);
}

bool USaveSubsystem::DeleteSave(const FString& Slot)
{
	const FString Name = ResolveSlot(Slot);
	const bool bDeleted = UGameplayStatics::DeleteGameInSlot(Name, 0);
	UE_LOG(LogShroomSave, Log, TEXT("Save: slot %s %s"), *Name, bDeleted ? TEXT("deleted") : TEXT("not found"));
	return bDeleted;
}

UShroomSaveGame* USaveSubsystem::CaptureSave() const
{
	UShroomSaveGame* Save = Cast<UShroomSaveGame>(UGameplayStatics::CreateSaveGameObject(UShroomSaveGame::StaticClass()));
	const UGameInstance* GameInstance = GetGameInstance();
	Save->SavedAtUtc = FDateTime::UtcNow();
	if (const UCalendarSubsystem* Calendar = GameInstance->GetSubsystem<UCalendarSubsystem>())
	{
		Save->TotalGameSeconds = Calendar->GetTotalGameSeconds();
	}
	if (const UForagingSubsystem* Foraging = GameInstance->GetSubsystem<UForagingSubsystem>())
	{
		Save->Foraging = Foraging->ExportSave();
	}
	if (const UKnowledgeSubsystem* Knowledge = GameInstance->GetSubsystem<UKnowledgeSubsystem>())
	{
		Save->Knowledge = Knowledge->ExportSave();
	}
	if (const UEconomySubsystem* Economy = GameInstance->GetSubsystem<UEconomySubsystem>())
	{
		Save->Economy = Economy->GetState();
	}
	if (const UWorld* World = GetGameWorld())
	{
		if (const UWeatherSubsystem* Weather = World->GetSubsystem<UWeatherSubsystem>())
		{
			Save->WeatherOverrides = Weather->GetOverrides();
		}
		Save->MapName = World->GetOutermost()->GetName();
		if (const APlayerController* Controller = World->GetFirstPlayerController())
		{
			if (const APawn* Pawn = Controller->GetPawn())
			{
				Save->bHasPlayerTransform = true;
				Save->PlayerTransform = Pawn->GetActorTransform();
				Save->ViewRotation = Controller->GetControlRotation();
			}
		}
	}
	return Save;
}

void USaveSubsystem::ApplySave(const UShroomSaveGame& Save)
{
	TGuardValue<bool> Applying(bApplyingSave, true);
	UGameInstance* GameInstance = GetGameInstance();
	if (UForagingSubsystem* Foraging = GameInstance->GetSubsystem<UForagingSubsystem>())
	{
		Foraging->ImportSave(Save.Foraging);
	}
	if (UKnowledgeSubsystem* Knowledge = GameInstance->GetSubsystem<UKnowledgeSubsystem>())
	{
		Knowledge->ImportSave(Save.Knowledge);
	}
	if (UEconomySubsystem* Economy = GameInstance->GetSubsystem<UEconomySubsystem>())
	{
		Economy->SetState(Save.Economy);
	}
	if (UCalendarSubsystem* Calendar = GameInstance->GetSubsystem<UCalendarSubsystem>())
	{
		Calendar->SetTotalGameSeconds(Save.TotalGameSeconds);
	}

	// World parts: now if a game world runs, else when it starts.
	PendingWeatherOverrides = Save.WeatherOverrides;
	bHasPendingWeather = true;
	bHasPendingPlayer = Save.bHasPlayerTransform;
	PendingPlayerTransform = Save.PlayerTransform;
	PendingViewRotation = Save.ViewRotation;
	PendingMapName = Save.MapName;
	if (UWorld* World = GetGameWorld())
	{
		if (UWeatherSubsystem* Weather = World->GetSubsystem<UWeatherSubsystem>())
		{
			Weather->SetOverrides(PendingWeatherOverrides);
			bHasPendingWeather = false;
		}
		// The mushrooms already drawn: picked ones disappear, the time may have changed.
		if (AMushroomSpawner* Spawner = AMushroomSpawner::Find(World))
		{
			Spawner->Refresh();
		}
	}
	if (bHasPendingPlayer && !PlayerTickerHandle.IsValid())
	{
		PendingPlayerTimeout = 15.0f;
		PlayerTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USaveSubsystem::TickPendingPlayer));
	}
}

bool USaveSubsystem::TickPendingPlayer(float DeltaSeconds)
{
	// The pawn spawns a little after the world starts: wait for it (or give up).
	PendingPlayerTimeout -= DeltaSeconds;
	UWorld* World = GetGameWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (Pawn && bHasPendingPlayer)
	{
		if (World->GetOutermost()->GetName() == PendingMapName)
		{
			Pawn->TeleportTo(PendingPlayerTransform.GetLocation(), PendingPlayerTransform.Rotator(), false, true);
			Controller->SetControlRotation(PendingViewRotation);
			UE_LOG(LogShroomSave, Log, TEXT("Save: player put back at %s"), *PendingPlayerTransform.GetLocation().ToCompactString());
		}
		bHasPendingPlayer = false;
	}
	if (!bHasPendingPlayer || PendingPlayerTimeout <= 0.0f)
	{
		bHasPendingPlayer = false;
		PlayerTickerHandle.Reset();
		return false; // remove the ticker
	}
	return true;
}

bool USaveSubsystem::SaveGame(const FString& Slot)
{
	const FString Name = ResolveSlot(Slot);
	UShroomSaveGame* Save = CaptureSave();
	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, Name, 0);
	LastSaveRealSeconds = FPlatformTime::Seconds();
	UE_LOG(LogShroomSave, Log, TEXT("Save: %s slot %s (day %.2f, %d in basket, %.2f EUR)"), bSaved ? TEXT("saved") : TEXT("FAILED to save"),
		*Name, Save->TotalGameSeconds / GameSecondsPerDay, Save->Foraging.Basket.Num(), Save->Economy.Money);
	return bSaved;
}

bool USaveSubsystem::LoadGame(const FString& Slot)
{
	const FString Name = ResolveSlot(Slot);
	if (!UGameplayStatics::DoesSaveGameExist(Name, 0))
	{
		UE_LOG(LogShroomSave, Log, TEXT("Save: no slot %s"), *Name);
		return false;
	}
	const UShroomSaveGame* Save = Cast<UShroomSaveGame>(UGameplayStatics::LoadGameFromSlot(Name, 0));
	if (!Save)
	{
		UE_LOG(LogShroomSave, Warning, TEXT("Save: slot %s is not a ShroomHunt save"), *Name);
		return false;
	}
	if (Save->SaveVersion > UShroomSaveGame::CurrentVersion)
	{
		UE_LOG(LogShroomSave, Warning, TEXT("Save: slot %s comes from a newer version (%d > %d): not loaded"), *Name, Save->SaveVersion, UShroomSaveGame::CurrentVersion);
		return false;
	}
	ApplySave(*Save);
	LastSaveRealSeconds = FPlatformTime::Seconds();
	UE_LOG(LogShroomSave, Log, TEXT("Save: loaded slot %s (version %d, day %.2f, %d in basket, %.2f EUR)"), *Name, Save->SaveVersion,
		Save->TotalGameSeconds / GameSecondsPerDay, Save->Foraging.Basket.Num(), Save->Economy.Money);
	return true;
}

void USaveSubsystem::HandleWorldInitializedActors(const FActorsInitializedParams& Params)
{
	UWorld* World = Params.World;
	if (!World || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	bQuitSaved = false;
	if (!bAutoLoadDone)
	{
		bAutoLoadDone = true;
		if (GetDefault<USaveSettings>()->bAutoLoad && HasSave())
		{
			LoadGame();
		}
	}
	if (bHasPendingWeather)
	{
		if (UWeatherSubsystem* Weather = World->GetSubsystem<UWeatherSubsystem>())
		{
			Weather->SetOverrides(PendingWeatherOverrides);
			bHasPendingWeather = false;
		}
	}
}

void USaveSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (World && World->IsGameWorld() && World->GetGameInstance() == GetGameInstance() && bAutoLoadDone && !bQuitSaved
		&& GetDefault<USaveSettings>()->bSaveOnQuit)
	{
		bQuitSaved = SaveGame();
	}
}

void USaveSubsystem::HandleDayStarted(int32 DayIndex)
{
	if (!bApplyingSave && GetDefault<USaveSettings>()->bAutoSaveEachDay && GetGameWorld())
	{
		SaveGame();
	}
}
