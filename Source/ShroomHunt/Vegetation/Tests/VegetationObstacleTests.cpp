#include "DynamicMesh/DynamicMesh3.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Misc/AutomationTest.h"
#include "Mycology/SpawnModel.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Vegetation/VegetationMeshBuilder.h"
#include "Vegetation/VegetationObstacles.h"
#include "Vegetation/VegetationScatter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVegetationObstacleTest, "ShroomHunt.Vegetation.Obstacles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVegetationObstacleTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(TEXT("F01_LowlandOakBeech"), Errors);
	const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors);
	TMap<FName, FMushroomSpeciesRow> Species;
	FString Source;
	if (!Field || !Vegetation || !USpeciesDatabaseSubsystem::LoadSpeciesForTools(Species, Source))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}

	// The obstacle sizes match the meshes.
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		FVegetationMeshSpec Spec;
		Spec.Kind = EVegetationKind::Log;
		Spec.Variant = Variant;
		UE::Geometry::FDynamicMesh3 Mesh;
		VegetationMeshBuilder::Build(*Vegetation, Spec, Mesh);
		float Length = 0.0f;
		float Radius = 0.0f;
		VegetationMeshBuilder::GetLogSize(Spec.GetSeed(), Length, Radius);
		TestTrue(TEXT("Log obstacle length matches its mesh"), FMath::Abs(Mesh.GetBounds().Width() / 100.0 - Length) < 0.2);
	}

	const FBox2d Whole(FVector2d::ZeroVector, FVector2d(Field->GetSizeM(), Field->GetSizeM()));
	TArray<FVegetationInstance> Trees;
	TArray<FVegetationInstance> Understory;
	VegetationScatter::Scatter(*Field, *Vegetation, EVegetationLayer::Trees, Whole, Trees);
	VegetationScatter::Scatter(*Field, *Vegetation, EVegetationLayer::Understory, Whole, Understory);
	FVegetationObstacleGrid TreeGrid;
	TreeGrid.AddInstances(*Vegetation, Trees);

	// Dead wood, saplings and plants do not grow inside a trunk.
	TMap<FString, int32> Inside;
	for (const FVegetationInstance& Instance : Understory)
	{
		FVegetationObstacle Footprint;
		const bool bSolid = FVegetationObstacleGrid::MakeObstacle(*Vegetation, Instance, Footprint);
		const bool bHit = bSolid ? TreeGrid.OverlapsCapsule(Footprint)
			: TreeGrid.Overlaps(FVector2f(static_cast<float>(Instance.PositionM.X), static_cast<float>(Instance.PositionM.Y)), 0.05f);
		if (bHit)
		{
			Inside.FindOrAdd(VegetationData::KindName(Instance.Kind))++;
		}
	}
	int32 TotalInside = 0;
	for (const TPair<FString, int32>& Pair : Inside)
	{
		AddInfo(FString::Printf(TEXT("%d %s instances overlap a trunk"), Pair.Value, *Pair.Key));
		TotalInside += Pair.Value;
	}
	TestEqual(TEXT("No understory instance overlaps a tree trunk"), TotalInside, 0);

	// Mushrooms after a long wet spell (all year, every patch at full strength).
	FVegetationObstacleGrid AllGrid;
	AllGrid.AddInstances(*Vegetation, Trees);
	AllGrid.AddInstances(*Vegetation, Understory);
	FSpawnContext Context;
	Context.Field = Field.Get();
	Context.Weather.RainMm = [](double From, double To) { return static_cast<float>((To - From) / 3600.0 * 1.0); };
	Context.Weather.MeanTemperatureC = [](double, double) { return 15.0f; };
	Context.IsBlocked = [&AllGrid](const FVector2f& P) { return AllGrid.Overlaps(P, 0.1f); };
	int32 Fruits = 0;
	int32 FruitsInside = 0;
	for (TPair<FName, FMushroomSpeciesRow>& Pair : Species)
	{
		FMushroomSpeciesRow AllYear = Pair.Value;
		AllYear.FruitingMonths = FTrapezoid();
		TArray<FMyceliumPatch> Patches;
		SpawnModel::GeneratePatches(AllYear, Pair.Key, *Field, Patches);
		for (FMyceliumPatch& Patch : Patches)
		{
			Patch.Strength = 1.0f;
			TArray<FFruitBody> Bodies;
			for (int32 Day = 40; Day < 46; ++Day)
			{
				SpawnModel::FruitBodiesAt(AllYear, Patch, Context, SpawnModel::GrowthTickSeconds(Day) + 0.6 * GameSecondsPerDay, Bodies);
			}
			for (const FFruitBody& Fruit : Bodies)
			{
				++Fruits;
				FruitsInside += AllGrid.Overlaps(Fruit.PositionM, 0.05f) ? 1 : 0;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d of %d mushrooms inside a trunk, log or stump"), FruitsInside, Fruits));
	TestTrue(TEXT("Mushrooms were generated"), Fruits > 50);
	TestEqual(TEXT("No mushroom inside a trunk, log or stump"), FruitsInside, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
