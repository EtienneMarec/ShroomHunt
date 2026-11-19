#include "Mushroom/MushroomMorphology.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
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

	float StageAge(const FMushroomSpeciesRow& Species, EMushroomStage Stage)
	{
		const float Young = FMath::Clamp(Species.YoungUntil, 0.05f, 0.9f);
		const float Mature = FMath::Clamp(Species.MatureUntil, Young + 0.05f, 0.95f);
		switch (Stage)
		{
		case EMushroomStage::Young:  return Young * 0.6f;
		case EMushroomStage::Mature: return 0.5f * (Young + Mature);
		default:                     return 0.5f * (Mature + 1.0f);
		}
	}

	float GrowthFraction(const FMushroomSpeciesRow& Species, float Age)
	{
		const float Mature = FMath::Clamp(Species.MatureUntil, 0.1f, 0.95f);
		const float G = FMath::Clamp(Age / Mature, 0.0f, 1.0f);
		// Fast at first, then slowing down to the final size when the mushroom is fully mature.
		return 0.12f + 0.88f * (1.0f - FMath::Square(1.0f - G));
	}

	FMushroomMorphology Sample(const FMushroomSpeciesRow& Species, FName SpeciesId, uint32 Seed, EMushroomStage Stage)
	{
		return SampleAtAge(Species, SpeciesId, Seed, StageAge(Species, Stage));
	}

	FMushroomMorphology SampleAtAge(const FMushroomSpeciesRow& Species, FName SpeciesId, uint32 Seed, float Age)
	{
		using namespace MushroomMorphologyDetail;
		using ShroomMath::SmoothStep01;

		const float A = FMath::Clamp(Age, 0.0f, 1.0f);
		const float Young = FMath::Clamp(Species.YoungUntil, 0.05f, 0.9f);
		const float Mature = FMath::Clamp(Species.MatureUntil, Young + 0.05f, 0.95f);
		const uint32 IndividualSeed = ShroomHash::Hash(Seed, ShroomHash::HashString(SpeciesId.ToString()), 0x6E0Du);

		FMushroomMorphology M;
		M.SpeciesId = SpeciesId;
		M.Seed = Seed;
		M.Age = A;
		M.Stage = A < Young ? EMushroomStage::Young : (A < Mature ? EMushroomStage::Mature : EMushroomStage::Old);

		// --- The individual: everything that stays true for its whole life (fixed draw order).
		FRandomStream Genome(static_cast<int32>(IndividualSeed));
		M.ShapeSeed = static_cast<uint32>(Genome.GetUnsignedInt());
		const float FinalSize = Genome.FRandRange(0.45f, 1.0f);
		const float StemJitter = Genome.FRandRange(-0.15f, 0.15f);
		const float ThicknessJitter = Genome.FRandRange(-0.1f, 0.1f);
		const float HeightJitter = Genome.FRandRange(0.85f, 1.15f);
		const float FleshJitter = Genome.FRandRange(0.9f, 1.1f);
		const float MarginWave = Species.Cap.Margin == EMarginType::Wavy ? Genome.FRandRange(0.5f, 1.0f) : Genome.FRandRange(0.0f, 0.15f);
		const int32 MarginLobes = Genome.RandRange(4, 7);
		const float Asymmetry = Genome.FRandRange(0.02f, 0.08f);
		const float TiltBase = Genome.FRand();
		const float TiltAzimuth = Genome.FRandRange(0.0f, 360.0f);
		const float Tint = Genome.FRand();
		const float Brightness = Genome.FRandRange(0.94f, 1.06f);
		const float WartJitter = Genome.FRandRange(0.8f, 1.2f);
		const int32 Gills = Genome.RandRange(44, 60);
		const int32 Ridges = Genome.RandRange(16, 24);
		const float BendBase = Genome.FRandRange(0.0f, 0.12f);
		const float BendAzimuth = Genome.FRandRange(0.0f, 360.0f);
		const float StemTint = Genome.FRand();
		const float RingHeight = Genome.FRandRange(0.78f, 0.88f);
		const float WormyRoll = Genome.FRand();
		// The shapes it will go through, one per stage (separate streams: stable if draws are added above).
		auto Pick = [&](EMushroomStage Stage)
		{
			FRandomStream Stream(static_cast<int32>(ShroomHash::Hash(IndividualSeed, 0x51u + static_cast<uint32>(Stage))));
			return PickShape(Species.Cap.GetShapes(Stage), Stream);
		};

		// --- Age keyframes: button, young, mature, old, rotten. The cap blends between them.
		struct FKey
		{
			float Age;
			ECapShape Shape;
			float HeightRatio;
		};
		const ECapShape OldShape = Pick(EMushroomStage::Old);
		const FKey Keys[5] = {
			{ 0.0f, ECapShape::Hemispherical, 1.15f }, // closed button, cap pressed on the stem
			{ Young * 0.6f, Pick(EMushroomStage::Young), 0.0f },
			{ 0.5f * (Young + Mature), Pick(EMushroomStage::Mature), 0.0f },
			{ 0.5f * (Mature + 1.0f), OldShape, 0.0f },
			{ 1.0f, OldShape, 0.0f } };
		int32 Segment = 0;
		while (Segment < 3 && A > Keys[Segment + 1].Age)
		{
			++Segment;
		}
		const FKey& From = Keys[Segment];
		const FKey& To = Keys[Segment + 1];
		const float T = SmoothStep01(From.Age, To.Age, A);
		auto KeyHeight = [](const FKey& Key) { return Key.HeightRatio > 0.0f ? Key.HeightRatio : GetShapeHeightRatio(Key.Shape); };
		M.CapShape = From.Shape;
		M.CapShapeB = To.Shape;
		M.CapShapeBlend = T;
		M.CapHeightRatio = FMath::Lerp(KeyHeight(From), KeyHeight(To), T) * HeightJitter;

		// Phases: Fade = past maturity (colors fade, cap tilts), Rot = second half of the old age.
		const float Fade = SmoothStep01(Mature, 1.0f, A);
		const float Rot = SmoothStep01(Keys[3].Age, 1.0f, A);
		M.CapSag = Rot;

		// --- Size: the cap grows fast, the stem a little later; young stems are stocky.
		M.CapDiameter = Species.Cap.DiameterCm.Lerp(FinalSize) * GrowthFraction(Species, A);
		const float StemGrowth = FMath::Clamp((A - 0.02f) / Mature, 0.0f, 1.0f);
		M.StemLength = Species.Stem.LengthCm.Lerp(FMath::Clamp(FinalSize + StemJitter, 0.0f, 1.0f)) * (0.12f + 0.88f * (1.0f - FMath::Pow(1.0f - StemGrowth, 1.6f)));
		M.StemDiameter = Species.Stem.ThicknessCm.Lerp(FMath::Clamp(FinalSize + ThicknessJitter, 0.0f, 1.0f)) * (0.5f + 0.5f * FMath::Sqrt(FMath::Min(1.0f, A / Mature)));

		switch (Species.Underside.Type)
		{
		case EUndersideType::Pores:  M.CapFleshRatio = 0.45f; break;
		case EUndersideType::Ridges: M.CapFleshRatio = 0.32f; break;
		case EUndersideType::Teeth:  M.CapFleshRatio = 0.3f; break;
		default:                     M.CapFleshRatio = 0.22f; break;
		}
		M.CapFleshRatio *= FleshJitter * (1.0f - 0.35f * Rot); // rotting flesh shrinks
		M.Margin = Species.Cap.Margin;
		M.MarginWave = MarginWave;
		M.MarginLobes = MarginLobes;
		M.CapAsymmetry = Asymmetry + 0.03f * Fade + 0.05f * Rot;
		M.CapTiltDeg = TiltBase * (7.0f + 5.0f * Fade + 10.0f * Rot);
		M.CapTiltAzimuthDeg = TiltAzimuth;

		// --- Colors: one tint per individual, lighter towards the margin; fading then browning with age.
		auto Aged = [Fade, Rot](const FLinearColor& Color)
		{
			const FLinearColor Faded = LerpColor(Color, FLinearColor(0.55f, 0.47f, 0.38f, 1.0f), 0.18f * Fade);
			return LerpColor(Faded, FLinearColor(0.28f, 0.22f, 0.16f, 1.0f), 0.45f * Rot);
		};
		const FLinearColor CapA = ToUnitSRGB(Species.Cap.Colors.A);
		const FLinearColor CapB = ToUnitSRGB(Species.Cap.Colors.B);
		M.CapColorCenter = Aged(Brighten(LerpColor(CapA, CapB, Tint), Brightness));
		M.CapColorMargin = Aged(Brighten(LerpColor(CapA, CapB, Tint * 0.55f), Brightness * 1.04f));

		M.bWarts = Species.Cap.Warts.bPresent;
		if (M.bWarts)
		{
			// Warts are veil remnants: dense on the young cap, washed away with age.
			const float WartFactor = A < Keys[2].Age ? FMath::Lerp(1.2f, 1.0f, SmoothStep01(Keys[1].Age, Keys[2].Age, A)) : FMath::Lerp(1.0f, 0.6f, SmoothStep01(Keys[2].Age, Keys[3].Age, A));
			M.WartCount = FMath::RoundToInt(Species.Cap.Warts.Density * 70.0f * WartFactor * WartJitter);
			M.WartColor = Aged(ToUnitSRGB(Species.Cap.Warts.Color));
		}

		M.UndersideType = Species.Underside.Type;
		M.Attachment = Species.Underside.Attachment;
		const FLinearColor UnderYoung = ToUnitSRGB(Species.Underside.YoungColor);
		const FLinearColor UnderMature = ToUnitSRGB(Species.Underside.MatureColor);
		const FLinearColor UnderOld = ToUnitSRGB(Species.Underside.OldColor);
		M.UndersideColor = Aged(A < Keys[2].Age
			? LerpColor(UnderYoung, UnderMature, SmoothStep01(Keys[1].Age, Keys[2].Age, A))
			: LerpColor(UnderMature, UnderOld, SmoothStep01(Keys[2].Age, Keys[3].Age, A)));
		switch (M.UndersideType)
		{
		case EUndersideType::Gills:  M.GillCount = Gills; break;
		case EUndersideType::Ridges: M.GillCount = Ridges; break;
		default:                     M.GillCount = 0; break;
		}

		// --- Stem.
		M.StemShape = Species.Stem.Shape;
		M.StemBend = BendBase + 0.12f * Rot; // rotting stems lean
		M.StemBendAzimuthDeg = BendAzimuth;
		const FLinearColor StemA = ToUnitSRGB(Species.Stem.Colors.A);
		const FLinearColor StemB = ToUnitSRGB(Species.Stem.Colors.B);
		M.StemColorTop = Aged(LerpColor(StemA, StemB, StemTint * 0.5f));
		M.StemColorBase = Aged(LerpColor(StemA, StemB, 0.5f + StemTint * 0.5f));

		M.bReticulation = Species.Stem.Reticulation.bPresent;
		M.ReticulationColor = ToUnitSRGB(Species.Stem.Reticulation.Color);
		M.ReticulationExtent = Species.Stem.Reticulation.Extent;

		M.Ring = Species.Stem.Ring;
		M.RingHeight = RingHeight;
		M.RingColor = Brighten(M.StemColorTop, 1.05f);
		M.Volva = Species.Stem.Volva;
		M.VolvaColor = M.bWarts ? M.WartColor : M.StemColorBase;

		M.bWormy = A >= Mature && WormyRoll < Species.WormyChance;
		return M;
	}
}
