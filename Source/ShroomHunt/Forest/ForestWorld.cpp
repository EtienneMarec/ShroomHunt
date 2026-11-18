#include "Forest/ForestWorld.h"

#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeProxy.h"

namespace ForestWorld
{
	FTransform GetFieldToWorld(UWorld* World, const FVector& FallbackOriginCm, bool* bOutFoundLandscape)
	{
		const ALandscapeProxy* Found = nullptr;
		if (World)
		{
			for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
			{
				Found = Found ? Found : *It;
				if (It->IsA<ALandscape>())
				{
					Found = *It;
					break;
				}
			}
		}
		if (bOutFoundLandscape)
		{
			*bOutFoundLandscape = Found != nullptr;
		}
		// A new landscape is centered on the location typed in the New Landscape tool; the
		// landscape transform maps vertex (x, y) and local Z = (value - 32768) / 128 = height (m).
		return Found ? Found->LandscapeActorToWorld() : FTransform(FQuat::Identity, FallbackOriginCm, FVector(100.0));
	}
}
