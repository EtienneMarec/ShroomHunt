#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ForestGenerationLibrary.generated.h"

/**
 * Editor/tool entry points for forest generation (Python: unreal.ForestGenerationLibrary,
 * console: sh.Forest.Bake).
 */
UCLASS()
class SHROOMHUNT_API UForestGenerationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Generates the forest field of Data/Forests/<ForestName>.json and writes the heightmap,
	 * weightmaps, debug maps and report to Saved/ForestBake/<ForestName>/.
	 * Returns a report starting with "OK" or "FAILED".
	 */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Forest")
	static FString BakeForest(const FString& ForestName);
};
