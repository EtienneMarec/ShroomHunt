#pragma once

#include "CoreMinimal.h"
#include "Vegetation/VegetationMeshBuilder.h"

/** One branch of a tree skeleton: a polyline with a radius per point (meters, Z up). */
struct SHROOMHUNT_API FTreeBranch
{
	TArray<FVector3d> Points;
	TArray<double> Radii;
	/** 0 trunk, 1 main branches (and the limbs of a decurrent crown), 2 secondary branches, 3 twigs. */
	int32 Level = 0;
	int32 Parent = INDEX_NONE;

	double GetLength() const;
};

/** One leaf of the crown: a blade from Base along Axis, its upper side facing Normal. */
struct SHROOMHUNT_API FTreeLeaf
{
	FVector3d Base = FVector3d::ZeroVector;
	FVector3d Axis = FVector3d::UnitX();
	FVector3d Normal = FVector3d::UnitZ();
	double Length = 0.2;
	double Width = 0.1;
	/** Uniform value in [0, 1) drawn per leaf (vertex color alpha: leaf fall order, color jitter). */
	float Random = 0.0f;
	/** Exposure to the light in [0, 1]: 0 deep inside and low in the crown, 1 at its top surface. */
	float Light = 1.0f;
};

/** A dark foliage mass inside the crown, so that the crown does not look see-through. */
struct SHROOMHUNT_API FTreeFill
{
	FVector3d Center = FVector3d::ZeroVector;
	double Radius = 0.5;
	float Random = 0.0f;
	float Light = 0.5f;
};

struct SHROOMHUNT_API FTreeSkeleton
{
	TArray<FTreeBranch> Branches;
	TArray<FTreeLeaf> Leaves;
	TArray<FTreeFill> Fills;

	int32 CountBranches(int32 Level) const;
};

/**
 * Grows the skeleton of a stylized tree (pure, deterministic, D-039).
 *
 * The trunk runs to the top (excurrent genera) or splits into a few limbs at the crown base
 * (decurrent oak). Main branches leave the trunk with the genus angle (flatter low in the crown,
 * steeper at the top), secondary branches and leaf-bearing twigs leave their parents
 * alternately left and right. Every branch bends toward the sky (UpCurve), hangs (Droop) and
 * kinks (Tortuosity), and stops at the crown envelope of the genus, which gives the crown its
 * outline. Leaves sit on the twigs and the outer part of the secondary branches, with a fixed
 * budget per form; a few dark fill masses close the inside of the crown.
 */
namespace TreeGrowth
{
	/** Trunk axis (m) at height Z: lean and slight wobble. */
	SHROOMHUNT_API FVector3d TrunkAxis(const FTreeShape& Shape, uint32 Seed, double Z);

	/** Horizontal crown radius (m) of the envelope at height Z (0 outside the crown height). */
	SHROOMHUNT_API double CrownRadiusAt(const FTreeArchetype& Archetype, const FTreeShape& Shape, double Z);

	/** Number of leaves of a form (bounds the triangle count of the tree meshes). */
	SHROOMHUNT_API int32 GetLeafBudget(EVegetationKind Kind);

	SHROOMHUNT_API FTreeSkeleton Grow(const FTreeArchetype& Archetype, const FTreeFormDefinition& Form, const FTreeShape& Shape, uint32 Seed);
}
