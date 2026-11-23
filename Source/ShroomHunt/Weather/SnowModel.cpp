#include "Weather/SnowModel.h"

#include "Core/ShroomMath.h"

namespace SnowModel
{
	float SnowFraction(float TemperatureC, const FSnowParams& Params)
	{
		return 1.0f - ShroomMath::SmoothStep01(Params.SnowBelowC, Params.RainAboveC, TemperatureC);
	}

	float SnowWaterMm(TFunctionRef<FWeatherSample(double GameSeconds)> Sample, double NowGameSeconds, const FSnowParams& Params, double StepGameSeconds)
	{
		const double Step = FMath::Max(StepGameSeconds, 60.0);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Params.LookbackGameHours * 3600.0 / Step));
		const double StepHours = Step / 3600.0;
		double Water = 0.0;
		for (int32 Index = Steps - 1; Index >= 0; --Index)
		{
			const FWeatherSample S = Sample(NowGameSeconds - (Index + 0.5) * Step);
			const float Precipitation = FMath::Max(S.PrecipitationMmPerHour, 0.0f);
			const float Snow = SnowFraction(S.TemperatureC, Params);
			const float Rain = Precipitation * (1.0f - Snow);
			Water += Precipitation * Snow * StepHours;
			const double Melt = (Params.MeltMmPerDegreeHour * FMath::Max(S.TemperatureC, 0.0f) + Params.RainMeltPerMm * Rain) * StepHours;
			Water = FMath::Max(0.0, Water - Melt);
		}
		return static_cast<float>(Water);
	}

	float Cover(float SnowWaterMm, const FSnowParams& Params)
	{
		// Exponential approach: thin snow leaves leaves and grass showing through.
		const float Scale = FMath::Max(Params.FullCoverMm, 0.1f) / 3.0f;
		return FMath::Clamp(1.0f - FMath::Exp(-FMath::Max(SnowWaterMm, 0.0f) / Scale), 0.0f, 1.0f);
	}

	float Frost(float RecentTemperatureC)
	{
		return 1.0f - ShroomMath::SmoothStep01(-2.5f, 1.5f, RecentTemperatureC);
	}

	float Dryness(float RainLastDaysMm, float TemperatureC)
	{
		const float Dry = 1.0f - ShroomMath::SmoothStep01(0.5f, 8.0f, RainLastDaysMm);
		return Dry * ShroomMath::SmoothStep01(12.0f, 24.0f, TemperatureC);
	}
}
