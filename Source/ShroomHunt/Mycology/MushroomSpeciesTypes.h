#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MushroomSpeciesTypes.generated.h"

class UTexture2D;

// -----------------------------------------------------------------------------
// Enums (names are the exact strings used in Data/Species/*.json)
// -----------------------------------------------------------------------------

/** Which field guide (book) teaches the species. */
UENUM(BlueprintType)
enum class EGuideGroup : uint8
{
	Boletes, Amanitas, Chanterelles, Russulas, Lepiotas, Morels, Tricholomas, Polypores, Others
};

UENUM(BlueprintType)
enum class EEdibility : uint8
{
	Excellent, Edible, EdibleWithCare, Inedible, Toxic, Deadly
};

UENUM(BlueprintType)
enum class EToxicSyndrome : uint8
{
	None, Gastrointestinal, Phalloidian, Pantherinic, Muscarinic, Orellanian, Gyromitrin, Coprinic
};

UENUM(BlueprintType)
enum class ETrophicMode : uint8
{
	Mycorrhizal, Saprotrophic, Parasitic, Grassland
};

UENUM(BlueprintType)
enum class ESubstrate : uint8
{
	Soil, Litter, Moss, DeadWood, Stump, LivingTrunk, Grass, Burnt
};

UENUM(BlueprintType)
enum class ESoilType : uint8
{
	Sandy, Loam, Clay, Limestone, Peat
};

UENUM(BlueprintType)
enum class EClustering : uint8
{
	Solitary, Scattered, Caespitose, FairyRing, Troop
};

UENUM(BlueprintType)
enum class ECapShape : uint8
{
	Hemispherical, Campanulate, Conical, Convex, Umbonate, Flat, Depressed, Infundibuliform,
	/** Egg-shaped honeycomb of pits fused to the stem (true morels). */
	Morchelloid,
	/** Irregular lobed cap with brain-like folds (false morels, Gyromitra). */
	Cerebriform
};

UENUM(BlueprintType)
enum class EMarginType : uint8
{
	Even, Inrolled, Wavy, Striate, Appendiculate
};

UENUM(BlueprintType)
enum class ECapSurface : uint8
{
	Smooth, Velvety, Scaly, Warty, Viscid, Fibrillose
};

UENUM(BlueprintType)
enum class EUndersideType : uint8
{
	Gills, Pores, Ridges, Teeth,
	/** No underside: spores form in the pits or folds of the cap (morels, false morels). */
	Pitted
};

UENUM(BlueprintType)
enum class EGillAttachment : uint8
{
	Free, Adnate, Decurrent, Sinuate
};

UENUM(BlueprintType)
enum class EStemShape : uint8
{
	Cylindrical, Clavate, Bulbous, Tapering, Rooting
};

UENUM(BlueprintType)
enum class ERingType : uint8
{
	None, Skirt, Fragile, Cortina
};

UENUM(BlueprintType)
enum class EVolvaType : uint8
{
	None, Sac, Friable, Bulb
};

UENUM(BlueprintType)
enum class EColorChange : uint8
{
	None, Blue, Red, Yellow, Black
};

UENUM(BlueprintType)
enum class ETaste : uint8
{
	Mild, Bitter, Acrid, Untested
};

/** Growth stage of a fruit body. */
UENUM(BlueprintType)
enum class EMushroomStage : uint8
{
	Young, Mature, Old
};

// -----------------------------------------------------------------------------
// Small value types
// -----------------------------------------------------------------------------

/**
 * Trapezoid response: 0 outside [Min, Max], 1 inside [OptLow, OptHigh], linear between.
 * JSON: [min, optLow, optHigh, max].
 */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FTrapezoid
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float Min = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float OptLow = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float OptHigh = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float Max = 0.0f;

	FTrapezoid() = default;
	FTrapezoid(float InMin, float InOptLow, float InOptHigh, float InMax)
		: Min(InMin), OptLow(InOptLow), OptHigh(InOptHigh), Max(InMax)
	{
	}

	/** True when Min <= OptLow <= OptHigh <= Max. */
	bool IsOrdered() const { return Min <= OptLow && OptLow <= OptHigh && OptHigh <= Max; }

	/** Response in [0, 1]. */
	float Evaluate(float X) const;

	/**
	 * Response for a periodic variable (months: Period 12). The trapezoid may extend past
	 * the period, e.g. [11, 12, 14, 15] = November to early March.
	 */
	float EvaluatePeriodic(float X, float Period) const;
};

/** Simple [Min, Max] range. JSON: [min, max]. */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FShroomRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float Min = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float Max = 0.0f;

	FShroomRange() = default;
	FShroomRange(float InMin, float InMax) : Min(InMin), Max(InMax) {}

	float Lerp(float Alpha) const { return FMath::Lerp(Min, Max, Alpha); }
};

