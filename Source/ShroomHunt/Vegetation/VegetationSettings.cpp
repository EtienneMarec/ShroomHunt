#include "Vegetation/VegetationSettings.h"

#include "Engine/StaticMesh.h"
#include "Vegetation/VegetationDefinition.h"

FString UVegetationSettings::GetGeneratedPackageName(const FVegetationMeshSpec& Spec) const
{
	FString Folder = GeneratedMeshFolder;
	Folder.RemoveFromEnd(TEXT("/"));
	return Folder / (TEXT("SM_") + Spec.GetKey());
}

FSoftObjectPath UVegetationSettings::GetMeshPath(const FVegetationMeshSpec& Spec) const
{
	const FString Key = Spec.GetKey();
	if (const TSoftObjectPtr<UStaticMesh>* Override = MeshOverrides.Find(Key))
	{
		return Override->ToSoftObjectPath();
	}
	FString Family = Key;
	int32 LastUnderscore = INDEX_NONE;
	if (Family.FindLastChar(TEXT('_'), LastUnderscore))
	{
		Family.LeftInline(LastUnderscore);
		if (const TSoftObjectPtr<UStaticMesh>* FamilyOverride = MeshOverrides.Find(Family))
		{
			return FamilyOverride->ToSoftObjectPath();
		}
	}
	const FString PackageName = GetGeneratedPackageName(Spec);
	return FSoftObjectPath(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName));
}
