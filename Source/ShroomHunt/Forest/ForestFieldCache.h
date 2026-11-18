#pragma once

#include "CoreMinimal.h"

class FForestField;
struct FVegetationDefinition;

/**
 * Shared, immutable forest data for tools and gameplay (PCG nodes, spawning, debug).
 *
 * Generating a field takes about a second, so it is done once per forest and reused. Each
 * call re-reads the small JSON file and regenerates only when its content changed, so
 * editing Data/ and regenerating a PCG graph picks the change up. Thread-safe (PCG executes
 * nodes on worker threads); the returned objects are never modified.
 */
namespace ForestFieldCache
{
	/** Field of Data/Forests/<ForestName>.json, or null (errors appended). */
	SHROOMHUNT_API TSharedPtr<const FForestField> GetField(const FString& ForestName, TArray<FString>& OutErrors);

	/** Data/Vegetation/Vegetation.json, or null (errors appended). */
	SHROOMHUNT_API TSharedPtr<const FVegetationDefinition> GetVegetation(TArray<FString>& OutErrors);

	SHROOMHUNT_API void Clear();
}
