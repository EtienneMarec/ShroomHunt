#include "Weather/WeatherSubsystem.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Time/CalendarSubsystem.h"
#include "Time/TimeSettings.h"
#include "Weather/WeatherSettings.h"

namespace
{
	constexpr float WetnessRefreshRealSeconds = 0.5f;
	constexpr double LightningBucketGameSeconds = 120.0;
	constexpr int32 WetnessSteps = 12;

	FString StateName(EWeatherState State)
	{
		return StaticEnum<EWeatherState>()->GetNameStringByValue(static_cast<int64>(State));
	}
}

bool UWeatherSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UWeatherSettings* Settings = GetDefault<UWeatherSettings>();
	SetClimate(static_cast<uint32>(Settings->DefaultSeed), Settings->DefaultClimate);
}

void UWeatherSubsystem::SetClimate(uint32 NewSeed, const FClimateProfile& NewClimate)
{
	const UCalendarSubsystem* Calendar = GetCalendar();
	const FCalendarConfig CalendarConfig = Calendar ? Calendar->GetCalendarConfig() : GetDefault<UTimeSettings>()->MakeCalendarConfig();
	Model = FWeatherModel(NewSeed, NewClimate, CalendarConfig);
	bHasState = false;
	UE_LOG(LogShroomWeather, Log, TEXT("Weather model set: seed %u"), NewSeed);
}

TStatId UWeatherSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWeatherSubsystem, STATGROUP_Tickables);
}

UCalendarSubsystem* UWeatherSubsystem::GetCalendar() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCalendarSubsystem>() : nullptr;
}

double UWeatherSubsystem::GetNowGameSeconds() const
{
	const UCalendarSubsystem* Calendar = GetCalendar();
	return Calendar ? Calendar->GetTotalGameSeconds() : 0.0;
}

void UWeatherSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const double Now = GetNowGameSeconds();
	CurrentSample = Model.Sample(Now, Overrides);

	// Debounced discrete state: a candidate must hold for a moment before it is reported.
	const EWeatherState Candidate = FWeatherModel::Classify(CurrentSample);
	if (!bHasState)
	{
		CurrentState = PendingState = Candidate;
		bHasState = true;
		UE_LOG(LogShroomWeather, Log, TEXT("Initial weather: %s"), *StateName(CurrentState));
	}
	else if (Candidate != CurrentState)
	{
		if (Candidate != PendingState)
		{
			PendingState = Candidate;
			PendingStateTime = 0.0f;
		}
		PendingStateTime += DeltaTime;
		if (PendingStateTime >= GetDefault<UWeatherSettings>()->StateDebounceRealSeconds)
		{
			const EWeatherState Old = CurrentState;
			CurrentState = Candidate;
			UE_LOG(LogShroomWeather, Log, TEXT("Weather: %s -> %s"), *StateName(Old), *StateName(CurrentState));
			OnWeatherStateChanged.Broadcast(Old, CurrentState);
		}
	}
	else
	{
		PendingState = CurrentState;
		PendingStateTime = 0.0f;
	}

	WetnessTimer -= DeltaTime;
	if (WetnessTimer <= 0.0f)
	{
		WetnessTimer = WetnessRefreshRealSeconds;
		UpdateWetness(Now);
	}

	UpdateLightning(Now);
}

void UWeatherSubsystem::UpdateWetness(double Now)
{
	// Recency-weighted rain over the window: recent rain counts fully, older rain less (drying).
	const UWeatherSettings* Settings = GetDefault<UWeatherSettings>();
	const double Window = FMath::Max(1.0f, Settings->WetnessWindowGameHours) * 3600.0;
	const double Step = Window / WetnessSteps;
	double Weighted = 0.0;
	for (int32 Index = 0; Index < WetnessSteps; ++Index)
	{
		const double Mid = Now - (Index + 0.5) * Step;
		const double Weight = 1.0 - 0.8 * (static_cast<double>(Index) / (WetnessSteps - 1));
		Weighted += Model.Sample(Mid, Overrides).PrecipitationMmPerHour * (Step / 3600.0) * Weight;
	}
	Wetness = FMath::Clamp(static_cast<float>(Weighted / FMath::Max(0.1f, Settings->WetnessSaturationMm)), 0.0f, 1.0f);
}

void UWeatherSubsystem::UpdateLightning(double Now)
{
	const int64 Bucket = static_cast<int64>(FMath::FloorToDouble(Now / LightningBucketGameSeconds));
	if (Bucket == LastLightningBucket)
	{
		return;
	}
	LastLightningBucket = Bucket;
	if (CurrentSample.StormIntensity < 0.3f)
	{
		return;
	}
	const uint32 H = ShroomHash::Hash64(ShroomHash::Hash(Model.GetSeed(), 0x11u), Bucket);
	if (ShroomHash::ToUnitFloat(H) < 0.2f * CurrentSample.StormIntensity)
	{
		const float Distance01 = ShroomHash::ToUnitFloat(ShroomHash::Mix(H));
		OnLightningStrike.Broadcast(Distance01);
	}
}

float UWeatherSubsystem::GetRainLastHours(float GameHours) const
{
	const double Now = GetNowGameSeconds();
	return GetRainfallMm(Now - static_cast<double>(FMath::Max(0.0f, GameHours)) * 3600.0, Now);
}

float UWeatherSubsystem::GetRainfallMm(double FromGameSeconds, double ToGameSeconds) const
{
	return Model.IntegrateRainMm(FromGameSeconds, ToGameSeconds, Overrides);
}

float UWeatherSubsystem::GetMeanTemperatureC(double FromGameSeconds, double ToGameSeconds) const
{
	return Model.MeanTemperatureC(FromGameSeconds, ToGameSeconds, Overrides);
}

void UWeatherSubsystem::ForceWeather(EWeatherState State, float DurationGameHours)
{
	if (State == EWeatherState::Count)
	{
		return;
	}
	const double Now = GetNowGameSeconds();
	FWeatherOverride Override;
	Override.State = State;
	Override.StartGameSeconds = Now;
	Override.EndGameSeconds = Now + static_cast<double>(FMath::Max(0.1f, DurationGameHours)) * 3600.0;
	Overrides.Add(Override);
	UE_LOG(LogShroomWeather, Log, TEXT("Forcing %s for %.1f game hours"), *StateName(State), DurationGameHours);
}

void UWeatherSubsystem::ClearForcedWeather()
{
	const double Now = GetNowGameSeconds();
	for (int32 Index = Overrides.Num() - 1; Index >= 0; --Index)
	{
		FWeatherOverride& Override = Overrides[Index];
		if (Override.StartGameSeconds >= Now)
		{
			Overrides.RemoveAt(Index);
		}
		else if (Override.EndGameSeconds > Now)
		{
			// Keep the past part in the rain history; end it now (it blends out quickly).
			Override.EndGameSeconds = Now;
		}
	}
	UE_LOG(LogShroomWeather, Log, TEXT("Forced weather cleared"));
}
