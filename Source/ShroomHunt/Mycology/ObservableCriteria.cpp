#include "Mycology/ObservableCriteria.h"

#define LOCTEXT_NAMESPACE "ShroomHunt.Criteria"

namespace ObservableCriteria
{
	FText ColorName(const FLinearColor& Srgb)
	{
		// HSV of the sRGB value (perceptual enough for naming).
		const float R = Srgb.R, G = Srgb.G, B = Srgb.B;
		const float Max = FMath::Max3(R, G, B);
		const float Min = FMath::Min3(R, G, B);
		const float Value = Max;
		const float Saturation = Max > 1e-4f ? (Max - Min) / Max : 0.0f;
		float Hue = 0.0f;
		if (Max - Min > 1e-4f)
		{
			if (Max == R) { Hue = 60.0f * FMath::Fmod((G - B) / (Max - Min), 6.0f); }
			else if (Max == G) { Hue = 60.0f * ((B - R) / (Max - Min) + 2.0f); }
			else { Hue = 60.0f * ((R - G) / (Max - Min) + 4.0f); }
		}
		Hue = Hue < 0.0f ? Hue + 360.0f : Hue;

		if (Saturation < 0.12f)
		{
			if (Value > 0.82f) { return LOCTEXT("White", "white"); }
			if (Value > 0.45f) { return LOCTEXT("Grey", "grey"); }
			return LOCTEXT("DarkGrey", "dark grey");
		}
		if (Saturation < 0.32f && Value > 0.72f && Hue >= 20.0f && Hue <= 70.0f)
		{
			return LOCTEXT("Cream", "cream");
		}
		if (Hue >= 12.0f && Hue < 50.0f && Value < 0.62f && Saturation < 0.8f)
		{
			return Value < 0.35f ? LOCTEXT("DarkBrown", "dark brown") : LOCTEXT("Brown", "brown");
		}
		if (Hue < 18.0f || Hue >= 340.0f) { return LOCTEXT("Red", "red"); }
		if (Hue < 40.0f) { return LOCTEXT("Orange", "orange"); }
		if (Hue < 70.0f) { return LOCTEXT("Yellow", "yellow"); }
		if (Hue < 170.0f) { return LOCTEXT("Green", "green"); }
		if (Hue < 260.0f) { return LOCTEXT("Blue", "blue"); }
		return LOCTEXT("Purple", "purple");
	}

	FText ShapeName(ECapShape Shape)
	{
		switch (Shape)
		{
		case ECapShape::Hemispherical:   return LOCTEXT("ShapeHemispherical", "hemispherical");
		case ECapShape::Campanulate:     return LOCTEXT("ShapeCampanulate", "bell-shaped");
		case ECapShape::Conical:         return LOCTEXT("ShapeConical", "conical");
		case ECapShape::Convex:          return LOCTEXT("ShapeConvex", "convex");
		case ECapShape::Umbonate:        return LOCTEXT("ShapeUmbonate", "with a central bump");
		case ECapShape::Flat:            return LOCTEXT("ShapeFlat", "flat");
		case ECapShape::Depressed:       return LOCTEXT("ShapeDepressed", "depressed in the center");
		case ECapShape::Infundibuliform: return LOCTEXT("ShapeFunnel", "funnel-shaped");
		case ECapShape::Morchelloid:     return LOCTEXT("ShapeMorchelloid", "egg-shaped, a honeycomb of pits");
		case ECapShape::Cerebriform:     return LOCTEXT("ShapeCerebriform", "lobed, folded like a brain");
		}
		return FText::GetEmpty();
	}

	FText AgeName(const FMushroomMorphology& M)
	{
		if (M.Age < 0.08f)
		{
			return LOCTEXT("AgeButton", "button (just emerged)");
		}
		if (M.CapSag > 0.4f)
		{
			return LOCTEXT("AgeDecaying", "decaying");
		}
		switch (M.Stage)
		{
		case EMushroomStage::Young: return LOCTEXT("AgeYoung", "young");
		case EMushroomStage::Mature: return LOCTEXT("AgeMature", "mature");
		default: return LOCTEXT("AgeOld", "old");
		}
	}

	FText VagueLabel(const FMushroomMorphology& M)
	{
		FText Kind;
		switch (M.UndersideType)
		{
		case EUndersideType::Pores:  Kind = LOCTEXT("KindPores", "mushroom with pores"); break;
		case EUndersideType::Ridges: Kind = LOCTEXT("KindRidges", "mushroom with ridges"); break;
		case EUndersideType::Teeth:  Kind = LOCTEXT("KindTeeth", "mushroom with spines"); break;
		case EUndersideType::Pitted: Kind = LOCTEXT("KindPitted", "mushroom with a pitted cap"); break;
		default:                     Kind = LOCTEXT("KindGills", "gilled mushroom"); break;
		}
		return FText::Format(LOCTEXT("VagueLabel", "Unknown {0} {1}"), ColorName(M.CapColorCenter), Kind);
	}

