#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Where a forest field sits in a level. */
namespace ForestWorld
{
	/**
	 * Transform from field coordinates (x m, y m, height m) to world centimeters: the level's
	 * Landscape (one vertex per meter, heightmap baked by ForestBake) when there is one, else
	 * FallbackOriginCm with 100 cm per meter. Game thread only (iterates the world's actors).
	 */
	SHROOMHUNT_API FTransform GetFieldToWorld(UWorld* World, const FVector& FallbackOriginCm, bool* bOutFoundLandscape = nullptr);
}
