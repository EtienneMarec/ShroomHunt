#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VegetationSettings.generated.h"

class UMaterialInterface;
class UStaticMesh;
struct FVegetationMeshSpec;

/** Vegetation meshes. Edit in Project Settings > ShroomHunt > Vegetation. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Vegetation"))
class SHROOMHUNT_API UVegetationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Content folder of the baked placeholder meshes (sh.Vegetation.BakeMeshes writes SM_<Key> there). */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	FString GeneratedMeshFolder = TEXT("/Game/ShroomHunt/Environment/Vegetation/Generated");

	/** Material of the placeholder meshes (vertex color albedo). */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	TSoftObjectPtr<UMaterialInterface> VegetationMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Environment/Vegetation/M_Vegetation.M_Vegetation")));

	/** Used when VegetationMaterial does not exist yet. */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	TSoftObjectPtr<UMaterialInterface> FallbackMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial")));

	/**
	 * Replacement meshes (e.g. Megascans trees later): key "Fagus_Mature_0" for one variant or
	 * "Fagus_Mature" for every variant of a genus and form. Unlisted meshes, and listed ones whose
	 * asset is not in the project (third-party assets are not versioned, D-010), use the baked placeholders.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	TMap<FString, TSoftObjectPtr<UStaticMesh>> MeshOverrides;

	/** Static mesh used for one palette entry (override, else the baked placeholder). */
	FSoftObjectPath GetMeshPath(const FVegetationMeshSpec& Spec) const;

	/** Package path of the baked placeholder, e.g. /Game/.../Generated/SM_Fagus_Mature_0. */
	FString GetGeneratedPackageName(const FVegetationMeshSpec& Spec) const;

	virtual FName GetCategoryName() const override { return TEXT("ShroomHunt"); }
};
