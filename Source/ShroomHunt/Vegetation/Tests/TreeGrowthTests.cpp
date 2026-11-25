#include "Misc/AutomationTest.h"
#include "Vegetation/TreeGrowth.h"
#include "Vegetation/VegetationDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTreeGrowthTest, "ShroomHunt.Vegetation.TreeGrowth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTreeGrowthTest::RunTest(const FString& Parameters)
{
	FVegetationDefinition Definition;
	TArray<FString> Errors;
	if (!VegetationData::LoadVegetation(Definition, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}

	for (const FTreeArchetype& Arch : Definition.Trees)
	{
		for (const FTreeFormDefinition& Form : Definition.Forms)
		{
			for (uint32 Seed = 1; Seed <= 3; ++Seed)
			{
				const FString Key = FString::Printf(TEXT("%s %s seed %u"), *Arch.Genus.ToString(), VegetationData::KindName(Form.Kind), Seed);
				const FTreeShape Shape = FTreeShape::Sample(Arch, Form, Seed);
				const FTreeSkeleton Skeleton = TreeGrowth::Grow(Arch, Form, Shape, Seed);
				const FTreeSkeleton Again = TreeGrowth::Grow(Arch, Form, Shape, Seed);

				if (!TestTrue(Key + TEXT(" has a trunk"), Skeleton.Branches.Num() > 0 && Skeleton.Branches[0].Level == 0))
				{
					continue;
				}
				TestTrue(Key + TEXT(" is deterministic"), Skeleton.Branches.Num() == Again.Branches.Num() && Skeleton.Leaves.Num() == Again.Leaves.Num()
					&& (Skeleton.Leaves.Num() == 0 || Skeleton.Leaves.Last().Base.Equals(Again.Leaves.Last().Base, 1e-9)));
				TestTrue(FString::Printf(TEXT("%s has main branches (%d)"), *Key, Skeleton.CountBranches(1)), Skeleton.CountBranches(1) >= 3);
				const int32 TwigLevel = Form.Kind == EVegetationKind::Sapling ? 2 : 3;
				TestTrue(Key + TEXT(" has leaf-bearing twigs"), Skeleton.CountBranches(TwigLevel) > 0);

				const int32 Budget = TreeGrowth::GetLeafBudget(Form.Kind);
				TestTrue(FString::Printf(TEXT("%s leaves %d within the budget %d"), *Key, Skeleton.Leaves.Num(), Budget),
					Skeleton.Leaves.Num() > Budget / 4 && Skeleton.Leaves.Num() <= FMath::CeilToInt(Budget * 1.15));

				// Branches stay in the crown: below the top, within the envelope (plus the twig slack and a leaf).
				bool bInside = true;
				bool bThinner = true;
				for (const FTreeBranch& Branch : Skeleton.Branches)
				{
					if (Branch.Level == 0)
					{
						continue;
					}
					for (const FVector3d& P : Branch.Points)
					{
						const FVector3d Axis = TreeGrowth::TrunkAxis(Shape, Seed, P.Z);
						const double Distance = FVector2d(P.X - Axis.X, P.Y - Axis.Y).Size();
						const double Allowed = Shape.CrownRadiusM * (1.0 + Arch.Lumpiness * 0.8) * 1.12 + 0.6;
						bInside &= P.Z <= Shape.HeightM * 1.04 && Distance <= Allowed;
					}
					if (Branch.Parent != INDEX_NONE)
					{
						bThinner &= Branch.Radii[0] <= Skeleton.Branches[Branch.Parent].Radii[0] + 1e-6;
					}
				}
				TestTrue(Key + TEXT(" branches stay inside the crown envelope"), bInside);
				TestTrue(Key + TEXT(" branches are thinner than their parent"), bThinner);

				bool bLeavesOk = true;
				for (const FTreeLeaf& Leaf : Skeleton.Leaves)
				{
					bLeavesOk &= FMath::Abs(FVector3d::DotProduct(Leaf.Axis, Leaf.Normal)) < 1e-3 && Leaf.Length > 0.0 && Leaf.Width > 0.0
						&& Leaf.Random >= 0.0f && Leaf.Random < 1.0f && Leaf.Light >= 0.0f && Leaf.Light <= 1.0f;
				}
				TestTrue(Key + TEXT(" leaves: normal across the blade, sizes and attributes valid"), bLeavesOk);

				if (Form.Kind == EVegetationKind::Mature)
				{
					const double TrunkTop = Skeleton.Branches[0].Points.Last().Z / Shape.HeightM;
					if (Arch.Excurrent >= 0.5f)
					{
						TestTrue(FString::Printf(TEXT("%s: excurrent trunk runs to the top (%.2f of the height)"), *Key, TrunkTop), TrunkTop > 0.85);
					}
					else
					{
						TestTrue(FString::Printf(TEXT("%s: decurrent trunk splits into limbs (%.2f of the height)"), *Key, TrunkTop), TrunkTop < 0.75);
					}
				}
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
