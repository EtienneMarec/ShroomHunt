#pragma once

#include "CoreMinimal.h"
#include "Vegetation/VegetationScatter.h"

/** The solid footprint of a placed tree trunk, log or stump: a capsule on the ground (field meters). */
struct SHROOMHUNT_API FVegetationObstacle
{
	FVector2f A = FVector2f::ZeroVector;
	FVector2f B = FVector2f::ZeroVector;
	float RadiusM = 0.0f;

	float DistanceTo(const FVector2f& P) const;
};

/**
 * Spatial hash of the solid parts of the vegetation (trunks, logs, stumps), so that other
 * things do not grow inside them: understory and dead wood around the trees, mushrooms
 * around everything. Soft plants (bracken, brambles) are not obstacles.
 */
class SHROOMHUNT_API FVegetationObstacleGrid
{
public:
	/** Footprint of one instance; false for soft plants. */
	static bool MakeObstacle(const FVegetationDefinition& Definition, const FVegetationInstance& Instance, FVegetationObstacle& Out);

	/**
	 * Low leafy cover of one instance: the disc of bracken, brambles and shrubs (and of the ground layer's
	 * grass tufts and herbs) that hides what lies on the ground from a low point of view (demo close-ups);
	 * false for everything else.
	 */
	static bool MakeCover(const FVegetationDefinition& Definition, const FVegetationInstance& Instance, FVegetationObstacle& Out);

	/**
	 * What blocks a view at eye height: the low cover (MakeCover) plus the crowns of saplings and young
	 * trees, which start low on the stem (demo shots of landmarks).
	 */
	static bool MakeViewCover(const FVegetationDefinition& Definition, const FVegetationInstance& Instance, FVegetationObstacle& Out);

	void Add(const FVegetationObstacle& Obstacle);
	void AddInstances(const FVegetationDefinition& Definition, const TArray<FVegetationInstance>& Instances);
	/** Adds the cover (MakeCover) of the instances instead of their solid parts. */
	void AddCover(const FVegetationDefinition& Definition, const TArray<FVegetationInstance>& Instances);
	/** Adds the view cover (MakeViewCover) of the instances. */
	void AddViewCover(const FVegetationDefinition& Definition, const TArray<FVegetationInstance>& Instances);

	/** True when a disc of RadiusM at P touches an obstacle. */
	bool Overlaps(const FVector2f& P, float RadiusM) const;

	/** True when a capsule (segment A-B, RadiusM) touches an obstacle (sampled every half radius). */
	bool OverlapsCapsule(const FVegetationObstacle& Capsule) const;

	int32 Num() const { return Obstacles.Num(); }

private:
	static constexpr float CellM = 4.0f;
	TArray<FVegetationObstacle> Obstacles;
	TMap<FIntPoint, TArray<int32>> Cells;
};
