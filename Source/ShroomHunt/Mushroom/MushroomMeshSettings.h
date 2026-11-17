#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MushroomMeshSettings.generated.h"

class UMaterialInterface;

/** Procedural mushroom mesh settings. Edit in Project Settings > ShroomHunt > Mushroom Meshes. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Mushroom Meshes"))
class SHROOMHUNT_API UMushroomMeshSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Material applied to every mushroom (reads vertex color and UV1 surface id). */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	TSoftObjectPtr<UMaterialInterface> MushroomMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Mushrooms/M_Mushroom.M_Mushroom")));

	/** Used when MushroomMaterial is missing: shows the vertex colors as-is. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering")
	TSoftObjectPtr<UMaterialInterface> FallbackMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial")));

	/** Cached mesh variants per species and stage (individuals pick one, plus scale/rotation). */
	UPROPERTY(Config, EditAnywhere, Category = "Cache", meta = (ClampMin = "1", ClampMax = "32"))
	int32 VariantsPerStage = 6;

	/** Screen size below which the far LOD (no gills, coarse) is used. */
	UPROPERTY(Config, EditAnywhere, Category = "Rendering", meta = (ClampMin = "0.001", ClampMax = "1"))
	float FarLodScreenSize = 0.05f;

	virtual FName GetCategoryName() const override { return TEXT("ShroomHunt"); }
};
