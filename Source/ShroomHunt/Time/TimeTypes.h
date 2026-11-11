#pragma once

#include "CoreMinimal.h"
#include "TimeTypes.generated.h"

/** Game seasons, in calendar order. A game year starts with spring. */
UENUM(BlueprintType)
enum class ESeason : uint8
{
	Spring,
	Summer,
	Autumn,
	Winter
};

/** Number of seconds in one game day (game time, not real time). */
inline constexpr double GameSecondsPerDay = 86400.0;

/**
 * Plain calendar configuration used by the pure calendar math.
 * Built from UTimeSettings at runtime; can be built by hand in tests.
 */
struct FCalendarConfig
{
	/** Real seconds for one full game day (default 10 minutes). */
	double DayLengthRealSeconds = 600.0;

	/** Game days per season, indexed by ESeason. */
	int32 SeasonLengthDays[4] = { 6, 5, 8, 5 };

	/** Biological processes run as if one game day lasted this many real days. */
	float BiologicalDaysPerGameDay = 4.0f;

	int32 GetYearLengthDays() const
	{
		return SeasonLengthDays[0] + SeasonLengthDays[1] + SeasonLengthDays[2] + SeasonLengthDays[3];
	}

	/** Game seconds that elapse per real second at normal speed (144 for a 10-minute day). */
	double GetBaseTimeScale() const
	{
		return DayLengthRealSeconds > 0.0 ? GameSecondsPerDay / DayLengthRealSeconds : 0.0;
	}
};

/** A fully decoded point in game time. Everything is derived from total game seconds. */
USTRUCT(BlueprintType)
struct FGameDate
{
	GENERATED_BODY()

	/** Absolute game day since the start of the calendar (day 0 = first spring day of year 1). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	int32 DayIndex = 0;

	/** 1-based year. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	int32 Year = 1;

	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	ESeason Season = ESeason::Spring;

	/** 0-based day inside the current season. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	int32 DayInSeason = 0;

	/** 0-based day inside the current year. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	int32 DayOfYear = 0;

	/** Hour of the day in [0, 24). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	float Hour = 0.0f;

	/** Progress through the current season in [0, 1). */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	float SeasonProgress = 0.0f;

	/** Equivalent real-world day of year in [0, 365), used for sun path and phenology. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	float RealDayOfYear = 0.0f;

	/** Equivalent real-world month as a continuous value in [1, 13): 10.5 = mid October. */
	UPROPERTY(BlueprintReadOnly, Category = "ShroomHunt|Time")
	float Month = 1.0f;

	/** Debug-friendly text, e.g. "Y1 Autumn d3/8 07:42". */
	SHROOMHUNT_API FString ToDebugString(const FCalendarConfig& Config) const;
};

/** Sun or moon position in the sky. */
struct FCelestialPosition
{
	/** Degrees above the horizon (negative = below). */
	float ElevationDeg = 0.0f;

	/** Degrees clockwise from north. */
	float AzimuthDeg = 0.0f;

	/** For the moon: lit fraction in [0, 1]. Always 1 for the sun. */
	float Illumination = 1.0f;
};
