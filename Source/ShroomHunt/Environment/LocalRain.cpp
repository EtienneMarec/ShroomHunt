#include "Environment/LocalRain.h"

#include "Core/ShroomMath.h"

namespace LocalRain
{
	FLocalRain Compute(float Intensity, float Wetness, float Canopy, const FLocalRainParams& Params)
	{
		using ShroomMath::SmoothStep01;
		const float Cover = FMath::Clamp(Canopy, 0.0f, 1.0f);
		const float Rain = FMath::Max(0.0f, Intensity);

		FLocalRain Out;
		Out.Direct = Rain * (1.0f - Params.CanopyInterception * Cover);
		// Leaves hold water: they drip during the rain and for a while after it.
		const float LeafWater = FMath::Max(FMath::Min(Rain, 1.0f), 0.6f * SmoothStep01(0.35f, 0.9f, Wetness));
		Out.Drip = SmoothStep01(Params.DripCanopyThreshold, 0.9f, Cover) * LeafWater;
		Out.WindFactor = 1.0f - Params.CanopyWindReduction * Cover;
		return Out;
	}
}
