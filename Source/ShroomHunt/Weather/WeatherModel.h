#pragma once

#include "CoreMinimal.h"
#include "Time/TimeTypes.h"
#include "Weather/WeatherTypes.h"

/**
 * Deterministic, random-access weather model (pure C++, no UObject).
 *
 * Weather at any game time is a function of (seed, climate, time): layered 1D
 * noise over time ("pressure systems" lasting about two game days, hourly
 * showers, daily fog and storm rolls) on top of the forest's monthly climate.
 * There is no simulation state, so the rain history of any period can be
 * recomputed on demand, which the mushroom spawn model relies on.
 * Overrides (debug "force weather") are blended in and are part of the history.
 */
class SHROOMHUNT_API FWeatherModel
{
public:
	FWeatherModel() = default;
	FWeatherModel(uint32 InSeed, const FClimateProfile& InClimate, const FCalendarConfig& InCalendar);

	/** Weather without any override. */
	FWeatherSample SampleNatural(double GameSeconds) const;

	/** Weather with overrides applied (the last matching override wins). */
	FWeatherSample Sample(double GameSeconds, TConstArrayView<FWeatherOverride> Overrides) const;

	/** Total rain in mm between two game times (midpoint integration). */
	float IntegrateRainMm(double FromGameSeconds, double ToGameSeconds, TConstArrayView<FWeatherOverride> Overrides, double StepGameSeconds = 1800.0) const;

	/** Mean air temperature between two game times. */
	float MeanTemperatureC(double FromGameSeconds, double ToGameSeconds, TConstArrayView<FWeatherOverride> Overrides, double StepGameSeconds = 3600.0) const;

	/** Maps continuous variables to a discrete state. */
	static EWeatherState Classify(const FWeatherSample& Sample);

	/** Target values used when a state is forced; temperature and humidity are taken from Natural. */
	static FWeatherSample MakeStatePreset(EWeatherState State, const FWeatherSample& Natural);

	/** Blend time at the start and end of an override, in game seconds. */
	static constexpr double OverrideBlendGameSeconds = 1800.0;

	uint32 GetSeed() const { return Seed; }
	const FClimateProfile& GetClimate() const { return Climate; }
	const FCalendarConfig& GetCalendar() const { return Calendar; }

private:
	uint32 Seed = 0;
	FClimateProfile Climate;
	FCalendarConfig Calendar;
};
