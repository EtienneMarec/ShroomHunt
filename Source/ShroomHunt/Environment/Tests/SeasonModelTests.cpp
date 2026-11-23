#include "Environment/SeasonModel.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/AutomationTest.h"
#include "Vegetation/VegetationDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SeasonModelTestsDetail
{
	const FTreePhenology* FindGenus(const TArray<FTreePhenology>& All, const TCHAR* Genus)
	{
		return All.FindByPredicate([Genus](const FTreePhenology& P) { return P.Genus == FName(Genus); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeasonFoliageTest, "ShroomHunt.Environment.Season.Foliage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSeasonFoliageTest::RunTest(const FString& Parameters)
{
	using namespace SeasonModelTestsDetail;
	FVegetationDefinition Definition;
	TArray<FString> Errors;
	if (!VegetationData::LoadVegetation(Definition, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	TArray<FTreePhenology> All;
	for (const FTreeArchetype& Tree : Definition.Trees)
	{
		All.Add(SeasonModel::FromArchetype(Tree));
	}

	for (const FTreePhenology& P : All)
	{
		const FString Genus = P.Genus.ToString();
		const FFoliageState Summer = SeasonModel::Evaluate(P, 7.5f);
		const FFoliageState Winter = SeasonModel::Evaluate(P, 1.5f);
		const FFoliageState Spring = SeasonModel::Evaluate(P, P.LeafOutMonth + 0.6f);
		TestTrue(Genus + TEXT(": full green crown in July"), Summer.LeafAmount > 0.99f && Summer.Coloring < 0.01f && Summer.Freshness < 0.01f);
		TestTrue(Genus + TEXT(": bare in January (except kept dead leaves)"), FMath::IsNearlyEqual(Winter.LeafAmount, P.WinterLeaves, 0.01f));
		TestTrue(Genus + TEXT(": young light green leaves in spring"), Spring.Freshness > 0.5f && Spring.LeafAmount > 0.8f);
		TestTrue(Genus + TEXT(": colored at the end of the coloring"), SeasonModel::Evaluate(P, P.ColoringFull + 0.05f).Coloring > 0.95f);
		TestTrue(Genus + TEXT(": fresh litter after the leaf fall, dark again by spring"),
			SeasonModel::Evaluate(P, P.FallEnd).FreshLitter > 0.9f && SeasonModel::Evaluate(P, 3.5f).FreshLitter < 0.2f);
		TestTrue(Genus + TEXT(": autumn color differs from summer"), !SeasonModel::Evaluate(P, P.ColoringFull + 0.05f).Color.Equals(Summer.Color, 0.05f));

		// Continuity over the whole year (no jump at the new year or at budburst).
		float MaxJump = 0.0f;
		FFoliageState Previous = SeasonModel::Evaluate(P, 1.0f);
		for (float Month = 1.01f; Month < 13.0f; Month += 0.01f)
		{
			const FFoliageState Now = SeasonModel::Evaluate(P, Month);
			MaxJump = FMath::Max3(MaxJump, FMath::Abs(Now.LeafAmount - Previous.LeafAmount), FMath::Abs(Now.Coloring - Previous.Coloring));
			MaxJump = FMath::Max(MaxJump, FMath::Abs(Now.FreshLitter - Previous.FreshLitter));
			Previous = Now;
		}
		const FFoliageState December = SeasonModel::Evaluate(P, 12.999f);
		const FFoliageState January = SeasonModel::Evaluate(P, 1.0f);
		MaxJump = FMath::Max(MaxJump, FMath::Abs(December.LeafAmount - January.LeafAmount));
		TestTrue(FString::Printf(TEXT("%s: smooth over the year (max step %.3f)"), *Genus, MaxJump), MaxJump < 0.06f);
	}

	// Order of the real forest: birch turns first, oak last; oak keeps leaves later than beech.
	const FTreePhenology* Betula = FindGenus(All, TEXT("Betula"));
	const FTreePhenology* Quercus = FindGenus(All, TEXT("Quercus"));
	const FTreePhenology* Fagus = FindGenus(All, TEXT("Fagus"));
	if (TestNotNull(TEXT("Betula"), Betula) && TestNotNull(TEXT("Quercus"), Quercus) && TestNotNull(TEXT("Fagus"), Fagus))
	{
		TestTrue(TEXT("Mid October: birch more colored than oak"), SeasonModel::Evaluate(*Betula, 10.5f).Coloring > SeasonModel::Evaluate(*Quercus, 10.5f).Coloring + 0.2f);
		TestTrue(TEXT("Late November: oak keeps more leaves than beech"), SeasonModel::Evaluate(*Quercus, 11.6f).LeafAmount > SeasonModel::Evaluate(*Fagus, 11.6f).LeafAmount);
		TestTrue(TEXT("A late year delays the coloring"), SeasonModel::Evaluate(*Fagus, 10.4f, 0.2f).Coloring < SeasonModel::Evaluate(*Fagus, 10.4f, -0.2f).Coloring);
	}

	// Year shifts: deterministic, small, not all the same.
	float MinShift = 1.0f;
	float MaxShift = -1.0f;
	for (int32 Year = 1; Year <= 20; ++Year)
	{
		const float Shift = SeasonModel::YearShiftMonths(1234u, Year, TEXT("Fagus"));
		TestEqual(TEXT("Year shift is deterministic"), Shift, SeasonModel::YearShiftMonths(1234u, Year, TEXT("Fagus")));
		MinShift = FMath::Min(MinShift, Shift);
		MaxShift = FMath::Max(MaxShift, Shift);
	}
	TestTrue(TEXT("Year shifts stay within a week or so"), MinShift >= -0.26f && MaxShift <= 0.26f);
	TestTrue(TEXT("Years differ (early and late autumns)"), MaxShift - MinShift > 0.1f);

	// Blend: weights respected.
	const FFoliageState Green = SeasonModel::Evaluate(*Fagus, 7.0f);
	const FFoliageState Bare = SeasonModel::Evaluate(*Betula, 1.5f);
	const FFoliageState Mixed = SeasonModel::Blend({ Green, Bare }, { 3.0f, 1.0f });
	TestTrue(TEXT("Blend is the weighted mean"), FMath::IsNearlyEqual(Mixed.LeafAmount, 0.75f * Green.LeafAmount + 0.25f * Bare.LeafAmount, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeasonCollectionTest, "ShroomHunt.Environment.Season.Collection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSeasonCollectionTest::RunTest(const FString& Parameters)
{
	// MPC_Season is created by Tools/Python/create_season_collection.py: every value the
	// director writes must exist, with unique parameter ids (materials reference them by id).
	const UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/ShroomHunt/Environment/MPC_Season.MPC_Season"));
	if (!TestNotNull(TEXT("MPC_Season exists (run Tools/Python/create_season_collection.py)"), Collection))
	{
		return false;
	}
	TSet<FGuid> Ids;
	for (const FCollectionScalarParameter& P : Collection->ScalarParameters)
	{
		Ids.Add(P.Id);
	}
	for (const FCollectionVectorParameter& P : Collection->VectorParameters)
	{
		Ids.Add(P.Id);
	}
	TestEqual(TEXT("Unique parameter ids"), Ids.Num(), Collection->ScalarParameters.Num() + Collection->VectorParameters.Num());

	TArray<FString> Scalars = { TEXT("LeafAmount"), TEXT("Coloring"), TEXT("Freshness"), TEXT("FreshLitter"), TEXT("Month"),
		TEXT("SnowCover"), TEXT("SnowFalling"), TEXT("Frost"), TEXT("Dryness") };
	TArray<FString> Vectors = { TEXT("FoliageColor") };
	FVegetationDefinition Definition;
	TArray<FString> Errors;
	if (VegetationData::LoadVegetation(Definition, Errors))
	{
		for (const FTreeArchetype& Tree : Definition.Trees)
		{
			const FString Genus = Tree.Genus.ToString();
			Scalars.Append({ Genus + TEXT("LeafAmount"), Genus + TEXT("Coloring"), Genus + TEXT("Freshness") });
			Vectors.Add(Genus + TEXT("FoliageColor"));
		}
	}
	for (const FString& Name : Scalars)
	{
		TestNotNull(FString::Printf(TEXT("Scalar %s"), *Name), Collection->GetScalarParameterByName(FName(*Name)));
	}
	for (const FString& Name : Vectors)
	{
		TestNotNull(FString::Printf(TEXT("Vector %s"), *Name), Collection->GetVectorParameterByName(FName(*Name)));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
