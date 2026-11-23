#pragma once

#include "CoreMinimal.h"

/** Strength of the bad-weather look at full gloom (storm under a black sky) and under a closed sky. */
struct SHROOMHUNT_API FSkyMoodParams
{
	/** Exposure compensation (EV) added at full gloom: auto exposure would otherwise brighten a dark sky back. */
	float ExposureBiasEV = -0.7f;
	/** Sky atmosphere luminance factor at full gloom (1 = unchanged). */
	float SkyLuminance = 0.3f;
	/** Sky light intensity factor at full gloom (it stays the main light under clouds). */
	float SkyLight = 0.65f;
	/** Color saturation at full gloom. */
	float Saturation = 0.65f;
	/** Fog inscattering brightness at full gloom (the fog turns grey). */
	float FogBrightness = 0.4f;
	/** Sun shadow strength under a closed sky (the light becomes diffuse). */
	float OvercastShadowAmount = 0.15f;
	/** Sun source angle multiplier under a closed sky (softer, blurred shadows). */
	float OvercastSourceAngleScale = 15.0f;
};

/**
 * How bad weather changes the light. Under a closed sky the sun's direct light and its
 * sharp shadows go away and the diffuse sky light remains (merely dimming the sun looks
 * like an eclipse); rain and storms then darken everything. Pure, unit-tested.
 */
struct SHROOMHUNT_API FSkyMood
{
	/** 0 = clear or broken clouds, 1 = storm under a solid black sky. */
	float Gloom = 0.0f;
	/** 0 = sharp sunlight, 1 = closed sky (diffuse light only). */
	float Diffuse = 0.0f;
	float SunFactor = 1.0f;
	float ShadowAmount = 1.0f;
	float SourceAngleScale = 1.0f;
	float SkyLightFactor = 1.0f;
	float SkyLuminanceFactor = 1.0f;
	float ExposureBiasEV = 0.0f;
	float Saturation = 1.0f;
	float FogBrightness = 1.0f;
};

namespace SkyMood
{
	/** Overcast: cloud cover in [0, 1]; Rain: rain intensity (1 = full); Storm: storm intensity in [0, 1]. */
	SHROOMHUNT_API FSkyMood Compute(float Overcast, float Rain, float Storm, const FSkyMoodParams& Params = FSkyMoodParams());
}
