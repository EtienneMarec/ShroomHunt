#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Weather/WeatherTypes.h"
#include "WeatherSettings.generated.h"

/**
 * Weather tunables. Edit in Project Settings > ShroomHunt > Weather.
 * The default seed/climate are used until forests provide their own (M1d).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Weather"))
class SHROOMHUNT_API UWeatherSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Seed of the default weather. Same seed => same weather on the same day. */
	UPROPERTY(Config, EditAnywhere, Category = "Default")
	int32 DefaultSeed = 1337;

	UPROPERTY(Config, EditAnywhere, Category = "Default")
	FClimateProfile DefaultClimate;

	/** A new discrete state must persist this long (real seconds) before it is reported (avoids flicker). */
	UPROPERTY(Config, EditAnywhere, Category = "State", meta = (ClampMin = "0", Units = "s"))
	float StateDebounceRealSeconds = 2.0f;

	/** Ground wetness looks at rain over this many past game hours. */
	UPROPERTY(Config, EditAnywhere, Category = "Wetness", meta = (ClampMin = "1"))
	float WetnessWindowGameHours = 6.0f;

	/** Rain (mm, recency-weighted) needed for fully wet ground. */
	UPROPERTY(Config, EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.1"))
	float WetnessSaturationMm = 2.0f;

	virtual FName GetCategoryName() const override { return TEXT("ShroomHunt"); }
};
