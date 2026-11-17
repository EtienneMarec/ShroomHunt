#include "Mushroom/MushroomMorphology.h"

#include "Core/ShroomHash.h"
#include "Math/RandomStream.h"

namespace MushroomMorphologyDetail
{
	FLinearColor ToUnitSRGB(const FColor& Color)
	{
		return FLinearColor(Color.R / 255.0f, Color.G / 255.0f, Color.B / 255.0f, 1.0f);
	}

	FLinearColor LerpColor(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(FMath::Lerp(A.R, B.R, Alpha), FMath::Lerp(A.G, B.G, Alpha), FMath::Lerp(A.B, B.B, Alpha), 1.0f);
	}

	/** Multiplies brightness by a factor around 1 (keeps hue). */
	FLinearColor Brighten(const FLinearColor& Color, float Factor)
	{
		return FLinearColor(FMath::Clamp(Color.R * Factor, 0.0f, 1.0f), FMath::Clamp(Color.G * Factor, 0.0f, 1.0f),
			FMath::Clamp(Color.B * Factor, 0.0f, 1.0f), 1.0f);
	}

	/** Old fruit bodies fade and brown slightly. */
	FLinearColor Age(const FLinearColor& Color, EMushroomStage Stage)
	{
		if (Stage != EMushroomStage::Old)
		{
			return Color;
		}
		const FLinearColor Faded(0.55f, 0.47f, 0.38f, 1.0f);
		return LerpColor(Color, Faded, 0.18f);
	}

	ECapShape PickShape(const TArray<FCapShapeWeight>& Shapes, FRandomStream& Random)
	{
		if (Shapes.Num() == 0)
		{
			return ECapShape::Convex;
		}
		float Total = 0.0f;
		for (const FCapShapeWeight& Shape : Shapes)
		{
			Total += FMath::Max(0.0f, Shape.Weight);
		}
		float Pick = Random.FRand() * Total;
		for (const FCapShapeWeight& Shape : Shapes)
		{
			Pick -= FMath::Max(0.0f, Shape.Weight);
			if (Pick <= 0.0f)
			{
				return Shape.Shape;
			}
		}
		return Shapes.Last().Shape;
	}

	/** Size fraction within the species range for a stage. */
	float StageSizeAlpha(EMushroomStage Stage, FRandomStream& Random)
	{
		switch (Stage)
		{
		case EMushroomStage::Young:  return Random.FRandRange(0.0f, 0.35f);
		case EMushroomStage::Mature: return Random.FRandRange(0.35f, 0.85f);
		default:                     return Random.FRandRange(0.65f, 1.0f);
		}
	}
}

float FMushroomMorphology::GetApproximateHeight() const
{
	return StemLength + 0.5f * CapDiameter * CapHeightRatio;
}

namespace MushroomMorphology
{
	float GetShapeHeightRatio(ECapShape Shape)
	{
		switch (Shape)
		{
		case ECapShape::Hemispherical:   return 0.85f;
		case ECapShape::Campanulate:     return 0.9f;
		case ECapShape::Conical:         return 1.1f;
		case ECapShape::Convex:          return 0.5f;
		case ECapShape::Umbonate:        return 0.45f;
		case ECapShape::Flat:            return 0.2f;
		case ECapShape::Depressed:       return 0.25f;
		case ECapShape::Infundibuliform: return 0.45f;
		default:                         return 0.5f;
		}
	}

