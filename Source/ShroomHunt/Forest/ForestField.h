#pragma once

#include "CoreMinimal.h"
#include "Forest/ForestDefinition.h"

/** A regular grid of floats covering the forest (meters). Node (i, j) sits at (i * CellSize, j * CellSize). */
struct SHROOMHUNT_API FFieldRaster
{
	int32 Width = 0;
	int32 Height = 0;
	float CellSize = 1.0f;
	TArray<float> Values;

	void Init(int32 InWidth, int32 InHeight, float InCellSize, float Fill = 0.0f);
	bool IsValid() const { return Width > 0 && Height > 0 && Values.Num() == Width * Height; }
	int32 Index(int32 X, int32 Y) const { return Y * Width + X; }
	float& At(int32 X, int32 Y) { return Values[Index(X, Y)]; }
	float At(int32 X, int32 Y) const { return Values[Index(X, Y)]; }

	/** Bilinear sample at a position in meters (clamped to the grid). */
	float Sample(float XM, float YM) const;
};

/** Everything the forest knows about one point. */
struct SHROOMHUNT_API FForestSample
{
	/** Height relative to the terrain's zero level (m) and real altitude (BaseAltitudeM + height). */
	float HeightM = 0.0f;
	float AltitudeM = 0.0f;
	/** Slope as rise over run (0 = flat, 1 = 45 degrees). */
	float Slope = 0.0f;
	/** -1 north-facing, +1 south-facing (north = +X). */
	float Southness = 0.0f;
	/** 0 dry - 1 waterlogged. */
	float Moisture = 0.0f;
	float PH = 5.5f;
	ESoilType Soil = ESoilType::Loam;
	/** 0 open - 1 closed canopy. */
	float Canopy = 0.0f;
	/** 1 at a clearing / clear-cut edge, 0 deep in the stand. */
	float Edgeness = 0.0f;
	/** Water (stream or pond) at this point. */
	bool bWater = false;
	int32 ParcelNumber = 0;
	EStandAgeClass AgeClass = EStandAgeClass::Mature;
	/** Fraction of each tree genus (same order as GetGenera()), sums to 1. */
	TArray<float, TInlineAllocator<8>> StandWeights;
};

/** One forest management parcel (Voronoi cell). */
struct FForestParcel
{
	/** Display number, 1..N (as painted on parcel boundary stones). */
	int32 Number = 0;
	FVector2f Site = FVector2f::ZeroVector;
	EStandAgeClass AgeClass = EStandAgeClass::Mature;
	int32 DominantGenus = 0;
};

/**
 * Deterministic description of a whole forest, generated from an FForestDefinition.
 *
 * Fine grid (1 m, the landscape heightmap): terrain height.
 * Coarse grid (4 m): hydrology (flow accumulation, streams, ponds), moisture (topographic
 * wetness), soil, pH, parcels, stand composition, canopy and edgeness.
 * Pure C++: used by the terrain bake (editor), PCG nodes and mushroom suitability (runtime).
 * Coordinates are field meters: (0, 0) is one corner, (SizeM, SizeM) the opposite one.
 */
class SHROOMHUNT_API FForestField
{
public:
	static constexpr float CoarseCellM = 4.0f;

	/** Generates every layer. Takes ~1 s for 1 km^2 in Development builds. */
	bool Generate(const FForestDefinition& Definition);

	bool IsGenerated() const { return Heights.IsValid(); }
	const FForestDefinition& GetDefinition() const { return Definition; }
	float GetSizeM() const { return Definition.GetSizeM(); }

	/** Everything at one point (field meters). */
	FForestSample Sample(float XM, float YM) const;

	// --- Raw layers (for the bake and debug maps) -----------------------------
	const FFieldRaster& GetHeights() const { return Heights; }
	const FFieldRaster& GetMoisture() const { return Moisture; }
	const FFieldRaster& GetPH() const { return PH; }
	const FFieldRaster& GetCanopy() const { return Canopy; }
	const FFieldRaster& GetEdgeness() const { return Edgeness; }
	const FFieldRaster& GetWaterDistance() const { return WaterDistance; }
	const FFieldRaster& GetFlowAccumulation() const { return FlowAccumulation; }
	const TArray<uint8>& GetSoilCells() const { return SoilCells; }
	const TArray<uint8>& GetWaterCells() const { return WaterCells; }
	const TArray<int32>& GetParcelCells() const { return ParcelCells; }
	const TArray<FForestParcel>& GetParcels() const { return Parcels; }
	const TArray<FName>& GetGenera() const { return Genera; }
	int32 GetCoarseWidth() const { return Moisture.Width; }

	/** Stand weight of genus GenusIndex at a coarse cell index. */
	float GetStandWeight(int32 CoarseIndex, int32 GenusIndex) const { return StandWeights[CoarseIndex * Genera.Num() + GenusIndex]; }

	/** Index of a genus in GetGenera(), or INDEX_NONE. */
	int32 FindGenus(FName Genus) const { return Genera.IndexOfByKey(Genus); }

	int32 GetStreamCellCount() const { return StreamCellCount; }
	int32 GetPondCellCount() const { return PondCellCount; }

private:
	void GenerateHeights();
	void GenerateHydrology();
	void CarveStreams(const FFieldRaster& StreamDepth, const FFieldRaster& StreamDistance);
	void GenerateMoistureAndSoil();
	void GenerateParcels();
	void GenerateStands();
	void GenerateCanopyAndEdges();
	int32 CoarseIndexAt(float XM, float YM) const;

	FForestDefinition Definition;
	uint32 Seed = 0;

	FFieldRaster Heights;          // fine, relative height (m)
	FFieldRaster CoarseHeights;    // coarse, after carving
	FFieldRaster FlowAccumulation; // coarse, upstream area in cells
	FFieldRaster WaterDistance;    // coarse, distance to stream/pond (m)
	FFieldRaster Slope;            // coarse
	FFieldRaster Southness;        // coarse
	FFieldRaster Moisture;         // coarse
	FFieldRaster PH;               // coarse
	FFieldRaster Canopy;           // coarse
	FFieldRaster Edgeness;         // coarse
	TArray<uint8> SoilCells;       // coarse, ESoilType
	TArray<uint8> WaterCells;      // coarse, 1 = stream, 2 = pond
	TArray<int32> ParcelCells;     // coarse, index into Parcels
	TArray<FForestParcel> Parcels;
	TArray<FName> Genera;
	TArray<float> StandWeights;    // coarse cells x genera

	int32 StreamCellCount = 0;
	int32 PondCellCount = 0;
};
