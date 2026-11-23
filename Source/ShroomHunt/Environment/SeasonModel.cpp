#include "Environment/SeasonModel.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "Vegetation/VegetationDefinition.h"

namespace SeasonModel
{
	FTreePhenology FromArchetype(const FTreeArchetype& Archetype)
	{
		FTreePhenology Out;
		Out.Genus = Archetype.Genus;
		Out.LeafOutMonth = Archetype.LeafOutMonth;
		if (Archetype.ColoringMonths.Num() == 2)
		{
			Out.ColoringStart = Archetype.ColoringMonths[0];
			Out.ColoringFull = Archetype.ColoringMonths[1];
		}
		if (Archetype.LeafFallMonths.Num() == 2)
		{
			Out.FallStart = Archetype.LeafFallMonths[0];
			Out.FallEnd = Archetype.LeafFallMonths[1];
		}
		Out.WinterLeaves = Archetype.WinterLeaves;
		Out.Summer = VegetationData::ToColor(Archetype.Foliage, Out.Summer);
		Out.Autumn = VegetationData::ToColor(Archetype.AutumnFoliage, Out.Autumn);
		return Out;
	}

	float YearShiftMonths(uint32 Seed, int32 Year, FName Genus)
	{
		using namespace ShroomHash;
		// One shift shared by every genus (the weather of that year), plus a little per genus.
		const float Shared = 0.4f * (ToUnitFloat(Hash(Seed, 0x5EA5u, static_cast<uint32>(Year))) - 0.5f);
		const float Own = 0.1f * (ToUnitFloat(Hash(Seed, HashString(Genus.ToString()), static_cast<uint32>(Year))) - 0.5f);
		return Shared + Own;
	}

	FFoliageState Evaluate(const FTreePhenology& P, float Month, float ShiftMonths)
	{
		using ShroomMath::SmoothStep01;
		FFoliageState Out;
		const float LeafOut = P.LeafOutMonth + ShiftMonths;
		// The leaf year runs from budburst to the next budburst: winter months count after December.
		const bool bBeforeLeafOut = Month < LeafOut;
		const float M = bBeforeLeafOut ? Month + 12.0f : Month;

		const float Grow = bBeforeLeafOut ? 0.0f : SmoothStep01(LeafOut, LeafOut + 0.7f, Month);
		const float Coloring = SmoothStep01(P.ColoringStart + ShiftMonths, P.ColoringFull + ShiftMonths, M);
		const float FallEnd = P.FallEnd + ShiftMonths;
		const float Fall = SmoothStep01(P.FallStart + ShiftMonths, FallEnd, M);

		// While the new leaves come out, last year's kept leaves drop.
		const float SeasonLeaves = 1.0f - Fall * (1.0f - P.WinterLeaves);
		Out.LeafAmount = FMath::Lerp(P.WinterLeaves, SeasonLeaves, Grow);
		Out.Coloring = FMath::Lerp(1.0f, Coloring, Grow);
		Out.Freshness = Grow * (1.0f - SmoothStep01(LeafOut + 0.7f, LeafOut + 2.0f, Month));
		Out.FreshLitter = Fall * FMath::Exp(-FMath::Max(0.0f, M - FallEnd) / 1.5f);

		FLinearColor Color = FMath::Lerp(P.Summer, P.Autumn, Out.Coloring);
		const FLinearColor Spring(FMath::Min(P.Summer.R * 1.45f, 1.0f), FMath::Min(P.Summer.G * 1.3f, 1.0f), FMath::Min(P.Summer.B * 1.1f, 1.0f), 1.0f);
		Color = FMath::Lerp(Color, Spring, Out.Freshness);
		Color.A = 1.0f;
		Out.Color = Color;
		return Out;
	}

	FFoliageState Blend(TConstArrayView<FFoliageState> States, TConstArrayView<float> Weights)
	{
		FFoliageState Out;
		const int32 Count = FMath::Min(States.Num(), Weights.Num());
		float Total = 0.0f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Total += FMath::Max(Weights[Index], 0.0f);
		}
		if (Total <= 0.0f)
		{
			return States.Num() > 0 ? States[0] : Out;
		}
		Out.LeafAmount = Out.Coloring = Out.Freshness = Out.FreshLitter = 0.0f;
		Out.Color = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float W = FMath::Max(Weights[Index], 0.0f) / Total;
			const FFoliageState& S = States[Index];
			Out.LeafAmount += W * S.LeafAmount;
			Out.Coloring += W * S.Coloring;
			Out.Freshness += W * S.Freshness;
			Out.FreshLitter += W * S.FreshLitter;
			Out.Color.R += W * S.Color.R;
			Out.Color.G += W * S.Color.G;
			Out.Color.B += W * S.Color.B;
		}
		return Out;
	}
}
