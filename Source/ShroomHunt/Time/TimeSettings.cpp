#include "Time/TimeSettings.h"

FCalendarConfig UTimeSettings::MakeCalendarConfig() const
{
	FCalendarConfig Config;
	Config.DayLengthRealSeconds = FMath::Max(1.0, static_cast<double>(DayLengthRealSeconds));
	Config.SeasonLengthDays[static_cast<int32>(ESeason::Spring)] = FMath::Max(1, SpringLengthDays);
	Config.SeasonLengthDays[static_cast<int32>(ESeason::Summer)] = FMath::Max(1, SummerLengthDays);
	Config.SeasonLengthDays[static_cast<int32>(ESeason::Autumn)] = FMath::Max(1, AutumnLengthDays);
	Config.SeasonLengthDays[static_cast<int32>(ESeason::Winter)] = FMath::Max(1, WinterLengthDays);
	Config.BiologicalDaysPerGameDay = FMath::Max(0.1f, BiologicalDaysPerGameDay);
	return Config;
}
