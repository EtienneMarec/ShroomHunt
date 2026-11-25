#include "DynamicMesh/DynamicMesh3.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestPaths.h"
#include "Misc/AutomationTest.h"
#include "Vegetation/VegetationDefinition.h"
#include "Vegetation/VegetationMeshBuilder.h"
#include "Vegetation/VegetationScatter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace VegetationTestsDetail
{
	const TCHAR* ForestName = TEXT("F01_LowlandOakBeech");

	/** Signed volume with the engine's triangle normal convention (positive = outward faces). */
	double SignedVolume(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		double Volume = 0.0;
		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			FVector3d Normal, Centroid;
			double Area = 0.0;
			Mesh.GetTriInfo(Tri, Normal, Area, Centroid);
			Volume += Area * FVector3d::DotProduct(Normal, Centroid) / 3.0;
		}
		return Volume;
	}

	uint32 HashMesh(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		uint32 H = GetTypeHash(Mesh.TriangleCount());
		for (const int32 V : Mesh.VertexIndicesItr())
		{
			const FVector3d P = Mesh.GetVertex(V);
			H = HashCombineFast(H, GetTypeHash(FIntVector(FMath::RoundToInt(P.X * 10.0), FMath::RoundToInt(P.Y * 10.0), FMath::RoundToInt(P.Z * 10.0))));
		}
		return H;
	}

	/** Order-independent signature of a set of instances (position to the millimeter, kind, genus, variant). */
	uint64 HashInstances(const TArray<FVegetationInstance>& Instances)
	{
		uint64 Sum = 0;
		for (const FVegetationInstance& I : Instances)
		{
			uint32 H = GetTypeHash(FIntVector(FMath::RoundToInt(I.PositionM.X * 1000.0), FMath::RoundToInt(I.PositionM.Y * 1000.0), FMath::RoundToInt(I.PositionM.Z * 1000.0)));
			H = HashCombineFast(H, HashCombineFast(GetTypeHash(static_cast<uint8>(I.Kind)), HashCombineFast(GetTypeHash(I.Genus), GetTypeHash(I.Variant))));
			Sum += H;
		}
		return Sum;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVegetationDefinitionTest, "ShroomHunt.Vegetation.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVegetationDefinitionTest::RunTest(const FString& Parameters)
{
	FVegetationDefinition Definition;
	TArray<FString> Errors;
	TestTrue(TEXT("Vegetation.json parses: ") + FString::Join(Errors, TEXT("; ")), VegetationData::LoadVegetation(Definition, Errors));
	TestEqual(TEXT("Five tree genera"), Definition.Trees.Num(), 5);
	TestNotNull(TEXT("Mature form"), Definition.FindForm(EVegetationKind::Mature));

	const TArray<FVegetationMeshSpec> Specs = VegetationData::ListMeshes(Definition);
	int32 Expected = 0;
	for (const FTreeFormDefinition& Form : Definition.Forms)
	{
		Expected += Definition.Trees.Num() * Form.Variants;
	}
	for (const FUnderstoryRule& Rule : Definition.Understory)
	{
		Expected += Rule.Kind == EVegetationKind::Sapling ? 0 : Rule.Variants;
	}
	for (const FUnderstoryRule& Rule : Definition.Ground)
	{
		Expected += Rule.Variants * (Rule.Kind == EVegetationKind::Leaf ? Definition.Trees.Num() : 1);
	}
	TestEqual(TEXT("One mesh per genus x form x variant plus understory and ground variants (leaf litter per genus)"), Specs.Num(), Expected);
	TestEqual(TEXT("Four ground debris kinds"), Definition.GetRules(EVegetationLayer::Ground).Num(), 4);
	TSet<FString> Keys;
	for (const FVegetationMeshSpec& Spec : Specs)
	{
		Keys.Add(Spec.GetKey());
	}
	TestEqual(TEXT("Mesh keys are unique"), Keys.Num(), Specs.Num());
	TestTrue(TEXT("Key format"), Keys.Contains(TEXT("Fagus_Mature_0")) && Keys.Contains(TEXT("Bracken_0"))
		&& Keys.Contains(TEXT("Quercus_Leaf_0")) && Keys.Contains(TEXT("Stones_0")));

	FVegetationDefinition Bad;
	TArray<FString> BadErrors;
	TestFalse(TEXT("A definition without trees and forms is rejected"),
		VegetationData::ParseVegetation(TEXT("{ \"Understory\": [ { \"Kind\": \"Mature\", \"Moisture\": [1, 0, 0, 0] } ] }"), TEXT("Test"), Bad, BadErrors));
	TestTrue(TEXT("Several errors reported (trees, forms, density, understory kind, trapezoids)"), BadErrors.Num() >= 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVegetationMeshTest, "ShroomHunt.Vegetation.Meshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVegetationMeshTest::RunTest(const FString& Parameters)
{
	using namespace VegetationTestsDetail;
	FVegetationDefinition Definition;
	TArray<FString> Errors;
	if (!VegetationData::LoadVegetation(Definition, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}

	for (const FVegetationMeshSpec& Spec : VegetationData::ListMeshes(Definition))
	{
		const FString Key = Spec.GetKey();
		UE::Geometry::FDynamicMesh3 Mesh;
		UE::Geometry::FDynamicMesh3 Again;
		FVegetationCollision Collision;
		if (!TestTrue(Key + TEXT(" builds"), VegetationMeshBuilder::Build(Definition, Spec, Mesh, &Collision)))
		{
			continue;
		}
		VegetationMeshBuilder::Build(Definition, Spec, Again);
		TestEqual(Key + TEXT(" is deterministic"), HashMesh(Mesh), HashMesh(Again));
		TestTrue(FString::Printf(TEXT("%s stays under 200k triangles (%d)"), *Key, Mesh.TriangleCount()), Mesh.TriangleCount() < 200000);

		const UE::Geometry::FAxisAlignedBox3d Bounds = Mesh.GetBounds();
		const bool bGround = VegetationData::GetLayer(Spec.Kind) == EVegetationLayer::Ground;
		if (bGround)
		{
			TestTrue(FString::Printf(TEXT("%s lies flat on the ground (%.1f to %.1f cm)"), *Key, Bounds.Min.Z, Bounds.Max.Z),
				Bounds.Min.Z < 2.0 && Bounds.Max.Z > 0.3 && Bounds.Max.Z < 40.0);
			TestTrue(Key + TEXT(" stays small (< 2.5 m wide: 2 m leaf squares plus overhang)"), Bounds.Width() < 250.0 && Bounds.Height() < 250.0);
			TestTrue(Key + TEXT(" is light (< 5k triangles: many instances)"), Mesh.TriangleCount() < 5000);
		}
		else
		{
			TestTrue(Key + TEXT(" stands on the ground"), Bounds.Min.Z < 5.0 && Bounds.Max.Z > 5.0);
		}

		const bool bTree = Spec.Kind == EVegetationKind::Pole || Spec.Kind == EVegetationKind::Mature || Spec.Kind == EVegetationKind::Old || Spec.Kind == EVegetationKind::Young;
		TestEqual(Key + TEXT(" collision: capsule for trees and dead wood only"), Collision.bHasCapsule,
			bTree || Spec.Kind == EVegetationKind::Log || Spec.Kind == EVegetationKind::Stump);
		if (Spec.Kind != EVegetationKind::Bracken && Spec.Kind != EVegetationKind::Leaf && !VegetationData::IsShrubKind(Spec.Kind))
		{
			// Every part is a closed or nearly closed surface facing outward (bracken and leaves are double-sided).
			TestTrue(Key + TEXT(" faces outward (positive volume)"), SignedVolume(Mesh) > 0.0);
		}
		if (Spec.Kind == EVegetationKind::Mature)
		{
			const FTreeArchetype* Tree = Definition.FindTree(Spec.Genus);
			const double HeightM = Bounds.Max.Z / 100.0;
			TestTrue(FString::Printf(TEXT("%s height %.1f m close to the archetype's %.0f m"), *Key, HeightM, Tree->HeightM),
				HeightM > Tree->HeightM * 0.8 && HeightM < Tree->HeightM * 1.2);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVegetationScatterTest, "ShroomHunt.Vegetation.Scatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVegetationScatterTest::RunTest(const FString& Parameters)
{
	using namespace VegetationTestsDetail;
	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(ForestName, Errors);
	const TSharedPtr<const FVegetationDefinition> Definition = ForestFieldCache::GetVegetation(Errors);
	if (!Field || !Definition)
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	const double SizeM = Field->GetSizeM();
	const FBox2d Whole(FVector2d::ZeroVector, FVector2d(SizeM, SizeM));

	for (const EVegetationLayer Layer : { EVegetationLayer::Trees, EVegetationLayer::Understory, EVegetationLayer::Ground })
	{
		const FString Name = StaticEnum<EVegetationLayer>()->GetNameStringByValue(static_cast<int64>(Layer));
		// Ground debris counts millions of leaves over the forest (generated around the player
		// at runtime): its tests run on a 9 ha area in the middle of the forest.
		const bool bGround = Layer == EVegetationLayer::Ground;
		const FBox2d Area = bGround ? FBox2d(FVector2d(350.0, 350.0), FVector2d(650.0, 650.0)) : Whole;
		const double AreaHa = Area.GetArea() / 10000.0;
		TArray<FVegetationInstance> All;
		TArray<FVegetationInstance> AllAgain;
		VegetationScatter::Scatter(*Field, *Definition, Layer, Area, All);
		VegetationScatter::Scatter(*Field, *Definition, Layer, Area, AllAgain);
		TestEqual(Name + TEXT(": deterministic"), HashInstances(All), HashInstances(AllAgain));

		// Uneven split (PCG partitions do not align with the candidate grid): same instances.
		TArray<FVegetationInstance> Parts;
		const double SplitX = Area.Min.X + 0.30037 * (Area.Max.X - Area.Min.X);
		const double SplitY = Area.Min.Y + 0.6119 * (Area.Max.Y - Area.Min.Y);
		const FBox2d Regions[4] = {
			FBox2d(Area.Min, FVector2d(SplitX, SplitY)),
			FBox2d(FVector2d(SplitX, Area.Min.Y), FVector2d(Area.Max.X, SplitY)),
			FBox2d(FVector2d(Area.Min.X, SplitY), FVector2d(SplitX, Area.Max.Y)),
			FBox2d(FVector2d(SplitX, SplitY), Area.Max) };
		for (const FBox2d& Region : Regions)
		{
			VegetationScatter::Scatter(*Field, *Definition, Layer, Region, Parts);
		}
		TestEqual(Name + TEXT(": partitions give the same count"), Parts.Num(), All.Num());
		TestEqual(Name + TEXT(": partitions give the same instances"), HashInstances(Parts), HashInstances(All));

		int32 InWater = 0;
		for (const FVegetationInstance& I : All)
		{
			const FForestSample S = Field->Sample(static_cast<float>(I.PositionM.X), static_cast<float>(I.PositionM.Y));
			InWater += S.bWater ? 1 : 0;
		}
		TestEqual(Name + TEXT(": nothing grows in the water"), InWater, 0);

		if (Layer == EVegetationLayer::Trees)
		{
			AddInfo(FString::Printf(TEXT("%d trees"), All.Num()));
			TestTrue(TEXT("Plausible number of trees for 1 km2 (5k - 60k)"), All.Num() > 5000 && All.Num() < 60000);

			double AlderWater = 0.0, BeechWater = 0.0;
			int32 Alders = 0, Beeches = 0;
			for (const FVegetationInstance& I : All)
			{
				const float Distance = Field->Sample(static_cast<float>(I.PositionM.X), static_cast<float>(I.PositionM.Y)).WaterDistanceM;
				if (I.Genus == FName(TEXT("Alnus"))) { AlderWater += Distance; ++Alders; }
				if (I.Genus == FName(TEXT("Fagus"))) { BeechWater += Distance; ++Beeches; }
			}
			TestTrue(TEXT("Alders and beeches present"), Alders > 0 && Beeches > 0);
			if (Alders > 0 && Beeches > 0)
			{
				TestTrue(FString::Printf(TEXT("Alders grow closer to water than beeches (%.0f m vs %.0f m)"), AlderWater / Alders, BeechWater / Beeches),
					AlderWater / Alders < BeechWater / Beeches);
			}
		}
		else if (Layer == EVegetationLayer::Ground)
		{
			TMap<EVegetationKind, int32> Counts;
			int32 OnRoad = 0, ForeignLeaves = 0, Litter = 0, LitterOnTracks = 0;
			double StoneSlope = 0.0, AllSlope = 0.0;
			int32 Stones = 0;
			for (const FVegetationInstance& I : All)
			{
				++Counts.FindOrAdd(I.Kind);
				const FForestSample S = Field->Sample(static_cast<float>(I.PositionM.X), static_cast<float>(I.PositionM.Y));
				const bool bOnPath = S.PathRank >= 0 && S.PathDistanceM < ForestPaths::HalfWidthM(static_cast<EForestPathRank>(S.PathRank), Field->GetDefinition());
				OnRoad += (bOnPath && S.PathRank == static_cast<int32>(EForestPathRank::Road)) ? 1 : 0;
				if (I.Kind == EVegetationKind::Leaf)
				{
					++Litter;
					LitterOnTracks += bOnPath ? 1 : 0;
					const int32 GenusIndex = Field->GetGenera().IndexOfByKey(I.Genus);
					ForeignLeaves += (GenusIndex == INDEX_NONE || !S.StandWeights.IsValidIndex(GenusIndex) || S.StandWeights[GenusIndex] <= 0.0f) ? 1 : 0;
				}
				const FFieldRaster& H = Field->GetHeights();
				const float X = static_cast<float>(I.PositionM.X), Y = static_cast<float>(I.PositionM.Y);
				const double Slope = FVector2d((H.Sample(X + 1.0f, Y) - H.Sample(X - 1.0f, Y)) * 0.5, (H.Sample(X, Y + 1.0f) - H.Sample(X, Y - 1.0f)) * 0.5).Size();
				AllSlope += Slope;
				if (I.Kind == EVegetationKind::Stones)
				{
					StoneSlope += Slope;
					++Stones;
				}
			}
			AddInfo(FString::Printf(TEXT("%d ground debris on %.0f ha: %d leaves, %d twigs, %d stones, %d moss"), All.Num(), AreaHa,
				Counts.FindRef(EVegetationKind::Leaf), Counts.FindRef(EVegetationKind::Twigs), Counts.FindRef(EVegetationKind::Stones), Counts.FindRef(EVegetationKind::Moss)));
			for (const EVegetationKind Kind : { EVegetationKind::Leaf, EVegetationKind::Twigs, EVegetationKind::Stones, EVegetationKind::Moss })
			{
				TestTrue(FString::Printf(TEXT("%s present"), VegetationData::KindName(Kind)), Counts.FindRef(Kind) > 0);
			}
			const double LeavesPerM2 = Counts.FindRef(EVegetationKind::Leaf) / (AreaHa * 10000.0);
			TestTrue(FString::Printf(TEXT("Leaves cover the ground one by one (%.1f per m2, expected 2 - 15)"), LeavesPerM2), LeavesPerM2 > 2.0 && LeavesPerM2 < 15.0);
			TestEqual(TEXT("No debris on the gravel roads"), OnRoad, 0);
			TestTrue(FString::Printf(TEXT("Leaves also cover rides and footpaths (%d)"), LitterOnTracks), LitterOnTracks > 0);
			TestEqual(TEXT("Fallen leaves come from the genera of the stand"), ForeignLeaves, 0);
			if (Stones > 0 && All.Num() > 0)
			{
				TestTrue(FString::Printf(TEXT("Stones prefer slopes (%.3f vs %.3f mean slope)"), StoneSlope / Stones, AllSlope / All.Num()),
					StoneSlope / Stones > AllSlope / All.Num());
			}
		}
		else
		{
			int32 BadBracken = 0, Bracken = 0;
			for (const FVegetationInstance& I : All)
			{
				if (I.Kind == EVegetationKind::Bracken)
				{
					++Bracken;
					BadBracken += Field->Sample(static_cast<float>(I.PositionM.X), static_cast<float>(I.PositionM.Y)).PH > 6.25f ? 1 : 0;
				}
			}
			AddInfo(FString::Printf(TEXT("%d understory instances, %d bracken"), All.Num(), Bracken));
			TestTrue(TEXT("Bracken present"), Bracken > 0);
			TestEqual(TEXT("Bracken only on acidic soils (pH <= 6.2)"), BadBracken, 0);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
