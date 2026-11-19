#pragma once

#include "CoreMinimal.h"

/** How the forest canopy changes the rain a player feels (pure, unit-tested). */
struct SHROOMHUNT_API FLocalRainParams
{
	/** Fraction of the rain caught by a full canopy. */
	float CanopyInterception = 0.7f;
	/** Canopy below which no drops fall from leaves. */
	float DripCanopyThreshold = 0.25f;
	/** Fraction of the wind stopped by a full canopy. */
	float CanopyWindReduction = 0.6f;
};

struct SHROOMHUNT_API FLocalRain
{
	/** Rain reaching the ground directly, same scale as the input intensity. */
	float Direct = 0.0f;
	/** Big drops falling from wet leaves, in [0, 1]; keeps falling after the rain while the forest is wet. */
	float Drip = 0.0f;
	/** Multiplier of the wind under the canopy. */
	float WindFactor = 1.0f;
};

namespace LocalRain
{
	/**
	 * Intensity: rain intensity above the canopy (1 = rain rate for full intensity);
	 * Wetness: ground wetness in [0, 1] (the forest stays wet after the rain);
	 * Canopy: canopy cover in [0, 1] at the player's position.
	 */
	SHROOMHUNT_API FLocalRain Compute(float Intensity, float Wetness, float Canopy, const FLocalRainParams& Params = FLocalRainParams());
}
