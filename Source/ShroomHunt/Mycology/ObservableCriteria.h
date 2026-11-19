#pragma once

#include "CoreMinimal.h"
#include "Mushroom/MushroomMorphology.h"

/**
 * What a player can see on a specimen without knowing its species (Docs/Progression.md:
 * unknown specimens are shown by observable criteria only): cap, underside, stem, ring,
 * volva, age. Player-facing text (FText, localizable).
 */
namespace ObservableCriteria
{
	/** Everyday color name of an sRGB albedo ("red", "brown", "cream"...). */
	SHROOMHUNT_API FText ColorName(const FLinearColor& Srgb);

	SHROOMHUNT_API FText ShapeName(ECapShape Shape);

	/** "young", "mature", "old", "decaying", or "button" for a just-emerged one. */
	SHROOMHUNT_API FText AgeName(const FMushroomMorphology& Morphology);

	/** Label of an unknown specimen from what it looks like: "Red gilled mushroom", "Brown mushroom with pores"... */
	SHROOMHUNT_API FText VagueLabel(const FMushroomMorphology& Morphology);

	/** One line per criterion, for the examine panel. */
	SHROOMHUNT_API TArray<FText> Describe(const FMushroomMorphology& Morphology, const FMushroomSpeciesRow& Species);

	/** Rough fresh weight from the shape (flesh ~0.55 g/cm3). */
	SHROOMHUNT_API float EstimateWeightGrams(const FMushroomMorphology& Morphology);
}
