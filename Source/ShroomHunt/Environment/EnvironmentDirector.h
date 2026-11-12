#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialParameters.h"
#include "Time/TimeTypes.h"
#include "Weather/WeatherTypes.h"
#include "EnvironmentDirector.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class ASkyLight;
class AVolumetricCloud;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UNiagaraComponent;

/**
 * Drives the sky from the calendar and the weather: sun and moon positions and
 * intensities, sky light, height fog, volumetric cloud coverage, rain particles
 * and a Material Parameter Collection read by materials (wetness, rain...).
 *
 * Place one per map (a Blueprint subclass, BP_EnvironmentDirector). Light, fog and
 * cloud actors are found automatically if not assigned. Their initial values in
 * the level are used as the "clear day" reference, so the level look is preserved.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Environment Director"))
class SHROOMHUNT_API AEnvironmentDirector : public AActor
{
	GENERATED_BODY()

public:
	AEnvironmentDirector();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called every frame after C++ applied the environment (for extra art direction in Blueprint). */
	UFUNCTION(BlueprintImplementableEvent, Category = "ShroomHunt|Environment")
	void OnEnvironmentUpdated(const FWeatherSample& Weather, float Wetness, const FGameDate& Date);

	/** Called on each lightning strike (play thunder here; Distance01 = 0 close, 1 far). */
	UFUNCTION(BlueprintImplementableEvent, Category = "ShroomHunt|Environment")
	void OnLightning(float Distance01);

protected:
	// --- Components ---------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment")
	TObjectPtr<USceneComponent> Root;

	/** Rain particles, kept above the camera. Assign a Niagara System asset in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment")
	TObjectPtr<UNiagaraComponent> RainParticles;

	// --- Scene references (auto-found when empty) ---------------------------

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ShroomHunt|Environment|References")
	bool bAutoFindActors = true;

	/** Directional light with Atmosphere Sun Light Index = 0. Must be Movable. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ShroomHunt|Environment|References")
	TObjectPtr<ADirectionalLight> SunLight;

	/** Optional directional light with Atmosphere Sun Light Index = 1. Must be Movable. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ShroomHunt|Environment|References")
	TObjectPtr<ADirectionalLight> MoonLight;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ShroomHunt|Environment|References")
	TObjectPtr<ASkyLight> SkyLight;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ShroomHunt|Environment|References")
	TObjectPtr<AExponentialHeightFog> HeightFog;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ShroomHunt|Environment|References")
	TObjectPtr<AVolumetricCloud> VolumetricCloud;

	/** Optional MPC (MPC_Environment) with scalar parameters: TimeOfDay, SunElevation, CloudCover,
	 *  RainIntensity, Wetness, FogDensity, WindSpeed, TemperatureC, SeasonProgress. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|References")
	TObjectPtr<UMaterialParameterCollection> EnvironmentParameters;

	// --- Sky ---------------------------------------------------------------------

	/** Yaw (degrees) of geographic north in the world. 0 = north along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Sky", meta = (Units = "deg"))
	float NorthYawDeg = 0.0f;

	/** Sun intensity under a clear sky at high elevation. Negative = use the level's sun intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Sky")
	float SunClearIntensity = -1.0f;

	/** Full-moon intensity under a clear sky. Negative = use the level's moon intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Sky")
	float MoonFullIntensity = -1.0f;

	/**
	 * Minimum night light (starlight / sky glow), as a fraction of the full-moon intensity.
	 * Applied through the moon light, so a moonless night stays dark but readable.
	 * 0 = physically dark nights.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float NightAmbientFraction = 0.15f;

	/** Elevation of the night ambient light when the moon is down (high = soft, few long shadows). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Sky", meta = (ClampMin = "10", ClampMax = "90", Units = "deg"))
	float NightAmbientElevationDeg = 60.0f;

	/** Azimuth (clockwise from north) of the night ambient light when the moon is down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Sky", meta = (ClampMin = "0", ClampMax = "360", Units = "deg"))
	float NightAmbientAzimuthDeg = 200.0f;

	/** Sky light intensity at night, as a fraction of the day intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float SkyLightNightFraction = 0.12f;

	/** How fast visuals follow the weather (1/s). Lower = smoother transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Sky", meta = (ClampMin = "0.05"))
	float VisualResponse = 0.8f;

	// --- Fog & clouds ---------------------------------------------------------------

	/** Fog density multiplier at full weather fog (on top of the level's fog density). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Fog", meta = (ClampMin = "1"))
	float FogDensityMultiplierAtFullFog = 12.0f;

	/** Extra fog multiplier during heavy rain (rain haze). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Fog", meta = (ClampMin = "1"))
	float FogDensityMultiplierInHeavyRain = 3.0f;

	/** Scalar parameter on the volumetric cloud material controlling coverage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Clouds")
	FName CloudCoverageParameter = TEXT("Cloud_GlobalCoverage");

	/** Coverage parameter value for a clear sky. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Clouds")
	float CloudCoverageClear = 0.15f;

	/** Coverage parameter value for an overcast sky. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Clouds")
	float CloudCoverageOvercast = 1.0f;

	// --- Rain -----------------------------------------------------------------------

	/** Niagara user parameter (float) receiving the rain intensity in [0, 1.5]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain")
	FName RainIntensityParameter = TEXT("RainIntensity");

	/** Rain rate (mm/h) mapped to intensity 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain", meta = (ClampMin = "0.1"))
	float RainRateForFullIntensity = 6.0f;

	/** Offset of the rain emitter relative to the camera (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain")
	FVector RainOffsetFromCamera = FVector(0.0, 0.0, 800.0);

	// --- Editor preview -------------------------------------------------------------

	/** Rotates the sun and moon in the editor viewport to preview a season and hour (not in game).
	 *  Only rotations are changed in the editor so the level's light settings are never altered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Editor Preview")
	bool bPreviewInEditor = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Editor Preview", meta = (EditCondition = "bPreviewInEditor"))
	ESeason PreviewSeason = ESeason::Autumn;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Editor Preview", meta = (EditCondition = "bPreviewInEditor", ClampMin = "0", ClampMax = "23.99"))
	float PreviewHour = 10.0f;


private:
	struct FInitialValues
	{
		float SunIntensity = 10.0f;
		float MoonIntensity = 0.3f;
		float SkyLightIntensity = 1.0f;
		float FogDensity = 0.02f;
		bool bCaptured = false;
	};

	void AutoFindActors();
	void CaptureInitialValues();
	void SetupCloudMaterial();
	void ApplyEnvironment(const FGameDate& Date, double TotalGameSeconds, const FWeatherSample& Weather, float Wetness,
		float LatitudeDeg, float LunarCycleGameDays, bool bInGame);
	void UpdateRain(const FWeatherSample& Weather);
	void SetEnvironmentParameter(FName Name, float Value) const;

	UFUNCTION()
	void HandleLightningStrike(float Distance01);

	FInitialValues Initial;
	FWeatherSample SmoothedWeather;
	bool bHasSmoothedWeather = false;
	float LightningFlashRemaining = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	FMaterialParameterInfo CloudCoverageInfo;
	bool bCloudCoverageFound = false;
};
