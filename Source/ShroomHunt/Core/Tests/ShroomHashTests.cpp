#include "Core/ShroomHash.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShroomHashDeterminismTest, "ShroomHunt.Core.Hash.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FShroomHashDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace ShroomHash;

	TestTrue(TEXT("Same inputs give the same hash"), Hash(42u, 1u, 2u, 3u) == Hash(42u, 1u, 2u, 3u));
	TestTrue(TEXT("Argument order matters"), Hash(42u, 1u, 2u) != Hash(42u, 2u, 1u));
	TestTrue(TEXT("Seed matters"), Hash(1u, 7u) != Hash(2u, 7u));
	TestTrue(TEXT("String hash is stable"), HashString(TEXT("BoletusEdulis")) == HashString(TEXT("BoletusEdulis")));
	TestTrue(TEXT("Different strings differ"), HashString(TEXT("BoletusEdulis")) != HashString(TEXT("AmanitaMuscaria")));

	// Unit floats stay in range and are roughly uniform.
	double Sum = 0.0;
	constexpr int32 Count = 100000;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Value = ToUnitFloat(Hash(7u, static_cast<uint32>(Index)));
		if (Value < 0.0f || Value >= 1.0f)
		{
			AddError(FString::Printf(TEXT("ToUnitFloat out of range: %f"), Value));
			return false;
		}
		Sum += Value;
	}
	TestTrue(TEXT("Mean of unit floats is close to 0.5"), FMath::Abs(Sum / Count - 0.5) < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShroomNoiseTest, "ShroomHunt.Core.Hash.Noise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FShroomNoiseTest::RunTest(const FString& Parameters)
{
	using namespace ShroomHash;

	float MinValue = 1.0f;
	float MaxValue = -1.0f;
	float MaxStep = 0.0f;
	float Previous = ValueNoise1D(3u, 0.0);
	for (int32 Index = 1; Index < 20000; ++Index)
	{
		const double X = Index * 0.01;
		const float Value = Fbm1D(3u, X, 3);
		MinValue = FMath::Min(MinValue, Value);
		MaxValue = FMath::Max(MaxValue, Value);
		const float Raw = ValueNoise1D(3u, X);
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Raw - Previous));
		Previous = Raw;
	}
	TestTrue(TEXT("fBm stays in [-1, 1]"), MinValue >= -1.0f && MaxValue <= 1.0f);
	TestTrue(TEXT("fBm uses a good part of its range"), MaxValue - MinValue > 0.8f);
	TestTrue(TEXT("Value noise is continuous (small steps)"), MaxStep < 0.05f);
	TestEqual(TEXT("2D noise is deterministic"), ValueNoise2D(9u, 12.3, -4.5), ValueNoise2D(9u, 12.3, -4.5));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
