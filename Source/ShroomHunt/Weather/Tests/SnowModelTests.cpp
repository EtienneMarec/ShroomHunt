#include "Misc/AutomationTest.h"
#include "Time/CalendarMath.h"
#include "Weather/SnowModel.h"
#include "Weather/WeatherModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SnowModelTestsDetail
{
	/** Synthetic weather: constant temperature and precipitation within [From, To), dry elsewhere. */
	FWeatherSample Constant(double T, double From, double To, float TemperatureC, float MmPerHour)
	{
		FWeatherSample S;
		S.TemperatureC = TemperatureC;
		S.PrecipitationMmPerHour = (T >= From && T < To) ? MmPerHour : 0.0f;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowModelTest, "ShroomHunt.Weather.Snow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSnowModelTest::RunTest(const FString& Parameters)
{
	using namespace SnowModelTestsDetail;
	const double Now = 30.0 * GameSecondsPerDay;
	const double H = 3600.0;

	TestEqual(TEXT("All snow when freezing"), SnowModel::SnowFraction(-3.0f), 1.0f);
	TestEqual(TEXT("All rain when mild"), SnowModel::SnowFraction(5.0f), 0.0f);
	const float Sleet = SnowModel::SnowFraction(2.0f);
	TestTrue(TEXT("Sleet in between"), Sleet > 0.0f && Sleet < 1.0f);

	// 6 hours of 1 mm/h snow at -3 C, ending 2 hours ago, still cold: ~6 mm on the ground.
	const float Pack = SnowModel::SnowWaterMm([&](double T) { return Constant(T, Now - 8.0 * H, Now - 2.0 * H, -3.0f, 1.0f); }, Now);
	TestTrue(FString::Printf(TEXT("Cold snowfall stays (%.2f mm)"), Pack), FMath::IsNearlyEqual(Pack, 6.0f, 0.2f));
	TestTrue(TEXT("6 mm of snow water covers the ground"), SnowModel::Cover(Pack) > 0.95f);

	// Same snowfall with the air at +6 C afterwards: degree-hour melt removes it within hours.
	const float Melted = SnowModel::SnowWaterMm([&](double T)
	{
		FWeatherSample S = Constant(T, Now - 30.0 * H, Now - 24.0 * H, -3.0f, 1.0f);
		if (T >= Now - 24.0 * H)
		{
			S.TemperatureC = 6.0f;
		}
		return S;
	}, Now);
	TestTrue(FString::Printf(TEXT("A mild day melts the snow (%.2f mm left)"), Melted), Melted < 0.01f);

	// Rain at +8 C never makes snow.
	const float Rain = SnowModel::SnowWaterMm([&](double T) { return Constant(T, Now - 10.0 * H, Now, 8.0f, 3.0f); }, Now);
	TestEqual(TEXT("Warm rain is no snow"), Rain, 0.0f);
	TestEqual(TEXT("No snow, no cover"), SnowModel::Cover(0.0f), 0.0f);

	TestTrue(TEXT("Frost on a freezing morning, none on a mild one"), SnowModel::Frost(-3.0f) > 0.9f && SnowModel::Frost(5.0f) < 0.01f);
	TestTrue(TEXT("Dry and warm: dry ground"), SnowModel::Dryness(0.0f, 26.0f) > 0.9f);
	TestTrue(TEXT("Rainy or cold: not dry"), SnowModel::Dryness(15.0f, 26.0f) < 0.01f && SnowModel::Dryness(0.0f, 8.0f) < 0.01f);

	// Classification and forced snow.
	FWeatherSample Cold;
	Cold.TemperatureC = -2.0f;
	Cold.PrecipitationMmPerHour = 1.0f;
	Cold.CloudCover = 1.0f;
	TestTrue(TEXT("Cold precipitation is the Snow state"), FWeatherModel::Classify(Cold) == EWeatherState::Snow);
	FWeatherSample Mild = Cold;
	Mild.TemperatureC = 6.0f;
	TestTrue(TEXT("Mild precipitation is rain"), FWeatherModel::Classify(Mild) == EWeatherState::Rain);
	const FWeatherSample Forced = FWeatherModel::MakeStatePreset(EWeatherState::Snow, Mild);
	TestTrue(TEXT("Forced snow is cold and classified as snow"), Forced.TemperatureC <= -2.0f && FWeatherModel::Classify(Forced) == EWeatherState::Snow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnowClimateTest, "ShroomHunt.Weather.SnowClimate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSnowClimateTest::RunTest(const FString& Parameters)
{
	// Lowland climate: a few snowy hours in winter, never in summer (real lowland France).
	const FCalendarConfig Calendar;
	const FWeatherModel Model(1337u, FClimateProfile(), Calendar);
	const int32 YearDays = Calendar.GetYearLengthDays();
	int32 WinterHours = 0;
	int32 WinterSnowHours = 0;
	int32 SummerSnowHours = 0;
	int32 WinterCoveredChecks = 0;
	int32 WinterChecks = 0;
	float MinWetTemperature = 100.0f;
	for (int32 Year = 0; Year < 4; ++Year)
	{
		for (int32 Hour = 0; Hour < YearDays * 24; ++Hour)
		{
			const double T = (static_cast<double>(Year) * YearDays * 24 + Hour + 0.5) * 3600.0;
			const float Month = CalendarMath::MonthAt(Calendar, T);
			const FWeatherSample S = Model.SampleNatural(T);
			const bool bSnow = FWeatherModel::Classify(S) == EWeatherState::Snow;
			const bool bWinter = Month >= 12.0f || Month < 3.0f;
			if (bWinter && S.PrecipitationMmPerHour >= 0.05f)
			{
				MinWetTemperature = FMath::Min(MinWetTemperature, S.TemperatureC);
			}
			if (bWinter)
			{
				++WinterHours;
				WinterSnowHours += bSnow ? 1 : 0;
				if (Hour % 6 == 0)
				{
					++WinterChecks;
					const float Pack = SnowModel::SnowWaterMm([&Model](double X) { return Model.SampleNatural(X); }, T);
					WinterCoveredChecks += SnowModel::Cover(Pack) > 0.5f ? 1 : 0;
				}
			}
			else if (Month >= 6.0f && Month < 9.0f)
			{
				SummerSnowHours += bSnow ? 1 : 0;
			}
		}
	}
	const float SnowShare = WinterHours > 0 ? static_cast<float>(WinterSnowHours) / WinterHours : 0.0f;
	const float CoveredShare = WinterChecks > 0 ? static_cast<float>(WinterCoveredChecks) / WinterChecks : 0.0f;
	AddInfo(FString::Printf(TEXT("Winter: snowing %.1f%% of hours, ground covered %.1f%% of the time (%d hours), coldest precipitation %.1f C"),
		SnowShare * 100.0f, CoveredShare * 100.0f, WinterHours, MinWetTemperature));
	TestEqual(TEXT("No snow in summer"), SummerSnowHours, 0);
	TestTrue(TEXT("Some snow in winter"), WinterSnowHours > 0);
	TestTrue(TEXT("Lowland: snow is occasional (< 8% of winter hours)"), SnowShare < 0.08f);
	TestTrue(TEXT("Lowland: the ground is mostly free of snow (< 35% of winter)"), CoveredShare < 0.35f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
