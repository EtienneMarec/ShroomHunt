#pragma once

#include "CoreMinimal.h"

/** Small shared math helpers (header-only). Shared here so unity builds never see duplicates. */
namespace ShroomMath
{
	/** Hermite smoothstep of X between Edge0 and Edge1, in [0, 1]. A degenerate range is a step at Edge1. */
	FORCEINLINE float SmoothStep01(float Edge0, float Edge1, float X)
	{
		if (Edge1 <= Edge0)
		{
			return X >= Edge1 ? 1.0f : 0.0f;
		}
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}
}
