#pragma once

#include "CoreMinimal.h"

struct FMushroomMorphology;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/** Surface id stored in UV channel 1 (U = (Id + 0.5) / 8) so the material can treat each part differently. */
enum class EMushroomSurface : uint8
{
	CapTop = 0,
	CapUnderside = 1,
	Gills = 2,
	Stem = 3,
	Ring = 4,
	Volva = 5,
	Warts = 6,
};

/** Level of detail of a generated mushroom mesh. */
struct SHROOMHUNT_API FMushroomMeshDetail
{
	/** Vertices around the cap and stem. */
	int32 RadialSegments = 32;
	/** Profile points from the cap center to the margin. */
	int32 CapProfileSegments = 12;
	/** Rings along the stem. */
	int32 StemRings = 12;
	/** Geometric gills/ridges under the cap (otherwise the underside color only). */
	bool bGeometricGills = true;
	/** Fraction of the warts / volva scales generated. */
	float WartFraction = 1.0f;

	/** Close-up quality (inspection, near LOD). */
	static FMushroomMeshDetail Near();

	/** Distant LOD: no gills, coarse rings, half the warts. */
	static FMushroomMeshDetail Far();
};

/**
 * Builds the mesh of one individual from its morphology (pure, deterministic).
 *
 * Cap, stem, ring and sac volva are surfaces of revolution of 2D profiles
 * (shape functions per ECapShape / EStemShape), deformed by a smooth irregular
 * outline, margin waves, cap tilt and stem bend. Gills/ridges are thin double-sided
 * fins, warts and volva scales small pyramids. Vertex colors carry the albedo (sRGB),
 * UV0 the surface coordinates (angle, profile), UV1 the surface id.
 * Origin: stem base on the ground, +Z up, centimeters.
 */
namespace MushroomMeshBuilder
{
	SHROOMHUNT_API void Build(const FMushroomMorphology& Morphology, const FMushroomMeshDetail& Detail, UE::Geometry::FDynamicMesh3& OutMesh);
}
