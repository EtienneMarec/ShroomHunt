#pragma once

#include "CoreMinimal.h"
#include "Forest/ForestDefinition.h"
#include "VegetationDefinition.generated.h"

/**
 * Everything the vegetation scatter places. The first four are tree forms (one mesh set per
 * genus); fallen leaves also have one mesh set per genus.
 */
UENUM(BlueprintType)
enum class EVegetationKind : uint8
{
	Sapling,
	Pole,
	Mature,
	Old,
	Bracken,
	Bramble,
	Log,
	Stump,
	/** Ground debris (V1-3): single fallen leaves, twigs, stones, moss cushions. */
	Leaf,
	Twigs,
	Stones,
	Moss
};

/** Which group of kinds a PCG node outputs (separate spawners: collision and cull distances differ). */
UENUM(BlueprintType)
enum class EVegetationLayer : uint8
{
	/** Pole, mature and old trees. */
	Trees,
	/** Saplings, bracken, brambles, dead wood. */
	Understory,
	/** Small ground debris: many instances, no collision, short cull distance. */
	Ground
};

/** Look and size of one tree genus at the Mature form. Colors are sRGB [r, g, b]. */
USTRUCT(BlueprintType)
struct FTreeArchetype
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FName Genus;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float HeightM = 25.0f;
	/** Trunk radius at breast height (1.3 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float TrunkRadiusM = 0.22f;
	/** Fraction of the height without crown (clear bole). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float ClearBole = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float CrownRadiusM = 5.0f;
	/** Relative height (0 crown base - 1 top) of the widest point of the crown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float CrownPeak = 0.4f;
	/** Crown outline exponent: 1 = cone/diamond, 2 = ellipse, > 2 = boxy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float CrownExponent = 2.0f;
	/** Foliage clump size relative to the crown radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float ClumpScale = 1.0f;
	/** Clump surface irregularity (0 smooth spheres). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Lumpiness = 0.25f;
	/** How much outer clumps hang down (birch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Droop = 0.0f;
	/** Main branch angle above horizontal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float BranchAngleDeg = 45.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> Bark;
	/** Brightness variation of the bark (fissures, lichens). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float BarkContrast = 0.1f;
	/** Optional second bark color in patches (dark marks of birch bark). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> BarkPatch;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float BarkPatchShare = 0.0f;
	/** Vertical bark ridges (oak) as a radius modulation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float BarkRidges = 0.0f;
	/** Fluted, "muscled" trunk (hornbeam). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Fluting = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> Foliage;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float FoliageVariation = 0.1f;
	/** Color of the dead leaves on the ground (leaf litter); optional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> Litter;
	/** Leaf length (cm) and number of lobes on each side (oak) for the leaf litter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float LeafSizeCm = 7.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") int32 LeafLobes = 0;
};

/** Size of a tree form relative to the genus archetype, and its number of mesh variants. */
USTRUCT(BlueprintType)
struct FTreeFormDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") EVegetationKind Kind = EVegetationKind::Mature;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Height = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float TrunkRadius = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Crown = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float ClearBole = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") int32 Clumps = 12;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") int32 Variants = 2;
};

/** A value per stand age class. */
USTRUCT(BlueprintType)
struct FAgeClassValues
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float ClearCut = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Regeneration = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Pole = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Mature = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Old = 0.0f;

	float Get(EStandAgeClass AgeClass) const;
	float GetMax() const { return FMath::Max(FMath::Max3(ClearCut, Regeneration, Pole), FMath::Max(Mature, Old)); }
};

/** Relative weights of the tree forms in a stand. */
USTRUCT(BlueprintType)
struct FTreeFormMix
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Pole = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Mature = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float Old = 0.0f;
};

USTRUCT(BlueprintType)
struct FAgeClassFormMix
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FTreeFormMix ClearCut;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FTreeFormMix Regeneration;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FTreeFormMix Pole;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FTreeFormMix Mature;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FTreeFormMix Old;

	const FTreeFormMix& Get(EStandAgeClass AgeClass) const;
};

/** Where and how densely trees grow. */
USTRUCT(BlueprintType)
struct FTreeDensityDefinition
{
	GENERATED_BODY()

