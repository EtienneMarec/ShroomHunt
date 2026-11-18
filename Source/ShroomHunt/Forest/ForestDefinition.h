#pragma once

#include "CoreMinimal.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "ForestDefinition.generated.h"

/** Forest management age class of a parcel. */
UENUM(BlueprintType)
enum class EStandAgeClass : uint8
{
	ClearCut,
	Regeneration,
	Pole,
	Mature,
	Old
};

/** Share of each age class among the parcels (should sum to ~1). */
USTRUCT(BlueprintType)
struct FAgeClassShares
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest") float ClearCut = 0.07f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest") float Regeneration = 0.13f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest") float Pole = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest") float Mature = 0.38f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest") float Old = 0.17f;
};

/** One tree genus of the forest palette and its habitat. JSON trapezoids are [min, optLow, optHigh, max]. */
USTRUCT(BlueprintType)
struct FStandSpecies
{
	GENERATED_BODY()

	/** Tree genus (matches species host names): Fagus, Quercus, Carpinus, Betula, Alnus... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	FName Genus;

	/** Share of parcels where this genus dominates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	float Share = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	TArray<float> Moisture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	TArray<float> PH;

	/** 0-1: colonizes clear-cuts, young stands and open acidic ground (birch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	float Pioneer = 0.0f;

	/** 0-1: lines streams and wet ground (alder). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	float Riparian = 0.0f;
};

/**
 * Parameters of one forest map. Source of truth: Data/Forests/<Name>.json.
 * Everything procedural (terrain, water, soils, stands) derives from these values and Seed.
 */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FForestDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Forest")
	int32 Seed = 0;

	/** Landscape vertices per side (1009 = 1 km at 1 m). Must be a valid landscape size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	int32 HeightmapResolution = 1009;

	/** Real altitude of the terrain's zero level (used by species altitude ranges). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	float BaseAltitudeM = 150.0f;

	/** Peak-to-peak relief of the hills. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	float ReliefM = 40.0f;

	/** Horizontal scale of the main hills. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	float TerrainScaleM = 400.0f;

	/** Depth of the main valley system. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	float ValleyDepthM = 8.0f;

	/** Upstream area needed to start a stream (hectares). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water")
	float StreamThresholdHa = 5.0f;

	/** Minimum depression depth that becomes a pond. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water")
	float PondMinDepthM = 0.4f;

	/** At most this many ponds (deepest qualifying depressions); others stay damp hollows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water")
	int32 MaxPonds = 3;

	/** Largest pond surface (hectares); bigger depressions stay damp hollows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water")
	float PondMaxAreaHa = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soil")
	float SoilScaleM = 250.0f;

	/** Fraction of the area on limestone (neutral to basic pH). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soil")
	float LimestoneShare = 0.15f;

	/** Mean parcel area (French forest management units). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stands")
	float ParcelSizeHa = 13.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stands")
	FAgeClassShares AgeClasses;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stands")
	TArray<FStandSpecies> Stands;

	/** Public road entrances on the map border. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	int32 Entrances = 2;

	/** Flat spots linked by forest roads (future clearings and landmarks). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	int32 Hubs = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	float HubSpacingM = 220.0f;

	/** Roads added to the minimum spanning tree to make loops. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	int32 ExtraRoadLoops = 1;

	/** Road routing cost per meter: 1 + RoadGradeCost x grade^2 (400: a 10 % grade costs 5x flat ground). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	float RoadGradeCost = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	float RoadHalfWidthM = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	float RideHalfWidthM = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	float FootpathHalfWidthM = 0.6f;

	/** Footpaths to ponds, then to old stands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	int32 Footpaths = 3;

	/** Shorter ride pieces (cut by water or the border) are dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paths")
	float RideMinLengthM = 60.0f;

	/** Side length in meters (1 m per heightmap vertex). */
	float GetSizeM() const { return static_cast<float>(HeightmapResolution - 1); }
};

namespace ForestData
{
	/** Parses and validates a forest JSON. Errors are appended (file + field). */
	SHROOMHUNT_API bool ParseForest(const FString& JsonText, const FString& SourceName, FForestDefinition& Out, TArray<FString>& OutErrors);

	/** Loads Data/Forests/<ForestName>.json. */
	SHROOMHUNT_API bool LoadForest(const FString& ForestName, FForestDefinition& Out, TArray<FString>& OutErrors);

	/** <Project>/Data/Forests */
	SHROOMHUNT_API FString GetDefaultFolder();

	/** Trapezoid from a [min, optLow, optHigh, max] array (or a flat 0-1 range when invalid). */
	SHROOMHUNT_API FTrapezoid ToTrapezoid(const TArray<float>& Values);
}
