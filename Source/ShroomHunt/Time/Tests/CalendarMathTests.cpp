#include "Misc/AutomationTest.h"
#include "Time/CalendarMath.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCalendarDateTest, "ShroomHunt.Time.Calendar.EncodeDecode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCalendarDateTest::RunTest(const FString& Parameters)
{
	const FCalendarConfig Config; // 6 / 5 / 8 / 5 days, 600 s per day
	TestEqual(TEXT("Year length"), Config.GetYearLengthDays(), 24);
	TestTrue(TEXT("Base time scale is 144 for a 10-minute day"), FMath::IsNearlyEqual(Config.GetBaseTimeScale(), 144.0));

	const double Seconds = CalendarMath::EncodeDate(Config, 2, ESeason::Autumn, 3, 13.5f);
	const FGameDate Date = CalendarMath::DecodeDate(Config, Seconds);
	TestEqual(TEXT("Year"), Date.Year, 2);
	TestTrue(TEXT("Season"), Date.Season == ESeason::Autumn);
	TestEqual(TEXT("Day in season"), Date.DayInSeason, 3);
	TestTrue(TEXT("Hour"), FMath::IsNearlyEqual(Date.Hour, 13.5f, 0.001f));
	TestEqual(TEXT("Day of year"), Date.DayOfYear, 6 + 5 + 3);
	TestEqual(TEXT("Absolute day"), Date.DayIndex, 24 + 14);

	// Year rollover: the day after the last winter day is spring of the next year.
	const FGameDate NewYear = CalendarMath::DecodeDate(Config, 24.0 * GameSecondsPerDay + 1.0);
	TestEqual(TEXT("Rollover year"), NewYear.Year, 2);
	TestTrue(TEXT("Rollover season"), NewYear.Season == ESeason::Spring);
	TestEqual(TEXT("Rollover day"), NewYear.DayInSeason, 0);

	// Start of autumn maps to 22 September (day 265) => month 9 + 22/30.
	const FGameDate AutumnStart = CalendarMath::DecodeDate(Config, CalendarMath::EncodeDate(Config, 1, ESeason::Autumn, 0, 0.0f));
	TestTrue(TEXT("Autumn starts on 22 Sep"), FMath::IsNearlyEqual(AutumnStart.RealDayOfYear, 265.0f, 0.01f));
	TestTrue(TEXT("Autumn start month ~9.73"), FMath::IsNearlyEqual(AutumnStart.Month, 9.0f + 22.0f / 30.0f, 0.01f));

	// Months are monotonic over a whole year (except the wrap from December to January).
	float PreviousMonth = 0.0f;
	int32 Wraps = 0;
	for (int32 Step = 0; Step < 24 * 24; ++Step)
	{
		const float Month = CalendarMath::MonthAt(Config, Step * 3600.0);
		if (Month < PreviousMonth)
		{
			++Wraps;
		}
		PreviousMonth = Month;
	}
	TestEqual(TEXT("Month wraps exactly once per year"), Wraps, 1);

	TestTrue(TEXT("Biological conversion"), FMath::IsNearlyEqual(CalendarMath::BiologicalDaysToGameDays(Config, 10.0), 2.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCalendarSunTest, "ShroomHunt.Time.Calendar.SunPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCalendarSunTest::RunTest(const FString& Parameters)
{
	constexpr float Latitude = 47.0f;
	constexpr float Tolerance = 0.6f;

	const float SummerSolstice = CalendarMath::SeasonProgressToRealDayOfYear(ESeason::Summer, 0.0f);
	const float WinterSolstice = CalendarMath::SeasonProgressToRealDayOfYear(ESeason::Winter, 0.0f);
	const float Equinox = CalendarMath::SeasonProgressToRealDayOfYear(ESeason::Spring, 0.0f);

	const FCelestialPosition SummerNoon = CalendarMath::ComputeSunPosition(Latitude, SummerSolstice, 12.0f);
	const FCelestialPosition WinterNoon = CalendarMath::ComputeSunPosition(Latitude, WinterSolstice, 12.0f);
	const FCelestialPosition EquinoxNoon = CalendarMath::ComputeSunPosition(Latitude, Equinox, 12.0f);
	const FCelestialPosition EquinoxDawn = CalendarMath::ComputeSunPosition(Latitude, Equinox, 6.0f);
	const FCelestialPosition Midnight = CalendarMath::ComputeSunPosition(Latitude, SummerSolstice, 0.0f);

	TestTrue(TEXT("Summer noon elevation = 90 - lat + 23.44"), FMath::IsNearlyEqual(SummerNoon.ElevationDeg, 66.44f, Tolerance));
	TestTrue(TEXT("Winter noon elevation = 90 - lat - 23.44"), FMath::IsNearlyEqual(WinterNoon.ElevationDeg, 19.56f, Tolerance));
	TestTrue(TEXT("Equinox noon elevation = 90 - lat"), FMath::IsNearlyEqual(EquinoxNoon.ElevationDeg, 43.0f, Tolerance));
	TestTrue(TEXT("Noon sun is due south"), FMath::IsNearlyEqual(EquinoxNoon.AzimuthDeg, 180.0f, 1.0f));
	TestTrue(TEXT("Equinox sunrise is near the horizon"), FMath::Abs(EquinoxDawn.ElevationDeg) < 1.5f);
	TestTrue(TEXT("Equinox sunrise is due east"), FMath::IsNearlyEqual(EquinoxDawn.AzimuthDeg, 90.0f, 2.0f));
	TestTrue(TEXT("Sun is below the horizon at midnight"), Midnight.ElevationDeg < 0.0f);

	const FVector Up = CalendarMath::DirectionTowards(FCelestialPosition{ 90.0f, 0.0f, 1.0f });
	TestTrue(TEXT("Zenith direction is +Z"), Up.Equals(FVector::UpVector, 1e-4));

	const FCelestialPosition FullMoon = CalendarMath::ComputeMoonPosition(Latitude, Equinox, 0.0f, 0.5f);
	TestTrue(TEXT("Full moon is up at midnight"), FullMoon.ElevationDeg > 20.0f);
	TestTrue(TEXT("Full moon is fully lit"), FMath::IsNearlyEqual(FullMoon.Illumination, 1.0f, 0.001f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