/** Two sRGB colors; an individual lerps between them. JSON: ["#RRGGBB", "#RRGGBB"]. */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FColorRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color")
	FColor A = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Color")
	FColor B = FColor::White;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FHostAffinity
{
	GENERATED_BODY()

	/** Tree genus, e.g. Fagus, Quercus, Betula. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	FName Genus;

	/** 0 = never with this tree, 1 = favourite host. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	float Affinity = 0.0f;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FSoilPreference
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	ESoilType Soil = ESoilType::Loam;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	float Weight = 0.0f;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FCapShapeWeight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	ECapShape Shape = ECapShape::Convex;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	float Weight = 1.0f;
};

// -----------------------------------------------------------------------------
// Morphology
// -----------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FCapWarts
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	bool bPresent = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColor Color = FColor::White;

	/** 0 = few, 1 = covered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	float Density = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	bool bWashOffByRain = false;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FCapMorphology
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	TArray<FCapShapeWeight> YoungShapes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	TArray<FCapShapeWeight> MatureShapes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	TArray<FCapShapeWeight> OldShapes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FShroomRange DiameterCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	EMarginType Margin = EMarginType::Even;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	ECapSurface Surface = ECapSurface::Smooth;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColorRange Colors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FCapWarts Warts;

	const TArray<FCapShapeWeight>& GetShapes(EMushroomStage Stage) const;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FUndersideMorphology
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	EUndersideType Type = EUndersideType::Gills;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	EGillAttachment Attachment = EGillAttachment::Free;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColor YoungColor = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColor MatureColor = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColor OldColor = FColor::White;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FReticulation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	bool bPresent = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColor Color = FColor::White;

	/** Fraction of the stem length covered from the top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	float Extent = 0.0f;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FStemMorphology
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FShroomRange LengthCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FShroomRange ThicknessCm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	EStemShape Shape = EStemShape::Cylindrical;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColorRange Colors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FReticulation Reticulation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	ERingType Ring = ERingType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	EVolvaType Volva = EVolvaType::None;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FFleshTraits
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColor Color = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	EColorChange ColorChange = EColorChange::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FText Odor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	ETaste Taste = ETaste::Mild;
};

USTRUCT(BlueprintType)
struct SHROOMHUNT_API FLookalike
{
	GENERATED_BODY()

	/** Species id; may refer to a species not (yet) in the game. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FName Species;

	/** Edibility of the lookalike. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	EEdibility Danger = EEdibility::Inedible;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TArray<FText> KeyDifferences;
};

// -----------------------------------------------------------------------------
// Species row
// -----------------------------------------------------------------------------

/**
 * One mushroom species: ecology, phenology, spawning, morphology and knowledge.
 * Source of truth: Data/Species/<RowName>.json (see Docs/SpeciesData.md).
 * Values are real-world values (days, mm, degrees C, months); the game converts them.
 */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FMushroomSpeciesRow : public FTableRowBase
{
	GENERATED_BODY()

	// --- Identity -------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString ScientificName;

	/** English common name (localizable). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText CommonName;

	/** French vernacular name, shown as data in the encyclopedia. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FString FrenchName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName Genus;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName Family;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EGuideGroup GuideGroup = EGuideGroup::Others;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EEdibility Edibility = EEdibility::Inedible;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EToxicSyndrome ToxicSyndrome = EToxicSyndrome::None;

	/** Base buying price (euros per kg); 0 = not bought. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	float PricePerKg = 0.0f;

	// --- Ecology --------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	ETrophicMode Trophic = ETrophicMode::Mycorrhizal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	TArray<ESubstrate> Substrates;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	TArray<FHostAffinity> Hosts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	FTrapezoid PH;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	FTrapezoid AltitudeM;

	/** 0 dry - 1 waterlogged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	FTrapezoid Moisture;

	/** 0 open - 1 closed canopy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	FTrapezoid Canopy;

	/** -1 forest interior ... +1 edges and clearings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	float EdgeAffinity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ecology")
	TArray<FSoilPreference> Soils;

	// --- Phenology and trigger ---------------------------------------------------
	/** Month trapezoid (1 = early January, 13 = end of December; may extend past 13). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phenology")
	FTrapezoid FruitingMonths;

	/** Rain accumulation window, real days. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phenology")
	float RainWindowDays = 10.0f;

	/** Min = rain needed to start fruiting, Max = rain for full response (mm over the window). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phenology")
	FShroomRange RainMm;

	/** Mean temperature over the window (degrees C). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phenology")
	FTrapezoid TempC;

	/** Delay from trigger to fruiting, real days. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phenology")
	FShroomRange LagDays;

	/** Reactivity k in P_flush = 1 - exp(-k S F). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phenology")
	float FlushRate = 1.0f;

	/** Real days before the same patch can flush again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phenology")
	float FlushCooldownDays = 7.0f;

	// --- Spawning ------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	float PatchDensityPerHa = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	FShroomRange PatchRadiusM;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	EClustering Clustering = EClustering::Scattered;

	/** Mean fruit bodies per flush at full suitability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	float FruitsPerFlush = 3.0f;

	/** Real days from emergence to collapse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	float LifetimeDays = 7.0f;

	/** Fraction of the lifetime spent young. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	float YoungUntil = 0.25f;

	/** Fraction of the lifetime after which the fruit body is old. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	float MatureUntil = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawning")
	float WormyChance = 0.0f;

	// --- Morphology -----------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FCapMorphology Cap;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FUndersideMorphology Underside;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FStemMorphology Stem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FFleshTraits Flesh;

	/** Hidden criterion: needs a spore print test. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Morphology")
	FColor SporePrint = FColor::White;

	// --- Knowledge --------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TArray<FLookalike> Lookalikes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	FText HabitatText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knowledge")
	TSoftObjectPtr<UTexture2D> Illustration;

	/** Affinity for a tree genus (0 if not a host). */
	float GetHostAffinity(FName TreeGenus) const;

	/** Preference weight for a soil type (0 if not listed). */
	float GetSoilWeight(ESoilType SoilType) const;

	/** Stage for an age expressed as a fraction of the lifetime. */
	EMushroomStage GetStageAt(float LifetimeFraction) const;
};
