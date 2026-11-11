#include "Time/CalendarMath.h"

namespace
{
	// Real-world season starts (0-based day of year) and lengths: 20 Mar, 21 Jun, 22 Sep, 21 Dec.
	constexpr float RealSeasonStartDay[4] = { 79.0f, 171.0f, 265.0f, 354.0f };
	constexpr float RealSeasonLengthDays[4] = { 92.0f, 94.0f, 89.0f, 90.0f };

	// 0-based day of year at which each month starts (non-leap year), plus the year end.
	constexpr float MonthStartDay[13] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };

	constexpr float AxialTiltDeg = 23.44f;

	const TCHAR* SeasonName(ESeason Season)
	{
		switch (Season)
		{
		case ESeason::Spring: return TEXT("Spring");
		case ESeason::Summer: return TEXT("Summer");
		case ESeason::Autumn: return TEXT("Autumn");
		case ESeason::Winter: return TEXT("Winter");
		default:              return TEXT("?");
		}
	}

	/** Floor division that also works for negative numerators. */
	int32 FloorDiv(int32 A, int32 B)
	{
		const int32 Q = A / B;
		return (A % B != 0 && ((A < 0) != (B < 0))) ? Q - 1 : Q;
	}
}

FString FGameDate::ToDebugString(const FCalendarConfig& Config) const
{
	const int32 Minutes = FMath::Clamp(FMath::FloorToInt(Hour * 60.0f), 0, 24 * 60 - 1);
	return FString::Printf(TEXT("Y%d %s d%d/%d %02d:%02d"),
		Year, SeasonName(Season), DayInSeason + 1, Config.SeasonLengthDays[static_cast<int32>(Season)],
		Minutes / 60, Minutes % 60);
}

namespace CalendarMath
{
	FGameDate DecodeDate(const FCalendarConfig& Config, double TotalGameSeconds)
	{
		FGameDate Date;

		const double DayFloat = TotalGameSeconds / GameSecondsPerDay;
		const double DayFloor = FMath::FloorToDouble(DayFloat);
		Date.DayIndex = static_cast<int32>(DayFloor);
		Date.Hour = FMath::Clamp(static_cast<float>((DayFloat - DayFloor) * 24.0), 0.0f, 23.9999f);

		const int32 YearLength = FMath::Max(1, Config.GetYearLengthDays());
		const int32 YearIndex = FloorDiv(Date.DayIndex, YearLength);
		Date.Year = YearIndex + 1;
		Date.DayOfYear = Date.DayIndex - YearIndex * YearLength;

		int32 Remaining = Date.DayOfYear;
		int32 SeasonIndex = 0;
		while (SeasonIndex < 3 && Remaining >= Config.SeasonLengthDays[SeasonIndex])
		{
			Remaining -= Config.SeasonLengthDays[SeasonIndex];
			++SeasonIndex;
		}
		Date.Season = static_cast<ESeason>(SeasonIndex);
		Date.DayInSeason = Remaining;

		const float SeasonLength = static_cast<float>(FMath::Max(1, Config.SeasonLengthDays[SeasonIndex]));
		Date.SeasonProgress = FMath::Clamp((static_cast<float>(Remaining) + Date.Hour / 24.0f) / SeasonLength, 0.0f, 0.99999f);
		Date.RealDayOfYear = SeasonProgressToRealDayOfYear(Date.Season, Date.SeasonProgress);
		Date.Month = RealDayOfYearToMonth(Date.RealDayOfYear);
		return Date;
	}

	double EncodeDate(const FCalendarConfig& Config, int32 Year, ESeason Season, int32 DayInSeason, float Hour)
	{
		const int32 SeasonIndex = static_cast<int32>(Season);
		int32 SeasonStart = 0;
		for (int32 Index = 0; Index < SeasonIndex; ++Index)
		{
			SeasonStart += Config.SeasonLengthDays[Index];
		}
		const int32 ClampedDay = FMath::Clamp(DayInSeason, 0, FMath::Max(0, Config.SeasonLengthDays[SeasonIndex] - 1));
		const int32 DayIndex = (Year - 1) * Config.GetYearLengthDays() + SeasonStart + ClampedDay;
		return static_cast<double>(DayIndex) * GameSecondsPerDay + static_cast<double>(FMath::Clamp(Hour, 0.0f, 24.0f)) * 3600.0;
	}

