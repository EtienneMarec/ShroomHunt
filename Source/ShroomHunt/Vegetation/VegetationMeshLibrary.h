#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VegetationMeshLibrary.generated.h"

/**
 * Editor/tool entry points for the vegetation (Python: unreal.VegetationMeshLibrary,
 * console: sh.Vegetation.BakeMeshes / sh.Vegetation.Stats).
 */
UCLASS()
class SHROOMHUNT_API UVegetationMeshLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds every mesh of Data/Vegetation/Vegetation.json and saves it as a Nanite static mesh
	 * asset SM_<Key> in Folder (empty = Project Settings > ShroomHunt > Vegetation). Existing
	 * assets are updated in place, so references (PCG graphs, levels) stay valid.
	 * Editor only. Returns a report starting with "OK" or "FAILED".
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Vegetation")
	static FString BakeVegetationMeshes(const FString& Folder);

	/** Instance counts per kind for a whole forest (to tune densities). */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Vegetation")
	static FString ScatterReport(const FString& ForestName);
};
