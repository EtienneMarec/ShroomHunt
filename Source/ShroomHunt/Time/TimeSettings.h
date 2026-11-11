#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Time/TimeTypes.h"
#include "TimeSettings.generated.h"

/**
 * Calendar tunables. Edit in Project Settings > ShroomHunt > Time
 * (saved as text in Config/DefaultGame.ini, no recompilation needed).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Time"))
class SHROOMHUNT_API UTimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Real seconds for one full game day. */
	UPROPERTY(Config, EditAnywhere, Category = "Day", meta = (ClampMin = "30", UIMin = "60", UIMax = "3600", Units = "s"))
	float DayLengthRealSeconds = 600.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Seasons", meta = (ClampMin = "1", ClampMax = "60"))
	int32 SpringLengthDays = 6;

	UPROPERTY(Config, EditAnywhere, Category = "Seasons", meta = (ClampMin = "1", ClampMax = "60"))
	int32 SummerLengthDays = 5;

	/** Longest season by default: it is the main mushroom season. */
	UPROPERTY(Config, EditAnywhere, Category = "Seasons", meta = (ClampMin = "1", ClampMax = "60"))
	int32 AutumnLengthDays = 8;

	UPROPERTY(Config, EditAnywhere, Category = "Seasons", meta = (ClampMin = "1", ClampMax = "60"))
	int32 WinterLengthDays = 5;

	/**
	 * Biological processes (rain-to-fruiting lag, lifetimes) run as if one game day
	 * lasted this many real days. See Docs/SpawnModel.md.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Biology", meta = (ClampMin = "0.5", ClampMax = "30"))
	float BiologicalDaysPerGameDay = 4.0f;

	/** Date at which a new game starts. */
	UPROPERTY(Config, EditAnywhere, Category = "Start")
	ESeason StartSeason = ESeason::Autumn;

	/** 0-based day inside the start season. */
	UPROPERTY(Config, EditAnywhere, Category = "Start", meta = (ClampMin = "0"))
	int32 StartDayInSeason = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Start", meta = (ClampMin = "0", ClampMax = "23.99"))
	float StartHour = 7.5f;

	/** Latitude used for the sun path (47 = central France). */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "-89", ClampMax = "89", Units = "deg"))
	float LatitudeDeg = 47.0f;

	/** Length of a full moon cycle, in game days. */
	UPROPERTY(Config, EditAnywhere, Category = "Sky", meta = (ClampMin = "1"))
	float LunarCycleGameDays = 8.0f;

	/** Builds the plain configuration consumed by CalendarMath. */
	FCalendarConfig MakeCalendarConfig() const;

	virtual FName GetCategoryName() const override { return TEXT("ShroomHunt"); }
};
