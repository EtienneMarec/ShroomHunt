#include "Demo/DemoReel.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestPaths.h"
#include "Misc/AutomationTest.h"
#include "Mycology/SpawnModel.h"
#include "Time/CalendarMath.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Time/TimeSettings.h"
#include "Vegetation/VegetationObstacles.h"
#include "Vegetation/VegetationScatter.h"
#include "Weather/WeatherModel.h"
#include "Weather/WeatherSettings.h"

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

	// Mushroom shots: the spawn model with the default weather and no forced weather (like sh.Spawn.Simulate).
	TMap<FName, FMushroomSpeciesRow> Species;
	FString Source;
	USpeciesDatabaseSubsystem::LoadSpeciesForTools(Species, Source);
	TArray<FMyceliumPatch> Patches;
	for (const TPair<FName, FMushroomSpeciesRow>& Pair : Species)
	{
		SpawnModel::GeneratePatches(Pair.Value, Pair.Key, *Field, Patches);
	}
	const TSharedRef<FVegetationObstacleGrid> Obstacles = MakeShared<FVegetationObstacleGrid>();
	const TSharedRef<FVegetationObstacleGrid> Cover = MakeShared<FVegetationObstacleGrid>();
	if (const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors))
	{
		const FBox2d Whole(FVector2d::ZeroVector, FVector2d(Field->GetSizeM(), Field->GetSizeM()));
		TArray<FVegetationInstance> Instances;
		VegetationScatter::Scatter(*Field, *Vegetation, EVegetationLayer::Trees, Whole, Instances);
		VegetationScatter::Scatter(*Field, *Vegetation, EVegetationLayer::Understory, Whole, Instances);
		Obstacles->AddInstances(*Vegetation, Instances);
		Cover->AddCover(*Vegetation, Instances);
		AddInfo(FString::Printf(TEXT("%d trunks, logs and stumps; %d bracken, bramble and shrub covers"), Obstacles->Num(), Cover->Num()));
	}
	const UWeatherSettings* WeatherSettings = GetDefault<UWeatherSettings>();
	const FCalendarConfig Calendar = GetDefault<UTimeSettings>()->MakeCalendarConfig();
	const TSharedRef<FWeatherModel> Weather = MakeShared<FWeatherModel>(static_cast<uint32>(WeatherSettings->DefaultSeed), WeatherSettings->DefaultClimate, Calendar);
	TMap<uint64, float> Cache;
	FSpawnContext Context;
	Context.Field = Field.Get();
	Context.Calendar = Calendar;
	Context.TriggerCache = &Cache;
	Context.IsBlocked = [Obstacles](const FVector2f& P) { return Obstacles->Overlaps(P, 0.1f); };
	Context.Weather.RainMm = [Weather](double From, double To) { return Weather->IntegrateRainMm(From, To, {}); };
	Context.Weather.MeanTemperatureC = [Weather](double From, double To) { return Weather->MeanTemperatureC(From, To, {}); };
	auto IsBlocked = [&Obstacles](const FVector2f& P, float Radius) { return Obstacles->Overlaps(P, Radius); };
	auto IsFruitCovered = [&Cover](const FVector2f& P) { return Cover->Overlaps(P, 0.05f); };
	int32 ClearViews = 0;
	int32 MushroomShots = 0;
	float Total = 0.0f;
	TSet<FString> Seasons;
	for (int32 Index = 0; Index < Reel.Shots.Num(); ++Index)
	{
		const FDemoShot& Shot = Reel.Shots[Index];
		Total += Shot.Seconds;
		Seasons.Add(Shot.Season);
		const FString Key = FString::Printf(TEXT("Shot %d %s"), Index, *Shot.Title);
		const bool bMushrooms = Shot.Place == EDemoPlace::Mushrooms;
		FDemoPlacement Placement;
		FDemoPlacement Again;
		if (bMushrooms)
		{
			++MushroomShots;
			const FMushroomSpeciesRow* Row = Species.Find(Shot.Species);
			if (!TestNotNull(Key + TEXT(": species exists"), Row))
			{
				continue;
			}
			const FDemoMushroomScene Scene = DemoReel::FindMushrooms(Shot, Reel.Year, *Row, Patches, Context, IsFruitCovered);
			TestTrue(Key + FString::Printf(TEXT(": at least %d mushrooms found"), Shot.MinFruits), Scene.bFound && Scene.Count >= Shot.MinFruits);
			const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors);
			const TSharedRef<FVegetationObstacleGrid> Ground = Vegetation ? DemoReel::MakeGroundCover(*Field, *Vegetation, Scene.CenterM, 3.0) : MakeShared<FVegetationObstacleGrid>();
			auto IsCovered = [&Cover, &Ground, &Scene](const FVector2f& P, float Radius)
			{
				return Cover->Overlaps(P, Radius) || (FVector2d::Distance(FVector2d(P), Scene.CenterM) < 0.7 && Ground->Overlaps(P, Radius));
			};
			Placement = DemoReel::PlaceMushroomShot(*Field, Shot, Scene, IsBlocked, IsCovered);
			Again = DemoReel::PlaceMushroomShot(*Field, Shot, DemoReel::FindMushrooms(Shot, Reel.Year, *Row, Patches, Context, IsFruitCovered), IsBlocked, IsCovered);
			// Share of this species' mushrooms under cover on the chosen day (some species grow mostly there).
			if (Scene.bFound)
			{
				TArray<FFruitBody> Fruits;
				for (const FMyceliumPatch& Patch : Patches)
				{
					if (Patch.SpeciesId == Shot.Species)
					{
						SpawnModel::FruitBodiesAt(*Row, Patch, Context, Scene.GameSeconds, Fruits);
					}
				}
				const int32 Covered = Fruits.FilterByPredicate([&](const FFruitBody& F) { return IsFruitCovered(F.PositionM); }).Num();
				AddInfo(FString::Printf(TEXT("%s: %d of %d mushrooms of the day under cover"), *Key, Covered, Fruits.Num()));
			}
			// The view from the start of the shot: no low plant on the camera or in front of it.
			if (Placement.bFound)
			{
				const FVector2d From(Placement.Camera.X, Placement.Camera.Y);
				bool bClear = !IsCovered(FVector2f(From), 0.4f);
				for (int32 Step = 1; bClear && Step < 8; ++Step)
				{
					bClear = !IsCovered(FVector2f(FMath::Lerp(From, Scene.CenterM, Step / 8.0)), 0.05f);
				}
				AddInfo(Key + (bClear ? TEXT(": clear view") : TEXT(": plants in front (no clear view found)")));
				ClearViews += bClear ? 1 : 0;
			}
			TestTrue(Key + TEXT(": the date is in the shot's season"), Placement.GameSeconds < 0.0
				|| StaticEnum<ESeason>()->GetNameStringByValue(static_cast<int64>(CalendarMath::DecodeDate(Calendar, Placement.GameSeconds).Season)) == Shot.Season);
		}
		else
		{
			const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors);
			Placement = DemoReel::FindPlace(*Field, Shot, Vegetation.Get());
			Again = DemoReel::FindPlace(*Field, Shot, Vegetation.Get());
		}
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
			if (Shot.Kind == EDemoShotKind::Rise)
			{
				// High up, the camera sees far: the forest's boundary (and the void beyond) must stay out of view.
				const double Edge = FMath::Min(FMath::Min(Pose.Position.X, Size - Pose.Position.X), FMath::Min(Pose.Position.Y, Size - Pose.Position.Y));
				TestTrue(FString::Printf(TEXT("%s at %.2f: far from the forest's boundary (%.0f m)"), *Key, T, Edge), Edge > 150.0);
			}
			TestTrue(FString::Printf(TEXT("%s at %.2f: above the ground (%.1f m)"), *Key, T, Pose.Position.Z - GroundM), Pose.Position.Z - GroundM > (bMushrooms ? 0.2 : 0.8));
			if (bMushrooms)
			{
				TestFalse(FString::Printf(TEXT("%s at %.2f: not inside a trunk"), *Key, T), Obstacles->Overlaps(FVector2f(Pose.Position.X, Pose.Position.Y), 0.25f));
				TestTrue(FString::Printf(TEXT("%s at %.2f: close to the mushrooms"), *Key, T), FVector3d::Distance(Pose.Position, Pose.LookAt) < 3.0);
			}
			TestFalse(FString::Printf(TEXT("%s at %.2f: not standing in water"), *Key, T), Sample.bWater && Shot.Kind != EDemoShotKind::Rise);
			TestTrue(FString::Printf(TEXT("%s at %.2f: looks somewhere"), *Key, T), FVector3d::Distance(Pose.Position, Pose.LookAt) > (bMushrooms ? 0.3 : 1.0));
			if (Shot.Kind == EDemoShotKind::Walk)
			{
				TestTrue(FString::Printf(TEXT("%s at %.2f: walks on the path (%.1f m from it)"), *Key, T, Sample.PathDistanceM),
					Sample.PathDistanceM < ForestPaths::HalfWidthM(EForestPathRank::Road, Field->GetDefinition()) + 1.0f);
			}
		}
	}
	TestTrue(FString::Printf(TEXT("Between 1.5 and 4 minutes (%.0f s)"), Total), Total > 90.0f && Total < 240.0f);
	TestEqual(TEXT("Every season shown"), Seasons.Num(), 4);
	TestTrue(FString::Printf(TEXT("The reel shows mushrooms (%d shots)"), MushroomShots), MushroomShots >= 4);
	TestTrue(FString::Printf(TEXT("Most mushroom close-ups have a clear view (%d of %d)"), ClearViews, MushroomShots), ClearViews * 4 >= MushroomShots * 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
