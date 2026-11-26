#pragma once

#include "CoreMinimal.h"
#include "GroundTextures.generated.h"

struct FVegetationDefinition;

/**
 * Look of one landscape layer (Litter, Moss, Grass, Mud, Gravel, Dirt): a tileable stylized
 * texture drawn from a base color, noise and small objects (fallen leaves, pebbles, grass blades,
 * moss bumps, twigs, puddles). Colors are sRGB [r, g, b]. Densities per square meter.
 */
USTRUCT(BlueprintType)
struct FGroundLayerStyle
{
	GENERATED_BODY()

	/** Landscape paint layer name (must match the layer infos of the forest maps). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") FName Layer;
	/** World size of one texture tile (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float TileM = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") TArray<float> Base;
	/** Second color mixed in by large noise (soil patches, sun-bleached areas). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") TArray<float> Mottle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float MottleAmount = 0.3f;
	/** Fine brightness noise (0 flat). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float Grain = 0.08f;
	/** Fallen leaves drawn into the texture, genera from FGroundTexturesDefinition::LeafGenera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float LeavesPerM2 = 0.0f;
	/** Share of dark, decayed leaves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float LeafDecay = 0.3f;
	/** Leaf size relative to the genus LeafSizeCm (stylized, like the 3D leaves). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float LeafScale = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float PebblesPerM2 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") TArray<float> Pebble;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float PebbleSizeCm = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float BladesPerM2 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") TArray<float> Blade;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float BladeLengthCm = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float MossBumpsPerM2 = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") TArray<float> MossTip;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float TwigsPerM2 = 0.0f;
	/** Share of the area in shallow, dark, flat puddles (mud). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float Puddles = 0.0f;
	/** Material roughness of the layer (read by Tools/Python/create_ground_material.py). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float Roughness = 0.85f;
	/** Relief strength of the normal map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float NormalStrength = 1.0f;
	/** Amplitude (cm) of the gentle undulation of the layer (leaf piles, moss mounds) in the displacement height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float ReliefCm = 1.0f;
	/** Stylized exaggeration of the objects' relief (leaves, pebbles, blades, moss bumps): 1 = real size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float ReliefScale = 1.0f;
};

/** The ground texture set. Source of truth: Data/Environment/GroundTextures.json (D-042). */
USTRUCT(BlueprintType)
struct SHROOMHUNT_API FGroundTexturesDefinition
{
	GENERATED_BODY()

	/** Texture size in pixels (power of two). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") int32 Resolution = 1024;
	/**
	 * Displacement range (cm) of one unit of the albedo alpha: must match the landscape material
	 * (Magnitude 0.1 at landscape scale 100 = 10 cm, Center 0.4 = ground level). The alpha is a
	 * physical height: a leaf adds a few millimeters, not a plateau.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") float ReliefRangeCm = 10.0f;
	/** Genera of the fallen leaves drawn into the textures, with weights (shapes and colors from Vegetation.json). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") TMap<FName, float> LeafGenera;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Ground") TArray<FGroundLayerStyle> Layers;
};

/**
 * One generated layer: albedo (sRGB; alpha = physical height for the height blend and the displacement,
 * 0.4 = ground level, one unit = ReliefRangeCm) and a tangent-space normal map (DirectX green) drawn
 * from an exaggerated shading height.
 */
struct SHROOMHUNT_API FGroundTextureImage
{
	int32 Size = 0;
	/** Share of the pixels covered by an object (leaf, pebble, blade, moss bump, twig). */
	float CoveredShare = 0.0f;
	TArray<FColor> Albedo;
	TArray<FColor> Normal;
};

/** Stylized tileable ground textures for the landscape layers (pure, deterministic, D-042). */
namespace GroundTextures
{
	/** Bump when the generated pixels change for the same data (the editor then rebakes the textures). */
	constexpr int32 GeneratorVersion = 5;

	SHROOMHUNT_API FString GetDefaultPath();
	SHROOMHUNT_API bool Parse(const FString& JsonText, FGroundTexturesDefinition& Out, TArray<FString>& OutErrors);
	SHROOMHUNT_API bool Load(FGroundTexturesDefinition& Out, TArray<FString>& OutErrors);

	/** Draws one layer. Leaves take their shape and litter color from the vegetation genera. */
	SHROOMHUNT_API FGroundTextureImage Generate(const FGroundTexturesDefinition& Definition, const FGroundLayerStyle& Style, const FVegetationDefinition& Vegetation);

	/** The albedo lit by the normal map from the upper left, tiled 2 x 2 at half size: a quick look at relief and seams. */
	SHROOMHUNT_API TArray<FColor> MakePreview(const FGroundTextureImage& Image);
}
