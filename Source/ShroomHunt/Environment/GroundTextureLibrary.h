#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GroundTextureLibrary.generated.h"

/**
 * Editor entry points for the generated ground textures (D-042). Console: sh.Ground.BakeTextures,
 * sh.Ground.Preview. Like the vegetation meshes, the textures are build output: the editor
 * rebuilds them at startup when Data/Environment/GroundTextures.json, the leaf data or the
 * generator changed (stamp in Saved/GroundTextureBake.txt).
 */
UCLASS()
class SHROOMHUNT_API UGroundTextureLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Generates every layer of GroundTextures.json and saves T_Ground_<Layer>_D (sRGB albedo,
	 * alpha = height) and T_Ground_<Layer>_N (normal map) in the ground texture folder, plus PNG
	 * previews in Saved/GroundTextures. Editor only. Returns a report starting with "OK" or "FAILED".
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Ground")
	static FString BakeGroundTextures();

	/** Writes only the PNG previews (no asset): albedo, normal and a lit 2 x 2 tiling per layer. */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Ground")
	static FString WriteGroundPreviews();

	static bool IsBakeUpToDate(FString& OutReason);
	static void BakeIfNeeded();
};
