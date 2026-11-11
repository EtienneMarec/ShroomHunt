#include "Debug/ShroomDebugSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Time/CalendarSubsystem.h"
#include "Weather/WeatherSubsystem.h"

namespace
{
	int32 GDebugHUD = 1;
	FAutoConsoleVariableRef CVarDebugHUD(
		TEXT("sh.Debug.HUD"),
		GDebugHUD,
		TEXT("Shows the ShroomHunt debug HUD (date, weather). 0 = off, 1 = on."),
		ECVF_Cheat);

	/** Window used for the "biological" rain readout: 15 real days, like the cep's rain window. */
	constexpr double ReferenceRainWindowRealDays = 15.0;

	// Stable keys so messages are replaced in place instead of stacking.
	constexpr uint64 KeyBase = 0x5348524F4F4D0000ULL; // "SHROOM"
}

bool UShroomDebugSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
#endif
}

TStatId UShroomDebugSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UShroomDebugSubsystem, STATGROUP_Tickables);
}

void UShroomDebugSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (GDebugHUD == 0 || !GEngine)
	{
		return;
	}

	const UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	const UCalendarSubsystem* Calendar = GameInstance ? GameInstance->GetSubsystem<UCalendarSubsystem>() : nullptr;
	const UWeatherSubsystem* Weather = GetWorld()->GetSubsystem<UWeatherSubsystem>();
	if (!Calendar || !Weather)
	{
		return;
	}

	const FCalendarConfig& Config = Calendar->GetCalendarConfig();
	const double Now = Calendar->GetTotalGameSeconds();

	// History queries integrate the model: refresh them a few times per second only.
	RefreshTimer -= DeltaTime;
	if (RefreshTimer <= 0.0f)
	{
		RefreshTimer = 0.5f;
		CachedRain24h = Weather->GetRainfallMm(Now - GameSecondsPerDay, Now);
		const double WindowGameSeconds = ReferenceRainWindowRealDays / Config.BiologicalDaysPerGameDay * GameSecondsPerDay;
		CachedRainWindow = Weather->GetRainfallMm(Now - WindowGameSeconds, Now);
	}

	const FWeatherSample Sample = Weather->GetCurrentSample();
	const FString StateText = StaticEnum<EWeatherState>()->GetNameStringByValue(static_cast<int64>(Weather->GetCurrentState()));

	int32 ActiveOverrides = 0;
	for (const FWeatherOverride& Override : Weather->GetOverrides())
	{
		if (Now >= Override.StartGameSeconds && Now <= Override.EndGameSeconds)
		{
			++ActiveOverrides;
		}
	}

	const FColor Header(255, 220, 120);
	const FColor Body(220, 220, 220);
	const float Duration = 0.0f; // shown for one frame, refreshed every tick

	// AddOnScreenDebugMessage with the same key replaces the line. Lines are drawn in key order (descending).
	GEngine->AddOnScreenDebugMessage(KeyBase + 4, Duration, Header,
		FString::Printf(TEXT("[ShroomHunt] %s   x%.0f%s%s   (month %.2f, day %d)"),
			*Calendar->GetDate().ToDebugString(Config), Calendar->GetEffectiveTimeScale(),
			Calendar->GetDebugTimeMultiplier() != 1.0f ? *FString::Printf(TEXT(" (debug x%.1f)"), Calendar->GetDebugTimeMultiplier()) : TEXT(""),
			Calendar->IsTimePaused() ? TEXT(" PAUSED") : TEXT(""),
			Calendar->GetDate().Month, Calendar->GetDate().DayIndex));

	GEngine->AddOnScreenDebugMessage(KeyBase + 3, Duration, Body,
		FString::Printf(TEXT("Weather %s%s | cloud %.2f  rain %.2f mm/h  fog %.2f  storm %.2f"),
			*StateText, ActiveOverrides > 0 ? TEXT(" (forced)") : TEXT(""),
			Sample.CloudCover, Sample.PrecipitationMmPerHour, Sample.FogDensity, Sample.StormIntensity));

	GEngine->AddOnScreenDebugMessage(KeyBase + 2, Duration, Body,
		FString::Printf(TEXT("Air %.1f C  RH %.0f%%  wind %.1f m/s  ground wetness %.2f"),
			Sample.TemperatureC, Sample.Humidity * 100.0f, Sample.WindSpeed, Weather->GetWetness()));

	GEngine->AddOnScreenDebugMessage(KeyBase + 1, Duration, Body,
		FString::Printf(TEXT("Rain last game day %.1f mm | last %.2f game days (=15 bio days) %.1f mm -> R_eq %.0f mm"),
			CachedRain24h, ReferenceRainWindowRealDays / Config.BiologicalDaysPerGameDay, CachedRainWindow,
			CachedRainWindow * Config.BiologicalDaysPerGameDay));
}
