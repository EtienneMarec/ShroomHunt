#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Vegetation/VegetationDefinition.h"
#include "Vegetation/VegetationSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVegetationOverrideTest, "ShroomHunt.Vegetation.MeshOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVegetationOverrideTest::RunTest(const FString& Parameters)
{
	// Every override key names a palette entry ("Fagus_Mature_0") or a family ("Fagus_Mature"):
	// a typo would silently keep the placeholder. The assets themselves may be missing (D-010).
	FVegetationDefinition Definition;
	TArray<FString> Errors;
	if (!VegetationData::LoadVegetation(Definition, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	TSet<FString> Keys;
	for (const FVegetationMeshSpec& Spec : VegetationData::ListMeshes(Definition))
	{
		const FString Key = Spec.GetKey();
		Keys.Add(Key);
		int32 LastUnderscore = INDEX_NONE;
		if (Key.FindLastChar(TEXT('_'), LastUnderscore))
		{
			Keys.Add(Key.Left(LastUnderscore));
		}
	}
	const UVegetationSettings* Settings = GetDefault<UVegetationSettings>();
	int32 Available = 0;
	for (const TPair<FString, TSoftObjectPtr<UStaticMesh>>& Pair : Settings->MeshOverrides)
	{
		TestTrue(FString::Printf(TEXT("Override key %s names a palette entry or family"), *Pair.Key), Keys.Contains(Pair.Key));
		const FString Package = Pair.Value.ToSoftObjectPath().GetLongPackageName();
		TestFalse(FString::Printf(TEXT("Override %s has a mesh path"), *Pair.Key), Package.IsEmpty());
		Available += !Package.IsEmpty() && FPackageName::DoesPackageExist(Package) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d mesh overrides, %d with their asset in the project"), Settings->MeshOverrides.Num(), Available));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
