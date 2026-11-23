#pragma once

#include "CoreMinimal.h"
#include "Weather/WeatherTypes.h"

/** Rain/snow split, snow pack and melt. Defaults fit a lowland forest. */
struct SHROOMHUNT_API FSnowParams
{
	/** Below this air temperature all precipitation is snow. */
	float SnowBelowC = 1.0f;
	/** Above this one it is all rain (sleet in between). */
	float RainAboveC = 3.0f;
	/** Degree-hour melt: mm of snow water melted per degree above 0 C per hour. */
	float MeltMmPerDegreeHour = 0.15f;
	/** Extra melt per mm of rain falling on the snow. */
	float RainMeltPerMm = 0.1f;
	/** History integrated for the snow pack (older snow has melted in a lowland winter). */
	float LookbackGameHours = 144.0f;
	/** Snow water (mm) that covers the ground completely (about 5 cm of fresh snow). */
	float FullCoverMm = 5.0f;
};

/**
 * Snow and frost from the deterministic weather history (M2). Like the rest of the weather,
 * a function of time: the snow pack at t is integrated over the hours before t, nothing is
 * stored. Pure, unit-tested with synthetic weather.
 */
namespace SnowModel
{
	/** Share of the precipitation falling as snow at an air temperature, in [0, 1]. */
	SHROOMHUNT_API float SnowFraction(float TemperatureC, const FSnowParams& Params = FSnowParams());

	/** Snow water equivalent (mm) on the ground at NowGameSeconds. */
	SHROOMHUNT_API float SnowWaterMm(TFunctionRef<FWeatherSample(double GameSeconds)> Sample, double NowGameSeconds,
		const FSnowParams& Params = FSnowParams(), double StepGameSeconds = 3600.0);

	/** Ground covered by snow, in [0, 1]. */
	SHROOMHUNT_API float Cover(float SnowWaterMm, const FSnowParams& Params = FSnowParams());

	/** Hoar frost on the ground and leaves from the mean temperature of the last hours, in [0, 1]. */
	SHROOMHUNT_API float Frost(float RecentTemperatureC);

	/** Dry ground and grass after days without rain in warm weather, in [0, 1]. */
	SHROOMHUNT_API float Dryness(float RainLastDaysMm, float TemperatureC);
}
