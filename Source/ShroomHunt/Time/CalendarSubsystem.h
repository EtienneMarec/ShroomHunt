#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Time/TimeTypes.h"
#include "CalendarSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameHourChanged, int32, Hour);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameDayStarted, int32, DayIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameSeasonChanged, ESeason, Season);

/**
 * Single source of truth for game time.
 *
 * Lives on the Game Instance, so time keeps flowing across map changes. It stores
 * only the total elapsed game seconds; date, season, sun position etc. are derived
 * through CalendarMath. Ticks with the game world and stops while the game is paused.
 */
UCLASS()
class SHROOMHUNT_API UCalendarSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

	/** Current decoded date. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Time")
	FGameDate GetDate() const { return CurrentDate; }

	/** Total game seconds since day 0 (the only persistent time state). */
	double GetTotalGameSeconds() const { return TotalGameSeconds; }

	/** Total game time in days, as a float (handy for Blueprints and debug). */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Time")
	float GetGameDays() const { return static_cast<float>(TotalGameSeconds / GameSecondsPerDay); }

	/** Restores time (save game, debug). Fires change events if needed. */
	void SetTotalGameSeconds(double NewTotalGameSeconds);

	/** Jumps to the given hour, today if still ahead, otherwise tomorrow. Time only moves forward. */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Time")
	void SetHour(float Hour);

	/** Advances time by whole days. */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Time")
	void SkipDays(int32 Days);

	/** Debug speed multiplier applied on top of the base time scale (1 = normal). */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Time")
	void SetDebugTimeMultiplier(float Multiplier);

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Time")
	float GetDebugTimeMultiplier() const { return DebugTimeMultiplier; }

	/** Game seconds elapsed per real second, including the debug multiplier. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Time")
	float GetEffectiveTimeScale() const;

	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Time")
	void SetTimePaused(bool bPaused) { bTimePaused = bPaused; }

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Time")
	bool IsTimePaused() const { return bTimePaused; }

	const FCalendarConfig& GetCalendarConfig() const { return CalendarConfig; }

	/** Latitude and lunar cycle from the settings (used by the environment). */
	float GetLatitudeDeg() const { return LatitudeDeg; }
	float GetLunarCycleGameDays() const { return LunarCycleGameDays; }

	UPROPERTY(BlueprintAssignable, Category = "ShroomHunt|Time")
	FOnGameHourChanged OnHourChanged;

	/** Fired once when a new day starts (once per jump when skipping several days). */
	UPROPERTY(BlueprintAssignable, Category = "ShroomHunt|Time")
	FOnGameDayStarted OnDayStarted;

	UPROPERTY(BlueprintAssignable, Category = "ShroomHunt|Time")
	FOnGameSeasonChanged OnSeasonChanged;

private:
	void RefreshDateAndNotify();

	FCalendarConfig CalendarConfig;
	FGameDate CurrentDate;
	double TotalGameSeconds = 0.0;
	float DebugTimeMultiplier = 1.0f;
	float LatitudeDeg = 47.0f;
	float LunarCycleGameDays = 8.0f;
	bool bTimePaused = false;
	bool bInitialized = false;
};
