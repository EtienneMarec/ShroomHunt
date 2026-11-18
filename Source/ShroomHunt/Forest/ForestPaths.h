#pragma once

#include "CoreMinimal.h"

struct FForestDefinition;

/** Path hierarchy of a French managed forest. */
enum class EForestPathRank : uint8
{
	/** Gravel forest road (route forestière), connects entrances, the base and hubs. */
	Road = 0,
	/** Straight grassy ride (layon / ligne) along parcel boundaries. */
	Ride = 1,
	/** Winding footpath (sentier) to ponds and remarkable stands. */
	Footpath = 2,
};

enum class EForestPathNodeKind : uint8
{
	/** Where a public road enters the forest (map border). */
	Entrance,
	/** The forester's house (player base), near the first entrance. */
	Base,
	/** Flat spot where roads meet (future landmarks, clearings). */
	Hub,
	/** Star crossroads of rides (Voronoi vertex of the parcels). */
	Crossroads,
	/** A path joins another one. */
	Junction,
	/** A ride reaches the map border or stops at water. */
	End,
	/** End of a footpath (pond shore, old stand). */
	Destination,
};

struct SHROOMHUNT_API FForestPathNode
{
	FVector2f PositionM = FVector2f::ZeroVector;
	EForestPathNodeKind Kind = EForestPathNodeKind::Junction;
};

/** One path: smoothed centerline resampled every ~2 m and its smoothed height profile. */
struct SHROOMHUNT_API FForestPath
{
	EForestPathRank Rank = EForestPathRank::Road;
	TArray<FVector2f> Points;
	/** Terrain height the path is graded to at each point (m). */
	TArray<float> Heights;
	int32 StartNode = INDEX_NONE;
	int32 EndNode = INDEX_NONE;

	float GetLengthM() const;
};

struct SHROOMHUNT_API FForestPathNetwork
{
	TArray<FForestPathNode> Nodes;
	TArray<FForestPath> Paths;

	int32 CountPaths(EForestPathRank Rank) const;
	float GetLengthM(EForestPathRank Rank) const;
	int32 CountNodes(EForestPathNodeKind Kind) const;
};

/** Pure helpers of the path generator (exposed for tests). */
namespace ForestPaths
{
	/** Half width of the walkable surface of a path (m). */
	SHROOMHUNT_API float HalfWidthM(EForestPathRank Rank, const FForestDefinition& Definition);

	/** Graded shoulder around the surface where the terrain blends back (m). */
	SHROOMHUNT_API float ShoulderM(EForestPathRank Rank);

	/**
	 * A* / Dijkstra on an 8-connected grid. StepCost(From, To, StepLengthInCells) returns the
	 * cost of a move, or a negative value when it is impossible. Heuristic must not overestimate
	 * (return 0 for Dijkstra). Returns the cells from Start to the first goal reached.
	 */
	SHROOMHUNT_API bool FindGridPath(int32 Width, int32 Height, int32 Start,
		TFunctionRef<float(int32 From, int32 To, float StepLength)> StepCost,
		TFunctionRef<bool(int32 Cell)> IsGoal,
		TFunctionRef<float(int32 Cell)> Heuristic,
		TArray<int32>& OutCells, float* OutCost = nullptr);

	/** Ramer-Douglas-Peucker simplification (keeps both ends). */
	SHROOMHUNT_API TArray<FVector2f> Simplify(const TArray<FVector2f>& Points, float Tolerance);

	/** Chaikin corner cutting (keeps both ends). */
	SHROOMHUNT_API TArray<FVector2f> Chaikin(const TArray<FVector2f>& Points, int32 Iterations);

	/** Points every Spacing along the polyline (both ends kept). */
	SHROOMHUNT_API TArray<FVector2f> Resample(const TArray<FVector2f>& Points, float Spacing);

	/** Moving average of radius Radius samples, Passes times (ends included). */
	SHROOMHUNT_API TArray<float> SmoothProfile(const TArray<float>& Values, int32 Radius, int32 Passes);

	/** One edge of a Voronoi diagram clipped to a box. */
	struct FVoronoiEdge
	{
		FVector2f A = FVector2f::ZeroVector;
		FVector2f B = FVector2f::ZeroVector;
		int32 SiteA = INDEX_NONE;
		int32 SiteB = INDEX_NONE;
		/** The end is a Voronoi vertex (three cells meet), not a cut by the box. */
		bool bAIsVertex = false;
		bool bBIsVertex = false;
	};

	/** Exact Voronoi edges of a few sites inside Box (O(n^3), fine for parcels). */
	SHROOMHUNT_API TArray<FVoronoiEdge> VoronoiEdges(const TArray<FVector2f>& Sites, const FBox2f& Box);
}
