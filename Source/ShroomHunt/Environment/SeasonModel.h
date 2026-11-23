#pragma once

#include "CoreMinimal.h"

struct FTreeArchetype;

/** Leaf calendar of one tree genus, in continuous real months (10.5 = mid October). */
struct SHROOMHUNT_API FTreePhenology
{
	FName Genus;
	/** Budburst; the leaves are fully out about three weeks later. */
	float LeafOutMonth = 4.5f;
	float ColoringStart = 10.0f;
	float ColoringFull = 10.8f;
	float FallStart = 10.6f;
	float FallEnd = 11.6f;
	/** Fraction of dead leaves kept through the winter (marcescence). */
	float WinterLeaves = 0.0f;
	/** sRGB foliage colors in summer and at full autumn coloring. */
	FLinearColor Summer = FLinearColor(0.22f, 0.36f, 0.10f);
	FLinearColor Autumn = FLinearColor(0.60f, 0.40f, 0.12f);
};

/** What a genus looks like at a point of the year (all in [0, 1] except the color). */
struct SHROOMHUNT_API FFoliageState
{
	/** Leaves on the tree: 1 in summer, WinterLeaves in winter. */
	float LeafAmount = 1.0f;
	/** 0 green, 1 full autumn color (also the dead leaves kept in winter). */
	float Coloring = 0.0f;
	/** Light green of the young spring leaves. */
	float Freshness = 0.0f;
	/** Bright freshly fallen leaves on the ground, darkening over the winter. */
	float FreshLitter = 0.0f;
	/** Resulting sRGB foliage color. */
	FLinearColor Color = FLinearColor(0.22f, 0.36f, 0.10f);
};

/**
 * Seasonal foliage (M2): leaf-out, autumn coloring and leaf fall per tree genus from the
 * real month of the compressed calendar, with a small deterministic shift per year (an
 * early or a late autumn). Pure, unit-tested; the environment director writes the result to
 * MPC_Season for the materials.
 */
namespace SeasonModel
{
	SHROOMHUNT_API FTreePhenology FromArchetype(const FTreeArchetype& Archetype);

	/** Shift of the leaf calendar of a genus in a given year, in months (about +-0.25). */
	SHROOMHUNT_API float YearShiftMonths(uint32 Seed, int32 Year, FName Genus);

	/** Foliage at a continuous month in [1, 13), the leaf calendar moved by ShiftMonths. */
	SHROOMHUNT_API FFoliageState Evaluate(const FTreePhenology& Phenology, float Month, float ShiftMonths = 0.0f);

	/** Weighted mean of several genera (forest average for the placeholder trees). */
	SHROOMHUNT_API FFoliageState Blend(TConstArrayView<FFoliageState> States, TConstArrayView<float> Weights);
}
