#pragma once

#include "CoreMinimal.h"
#include "Vegetation/VegetationDefinition.h"

class FForestField;
struct FForestSample;

/** One placed plant or piece of dead wood. */
struct SHROOMHUNT_API FVegetationInstance
{
	/** Field meters; Z is the terrain height (m) minus a small sink. */
	FVector3d PositionM = FVector3d::ZeroVector;
	float YawDeg = 0.0f;
	float PitchDeg = 0.0f;
	float RollDeg = 0.0f;
	float Scale = 1.0f;
	EVegetationKind Kind = EVegetationKind::Mature;
	/** Tree genus (trees and saplings), NAME_None otherwise. */
	FName Genus;
	int32 Variant = 0;

	FVegetationMeshSpec GetMeshSpec() const;
};

/**
 * Deterministic vegetation placement from the forest field (pure C++, D-024).
 *
 * Each process (trees, and each understory kind) has a global grid of candidate cells with
 * one jittered candidate per cell, kept with probability density x cell area, then thinned
 * Matern-style: a candidate is dropped when a kept neighbor with a higher priority hash is
 * closer than the spacing radius. Everything is a function of the forest seed and the
 * position, so any region (a PCG partition) gives exactly the instances of the whole forest
 * that fall in it.
 */
namespace VegetationScatter
{
	/**
	 * Instances of one layer whose position lies in [Min, Max) of BoundsM (field meters).
	 * Ground debris is tilted onto the terrain slope; it skips the trunk test of the understory.
	 */
	SHROOMHUNT_API void Scatter(const FForestField& Field, const FVegetationDefinition& Definition, EVegetationLayer Layer, const FBox2d& BoundsM, TArray<FVegetationInstance>& Out);

	/** Trees per hectare at a point before thinning and water margins (age class x canopy gaps). */
	SHROOMHUNT_API float TreeDensityPerHa(const FVegetationDefinition& Definition, const FForestSample& Sample);

	/** Instances per hectare of an understory kind at a point before thinning. */
	SHROOMHUNT_API float UnderstoryDensityPerHa(const FUnderstoryRule& Rule, const FForestSample& Sample);

	/** Thinning radius never exceeds this (keeps the neighborhood search bounded). */
	constexpr double MaxSpacingM = 10.0;
}
