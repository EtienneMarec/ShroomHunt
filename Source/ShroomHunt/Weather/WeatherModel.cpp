#include "Weather/WeatherModel.h"

#include "Core/ShroomHash.h"
#include "Time/CalendarMath.h"

namespace
{
	// Independent noise channels (salts) so that each variable has its own random stream.
	namespace Channel
	{
		constexpr uint32 Synoptic = 1;
		constexpr uint32 Showers = 2;
		constexpr uint32 StormDay = 3;
		constexpr uint32 TemperatureDrift = 4;
		constexpr uint32 Wind = 5;
		constexpr uint32 FogDay = 6;
		constexpr uint32 CloudDetail = 7;
		constexpr uint32 FogDetail = 8;
	}

	float SmoothStep01(float Edge0, float Edge1, float X)
	{
		if (Edge1 <= Edge0)
		{
			return X >= Edge1 ? 1.0f : 0.0f;
		}
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	/**
	 * Converts fBm noise (roughly Gaussian, std ~0.3) into an approximately
	 * uniform value in [0, 1], so climate fractions translate into time fractions.
	 */
	float NoiseToUniform(float Noise)
	{
		return 1.0f / (1.0f + FMath::Exp(-5.6f * Noise));
	}

	float CircularMonthLerp(const TArray<float>& Values, float Month, float Fallback)
	{
		if (Values.Num() != 12)
		{
			return Fallback;
		}
		// Each monthly value sits at the middle of its month (January -> 1.5).
		float X = Month - 1.5f;
		X = FMath::Fmod(X + 24.0f, 12.0f);
		const int32 Index0 = FMath::FloorToInt(X) % 12;
		const int32 Index1 = (Index0 + 1) % 12;
		const float T = X - FMath::FloorToFloat(X);
		return FMath::Lerp(Values[Index0], Values[Index1], T);
	}
}

FWeatherSample FWeatherSample::Lerp(const FWeatherSample& A, const FWeatherSample& B, float Alpha)
{
	FWeatherSample Out;
	Out.CloudCover = FMath::Lerp(A.CloudCover, B.CloudCover, Alpha);
	Out.PrecipitationMmPerHour = FMath::Lerp(A.PrecipitationMmPerHour, B.PrecipitationMmPerHour, Alpha);
	Out.FogDensity = FMath::Lerp(A.FogDensity, B.FogDensity, Alpha);
	Out.WindSpeed = FMath::Lerp(A.WindSpeed, B.WindSpeed, Alpha);
	Out.TemperatureC = FMath::Lerp(A.TemperatureC, B.TemperatureC, Alpha);
	Out.Humidity = FMath::Lerp(A.Humidity, B.Humidity, Alpha);
	Out.StormIntensity = FMath::Lerp(A.StormIntensity, B.StormIntensity, Alpha);
	return Out;
}

FClimateProfile::FClimateProfile()
{
	// Lowland temperate France (oak-beech forest, ~150 m), January -> December.
	MonthlyMeanTemperatureC = { 4.0f, 5.0f, 8.0f, 11.0f, 14.5f, 17.5f, 19.5f, 19.5f, 16.0f, 12.0f, 7.5f, 4.5f };
	MonthlyDiurnalRangeC    = { 6.0f, 7.0f, 9.0f, 11.0f, 11.5f, 12.0f, 12.5f, 12.0f, 11.0f, 9.0f, 7.0f, 6.0f };
	MonthlyRainFraction     = { 0.14f, 0.13f, 0.12f, 0.12f, 0.12f, 0.10f, 0.08f, 0.09f, 0.10f, 0.13f, 0.14f, 0.14f };
	MonthlyFogPropensity    = { 0.25f, 0.20f, 0.15f, 0.10f, 0.08f, 0.05f, 0.05f, 0.08f, 0.18f, 0.28f, 0.32f, 0.30f };
	MonthlyStormPropensity  = { 0.00f, 0.00f, 0.00f, 0.02f, 0.06f, 0.12f, 0.15f, 0.13f, 0.06f, 0.01f, 0.00f, 0.00f };
}

FClimateAtMonth FClimateProfile::Evaluate(float Month) const
{
	FClimateAtMonth Out;
	Out.MeanTemperatureC = CircularMonthLerp(MonthlyMeanTemperatureC, Month, Out.MeanTemperatureC);
	Out.DiurnalRangeC = CircularMonthLerp(MonthlyDiurnalRangeC, Month, Out.DiurnalRangeC);
	Out.RainFraction = FMath::Clamp(CircularMonthLerp(MonthlyRainFraction, Month, Out.RainFraction), 0.0f, 0.95f);
	Out.FogPropensity = FMath::Clamp(CircularMonthLerp(MonthlyFogPropensity, Month, Out.FogPropensity), 0.0f, 1.0f);
	Out.StormPropensity = FMath::Clamp(CircularMonthLerp(MonthlyStormPropensity, Month, Out.StormPropensity), 0.0f, 1.0f);
	return Out;
}

FWeatherModel::FWeatherModel(uint32 InSeed, const FClimateProfile& InClimate, const FCalendarConfig& InCalendar)
	: Seed(InSeed)
	, Climate(InClimate)
	, Calendar(InCalendar)
{
}

FWeatherSample FWeatherModel::SampleNatural(double GameSeconds) const
{
	using namespace ShroomHash;

	const double Days = GameSeconds / GameSecondsPerDay;
	const double Hours = GameSeconds / 3600.0;
	const float Hour = static_cast<float>(FMath::Fmod(Hours, 24.0));
	const int32 DayIndex = static_cast<int32>(FMath::FloorToDouble(Days));
	const FClimateAtMonth C = Climate.Evaluate(CalendarMath::MonthAt(Calendar, GameSeconds));

	FWeatherSample Out;

	// --- Synoptic regime: one smooth signal ~ "low pressure" (wet) vs "high pressure" (dry).
	const float Synoptic = Fbm1D(Hash(Seed, Channel::Synoptic), Days / FMath::Max(0.25f, Climate.SynopticPeriodGameDays), 3);
	const float Regime = NoiseToUniform(Synoptic);
	const float RainThreshold = 1.0f - C.RainFraction;
	const float RainRegime = Regime > RainThreshold ? (Regime - RainThreshold) / FMath::Max(0.01f, C.RainFraction) : 0.0f;

	// --- Clouds build up before the rain and stay high during it; small hourly detail breaks them up.
	const float CloudDetail = 0.08f * Fbm1D(Hash(Seed, Channel::CloudDetail), Hours / 3.0, 2);
	float Cloud = Regime >= RainThreshold
		? 0.85f + 0.15f * RainRegime
		: 0.85f * SmoothStep01(RainThreshold - 0.55f, RainThreshold, Regime);
	Cloud = FMath::Clamp(Cloud + CloudDetail, 0.0f, 1.0f);

	// --- Rain: intensity grows with the regime depth, modulated by hourly showers.
	const float Showers = 0.5f + 0.5f * Fbm1D(Hash(Seed, Channel::Showers), Hours / 1.5, 2);
	float Precipitation = RainRegime > 0.0f
		? Climate.MaxRainRateMmPerHour * FMath::Square(RainRegime) * (0.35f + 0.65f * FMath::Clamp(Showers, 0.0f, 1.0f))
		: 0.0f;

	// --- Temperature: monthly mean + slow drift + diurnal cycle damped by clouds (max ~15 h, min ~3 h).
	const float Drift = 2.5f * Fbm1D(Hash(Seed, Channel::TemperatureDrift), Days / 5.0, 2);
	const float Diurnal = 0.5f * C.DiurnalRangeC * FMath::Cos(UE_TWO_PI * (Hour - 15.0f) / 24.0f) * (1.0f - 0.6f * Cloud);
	float Temperature = C.MeanTemperatureC + Drift + Diurnal;

	// --- Thunderstorms: some warm days are "storm days"; cells peak in the late afternoon.
	const float StormRoll = ToUnitFloat(Hash(Seed, Channel::StormDay, static_cast<uint32>(DayIndex)));
	float Storm = 0.0f;
	if (StormRoll < C.StormPropensity)
	{
		const float Afternoon = FMath::Exp(-FMath::Square((Hour - 16.5f) / 2.5f));
		const float Warmth = SmoothStep01(15.0f, 22.0f, C.MeanTemperatureC + Drift);
		Storm = FMath::Clamp(Afternoon * Warmth * 1.3f, 0.0f, 1.0f);
	}
	if (Storm > 0.05f)
	{
		Cloud = FMath::Max(Cloud, 0.6f + 0.4f * Storm);
		Precipitation = FMath::Max(Precipitation, Storm * Climate.MaxRainRateMmPerHour * 2.0f);
	}

	// Rain cools the air.
	Temperature -= 1.5f * FMath::Clamp(Precipitation / 4.0f, 0.0f, 1.0f);

	// --- Wind: calm under high pressure, windier with fronts and storms.
	const float Gusts = FMath::Abs(Fbm1D(Hash(Seed, Channel::Wind), Hours / 2.0, 2));
	const float WindSpeed = FMath::Clamp(1.0f + 6.0f * Gusts + 5.0f * RainRegime + 10.0f * Storm, 0.0f, 25.0f);

	// --- Humidity: higher with clouds, rain and at night.
	const float Night = 0.5f + 0.5f * FMath::Cos(UE_TWO_PI * (Hour - 3.0f) / 24.0f);
	const float Humidity = FMath::Clamp(0.5f + 0.25f * Cloud + 0.2f * FMath::Clamp(Precipitation, 0.0f, 1.0f) + 0.15f * Night, 0.0f, 1.0f);

	// --- Fog: some calm mornings are foggy. Evening hours belong to the next morning's roll.
	const int32 FogDayIndex = static_cast<int32>(FMath::FloorToDouble((GameSeconds + 4.0 * 3600.0) / GameSecondsPerDay));
	const float FogRoll = ToUnitFloat(Hash(Seed, Channel::FogDay, static_cast<uint32>(FogDayIndex)));
	const float FogChance = C.FogPropensity * FMath::Clamp(1.2f - WindSpeed / 8.0f, 0.0f, 1.0f);
	float Fog = 0.0f;
	if (FogRoll < FogChance)
	{
		const float Window = Hour < 12.0f
			? 1.0f - SmoothStep01(8.0f, 11.0f, Hour)
			: SmoothStep01(19.0f, 23.0f, Hour);
		const float Detail = 0.75f + 0.25f * Fbm1D(Hash(Seed, Channel::FogDetail), Hours / 1.0, 2);
		Fog = Window * Detail * (1.0f - FMath::Clamp(Precipitation / 2.0f, 0.0f, 1.0f));
	}
	// Light haze when the air is humid.
	Fog = FMath::Clamp(FMath::Max(Fog, 0.15f * SmoothStep01(0.8f, 1.0f, Humidity)), 0.0f, 1.0f);

	Out.CloudCover = Cloud;
	Out.PrecipitationMmPerHour = FMath::Max(0.0f, Precipitation);
	Out.FogDensity = Fog;
	Out.WindSpeed = WindSpeed;
	Out.TemperatureC = Temperature;
	Out.Humidity = Humidity;
	Out.StormIntensity = Storm;
	return Out;
}

FWeatherSample FWeatherModel::Sample(double GameSeconds, TConstArrayView<FWeatherOverride> Overrides) const
{
	FWeatherSample Result = SampleNatural(GameSeconds);
	for (const FWeatherOverride& Override : Overrides)
	{
		if (GameSeconds < Override.StartGameSeconds || GameSeconds > Override.EndGameSeconds)
		{
			continue;
		}
		const double Duration = Override.EndGameSeconds - Override.StartGameSeconds;
		const double Blend = FMath::Min(OverrideBlendGameSeconds, Duration * 0.5);
		const double FromStart = GameSeconds - Override.StartGameSeconds;
		const double ToEnd = Override.EndGameSeconds - GameSeconds;
		const float Weight = Blend > 0.0
			? static_cast<float>(FMath::Clamp(FMath::Min(FromStart, ToEnd) / Blend, 0.0, 1.0))
			: 1.0f;
		const float SmoothWeight = Weight * Weight * (3.0f - 2.0f * Weight);
		Result = FWeatherSample::Lerp(Result, MakeStatePreset(Override.State, Result), SmoothWeight);
	}
	return Result;
}

float FWeatherModel::IntegrateRainMm(double FromGameSeconds, double ToGameSeconds, TConstArrayView<FWeatherOverride> Overrides, double StepGameSeconds) const
{
	if (ToGameSeconds <= FromGameSeconds || StepGameSeconds <= 0.0)
	{
		return 0.0f;
	}
	const int32 Steps = FMath::Max(1, FMath::CeilToInt((ToGameSeconds - FromGameSeconds) / StepGameSeconds));
	const double Step = (ToGameSeconds - FromGameSeconds) / Steps;
	double TotalMm = 0.0;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		const double Mid = FromGameSeconds + (Index + 0.5) * Step;
		TotalMm += Sample(Mid, Overrides).PrecipitationMmPerHour * (Step / 3600.0);
	}
	return static_cast<float>(TotalMm);
}

