#pragma once

#include "CoreMinimal.h"
#include "Forest/ForestLandmarks.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/** Simple collision of a landmark: boxes in mesh space (centimeters). */
struct SHROOMHUNT_API FLandmarkCollision
{
	TArray<FBox> Boxes;
};

/**
 * Placeholder landmark meshes (pure, deterministic): stylized buildings and props made of
 * boxes, prisms and cylinders with vertex colors, like the placeholder vegetation (D-024).
 * Mesh space: origin on the ground at the landmark position, +X = the landmark's facing
 * direction (door, sign face), +Z up, centimeters.
 */
namespace LandmarkMeshBuilder
{
	/** The landmark's own mesh (sizes from the landmark for bridges and log piles). */
	SHROOMHUNT_API bool Build(const FForestLandmark& Landmark, UE::Geometry::FDynamicMesh3& OutMesh, FLandmarkCollision* OutCollision = nullptr);

	/** One signpost arrow board, 1.2 m long along +X, pointing to +X, centered on its base end. */
	SHROOMHUNT_API void BuildSignBoard(UE::Geometry::FDynamicMesh3& OutMesh);

	/** Height of a signpost's name board center and of its first arrow board (cm). */
	inline constexpr float SignNameBoardZ = 235.0f;
	inline constexpr float SignFirstArrowZ = 195.0f;
	inline constexpr float SignArrowSpacing = 32.0f;
}
