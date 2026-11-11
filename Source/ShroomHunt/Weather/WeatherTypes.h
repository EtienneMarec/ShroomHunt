#pragma once

#include "CoreMinimal.h"
#include "WeatherTypes.generated.h"

/** Discrete weather state, derived from the continuous FWeatherSample (UI, gameplay, debug). */
UENUM(BlueprintType)
enum class EWeatherState : uint8
{
	Clear,
	PartlyCloudy,
	Overcast,
	Fog,
	Drizzle,
	Rain,
	HeavyRain,
	Storm,
	Count UMETA(Hidden)
};

/** Continuous weather variables at one point in time. Visuals read these, not the discrete state. */
USTRUCT(BlueprintType)
struct FWeatherSample
{
	GENERATED_BODY()

	/** Sky cloud cover in [0, 1]. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	float CloudCover = 0.0f;

	/** Precipitation rate in mm per game hour. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	float PrecipitationMmPerHour = 0.0f;

	/** Ground fog density in [0, 1]. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	float FogDensity = 0.0f;

	/** Wind speed in m/s. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	float WindSpeed = 0.0f;

	/** Air temperature in degrees Celsius. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	float TemperatureC = 10.0f;

	/** Relative humidity in [0, 1]. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	float Humidity = 0.6f;

	/** Thunderstorm intensity in [0, 1] (lightning when high). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	float StormIntensity = 0.0f;

	/** Component-wise linear interpolation. */
	static FWeatherSample Lerp(const FWeatherSample& A, const FWeatherSample& B, float Alpha);
};

/** Climate values of one forest for a given (continuous) month. */
struct FClimateAtMonth
{
	float MeanTemperatureC = 10.0f;
	float DiurnalRangeC = 8.0f;
	float RainFraction = 0.1f;
	float FogPropensity = 0.2f;
	float StormPropensity = 0.0f;
};

/**
 * Monthly climate of a forest (12 values per curve, January first).
 * Defaults describe a lowland temperate French oak-beech forest.
 */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FClimateProfile
{
	GENERATED_BODY()

	FClimateProfile();

	/** Mean daily temperature per month (degrees C). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climate", meta = (EditFixedOrder))
	TArray<float> MonthlyMeanTemperatureC;

	/** Difference between daily max and min temperature per month (degrees C). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climate", meta = (EditFixedOrder))
	TArray<float> MonthlyDiurnalRangeC;

	/** Fraction of the time it rains, per month (0-1). Real lowland France is about 0.08-0.15. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climate", meta = (EditFixedOrder))
	TArray<float> MonthlyRainFraction;

	/** Probability that a given morning is foggy, per month (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climate", meta = (EditFixedOrder))
	TArray<float> MonthlyFogPropensity;

	/** Probability that a given day has a thunderstorm, per month (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climate", meta = (EditFixedOrder))
	TArray<float> MonthlyStormPropensity;

	/** Peak rain rate of a strong (non-storm) rain, mm per hour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climate", meta = (ClampMin = "0.5"))
	float MaxRainRateMmPerHour = 3.5f;

	/** Typical duration of a weather regime (pressure system), in game days. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climate", meta = (ClampMin = "0.25"))
	float SynopticPeriodGameDays = 2.2f;

	/** Interpolates the monthly curves at a continuous month in [1, 13). */
	FClimateAtMonth Evaluate(float Month) const;
};

/** A debug or scripted weather override over a time interval (game seconds). */
USTRUCT(BlueprintType)
struct FWeatherOverride
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	EWeatherState State = EWeatherState::Clear;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	double StartGameSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Weather")
	double EndGameSeconds = 0.0;
};
