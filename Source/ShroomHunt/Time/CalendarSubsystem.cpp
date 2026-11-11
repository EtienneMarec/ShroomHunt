#include "Time/CalendarSubsystem.h"

#include "Core/ShroomLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Time/CalendarMath.h"
#include "Time/TimeSettings.h"

void UCalendarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UTimeSettings* Settings = GetDefault<UTimeSettings>();
	CalendarConfig = Settings->MakeCalendarConfig();
	LatitudeDeg = Settings->LatitudeDeg;
	LunarCycleGameDays = Settings->LunarCycleGameDays;

	TotalGameSeconds = CalendarMath::EncodeDate(CalendarConfig, 1, Settings->StartSeason, Settings->StartDayInSeason, Settings->StartHour);
	CurrentDate = CalendarMath::DecodeDate(CalendarConfig, TotalGameSeconds);
	bInitialized = true;

	UE_LOG(LogShroomTime, Log, TEXT("Calendar initialized at %s (time scale x%.1f, year = %d days)"),
		*CurrentDate.ToDebugString(CalendarConfig), CalendarConfig.GetBaseTimeScale(), CalendarConfig.GetYearLengthDays());
}

void UCalendarSubsystem::Deinitialize()
{
	bInitialized = false;
	Super::Deinitialize();
}

void UCalendarSubsystem::Tick(float DeltaTime)
{
	if (bTimePaused)
	{
		return;
	}
	TotalGameSeconds += static_cast<double>(DeltaTime) * static_cast<double>(GetEffectiveTimeScale());
	RefreshDateAndNotify();
}

bool UCalendarSubsystem::IsTickable() const
{
	if (!bInitialized || IsTemplate())
	{
		return false;
	}
	const UWorld* World = GetTickableGameObjectWorld();
	return World != nullptr && World->IsGameWorld() && !World->IsPaused();
}

TStatId UCalendarSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCalendarSubsystem, STATGROUP_Tickables);
}

UWorld* UCalendarSubsystem::GetTickableGameObjectWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

void UCalendarSubsystem::SetTotalGameSeconds(double NewTotalGameSeconds)
{
	TotalGameSeconds = FMath::Max(0.0, NewTotalGameSeconds);
	RefreshDateAndNotify();
}

void UCalendarSubsystem::SetHour(float Hour)
{
	const double DayStart = FMath::FloorToDouble(TotalGameSeconds / GameSecondsPerDay) * GameSecondsPerDay;
	double Target = DayStart + static_cast<double>(FMath::Clamp(Hour, 0.0f, 23.999f)) * 3600.0;
	if (Target <= TotalGameSeconds)
	{
		Target += GameSecondsPerDay;
	}
	UE_LOG(LogShroomTime, Log, TEXT("Time set to %.2f h"), Hour);
	SetTotalGameSeconds(Target);
}

void UCalendarSubsystem::SkipDays(int32 Days)
{
	if (Days > 0)
	{
		UE_LOG(LogShroomTime, Log, TEXT("Skipping %d day(s)"), Days);
		SetTotalGameSeconds(TotalGameSeconds + static_cast<double>(Days) * GameSecondsPerDay);
	}
}

void UCalendarSubsystem::SetDebugTimeMultiplier(float Multiplier)
{
	DebugTimeMultiplier = FMath::Clamp(Multiplier, 0.0f, 1000.0f);
	UE_LOG(LogShroomTime, Log, TEXT("Debug time multiplier = %.2f (effective x%.1f)"), DebugTimeMultiplier, GetEffectiveTimeScale());
}

float UCalendarSubsystem::GetEffectiveTimeScale() const
{
	return static_cast<float>(CalendarConfig.GetBaseTimeScale()) * DebugTimeMultiplier;
}

void UCalendarSubsystem::RefreshDateAndNotify()
{
	const FGameDate Previous = CurrentDate;
	CurrentDate = CalendarMath::DecodeDate(CalendarConfig, TotalGameSeconds);

	if (CurrentDate.DayIndex != Previous.DayIndex)
	{
		UE_LOG(LogShroomTime, Verbose, TEXT("New day: %s"), *CurrentDate.ToDebugString(CalendarConfig));
		OnDayStarted.Broadcast(CurrentDate.DayIndex);
	}
	if (CurrentDate.Season != Previous.Season || CurrentDate.Year != Previous.Year)
	{
		UE_LOG(LogShroomTime, Log, TEXT("Season changed: %s"), *CurrentDate.ToDebugString(CalendarConfig));
		OnSeasonChanged.Broadcast(CurrentDate.Season);
	}
	const int32 PreviousHour = FMath::FloorToInt(Previous.Hour);
	const int32 NewHour = FMath::FloorToInt(CurrentDate.Hour);
	if (NewHour != PreviousHour || CurrentDate.DayIndex != Previous.DayIndex)
	{
		OnHourChanged.Broadcast(NewHour);
	}
}
