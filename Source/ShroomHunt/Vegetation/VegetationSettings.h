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
	/**
	 * Content folder of the baked vegetation meshes (sh.Vegetation.BakeMeshes writes SM_<Key> there).
	 * Build output, not versioned: the editor rebuilds them at startup when needed (D-040).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	FString GeneratedMeshFolder = TEXT("/Game/ShroomHunt/Environment/Vegetation/Generated");

	/**
	 * Rebuild the vegetation meshes when the editor starts (before the startup map loads) if one is
	 * missing or Data/Vegetation/Vegetation.json / the builder version changed since the last bake.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	bool bBakeOnEditorStartup = true;

	/**
	 * Content folder of the generated ground textures T_Ground_<Layer>_D / _N (D-042). Build output like
	 * the meshes: rebuilt at editor startup from Data/Environment/GroundTextures.json when needed.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Ground Textures")
	FString GroundTextureFolder = TEXT("/Game/ShroomHunt/Environment/Landscape/Generated");

	/** Material of the opaque slot (bark, wood, berries, ground litter; D-044): an opaque instance of M_Vegetation. */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	TSoftObjectPtr<UMaterialInterface> VegetationMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Environment/Vegetation/MI_VegetationWood.MI_VegetationWood")));

	/** Material of the foliage slot (living leaves and fronds): a masked, two-sided instance of M_Vegetation. */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	TSoftObjectPtr<UMaterialInterface> FoliageMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Environment/Vegetation/MI_VegetationFoliage.MI_VegetationFoliage")));

	/** The master vegetation material (vertex color albedo, seasons, wetness, snow), used while an instance is missing. */
	UPROPERTY(Config, EditAnywhere, Category = "Meshes")
	TSoftObjectPtr<UMaterialInterface> MasterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Environment/Vegetation/M_Vegetation.M_Vegetation")));

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
