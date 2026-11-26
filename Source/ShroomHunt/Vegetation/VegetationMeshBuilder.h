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
	/** Fallen leaves on the ground (kept apart from the living Foliage for the seasons, M2). */
	Litter = 5,
	/** Berries (holly): never colored nor dropped by the seasonal material. */
	Fruit = 6,
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
	/** Dark foliage masses inside the crown (FTreeFill). */
	int32 Clumps = 12;
	int32 RadialSegments = 12;

	static FTreeShape Sample(const FTreeArchetype& Archetype, const FTreeFormDefinition& Form, uint32 Seed);
};

/**
 * Builds the placeholder vegetation meshes (pure, deterministic, D-024).
 *
 * Trees (stylized, D-039): the skeleton of TreeGrowth (trunk with root flare, bark ridges or
 * fluting, branches down to the twigs) as tapered tubes, leaf blades shaped per genus and a
 * few dark fill masses inside the crown. Leaves and fills carry a per-leaf random value in the
 * vertex color alpha (leaf fall order for the seasonal material).
 * Understory: bracken (double-sided fronds), bramble mounds with canes, fallen logs and
 * stumps. Ground debris: single fallen leaves of a genus (double-sided blades), twigs,
 * stones and moss cushions. Vertex colors carry the sRGB albedo, UV1 the surface id.
 * Origin: base on the ground, +Z up, centimeters.
 */
namespace VegetationMeshBuilder
{
	/**
	 * Bump whenever the meshes this builder (or TreeGrowth) produces change for the same data:
	 * the editor then rebuilds the baked vegetation meshes at its next start (D-040).
	 */
	constexpr int32 BuilderVersion = 8;

	/**
	 * Material slots (D-044): slot 0 "Vegetation" = opaque, one-sided (bark, wood, canes, berries, ground
	 * litter); slot 1 "Foliage" = masked, two-sided (living leaves and fronds: single-sided geometry).
	 * Opaque wood keeps Nanite and the virtual shadow maps on their fast path.
	 */
	inline const FName WoodSlot = TEXT("Vegetation");
	inline const FName FoliageSlot = TEXT("Foliage");
	SHROOMHUNT_API TConstArrayView<uint8> GetSlotPerSurface();

	/** Returns false when the spec refers to a genus or form missing from the definition. */
	SHROOMHUNT_API bool Build(const FVegetationDefinition& Definition, const FVegetationMeshSpec& Spec, UE::Geometry::FDynamicMesh3& OutMesh, FVegetationCollision* OutCollision = nullptr);

	/** Size of the log mesh built from Seed (same random draws as the builder): length and radius (m). */
	SHROOMHUNT_API void GetLogSize(uint32 Seed, float& OutLengthM, float& OutRadiusM);

	/** Radius of the stump mesh built from Seed at the ground, root flare included (m). */
	SHROOMHUNT_API float GetStumpRadius(uint32 Seed);
}
