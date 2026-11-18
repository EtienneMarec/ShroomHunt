#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UObject;
class UStaticMesh;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

namespace ShroomMesh
{
	/**
	 * Builds a transient UStaticMesh (not an asset) from a generated mesh, with one material and
	 * optional simple collision boxes (mesh space). Works in the editor and in game.
	 */
	SHROOMHUNT_API UStaticMesh* CreateTransientStaticMesh(const UE::Geometry::FDynamicMesh3& Mesh, UMaterialInterface* Material, FName MeshName,
		const TArray<FBox>* CollisionBoxes = nullptr);
}
