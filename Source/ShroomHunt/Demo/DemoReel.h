#pragma once

#include "CoreMinimal.h"
#include "DemoReel.generated.h"

class FForestField;
struct FMushroomSpeciesRow;
struct FMyceliumPatch;
struct FSpawnContext;
struct FVegetationDefinition;
class FVegetationObstacleGrid;

/** How the camera moves during a shot. */
UENUM(BlueprintType)
enum class EDemoShotKind : uint8
{
	/** Walks along a path at eye height, looking ahead. */
	Walk,
	/** Stands, slowly dollying forward, looking at the place. */
	Static,
	/** Rises from eye height over the canopy, tilting down. */
	Rise,
	/** Circles slowly around the place at EyeM, looking at it (mushroom close-ups). */
	Orbit
};

/** Where a shot happens; the place is found in the forest field (nothing to place by hand). */
UENUM(BlueprintType)
enum class EDemoPlace : uint8
{
	Ride,
	Road,
	Footpath,
	/** A gap in the canopy with trees all around (not an empty clear-cut). */
	Clearing,
	/** The bank of a pond. */
	Pond,
	/** Inside an old stand dominated by Genus. */
	OldStand,
	/** At the edge of a regeneration or a pole stand. */
	YoungStand,
	/** In front of a landmark of kind Landmark (Signpost, Bridge, HuntingTower...). */
	Landmark,
	/** In front of the forester's house. */
	Base,
	/** Standing mushrooms of Species: a day of the shot's season when a patch fruits well (spawn model). */
	Mushrooms
};

/** One shot of the demo reel (Data/Demo/DemoReel.json, D-045). */
USTRUCT(BlueprintType)
struct FDemoShot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") FString Title;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") EDemoShotKind Kind = EDemoShotKind::Static;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") EDemoPlace Place = EDemoPlace::Ride;
	/** OldStand: the genus that dominates the stand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") FName Genus;
	/** Landmark: its kind name (ForestLandmarks::KindName). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") FString Landmark;
	/** Mushrooms: the species to show (DT_Species row name). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") FName Species;
	/** Mushrooms: fewest standing mushrooms in the group (rare species: 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") int32 MinFruits = 3;
	/** Mushrooms: distance from the camera to the group; Landmark: farthest camera distance, the view kept open (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float DistanceM = 1.3f;
	/** Landmark: height of the point looked at above the ground (m); negative = the place's default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float LookAtM = -1.0f;
	/**
	 * Landmark: the side the camera stands on, in degrees from the landmark's front, counterclockwise seen
	 * from above (+40 = front-left for someone facing the landmark); 0 = the most open view near the front.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float ViewDeg = 0.0f;
	/** Orbit: angle swept around the place over the shot (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float OrbitDeg = 50.0f;
	/** Which candidate (0 = best): try another one when a place does not look good. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") int32 Pick = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float Seconds = 8.0f;
	/** Walk speed (m/s); Static: dolly distance over the shot (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float SpeedMps = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float DollyM = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float EyeM = 1.7f;
	/** Rise: final height above the ground (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float RiseM = 32.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float FovDeg = 70.0f;
	/** Date: season name (Spring, Summer, Autumn, Winter), day in the season, hour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") FString Season = TEXT("Summer");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") int32 Day = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float Hour = 10.0f;
	/** Forced weather state name (Clear, PartlyCloudy, Overcast, Fog, Drizzle, Rain, HeavyRain, Storm, Snow); empty = the forest's own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") FString Weather;
	/** Snow on the ground (cm) shown during the shot; negative = the real snow pack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float SnowCm = -1.0f;
	/** Game time speed during the shot (multiplier of the normal speed; 0 = frozen sun). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float TimeScale = 0.0f;
};

USTRUCT(BlueprintType)
struct FDemoReel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") FString ForestName = TEXT("F01_LowlandOakBeech");
	/** Game year of every shot (the weather and the snow pack depend on it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") int32 Year = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") float FadeSeconds = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Demo") TArray<FDemoShot> Shots;
};

/** Where a shot was placed in the forest (field meters). */
struct SHROOMHUNT_API FDemoPlacement
{
	bool bFound = false;
	/** Walk: the camera follows this polyline (with ground heights); others: a single anchor. */
	TArray<FVector3d> Track;
	/** Static and Rise: the camera position at the start and the point it looks at. */
	FVector3d Camera = FVector3d::ZeroVector;
	FVector3d Target = FVector3d::ZeroVector;
	/** Mushrooms: the date found for the shot (game seconds); negative = the shot's own date. */
	double GameSeconds = -1.0;
	FString Description;
};