	TArray<FText> Describe(const FMushroomMorphology& M, const FMushroomSpeciesRow& Species)
	{
		TArray<FText> Lines;
		const ECapShape Shape = M.CapShapeBlend < 0.5f ? M.CapShape : M.CapShapeB;
		FText Surface;
		switch (Species.Cap.Surface)
		{
		case ECapSurface::Velvety:    Surface = LOCTEXT("SurfaceVelvety", ", velvety"); break;
		case ECapSurface::Scaly:      Surface = LOCTEXT("SurfaceScaly", ", scaly"); break;
		case ECapSurface::Viscid:     Surface = LOCTEXT("SurfaceViscid", ", slimy"); break;
		case ECapSurface::Fibrillose: Surface = LOCTEXT("SurfaceFibrillose", ", fibrous"); break;
		default:                      Surface = FText::GetEmpty(); break;
		}
		const FText Warts = M.bWarts && M.WartCount > 0
			? FText::Format(LOCTEXT("Warts", ", with {0} warts"), ColorName(M.WartColor)) : FText::GetEmpty();
		Lines.Add(FText::Format(LOCTEXT("Cap", "Cap: {0}, {1} cm across, {2}{3}{4}"),
			ShapeName(Shape), FText::AsNumber(FMath::RoundToInt(M.CapDiameter)), ColorName(M.CapColorCenter), Surface, Warts));

		FText Underside;
		switch (M.UndersideType)
		{
		case EUndersideType::Pores:  Underside = LOCTEXT("UnderPores", "pores (a sponge, no gills)"); break;
		case EUndersideType::Ridges: Underside = LOCTEXT("UnderRidges", "blunt forked ridges running down the stem"); break;
		case EUndersideType::Teeth:  Underside = LOCTEXT("UnderTeeth", "spines"); break;
		default:
			Underside = M.Attachment == EGillAttachment::Free ? LOCTEXT("UnderGillsFree", "gills, free from the stem")
				: (M.Attachment == EGillAttachment::Decurrent ? LOCTEXT("UnderGillsDecurrent", "gills running down the stem") : LOCTEXT("UnderGills", "gills"));
			break;
		}
		if (M.UndersideType == EUndersideType::Pitted)
		{
			Lines.Add(FText::Format(LOCTEXT("UnderPitted", "Underside: none, no gills, the cap is joined to the stem; {0} inside the hollows"), ColorName(M.UndersideColor)));
		}
		else
		{
			Lines.Add(FText::Format(LOCTEXT("Underside", "Underside: {0}, {1}"), Underside, ColorName(M.UndersideColor)));
		}

		FText StemShape;
		switch (M.StemShape)
		{
		case EStemShape::Clavate:  StemShape = LOCTEXT("StemClavate", ", club-shaped"); break;
		case EStemShape::Bulbous:  StemShape = LOCTEXT("StemBulbous", ", swollen base"); break;
		case EStemShape::Tapering: StemShape = LOCTEXT("StemTapering", ", tapering down"); break;
		case EStemShape::Rooting:  StemShape = LOCTEXT("StemRooting", ", rooting"); break;
		default:                   StemShape = FText::GetEmpty(); break;
		}
		const FText Network = M.bReticulation && M.ReticulationExtent > 0.05f
			? FText::Format(LOCTEXT("Network", ", with a fine {0} network"), ColorName(M.ReticulationColor)) : FText::GetEmpty();
		Lines.Add(FText::Format(LOCTEXT("Stem", "Stem: {0} cm tall, {1} cm thick{2}, {3}{4}"),
			FText::AsNumber(FMath::RoundToInt(M.StemLength)), FText::AsNumber(FMath::RoundToInt(M.StemDiameter)), StemShape, ColorName(M.StemColorTop), Network));

		if (M.Ring != ERingType::None)
		{
			Lines.Add(M.Ring == ERingType::Cortina ? LOCTEXT("RingCortina", "Ring: a cobweb-like veil")
				: (M.Ring == ERingType::Skirt ? LOCTEXT("RingSkirt", "Ring: a hanging skirt") : LOCTEXT("RingFragile", "Ring: fragile")));
		}
		if (M.Volva != EVolvaType::None)
		{
			Lines.Add(M.Volva == EVolvaType::Sac ? LOCTEXT("VolvaSac", "Base: in a sac-like cup (volva)")
				: LOCTEXT("VolvaBulb", "Base: bulb with rings of warts (volva)"));
		}
		Lines.Add(FText::Format(LOCTEXT("Age", "Age: {0}"), AgeName(M)));
		if (M.bWormy)
		{
			Lines.Add(LOCTEXT("Wormy", "Small holes: probably wormy"));
		}
		return Lines;
	}

	float EstimateWeightGrams(const FMushroomMorphology& M)
	{
		const float CapRadius = 0.5f * M.CapDiameter;
		const float CapVolume = UE_PI * CapRadius * CapRadius * (M.CapFleshRatio * CapRadius + 0.3f * M.CapHeightRatio * CapRadius);
		const float StemVolume = UE_PI * FMath::Square(0.5f * M.StemDiameter) * M.StemLength;
		return 0.55f * (CapVolume + StemVolume);
	}
}

#undef LOCTEXT_NAMESPACE
