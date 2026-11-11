#pragma once

#include "CoreMinimal.h"
#include "Time/TimeTypes.h"

/**
 * Pure calendar and astronomy functions (no UObject, fully testable).
 *
 * Mapping between game time and the real calendar:
 * each game season is stretched piecewise-linearly onto its real span
 * (spring = 20 Mar -> 21 Jun, summer -> 22 Sep, autumn -> 21 Dec, winter -> 20 Mar),
 * so the sun path and mushroom phenology follow real months even though a
 * season only lasts a few game days.
 */
namespace CalendarMath
{
	/** Decodes total game seconds into a date. */
	SHROOMHUNT_API FGameDate DecodeDate(const FCalendarConfig& Config, double TotalGameSeconds);

	/** Encodes a date into total game seconds. DayInSeason is clamped to the season length. */
	SHROOMHUNT_API double EncodeDate(const FCalendarConfig& Config, int32 Year, ESeason Season, int32 DayInSeason, float Hour);

	/** Real-world day of year in [0, 365) for a game season and its progress in [0, 1). */
	SHROOMHUNT_API float SeasonProgressToRealDayOfYear(ESeason Season, float SeasonProgress);

	/** Continuous real month in [1, 13) from a real day of year in [0, 365). */
	SHROOMHUNT_API float RealDayOfYearToMonth(float RealDayOfYear);

	/** Continuous real month in [1, 13) for a point in game time. */
	SHROOMHUNT_API float MonthAt(const FCalendarConfig& Config, double TotalGameSeconds);

	/** Converts real-world biological days into game days (divides by BiologicalDaysPerGameDay). */
	SHROOMHUNT_API double BiologicalDaysToGameDays(const FCalendarConfig& Config, double RealDays);

	/**
	 * Sun position for a latitude, real day of year and local solar hour.
	 * Uses the standard declination / hour-angle formulas (no equation of time).
	 */
	SHROOMHUNT_API FCelestialPosition ComputeSunPosition(float LatitudeDeg, float RealDayOfYear, float SolarHour);

	/**
	 * Simplified moon: follows the sun's path shifted by its phase.
	 * @param MoonPhase 0 = new moon, 0.5 = full moon.
	 */
	SHROOMHUNT_API FCelestialPosition ComputeMoonPosition(float LatitudeDeg, float RealDayOfYear, float SolarHour, float MoonPhase);

	/** Moon phase in [0, 1) for a game time, with a lunar cycle expressed in game days. */
	SHROOMHUNT_API float MoonPhaseAt(double TotalGameSeconds, float LunarCycleGameDays);

	/** Unit vector pointing from the ground towards a celestial body (X = north, Y = east, Z = up). */
	SHROOMHUNT_API FVector DirectionTowards(const FCelestialPosition& Position);
}