/** Mushrooms: where and when a species stands in numbers (result of DemoReel::FindMushrooms). */
struct SHROOMHUNT_API FDemoMushroomScene
{
	bool bFound = false;
	double GameSeconds = 0.0;
	/** Center of the densest group of good-looking (not rotten) mushrooms, field meters. */
	FVector2d CenterM = FVector2d::ZeroVector;
	int32 Count = 0;
	FString Description;
};

/** Camera pose at one moment of a shot (field meters, Z up). */
struct SHROOMHUNT_API FDemoPose
{
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d LookAt = FVector3d::UnitX();
};

/** The demo reel: data, places found in the forest field, camera motion (pure, D-045). */
namespace DemoReel
{
	SHROOMHUNT_API FString GetDefaultPath();
	SHROOMHUNT_API bool Load(FDemoReel& Out, TArray<FString>& OutErrors);
	SHROOMHUNT_API bool Parse(const FString& JsonText, FDemoReel& Out, TArray<FString>& OutErrors);

	/**
	 * Place of a shot from the field; Mushrooms shots need FindMushrooms + PlaceMushroomShot. With the
	 * vegetation, Landmark shots also keep saplings, young trees, shrubs and bracken out of the view.
	 */
	SHROOMHUNT_API FDemoPlacement FindPlace(const FForestField& Field, const FDemoShot& Shot, const FVegetationDefinition* Vegetation = nullptr);

	/**
	 * Mushrooms: scans the days of the shot's season (year Year, then up to five following years if the species
	 * does not fruit) and returns the Pick-th best group of at least MinFruits mushrooms of the
	 * species, by the same deterministic model the Mushroom Spawner draws. Mushrooms under low cover
	 * (IsCovered: bracken, brambles, shrubs) do not count: they would not be seen.
	 */
	SHROOMHUNT_API FDemoMushroomScene FindMushrooms(const FDemoShot& Shot, int32 Year, const FMushroomSpeciesRow& Species,
		const TArray<FMyceliumPatch>& Patches, const FSpawnContext& Context, TFunctionRef<bool(const FVector2f& PositionM)> IsCovered);

	/**
	 * Mushrooms: a low camera DistanceM from the group, with a free view and a free orbit: no trunk, no
	 * water (IsBlocked), and when possible no bracken, bramble or shrub on the camera or in the line of
	 * sight (IsCovered).
	 */
	SHROOMHUNT_API FDemoPlacement PlaceMushroomShot(const FForestField& Field, const FDemoShot& Shot, const FDemoMushroomScene& Scene,
		TFunctionRef<bool(const FVector2f& PositionM, float RadiusM)> IsBlocked, TFunctionRef<bool(const FVector2f& PositionM, float RadiusM)> IsCovered);

	/**
	 * Mushrooms: grass tufts and herbs of the ground layer within RadiusM of Center (generated at runtime
	 * around the player, so not in the spawner's cover): they hide small mushrooms from a low camera.
	 */
	SHROOMHUNT_API TSharedRef<FVegetationObstacleGrid> MakeGroundCover(const FForestField& Field, const FVegetationDefinition& Vegetation,
		const FVector2d& Center, double RadiusM);

	/** Camera pose at T in [0, 1] of the shot. */
	SHROOMHUNT_API FDemoPose Evaluate(const FForestField& Field, const FDemoShot& Shot, const FDemoPlacement& Placement, float T);
}
