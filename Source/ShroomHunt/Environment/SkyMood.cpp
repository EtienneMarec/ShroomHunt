#include "Environment/SkyMood.h"

#include "Core/ShroomMath.h"

namespace SkyMood
{
	FSkyMood Compute(float Overcast, float Rain, float Storm, const FSkyMoodParams& Params)
	{
		using ShroomMath::SmoothStep01;
		const float Cloud = FMath::Clamp(Overcast, 0.0f, 1.0f);
		const float RainAmount = FMath::Clamp(Rain, 0.0f, 1.0f);
		const float StormAmount = FMath::Clamp(Storm, 0.0f, 1.0f);

		FSkyMood Mood;
		// Broken clouds stay bright; a closed sky, rain and storms darken it.
		Mood.Gloom = FMath::Clamp(0.6f * SmoothStep01(0.4f, 1.0f, Cloud) + 0.25f * RainAmount + 0.35f * StormAmount, 0.0f, 1.0f);
		Mood.SunFactor = (1.0f - 0.85f * Cloud) * (1.0f - 0.8f * StormAmount);
		Mood.SkyLightFactor = FMath::Lerp(1.0f, Params.SkyLight, Mood.Gloom);
		Mood.SkyLuminanceFactor = FMath::Lerp(1.0f, Params.SkyLuminance, Mood.Gloom);
		Mood.ExposureBiasEV = Params.ExposureBiasEV * Mood.Gloom;
		Mood.Saturation = FMath::Lerp(1.0f, Params.Saturation, Mood.Gloom);
		Mood.FogBrightness = FMath::Lerp(1.0f, Params.FogBrightness, Mood.Gloom);
		return Mood;
	}
}
