#pragma once

#include "CoreMinimal.h"

/** Recognizable places that help the player learn a forest (Docs/WorldGeneration.md section 4). */
enum class EForestLandmarkKind : uint8
{
	/** The forester's house, player base, at the end of the first entrance road. */
	ForesterHouse,
	/** Wooden hunting hut near a hub, at the edge of a young parcel. */
	HuntingHut,
	/** Raised hide (mirador) at a stand edge facing a clear-cut or a regeneration. */
	HuntingTower,
	/** Stone calvary at a road junction. */
	Calvary,
	/** Signpost with the place name and arrows to the neighboring named places. */
	Signpost,
	/** Parcel boundary stone at a star crossroads, painted with the parcel numbers. */
	ParcelStone,
	/** Footbridge or road bridge over a stream. */
	Bridge,
	/** Log pile along a road next to a mature stand. */
	LogPile,
};

/** One placed landmark (field meters; GroundM is the terrain height at the position). */
struct SHROOMHUNT_API FForestLandmark
{
	EForestLandmarkKind Kind = EForestLandmarkKind::Signpost;
	FVector2f PositionM = FVector2f::ZeroVector;
	float GroundM = 0.0f;
	/** Facing direction (degrees, 0 = +X, counter-clockwise seen from above = toward +Y). */
	float YawDeg = 0.0f;
	/** Bridges: span and width; log piles: length. */
	float LengthM = 0.0f;
	float WidthM = 0.0f;
	/** Path node the landmark belongs to (signposts, stones, calvary...), or INDEX_NONE. */
	int32 Node = INDEX_NONE;
	/** Parcel numbers painted on a boundary stone. */
	TArray<int32> ParcelNumbers;
	/** Vegetation keeps this far from the position. */
	float ClearanceM = 2.0f;
};

namespace ForestLandmarks
{
	SHROOMHUNT_API const TCHAR* KindName(EForestLandmarkKind Kind);
}
