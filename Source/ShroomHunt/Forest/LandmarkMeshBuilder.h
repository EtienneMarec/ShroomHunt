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

	/**
	 * Where a readable sheet (poster, guide page) hangs on a landmark: transform in the
	 * landmark's mesh space (the sheet faces its +X) and sheet size (cm: X = width, Y = height).
	 * False for kinds that carry none.
	 */
	SHROOMHUNT_API bool GetPaperMount(EForestLandmarkKind Kind, FTransform& OutLocal, FVector2D& OutSizeCm);

	/**
	 * A sheet of paper facing +X, centered on the origin, with a title band and one colored
	 * illustration per swatch; a torn sheet has a ragged lower edge.
	 */
	SHROOMHUNT_API void BuildPaper(const FVector2D& SizeCm, TConstArrayView<FLinearColor> Swatches, bool bTorn,
		UE::Geometry::FDynamicMesh3& OutMesh, FLandmarkCollision* OutCollision = nullptr);

	/** Height of a signpost's name board center and of its first arrow board (cm). */
	inline constexpr float SignNameBoardZ = 235.0f;
	inline constexpr float SignFirstArrowZ = 195.0f;
	inline constexpr float SignArrowSpacing = 32.0f;
}
