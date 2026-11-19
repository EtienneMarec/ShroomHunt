#include "Environment/LocalRain.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocalRainTest, "ShroomHunt.Environment.LocalRain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLocalRainTest::RunTest(const FString& Parameters)
{
	// Open sky: all the rain, no drops from leaves, full wind.
	const FLocalRain Open = LocalRain::Compute(1.0f, 0.5f, 0.0f);
	TestEqual(TEXT("Open sky: direct rain"), Open.Direct, 1.0f);
	TestEqual(TEXT("Open sky: no drips"), Open.Drip, 0.0f);
	TestEqual(TEXT("Open sky: full wind"), Open.WindFactor, 1.0f);

	// Dense canopy: less direct rain, drops from the leaves, sheltered from the wind.
	const FLocalRain Dense = LocalRain::Compute(1.0f, 0.5f, 1.0f);
	TestTrue(TEXT("Dense canopy catches most of the rain"), Dense.Direct < 0.4f && Dense.Direct > 0.0f);
	TestTrue(TEXT("Dense canopy drips"), Dense.Drip > 0.9f);
	TestTrue(TEXT("Dense canopy shelters from the wind"), Dense.WindFactor < 0.5f);

	// After the rain: the wet forest keeps dripping, the open sky is dry; a dry forest does not drip.
	TestTrue(TEXT("Wet forest drips after the rain"), LocalRain::Compute(0.0f, 1.0f, 1.0f).Drip > 0.5f);
	TestEqual(TEXT("No rain in the open after the rain"), LocalRain::Compute(0.0f, 1.0f, 0.0f).Direct, 0.0f);
	TestEqual(TEXT("A dry forest does not drip"), LocalRain::Compute(0.0f, 0.1f, 1.0f).Drip, 0.0f);

	// Monotonic in the canopy.
	float LastDirect = MAX_flt;
	for (int32 Step = 0; Step <= 10; ++Step)
	{
		const float Direct = LocalRain::Compute(1.0f, 0.5f, Step / 10.0f).Direct;
		TestTrue(TEXT("Direct rain decreases with the canopy"), Direct <= LastDirect);
		LastDirect = Direct;
	}

	// The F01 forest has both clearings and dense stands, so the effect is visible when walking.
	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(TEXT("F01_LowlandOakBeech"), Errors);
	if (!TestTrue(TEXT("Forest field"), Field.IsValid()))
	{
		return false;
	}
	int32 Clear = 0, DenseCount = 0, Total = 0;
	for (float Y = 20.0f; Y < 1000.0f; Y += 40.0f)
	{
		for (float X = 20.0f; X < 1000.0f; X += 40.0f)
		{
			const float Canopy = Field->Sample(X, Y).Canopy;
			Clear += Canopy < 0.3f ? 1 : 0;
			DenseCount += Canopy > 0.8f ? 1 : 0;
			++Total;
		}
	}
	AddInfo(FString::Printf(TEXT("F01 canopy: %d clear, %d dense of %d samples"), Clear, DenseCount, Total));
	TestTrue(TEXT("F01 has open places"), Clear > 0);
	TestTrue(TEXT("F01 has dense stands"), DenseCount > Total / 10);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
