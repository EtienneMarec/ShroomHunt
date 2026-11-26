#pragma once

#include "CoreMinimal.h"
#include "DemoReel.generated.h"

class FForestField;

/** How the camera moves during a shot. */
UENUM(BlueprintType)
enum class EDemoShotKind : uint8
{
	/** Walks along a path at eye height, looking ahead. */
	Walk,
	/** Stands, slowly dollying forward, looking at the place. */
	Static,
	/** Rises from eye height over the canopy, tilting down. */
	Rise
};

/** Where a shot happens; the place is found in the forest field (nothing to place by hand). */
UENUM(BlueprintType)
enum class EDemoPlace : uint8
{
	Ride,
	Road,
	Footpath,
	/** An open area: clear-cut or a gap in the canopy. */
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
	Base
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

	SHROOMHUNT_API FDemoPlacement FindPlace(const FForestField& Field, const FDemoShot& Shot);

	/** Camera pose at T in [0, 1] of the shot. */
	SHROOMHUNT_API FDemoPose Evaluate(const FForestField& Field, const FDemoShot& Shot, const FDemoPlacement& Placement, float T);
}