	/** Stems per hectare under a closed canopy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FAgeClassValues PerHa;
	/** Minimum distance between trees as a fraction of the mean spacing sqrt(1 ha / density). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float SpacingFactor = 0.55f;
	/** Natural gaps: density is scaled by smoothstep(GapCanopy[0], GapCanopy[1], canopy) in closed stands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> GapCanopy;
	/** No trees closer to water than this (riparian genera use RiparianMarginM). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float WaterMarginM = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float RiparianMarginM = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float ScaleJitter = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FAgeClassFormMix FormMix;
};

/** One understory kind: density per age class times its habitat response. JSON trapezoids are [min, optLow, optHigh, max]. */
USTRUCT(BlueprintType)
struct FUnderstoryRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") EVegetationKind Kind = EVegetationKind::Bracken;
	/** Instances per hectare at optimal habitat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FAgeClassValues PerHa;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> Moisture;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> PH;
	/** Light at the ground = 1 - canopy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> Light;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float SpacingFactor = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float WaterMarginM = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float ScaleJitter = 0.25f;
	/** Density multiplier 1 + SlopeBoost x slope (stones on steep ground). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float SlopeBoost = 0.0f;
	/** Density multiplier up to 1 + BankBoost within a few meters of water (stones along streams). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") float BankBoost = 0.0f;
	/**
	 * Density multiplier on the path surfaces [road, ride, footpath] (leaves cover rides and
	 * footpaths too). Empty: never on a path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<float> PathFactor;
	/** Mesh variants (saplings use the Sapling tree form instead; leaf litter has them per genus). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") int32 Variants = 2;
};

/** Vegetation palette shared by the forests. Source of truth: Data/Vegetation/Vegetation.json. */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FVegetationDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<FTreeArchetype> Trees;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<FTreeFormDefinition> Forms;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") FTreeDensityDefinition TreeDensity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<FUnderstoryRule> Understory;
	/** Ground debris rules (same fields as the understory), placed by the Ground layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vegetation") TArray<FUnderstoryRule> Ground;

	/** Understory and ground rules together (the layer of a rule follows from its kind). */
	TArray<const FUnderstoryRule*> GetRules(EVegetationLayer Layer) const;
	const FUnderstoryRule* FindRule(EVegetationKind Kind) const;

	const FTreeArchetype* FindTree(FName Genus) const { return Trees.FindByPredicate([Genus](const FTreeArchetype& T) { return T.Genus == Genus; }); }
	const FTreeFormDefinition* FindForm(EVegetationKind Kind) const { return Forms.FindByPredicate([Kind](const FTreeFormDefinition& F) { return F.Kind == Kind; }); }
	int32 GetVariantCount(EVegetationKind Kind) const;
};

/** One mesh of the vegetation palette (a genus x form x variant, or an understory kind x variant). */
struct SHROOMHUNT_API FVegetationMeshSpec
{
	EVegetationKind Kind = EVegetationKind::Mature;
	/** NAME_None for kinds other than tree forms and leaf litter. */
	FName Genus;
	int32 Variant = 0;

	/** "Fagus_Mature_0", "Bracken_1": the asset is SM_<Key>. */
	FString GetKey() const;
	uint32 GetSeed() const;
};

namespace VegetationData
{
	SHROOMHUNT_API bool IsTreeKind(EVegetationKind Kind);
	/** Kinds with one mesh set per genus: tree forms and leaf litter. */
	SHROOMHUNT_API bool HasGenus(EVegetationKind Kind);
	SHROOMHUNT_API EVegetationLayer GetLayer(EVegetationKind Kind);
	SHROOMHUNT_API const TCHAR* KindName(EVegetationKind Kind);

	/** Parses and validates the vegetation JSON. Errors are appended (file + field). */
	SHROOMHUNT_API bool ParseVegetation(const FString& JsonText, const FString& SourceName, FVegetationDefinition& Out, TArray<FString>& OutErrors);

	/** Loads <Project>/Data/Vegetation/Vegetation.json. */
	SHROOMHUNT_API bool LoadVegetation(FVegetationDefinition& Out, TArray<FString>& OutErrors);

	SHROOMHUNT_API FString GetDefaultPath();

	/** Every mesh of the palette, in a stable order. */
	SHROOMHUNT_API TArray<FVegetationMeshSpec> ListMeshes(const FVegetationDefinition& Definition);

	/** sRGB color from a JSON [r, g, b] array (Fallback when invalid). */
	SHROOMHUNT_API FLinearColor ToColor(const TArray<float>& Values, const FLinearColor& Fallback);
}
