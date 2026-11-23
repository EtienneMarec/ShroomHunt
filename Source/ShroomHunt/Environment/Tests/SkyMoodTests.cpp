#include "Environment/SkyMood.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkyMoodTest, "ShroomHunt.Environment.SkyMood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSkyMoodTest::RunTest(const FString& Parameters)
{
	// Same inputs as the weather state presets (cloud, rain intensity, storm).
	const FSkyMood Clear = SkyMood::Compute(0.05f, 0.0f, 0.0f);
	const FSkyMood Broken = SkyMood::Compute(0.5f, 0.0f, 0.0f);
	const FSkyMood Overcast = SkyMood::Compute(0.9f, 0.0f, 0.0f);
	const FSkyMood Rain = SkyMood::Compute(0.95f, 0.33f, 0.0f);
	const FSkyMood Heavy = SkyMood::Compute(1.0f, 1.0f, 0.0f);
	const FSkyMood Storm = SkyMood::Compute(1.0f, 1.0f, 1.0f);

	TestTrue(TEXT("Clear sky is neutral"), Clear.Gloom < 0.01f && FMath::IsNearlyZero(Clear.ExposureBiasEV, 0.02f) && Clear.SkyLuminanceFactor > 0.99f);
	TestTrue(TEXT("Broken clouds barely darken"), Broken.Gloom < 0.1f);
	TestTrue(TEXT("Overcast < rain < heavy rain < storm"), Overcast.Gloom < Rain.Gloom && Rain.Gloom < Heavy.Gloom && Heavy.Gloom < Storm.Gloom);
	TestEqual(TEXT("A storm is full gloom"), Storm.Gloom, 1.0f);
	const FSkyMoodParams Params;
	TestTrue(TEXT("Storm uses the full darkening"), FMath::IsNearlyEqual(Storm.ExposureBiasEV, Params.ExposureBiasEV)
		&& FMath::IsNearlyEqual(Storm.SkyLuminanceFactor, Params.SkyLuminance) && FMath::IsNearlyEqual(Storm.Saturation, Params.Saturation));
	TestTrue(TEXT("The sun fades under storm clouds"), Storm.SunFactor < 0.05f && Clear.SunFactor > 0.95f);
	// No eclipse light: under a closed sky the direct sun and its sharp shadows are gone, broken clouds keep them.
	TestTrue(TEXT("Overcast: almost no direct sun"), Overcast.SunFactor < 0.1f && Overcast.Diffuse > 0.9f);
	TestTrue(TEXT("Overcast: faint, soft shadows"), Overcast.ShadowAmount < 0.25f && Overcast.SourceAngleScale > 10.0f);
	TestTrue(TEXT("Broken clouds: sunlight and shadows stay"), Broken.SunFactor > 0.7f && Broken.ShadowAmount > 0.8f);
	TestTrue(TEXT("Clear sky: sharp shadows"), Clear.ShadowAmount > 0.99f && Clear.SourceAngleScale < 1.01f);
	TestTrue(TEXT("The sky light stays the main light under clouds"), Storm.SkyLightFactor > 0.5f);
	TestTrue(TEXT("Factors stay positive"), Storm.SkyLightFactor > 0.0f && Storm.FogBrightness > 0.0f && Storm.SunFactor >= 0.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
