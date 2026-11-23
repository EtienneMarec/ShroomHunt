#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Weather/WeatherTypes.h"
#include "SaveSubsystem.generated.h"

class UShroomSaveGame;
class UWorld;

/**
 * Saves and loads the game (M4): calendar time, weather overrides, player position, basket and
 * picks, knowledge, money. Each domain exports its own save struct; this subsystem only
 * gathers them into a UShroomSaveGame and puts them back. Autosave every game day and when
 * the game world closes, autoload when the first game world starts (USaveSettings).
 */
UCLASS()
class SHROOMHUNT_API USaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Saves now. Empty slot = the settings' slot. */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Save")
	bool SaveGame(const FString& Slot = TEXT(""));

	/** Loads a slot and applies it to every system. False if missing or from a newer version. */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Save")
	bool LoadGame(const FString& Slot = TEXT(""));

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Save")
	bool HasSave(const FString& Slot = TEXT("")) const;

	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Save")
	bool DeleteSave(const FString& Slot = TEXT(""));

	/** Gathers the current game into a new save object (also used by the tests). */
	UShroomSaveGame* CaptureSave() const;

	/** Applies a save object to every system. */
	void ApplySave(const UShroomSaveGame& Save);

	/** Seconds of real time since the last save or load (debug). */
	double GetLastSaveTime() const { return LastSaveRealSeconds; }

private:
	FString ResolveSlot(const FString& Slot) const;
	UWorld* GetGameWorld() const;
	void HandleWorldInitializedActors(const struct FActorsInitializedParams& Params);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	bool TickPendingPlayer(float DeltaSeconds);

	UFUNCTION()
	void HandleDayStarted(int32 DayIndex);

	bool bAutoLoadDone = false;
	/** True while a save is applied: changing the time fires the new-day event (no autosave then). */
	bool bApplyingSave = false;
	/** Saved when the current game world closed (no second save at shutdown). */
	bool bQuitSaved = false;
	bool bHasPendingPlayer = false;
	FTransform PendingPlayerTransform;
	FRotator PendingViewRotation = FRotator::ZeroRotator;
	FString PendingMapName;
	float PendingPlayerTimeout = 0.0f;
	TArray<FWeatherOverride> PendingWeatherOverrides;
	bool bHasPendingWeather = false;
	double LastSaveRealSeconds = 0.0;

	FDelegateHandle WorldInitializedHandle;
	FDelegateHandle WorldCleanupHandle;
	FTSTicker::FDelegateHandle PlayerTickerHandle;
};
