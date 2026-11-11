// Debug console commands (open the console in game with the ` or ² key).
// Registered as dotted "sh.*" commands through IConsoleManager.

#include "Core/ShroomLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Time/CalendarSubsystem.h"
#include "Weather/WeatherSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace
{
	UCalendarSubsystem* FindCalendar(UWorld* World)
	{
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		UCalendarSubsystem* Calendar = GameInstance ? GameInstance->GetSubsystem<UCalendarSubsystem>() : nullptr;
		if (!Calendar)
		{
			UE_LOG(LogShroomDebug, Warning, TEXT("No calendar: run this command while playing (PIE)."));
		}
		return Calendar;
	}

	UWeatherSubsystem* FindWeather(UWorld* World)
	{
		UWeatherSubsystem* Weather = World ? World->GetSubsystem<UWeatherSubsystem>() : nullptr;
		if (!Weather)
		{
			UE_LOG(LogShroomDebug, Warning, TEXT("No weather subsystem: run this command while playing (PIE)."));
		}
		return Weather;
	}

	bool ParseWeatherState(const FString& Text, EWeatherState& OutState)
	{
		const UEnum* Enum = StaticEnum<EWeatherState>();
		for (int32 Index = 0; Index < static_cast<int32>(EWeatherState::Count); ++Index)
		{
			if (Enum->GetNameStringByIndex(Index).Equals(Text, ESearchCase::IgnoreCase))
			{
				OutState = static_cast<EWeatherState>(Enum->GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	FString WeatherStateList()
	{
		const UEnum* Enum = StaticEnum<EWeatherState>();
		TArray<FString> Names;
		for (int32 Index = 0; Index < static_cast<int32>(EWeatherState::Count); ++Index)
		{
			Names.Add(Enum->GetNameStringByIndex(Index));
		}
		return FString::Join(Names, TEXT(", "));
	}

	FAutoConsoleCommandWithWorldAndArgs CmdTimeScale(
		TEXT("sh.Time.Scale"),
		TEXT("sh.Time.Scale <multiplier>: speeds up game time (1 = normal, 10 = ten times faster, 0 = frozen)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UCalendarSubsystem* Calendar = FindCalendar(World))
			{
				Calendar->SetDebugTimeMultiplier(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.0f);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdTimeSet(
		TEXT("sh.Time.Set"),
		TEXT("sh.Time.Set <hour>: jumps forward to the given hour (0-24), e.g. sh.Time.Set 21.5"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UCalendarSubsystem* Calendar = FindCalendar(World);
			if (Calendar && Args.Num() > 0)
			{
				Calendar->SetHour(FCString::Atof(*Args[0]));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdTimeSkipDays(
		TEXT("sh.Time.SkipDays"),
		TEXT("sh.Time.SkipDays <days>: advances time by whole days."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UCalendarSubsystem* Calendar = FindCalendar(World))
			{
				Calendar->SkipDays(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdTimePause(
		TEXT("sh.Time.Pause"),
		TEXT("sh.Time.Pause: toggles the game clock (the game itself keeps running)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UCalendarSubsystem* Calendar = FindCalendar(World))
			{
				Calendar->SetTimePaused(!Calendar->IsTimePaused());
				UE_LOG(LogShroomDebug, Log, TEXT("Game clock %s"), Calendar->IsTimePaused() ? TEXT("paused") : TEXT("running"));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdWeatherForce(
		TEXT("sh.Weather.Force"),
		TEXT("sh.Weather.Force <State> [game hours = 3]: forces a weather state. States: Clear, PartlyCloudy, Overcast, Fog, Drizzle, Rain, HeavyRain, Storm."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UWeatherSubsystem* Weather = FindWeather(World);
			if (!Weather)
			{
				return;
			}
			EWeatherState State;
			if (Args.Num() == 0 || !ParseWeatherState(Args[0], State))
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("Usage: sh.Weather.Force <State> [hours]. States: %s"), *WeatherStateList());
				return;
			}
			const float Hours = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 3.0f;
			Weather->ForceWeather(State, Hours);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdWeatherClear(
		TEXT("sh.Weather.Clear"),
		TEXT("sh.Weather.Clear: removes forced weather and returns to the natural weather."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UWeatherSubsystem* Weather = FindWeather(World))
			{
				Weather->ClearForcedWeather();
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
