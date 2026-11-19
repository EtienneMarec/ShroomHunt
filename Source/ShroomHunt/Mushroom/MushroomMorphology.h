#pragma once

#include "CoreMinimal.h"
#include "Mycology/MushroomSpeciesTypes.h"

/**
 * Exact shape and colors of ONE mushroom individual at one age, drawn deterministically from
 * the species ranges (FMushroomSpeciesRow) and a seed.
 *
 * The seed fixes the individual (proportions, tint, irregularities, the shapes it goes
 * through); the age in [0, 1] moves it continuously from a closed button to a rotting cap.
 * Units: centimeters (= Unreal units). Colors are sRGB in [0, 1].
 */
struct SHROOMHUNT_API FMushroomMorphology
{
	FName SpeciesId;
	uint32 Seed = 0;
	EMushroomStage Stage = EMushroomStage::Mature;
	/** 0 = button (just emerged), 1 = rotten. Stage is derived from it with the species thresholds. */
	float Age = 0.5f;

	// --- Cap ---------------------------------------------------------------------
	ECapShape CapShape = ECapShape::Convex;
	/** The cap profile is blended from CapShape (0) to CapShapeB (1) while the cap opens. */
	ECapShape CapShapeB = ECapShape::Convex;
	float CapShapeBlend = 0.0f;
	/** Rotting: the cap margin sags (0 fresh - 1 collapsed). */
	float CapSag = 0.0f;
	float CapDiameter = 8.0f;
	/** Cap height above the margin divided by the cap radius (shape-dependent). */
	float CapHeightRatio = 0.5f;
	/** Flesh thickness at the center divided by the cap radius. */
	float CapFleshRatio = 0.3f;
	EMarginType Margin = EMarginType::Even;
	/** Margin undulation amplitude in [0, 1] and number of lobes. */
	float MarginWave = 0.0f;
	int32 MarginLobes = 5;
	/** Radial irregularity of the cap outline in [0, 0.15]. */
	float CapAsymmetry = 0.05f;
	float CapTiltDeg = 0.0f;
	float CapTiltAzimuthDeg = 0.0f;
	FLinearColor CapColorCenter = FLinearColor::White;
	FLinearColor CapColorMargin = FLinearColor::White;

	bool bWarts = false;
	int32 WartCount = 0;
	FLinearColor WartColor = FLinearColor::White;

	EUndersideType UndersideType = EUndersideType::Gills;
	EGillAttachment Attachment = EGillAttachment::Free;
	FLinearColor UndersideColor = FLinearColor::White;
	/** Number of full-length gills or ridges (short gills are added in between). */
	int32 GillCount = 0;

	// --- Stem --------------------------------------------------------------------
	float StemLength = 6.0f;
	float StemDiameter = 1.5f;
	EStemShape StemShape = EStemShape::Cylindrical;
	/** Lateral offset of the stem top, as a fraction of the stem length. */
	float StemBend = 0.0f;
	float StemBendAzimuthDeg = 0.0f;
	FLinearColor StemColorTop = FLinearColor::White;
	FLinearColor StemColorBase = FLinearColor::White;

	bool bReticulation = false;
	FLinearColor ReticulationColor = FLinearColor::White;
	float ReticulationExtent = 0.0f;

	ERingType Ring = ERingType::None;
	/** Ring attachment height as a fraction of the stem length. */
	float RingHeight = 0.8f;
	FLinearColor RingColor = FLinearColor::White;

	EVolvaType Volva = EVolvaType::None;
	FLinearColor VolvaColor = FLinearColor::White;

	/** Old specimens may be worm-eaten (affects quality later). */
	bool bWormy = false;

	/** Seed for the mesh builder's small-scale noise (outline, warts placement). */
	uint32 ShapeSeed = 0;

	/** Approximate total height (stem + cap), cm. */
	float GetApproximateHeight() const;
};

namespace MushroomMorphology
{
	/**
	 * Draws an individual at a continuous age in [0, 1]. Same (row, seed) => the same individual
	 * at every age, so a mushroom grows instead of changing shape.
	 * @param Seed individual seed (e.g. hash of patch, flush day and index).
	 */
	SHROOMHUNT_API FMushroomMorphology SampleAtAge(const FMushroomSpeciesRow& Species, FName SpeciesId, uint32 Seed, float Age);

	/** Draws an individual at the typical age of a stage (middle of the stage). */
	SHROOMHUNT_API FMushroomMorphology Sample(const FMushroomSpeciesRow& Species, FName SpeciesId, uint32 Seed, EMushroomStage Stage);

	/** Typical age of a stage for a species (middle of its age window). */
	SHROOMHUNT_API float StageAge(const FMushroomSpeciesRow& Species, EMushroomStage Stage);

	/** Size of an individual at Age relative to its final size (growth curve, ~0.12 at the button). */
	SHROOMHUNT_API float GrowthFraction(const FMushroomSpeciesRow& Species, float Age);

	/** Typical cap height ratio (height / radius) of a shape. */
	SHROOMHUNT_API float GetShapeHeightRatio(ECapShape Shape);
}
