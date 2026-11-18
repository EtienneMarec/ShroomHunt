#pragma once

#include "CoreMinimal.h"
#include "Vegetation/VegetationDefinition.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/** Surface id stored in UV channel 1 (U = (Id + 0.5) / 8), e.g. for wind on foliage later. */
enum class EVegetationSurface : uint8
{
	Bark = 0,
	Wood = 1,
	Foliage = 2,
	Frond = 3,
	Cane = 4,
};

/** Simple collision of a vegetation mesh: one capsule (centimeters, mesh space). */
struct SHROOMHUNT_API FVegetationCollision
{
	bool bHasCapsule = false;
	FVector Center = FVector::ZeroVector;
	/** Capsule axis: Z for trunks and stumps, X for logs. */
	FRotator Rotation = FRotator::ZeroRotator;
	float Radius = 0.0f;
	/** Length of the cylinder part (total length minus both hemispheres). */
	float Length = 0.0f;
};

/** Tree shape sampled from a genus archetype, a form and a seed (meters). */
struct SHROOMHUNT_API FTreeShape
{
	float HeightM = 25.0f;
	float TrunkRadiusM = 0.2f;
	float ClearBoleM = 10.0f;
	float CrownRadiusM = 5.0f;
	float LeanX = 0.0f;
	float LeanY = 0.0f;
	int32 Clumps = 12;
	int32 RadialSegments = 12;
	/** Foliage sphere subdivision (2 = 320 triangles, 1 = 80). */
	int32 ClumpDetail = 2;

	static FTreeShape Sample(const FTreeArchetype& Archetype, const FTreeFormDefinition& Form, uint32 Seed);
};

/**
 * Builds the placeholder vegetation meshes (pure, deterministic, D-024).
 *
 * Trees: a tapered trunk with root flare, bark ridges or fluting, foliage clumps spread
 * in the crown envelope of the genus and one branch from the trunk to each outer clump.
 * Understory: bracken (double-sided fronds), bramble mounds with canes, fallen logs and
 * stumps. Vertex colors carry the sRGB albedo, UV1 the surface id.
 * Origin: base on the ground, +Z up, centimeters.
 */
namespace VegetationMeshBuilder
{
	/** Returns false when the spec refers to a genus or form missing from the definition. */
	SHROOMHUNT_API bool Build(const FVegetationDefinition& Definition, const FVegetationMeshSpec& Spec, UE::Geometry::FDynamicMesh3& OutMesh, FVegetationCollision* OutCollision = nullptr);
}