	float SeasonProgressToRealDayOfYear(ESeason Season, float SeasonProgress)
	{
		const int32 Index = static_cast<int32>(Season);
		const float Day = RealSeasonStartDay[Index] + FMath::Clamp(SeasonProgress, 0.0f, 1.0f) * RealSeasonLengthDays[Index];
		return FMath::Fmod(Day, 365.0f);
	}

	float RealDayOfYearToMonth(float RealDayOfYear)
	{
		const float Day = FMath::Clamp(RealDayOfYear, 0.0f, 364.9999f);
		int32 MonthIndex = 0;
		while (MonthIndex < 11 && Day >= MonthStartDay[MonthIndex + 1])
		{
			++MonthIndex;
		}
		const float Start = MonthStartDay[MonthIndex];
		const float Length = MonthStartDay[MonthIndex + 1] - Start;
		return static_cast<float>(MonthIndex + 1) + (Day - Start) / Length;
	}

	float MonthAt(const FCalendarConfig& Config, double TotalGameSeconds)
	{
		return DecodeDate(Config, TotalGameSeconds).Month;
	}

	double BiologicalDaysToGameDays(const FCalendarConfig& Config, double RealDays)
	{
		return Config.BiologicalDaysPerGameDay > 0.0f ? RealDays / Config.BiologicalDaysPerGameDay : RealDays;
	}

	FCelestialPosition ComputeSunPosition(float LatitudeDeg, float RealDayOfYear, float SolarHour)
	{
		// Declination: 0 at the March equinox (day ~79), +23.44 deg at the June solstice.
		const float Declination = FMath::DegreesToRadians(AxialTiltDeg) * FMath::Sin(UE_TWO_PI * (RealDayOfYear - 79.0f) / 365.0f);
		const float Latitude = FMath::DegreesToRadians(LatitudeDeg);
		const float HourAngle = FMath::DegreesToRadians(15.0f * (SolarHour - 12.0f));

		// Sun vector in local East/North/Up coordinates.
		const float East = -FMath::Cos(Declination) * FMath::Sin(HourAngle);
		const float North = FMath::Sin(Declination) * FMath::Cos(Latitude) - FMath::Cos(Declination) * FMath::Cos(HourAngle) * FMath::Sin(Latitude);
		const float Up = FMath::Sin(Declination) * FMath::Sin(Latitude) + FMath::Cos(Declination) * FMath::Cos(HourAngle) * FMath::Cos(Latitude);

		FCelestialPosition Result;
		Result.ElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up, -1.0f, 1.0f)));
		float Azimuth = FMath::RadiansToDegrees(FMath::Atan2(East, North));
		if (Azimuth < 0.0f)
		{
			Azimuth += 360.0f;
		}
		Result.AzimuthDeg = Azimuth;
		Result.Illumination = 1.0f;
		return Result;
	}

	FCelestialPosition ComputeMoonPosition(float LatitudeDeg, float RealDayOfYear, float SolarHour, float MoonPhase)
	{
		// The moon lags the sun by Phase * 24 h; a full moon rides where the sun would be half a year later.
		const float ShiftedHour = FMath::Fmod(SolarHour - MoonPhase * 24.0f + 48.0f, 24.0f);
		const float ShiftedDay = FMath::Fmod(RealDayOfYear + MoonPhase * 365.0f, 365.0f);
		FCelestialPosition Result = ComputeSunPosition(LatitudeDeg, ShiftedDay, ShiftedHour);
		Result.Illumination = 0.5f * (1.0f - FMath::Cos(UE_TWO_PI * MoonPhase));
		return Result;
	}

	float MoonPhaseAt(double TotalGameSeconds, float LunarCycleGameDays)
	{
		if (LunarCycleGameDays <= 0.0f)
		{
			return 0.5f;
		}
		// Offset so that day 0 is not a new moon (the first nights are not pitch black).
		const double Cycles = TotalGameSeconds / GameSecondsPerDay / LunarCycleGameDays + 0.35;
		return static_cast<float>(Cycles - FMath::FloorToDouble(Cycles));
	}

	FVector DirectionTowards(const FCelestialPosition& Position)
	{
		const double Elevation = FMath::DegreesToRadians(static_cast<double>(Position.ElevationDeg));
		const double Azimuth = FMath::DegreesToRadians(static_cast<double>(Position.AzimuthDeg));
		return FVector(FMath::Cos(Elevation) * FMath::Cos(Azimuth), FMath::Cos(Elevation) * FMath::Sin(Azimuth), FMath::Sin(Elevation));
	}
}
