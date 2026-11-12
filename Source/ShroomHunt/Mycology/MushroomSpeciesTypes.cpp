#include "Mycology/MushroomSpeciesTypes.h"

float FTrapezoid::Evaluate(float X) const
{
	if (X < Min || X > Max)
	{
		return 0.0f;
	}
	if (X < OptLow)
	{
		return OptLow > Min ? (X - Min) / (OptLow - Min) : 1.0f;
	}
	if (X > OptHigh)
	{
		return Max > OptHigh ? (Max - X) / (Max - OptHigh) : 1.0f;
	}
	return 1.0f;
}

float FTrapezoid::EvaluatePeriodic(float X, float Period) const
{
	if (Period <= 0.0f)
	{
		return Evaluate(X);
	}
	// Test the value and its neighbours one period away: handles windows crossing the year end.
	return FMath::Max3(Evaluate(X), Evaluate(X + Period), Evaluate(X - Period));
}

const TArray<FCapShapeWeight>& FCapMorphology::GetShapes(EMushroomStage Stage) const
{
	switch (Stage)
	{
	case EMushroomStage::Young:  return YoungShapes;
	case EMushroomStage::Mature: return MatureShapes;
	default:                     return OldShapes;
	}
}

float FMushroomSpeciesRow::GetHostAffinity(FName TreeGenus) const
{
	for (const FHostAffinity& Host : Hosts)
	{
		if (Host.Genus == TreeGenus)
		{
			return Host.Affinity;
		}
	}
	return 0.0f;
}

float FMushroomSpeciesRow::GetSoilWeight(ESoilType SoilType) const
{
	for (const FSoilPreference& Preference : Soils)
	{
		if (Preference.Soil == SoilType)
		{
			return Preference.Weight;
		}
	}
	return 0.0f;
}

EMushroomStage FMushroomSpeciesRow::GetStageAt(float LifetimeFraction) const
{
	if (LifetimeFraction < YoungUntil)
	{
		return EMushroomStage::Young;
	}
	return LifetimeFraction < MatureUntil ? EMushroomStage::Mature : EMushroomStage::Old;
}
