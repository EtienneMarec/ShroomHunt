#include "Demo/DemoReel.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestPaths.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoReelTest, "ShroomHunt.Demo.Reel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDemoReelTest::RunTest(const FString& Parameters)
{
	FDemoReel Reel;
	TArray<FString> Errors;
	if (!DemoReel::Load(Reel, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(Reel.ForestName, Errors);
	if (!TestTrue(TEXT("Forest field"), Field.IsValid()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("A reel of a few minutes (%d shots)"), Reel.Shots.Num()), Reel.Shots.Num() >= 8);
	float Total = 0.0f;
	TSet<FString> Seasons;
	for (int32 Index = 0; Index < Reel.Shots.Num(); ++Index)
	{
		const FDemoShot& Shot = Reel.Shots[Index];
		Total += Shot.Seconds;
		Seasons.Add(Shot.Season);
		const FString Key = FString::Printf(TEXT("Shot %d %s"), Index, *Shot.Title);
		const FDemoPlacement Placement = DemoReel::FindPlace(*Field, Shot);
		const FDemoPlacement Again = DemoReel::FindPlace(*Field, Shot);
		if (!TestTrue(Key + TEXT(" finds its place"), Placement.bFound))
		{
			continue;
		}
		TestTrue(Key + TEXT(" is deterministic"), Placement.Description == Again.Description);
		AddInfo(Key + TEXT(": ") + Placement.Description);
		for (const float T : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
		{
			const FDemoPose Pose = DemoReel::Evaluate(*Field, Shot, Placement, T);
			const double Size = Field->GetSizeM();
			const bool bInside = Pose.Position.X > 0.0 && Pose.Position.Y > 0.0 && Pose.Position.X < Size && Pose.Position.Y < Size;
			const double GroundM = Field->GetHeights().Sample(static_cast<float>(FMath::Clamp(Pose.Position.X, 0.0, Size - 1.0)),
				static_cast<float>(FMath::Clamp(Pose.Position.Y, 0.0, Size - 1.0)));
			const FForestSample Sample = Field->Sample(static_cast<float>(Pose.Position.X), static_cast<float>(Pose.Position.Y));
			TestTrue(FString::Printf(TEXT("%s at %.2f: inside the forest"), *Key, T), bInside);
			TestTrue(FString::Printf(TEXT("%s at %.2f: above the ground (%.1f m)"), *Key, T, Pose.Position.Z - GroundM), Pose.Position.Z - GroundM > 0.8);
			TestFalse(FString::Printf(TEXT("%s at %.2f: not standing in water"), *Key, T), Sample.bWater && Shot.Kind != EDemoShotKind::Rise);
			TestTrue(FString::Printf(TEXT("%s at %.2f: looks somewhere"), *Key, T), FVector3d::Distance(Pose.Position, Pose.LookAt) > 1.0);
			if (Shot.Kind == EDemoShotKind::Walk)
			{
				TestTrue(FString::Printf(TEXT("%s at %.2f: walks on the path (%.1f m from it)"), *Key, T, Sample.PathDistanceM),
					Sample.PathDistanceM < ForestPaths::HalfWidthM(EForestPathRank::Road, Field->GetDefinition()) + 1.0f);
			}
		}
	}
	TestTrue(FString::Printf(TEXT("Between 1.5 and 4 minutes (%.0f s)"), Total), Total > 90.0f && Total < 240.0f);
	TestEqual(TEXT("Every season shown"), Seasons.Num(), 4);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
