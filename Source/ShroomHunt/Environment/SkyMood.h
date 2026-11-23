#pragma once

#include "CoreMinimal.h"

/** Strength of the bad-weather darkening at full gloom (storm under a black sky). */
struct SHROOMHUNT_API FSkyMoodParams
{
	/** Exposure compensation (EV) added at full gloom: auto exposure would otherwise brighten a dark sky back. */
	float ExposureBiasEV = -1.3f;
	/** Sky atmosphere luminance factor at full gloom (1 = unchanged). */
	float SkyLuminance = 0.3f;
	/** Sky light intensity factor at full gloom. */
	float SkyLight = 0.45f;
	/** Color saturation at full gloom. */
	float Saturation = 0.65f;
	/** Fog inscattering brightness at full gloom (the fog turns grey). */
	float FogBrightness = 0.4f;
};

/** How bad weather darkens the scene, from 0 (neutral) to full gloom. Pure, unit-tested. */
struct SHROOMHUNT_API FSkyMood
{
	/** 0 = clear or broken clouds, 1 = storm under a solid black sky. */
	float Gloom = 0.0f;
	float SunFactor = 1.0f;
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