	FMushroomMorphology Sample(const FMushroomSpeciesRow& Species, FName SpeciesId, uint32 Seed, EMushroomStage Stage)
	{
		using namespace MushroomMorphologyDetail;

		FRandomStream Random(static_cast<int32>(ShroomHash::Hash(Seed, ShroomHash::HashString(SpeciesId.ToString()), static_cast<uint32>(Stage))));

		FMushroomMorphology M;
		M.SpeciesId = SpeciesId;
		M.Seed = Seed;
		M.Stage = Stage;
		M.ShapeSeed = static_cast<uint32>(Random.GetUnsignedInt());

		// --- Size: cap and stem grow together, young stems are stocky.
		const float SizeAlpha = StageSizeAlpha(Stage, Random);
		M.CapDiameter = Species.Cap.DiameterCm.Lerp(SizeAlpha);
		const float StemAlpha = FMath::Clamp(SizeAlpha + Random.FRandRange(-0.15f, 0.15f), 0.0f, 1.0f);
		M.StemLength = Species.Stem.LengthCm.Lerp(StemAlpha) * (Stage == EMushroomStage::Young ? 0.7f : 1.0f);
		const float ThicknessAlpha = FMath::Clamp(FMath::Max(SizeAlpha, Stage == EMushroomStage::Young ? 0.45f : 0.0f) + Random.FRandRange(-0.1f, 0.1f), 0.0f, 1.0f);
		M.StemDiameter = Species.Stem.ThicknessCm.Lerp(ThicknessAlpha);

		// --- Cap shape.
		M.CapShape = PickShape(Species.Cap.GetShapes(Stage), Random);
		M.CapHeightRatio = GetShapeHeightRatio(M.CapShape) * Random.FRandRange(0.85f, 1.15f);
		switch (Species.Underside.Type)
		{
		case EUndersideType::Pores:  M.CapFleshRatio = 0.45f; break;
		case EUndersideType::Ridges: M.CapFleshRatio = 0.32f; break;
		case EUndersideType::Teeth:  M.CapFleshRatio = 0.3f; break;
		default:                     M.CapFleshRatio = 0.22f; break;
		}
		M.CapFleshRatio *= Random.FRandRange(0.9f, 1.1f);
		M.Margin = Species.Cap.Margin;
		M.MarginWave = Species.Cap.Margin == EMarginType::Wavy ? Random.FRandRange(0.5f, 1.0f) : Random.FRandRange(0.0f, 0.15f);
		M.MarginLobes = Random.RandRange(4, 7);
		M.CapAsymmetry = Random.FRandRange(0.02f, 0.08f) + (Stage == EMushroomStage::Old ? 0.03f : 0.0f);
		M.CapTiltDeg = Random.FRandRange(0.0f, Stage == EMushroomStage::Old ? 12.0f : 7.0f);
		M.CapTiltAzimuthDeg = Random.FRandRange(0.0f, 360.0f);

		// --- Colors: one tint per individual inside the species range, lighter towards the margin.
		const float Tint = Random.FRand();
		const FLinearColor CapA = ToUnitSRGB(Species.Cap.Colors.A);
		const FLinearColor CapB = ToUnitSRGB(Species.Cap.Colors.B);
		const float Brightness = Random.FRandRange(0.94f, 1.06f);
		M.CapColorCenter = Age(Brighten(LerpColor(CapA, CapB, Tint), Brightness), Stage);
		M.CapColorMargin = Age(Brighten(LerpColor(CapA, CapB, Tint * 0.55f), Brightness * 1.04f), Stage);

		M.bWarts = Species.Cap.Warts.bPresent;
		if (M.bWarts)
		{
			const float StageFactor = Stage == EMushroomStage::Young ? 1.2f : (Stage == EMushroomStage::Old ? 0.6f : 1.0f);
			M.WartCount = FMath::RoundToInt(Species.Cap.Warts.Density * 70.0f * StageFactor * Random.FRandRange(0.8f, 1.2f));
			M.WartColor = Age(ToUnitSRGB(Species.Cap.Warts.Color), Stage);
		}

		M.UndersideType = Species.Underside.Type;
		M.Attachment = Species.Underside.Attachment;
		const FColor& UnderColor = Stage == EMushroomStage::Young ? Species.Underside.YoungColor
			: (Stage == EMushroomStage::Mature ? Species.Underside.MatureColor : Species.Underside.OldColor);
		M.UndersideColor = ToUnitSRGB(UnderColor);
		switch (M.UndersideType)
		{
		case EUndersideType::Gills:  M.GillCount = Random.RandRange(44, 60); break;
		case EUndersideType::Ridges: M.GillCount = Random.RandRange(16, 24); break;
		default:                     M.GillCount = 0; break;
		}

		// --- Stem.
		M.StemShape = Species.Stem.Shape;
		M.StemBend = Random.FRandRange(0.0f, 0.12f);
		M.StemBendAzimuthDeg = Random.FRandRange(0.0f, 360.0f);
		const float StemTint = Random.FRand();
		const FLinearColor StemA = ToUnitSRGB(Species.Stem.Colors.A);
		const FLinearColor StemB = ToUnitSRGB(Species.Stem.Colors.B);
		M.StemColorTop = Age(LerpColor(StemA, StemB, StemTint * 0.5f), Stage);
		M.StemColorBase = Age(LerpColor(StemA, StemB, 0.5f + StemTint * 0.5f), Stage);

		M.bReticulation = Species.Stem.Reticulation.bPresent;
		M.ReticulationColor = ToUnitSRGB(Species.Stem.Reticulation.Color);
		M.ReticulationExtent = Species.Stem.Reticulation.Extent;

		M.Ring = Species.Stem.Ring;
		M.RingHeight = Random.FRandRange(0.78f, 0.88f);
		M.RingColor = Brighten(M.StemColorTop, 1.05f);
		M.Volva = Species.Stem.Volva;
		M.VolvaColor = M.bWarts ? M.WartColor : M.StemColorBase;

		M.bWormy = Stage == EMushroomStage::Old && Random.FRand() < Species.WormyChance;
		return M;
	}
}
