#include "Misc/AutomationTest.h"
#include "Weather/WeatherModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FWeatherModel MakeDefaultModel(uint32 Seed)
	{
		return FWeatherModel(Seed, FClimateProfile(), FCalendarConfig());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherDeterminismTest, "ShroomHunt.Weather.Model.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWeatherDeterminismTest::RunTest(const FString& Parameters)
{
	const FWeatherModel A = MakeDefaultModel(1337);
	const FWeatherModel B = MakeDefaultModel(1337);
	const FWeatherModel C = MakeDefaultModel(4242);

	int32 Differences = 0;
	for (int32 Hour = 0; Hour < 24 * 48; Hour += 7)
	{
		const double T = Hour * 3600.0 + 123.0;
		const FWeatherSample SA = A.SampleNatural(T);
		const FWeatherSample SB = B.SampleNatural(T);
		if (SA.PrecipitationMmPerHour != SB.PrecipitationMmPerHour || SA.CloudCover != SB.CloudCover || SA.TemperatureC != SB.TemperatureC)
		{
			AddError(FString::Printf(TEXT("Same seed gave different weather at hour %d"), Hour));
			return false;
		}
		if (!FMath::IsNearlyEqual(SA.CloudCover, C.SampleNatural(T).CloudCover, 1e-3f))
		{
			++Differences;
		}
	}
	TestTrue(TEXT("Different seeds give different weather"), Differences > 50);

	// Random access: sampling order does not matter.
	const float Forward = A.SampleNatural(500000.0).TemperatureC;
	A.SampleNatural(10.0);
	TestEqual(TEXT("Random access is order-independent"), A.SampleNatural(500000.0).TemperatureC, Forward);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherClimateTest, "ShroomHunt.Weather.Model.ClimateCalibration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWeatherClimateTest::RunTest(const FString& Parameters)
{
	// Over many years, the share of rainy hours should match the climate's rain fraction
	// (~0.12 on average for the default lowland climate) and temperatures stay plausible.
	const FWeatherModel Model = MakeDefaultModel(7);
	constexpr int32 Days = 24 * 60; // 60 game years
	int32 RainyHours = 0;
	int32 TotalHours = 0;
	double TotalRainMm = 0.0;
	float MinTemp = 100.0f;
	float MaxTemp = -100.0f;
	int32 FogHours = 0;
	int32 StormHours = 0;

	for (int32 Hour = 0; Hour < Days * 24; ++Hour)
	{
		const FWeatherSample S = Model.SampleNatural(Hour * 3600.0 + 1800.0);
		++TotalHours;
		if (S.PrecipitationMmPerHour > 0.05f)
		{
			++RainyHours;
		}
		TotalRainMm += S.PrecipitationMmPerHour;
		MinTemp = FMath::Min(MinTemp, S.TemperatureC);
		MaxTemp = FMath::Max(MaxTemp, S.TemperatureC);
		FogHours += FWeatherModel::Classify(S) == EWeatherState::Fog ? 1 : 0;
		StormHours += FWeatherModel::Classify(S) == EWeatherState::Storm ? 1 : 0;
	}

	const double RainFraction = static_cast<double>(RainyHours) / TotalHours;
	const double RainPerDay = TotalRainMm / Days;
	AddInfo(FString::Printf(TEXT("Rainy hours %.3f, rain %.2f mm/day, temp [%.1f, %.1f], fog hours %.3f, storm hours %.4f"),
		RainFraction, RainPerDay, MinTemp, MaxTemp, static_cast<double>(FogHours) / TotalHours, static_cast<double>(StormHours) / TotalHours));

	TestTrue(TEXT("Rainy-hour fraction is close to the climate (0.07 - 0.20)"), RainFraction > 0.07 && RainFraction < 0.20);
	TestTrue(TEXT("Mean rain is plausible for lowland France (0.8 - 5 mm/day)"), RainPerDay > 0.8 && RainPerDay < 5.0);
	TestTrue(TEXT("Temperatures stay plausible"), MinTemp > -15.0f && MaxTemp < 38.0f);
	TestTrue(TEXT("Some foggy hours exist"), FogHours > 0);
	TestTrue(TEXT("Some storm hours exist"), StormHours > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherOverrideTest, "ShroomHunt.Weather.Model.Overrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWeatherOverrideTest::RunTest(const FString& Parameters)
{
	const FWeatherModel Model = MakeDefaultModel(1337);

	// Every preset must classify back to its own state.
	const FWeatherSample Neutral;
	for (int32 Index = 0; Index < static_cast<int32>(EWeatherState::Count); ++Index)
	{
		const EWeatherState State = static_cast<EWeatherState>(Index);
		TestTrue(FString::Printf(TEXT("Preset %d classifies to itself"), Index),
			FWeatherModel::Classify(FWeatherModel::MakeStatePreset(State, Neutral)) == State);
	}

	// A 10-hour heavy-rain override yields ~6 mm/h minus the blend ramps.
	const double Start = 100.0 * 3600.0;
	FWeatherOverride Override;
	Override.State = EWeatherState::HeavyRain;
	Override.StartGameSeconds = Start;
	Override.EndGameSeconds = Start + 10.0 * 3600.0;
	const TArray<FWeatherOverride> Overrides = { Override };

	const FWeatherSample Mid = Model.Sample(Start + 5.0 * 3600.0, Overrides);
	TestTrue(TEXT("Forced rain rate at mid-override"), FMath::IsNearlyEqual(Mid.PrecipitationMmPerHour, 6.0f, 0.01f));
	TestTrue(TEXT("Forced state"), FWeatherModel::Classify(Mid) == EWeatherState::HeavyRain);

	const float Total = Model.IntegrateRainMm(Start, Start + 10.0 * 3600.0, Overrides, 600.0);
	TestTrue(FString::Printf(TEXT("Integrated forced rain %.1f mm in [50, 60]"), Total), Total > 50.0f && Total < 60.0f);

	// Outside the override the natural weather is untouched.
	const double After = Start + 12.0 * 3600.0;
	TestEqual(TEXT("No effect after the override"), Model.Sample(After, Overrides).CloudCover, Model.SampleNatural(After).CloudCover);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