float FWeatherModel::MeanTemperatureC(double FromGameSeconds, double ToGameSeconds, TConstArrayView<FWeatherOverride> Overrides, double StepGameSeconds) const
{
	if (ToGameSeconds <= FromGameSeconds || StepGameSeconds <= 0.0)
	{
		return Sample(FromGameSeconds, Overrides).TemperatureC;
	}
	const int32 Steps = FMath::Max(1, FMath::CeilToInt((ToGameSeconds - FromGameSeconds) / StepGameSeconds));
	const double Step = (ToGameSeconds - FromGameSeconds) / Steps;
	double Sum = 0.0;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		Sum += Sample(FromGameSeconds + (Index + 0.5) * Step, Overrides).TemperatureC;
	}
	return static_cast<float>(Sum / Steps);
}

EWeatherState FWeatherModel::Classify(const FWeatherSample& Sample)
{
	if (Sample.StormIntensity >= 0.35f)
	{
		return EWeatherState::Storm;
	}
	if (Sample.PrecipitationMmPerHour >= 3.0f)
	{
		return EWeatherState::HeavyRain;
	}
	if (Sample.PrecipitationMmPerHour >= 0.8f)
	{
		return EWeatherState::Rain;
	}
	if (Sample.PrecipitationMmPerHour >= 0.05f)
	{
		return EWeatherState::Drizzle;
	}
	if (Sample.FogDensity >= 0.45f)
	{
		return EWeatherState::Fog;
	}
	if (Sample.CloudCover >= 0.8f)
	{
		return EWeatherState::Overcast;
	}
	if (Sample.CloudCover >= 0.3f)
	{
		return EWeatherState::PartlyCloudy;
	}
	return EWeatherState::Clear;
}

