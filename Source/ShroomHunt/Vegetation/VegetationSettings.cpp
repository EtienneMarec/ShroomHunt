#include "Vegetation/VegetationSettings.h"

#include "Engine/StaticMesh.h"
#include "Vegetation/VegetationDefinition.h"

FString UVegetationSettings::GetGeneratedPackageName(const FVegetationMeshSpec& Spec) const
{
	FString Folder = GeneratedMeshFolder;
	Folder.RemoveFromEnd(TEXT("/"));
	return Folder / (TEXT("SM_") + Spec.GetKey());
}

namespace VegetationSettingsDetail
{
	/** Replacement meshes are third-party assets not in the repository (D-010): a missing one falls back to the placeholder. */
	bool IsAvailable(const TSoftObjectPtr<UStaticMesh>* Override)
	{
		if (!Override || Override->IsNull())
		{
			return false;
		}
		const FString PackageName = Override->ToSoftObjectPath().GetLongPackageName();
		return !PackageName.IsEmpty() && FPackageName::DoesPackageExist(PackageName);
	}
}

FSoftObjectPath UVegetationSettings::GetMeshPath(const FVegetationMeshSpec& Spec) const
{
	using namespace VegetationSettingsDetail;
	const FString Key = Spec.GetKey();
	const TSoftObjectPtr<UStaticMesh>* Override = MeshOverrides.Find(Key);
	if (IsAvailable(Override))
	{
		return Override->ToSoftObjectPath();
	}
	FString Family = Key;
	int32 LastUnderscore = INDEX_NONE;
	if (Family.FindLastChar(TEXT('_'), LastUnderscore))
	{
		Family.LeftInline(LastUnderscore);
		const TSoftObjectPtr<UStaticMesh>* FamilyOverride = MeshOverrides.Find(Family);
		if (IsAvailable(FamilyOverride))
		{
			return FamilyOverride->ToSoftObjectPath();
		}
	}
	const FString PackageName = GetGeneratedPackageName(Spec);
	return FSoftObjectPath(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName));
}
