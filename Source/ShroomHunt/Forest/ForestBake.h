#pragma once

#include "CoreMinimal.h"

class FForestField;

/**
 * Writes the files needed to build a forest map in the editor:
 *  - heightmap.png       16-bit grayscale, HeightmapResolution^2, for Landscape > Import from File
 *  - weight_<Layer>.png  8-bit weightmaps (Litter, Moss, Grass, Mud, Gravel, Dirt), same resolution
 *  - debug_maps.png      contact sheet: terrain+water, moisture, soil, pH, stands+parcels, canopy
 *  - bake_report.txt     import settings and statistics
 * Default folder: Saved/ForestBake/<ForestName>/ (generated data, not versioned).
 */
namespace ForestBake
{
	/** Landscape Z scale the heightmap is encoded for (Landscape Scale Z = 100 => 1 unit = 1/128 m). */
	inline constexpr float LandscapeZScale = 100.0f;

	/** Weightmap layer names (must match the landscape material layer names). */
	SHROOMHUNT_API const TArray<FName>& GetLayerNames();

	SHROOMHUNT_API bool BakeAll(const FForestField& Field, const FString& OutputFolder, FString& OutReport);

	/** Default output folder for a forest. */
	SHROOMHUNT_API FString GetDefaultOutputFolder(const FString& ForestName);

	/** Writes an 8-bit RGBA PNG (debug maps). */
	SHROOMHUNT_API bool WriteColorPng(const FString& Path, const TArray<FColor>& Pixels, int32 Width, int32 Height);

	/** Encodes a height (m, relative) as a landscape 16-bit value for LandscapeZScale. */
	SHROOMHUNT_API uint16 EncodeHeight(float HeightM);
}