FWeatherSample FWeatherModel::MakeStatePreset(EWeatherState State, const FWeatherSample& Natural)
{
	FWeatherSample Preset = Natural;
	auto Set = [&Preset](float Cloud, float Rain, float Fog, float Storm)
	{
		Preset.CloudCover = Cloud;
		Preset.PrecipitationMmPerHour = Rain;
		Preset.FogDensity = Fog;
		Preset.StormIntensity = Storm;
	};

	switch (State)
	{
	case EWeatherState::Clear:        Set(0.05f, 0.0f, 0.0f, 0.0f); break;
	case EWeatherState::PartlyCloudy: Set(0.5f, 0.0f, 0.0f, 0.0f); break;
	case EWeatherState::Overcast:     Set(0.9f, 0.0f, 0.05f, 0.0f); break;
	case EWeatherState::Fog:          Set(0.7f, 0.0f, 0.85f, 0.0f); Preset.WindSpeed = 0.5f; Preset.Humidity = 1.0f; break;
	case EWeatherState::Drizzle:      Set(0.9f, 0.3f, 0.2f, 0.0f); break;
	case EWeatherState::Rain:         Set(0.95f, 2.0f, 0.1f, 0.0f); break;
	case EWeatherState::HeavyRain:    Set(1.0f, 6.0f, 0.15f, 0.0f); Preset.WindSpeed = FMath::Max(Natural.WindSpeed, 6.0f); break;
	case EWeatherState::Storm:        Set(1.0f, 9.0f, 0.1f, 1.0f); Preset.WindSpeed = FMath::Max(Natural.WindSpeed, 12.0f); break;
	default: break;
	}
	return Preset;
}
