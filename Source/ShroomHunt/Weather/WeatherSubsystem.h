#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Weather/WeatherModel.h"
#include "Weather/WeatherTypes.h"
#include "WeatherSubsystem.generated.h"

class UCalendarSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeatherStateChanged, EWeatherState, OldState, EWeatherState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLightningStrike, float, Distance01);

/**
 * Weather of the current world (forest). Evaluates the deterministic FWeatherModel
 * at the calendar time, exposes the continuous sample and a debounced discrete state,
 * ground wetness, rain/temperature history queries and debug overrides.
 */
UCLASS()
class SHROOMHUNT_API UWeatherSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Replaces the climate and seed (called when a forest definition is loaded). */
	void SetClimate(uint32 NewSeed, const FClimateProfile& NewClimate);

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Weather")
	FWeatherSample GetCurrentSample() const { return CurrentSample; }

	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Weather")
	EWeatherState GetCurrentState() const { return CurrentState; }

	/** Ground wetness in [0, 1] from recent rain. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Weather")
	float GetWetness() const { return Wetness; }

	/** Snow water equivalent on the ground in mm (M2), from the recent weather history. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Weather")
	float GetSnowWaterMm() const { return SnowWaterMm; }

	/** Ground covered by snow in [0, 1]. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Weather")
	float GetSnowCover() const { return SnowCover; }

	/** Hoar frost in [0, 1] (cold nights and mornings). */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Weather")
	float GetFrost() const { return Frost; }

	/** Dry ground and grass in [0, 1] (warm days without rain). */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Weather")
	float GetDryness() const { return Dryness; }

	/** Share of the current precipitation falling as snow, in [0, 1]. */
	UFUNCTION(BlueprintPure, Category = "ShroomHunt|Weather")
	float GetSnowFraction() const;

	/** Rain in mm over the last N game hours. */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Weather")
	float GetRainLastHours(float GameHours) const;

	/** Rain in mm between two game times (game seconds). */
	float GetRainfallMm(double FromGameSeconds, double ToGameSeconds) const;

	/** Mean temperature between two game times (game seconds). */
	float GetMeanTemperatureC(double FromGameSeconds, double ToGameSeconds) const;

	/**
	 * Forces a weather state for some game hours, starting now (blends in and out). bAtOnce starts it one
	 * blend earlier so that it is fully there now (demo shots, often with the game time frozen).
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Weather")
	void ForceWeather(EWeatherState State, float DurationGameHours, bool bAtOnce = false);

	/** Ends active and future overrides now (past ones stay in the history). */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Weather")
	void ClearForcedWeather();

	const TArray<FWeatherOverride>& GetOverrides() const { return Overrides; }

	/** Restores the override history (save game): they are part of the rain history. */
	void SetOverrides(const TArray<FWeatherOverride>& NewOverrides);
	const FWeatherModel& GetModel() const { return Model; }

	/** Current game time in seconds (0 if no calendar). */
	double GetNowGameSeconds() const;

	UPROPERTY(BlueprintAssignable, Category = "ShroomHunt|Weather")
	FOnWeatherStateChanged OnWeatherStateChanged;

	/** Fired during storms; Distance01 = 0 for a close strike, 1 for a distant one. */
	UPROPERTY(BlueprintAssignable, Category = "ShroomHunt|Weather")
	FOnLightningStrike OnLightningStrike;

private:
	UCalendarSubsystem* GetCalendar() const;
	void UpdateWetness(double Now);
	void UpdateLightning(double Now);
	void UpdateGround(double Now);

	FWeatherModel Model;
	TArray<FWeatherOverride> Overrides;

	FWeatherSample CurrentSample;
	EWeatherState CurrentState = EWeatherState::Clear;
	EWeatherState PendingState = EWeatherState::Clear;
	float PendingStateTime = 0.0f;
	bool bHasState = false;

	float Wetness = 0.0f;
	float WetnessTimer = 0.0f;

	float SnowWaterMm = 0.0f;
	float SnowCover = 0.0f;
	float Frost = 0.0f;
	float Dryness = 0.0f;
	float GroundTimer = 0.0f;

	int64 LastLightningBucket = -1;
};
