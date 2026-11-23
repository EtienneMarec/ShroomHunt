#pragma once

#include "CoreMinimal.h"
#include "Environment/LocalRain.h"
#include "Environment/SkyMood.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialParameters.h"
#include "Time/TimeTypes.h"
#include "Weather/WeatherTypes.h"
#include "EnvironmentDirector.generated.h"

class ADirectionalLight;
class FForestField;
class UAudioComponent;
class AExponentialHeightFog;
class ASkyAtmosphere;
class ASkyLight;
class UPostProcessComponent;
class AVolumetricCloud;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UNiagaraComponent;

/**
 * Drives the sky from the calendar and the weather: sun and moon positions and
 * intensities, sky light, height fog, volumetric cloud coverage, rain particles
 * (slanted by the wind, thinner under the canopy, drops falling from wet leaves),
 * the rain sound and a Material Parameter Collection read by materials (wetness, rain...).
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

	/** Rain felt at the camera this frame (debug HUD). */
	const FLocalRain& GetLocalRain() const { return LocalRainNow; }
	float GetCanopyAtCamera() const { return CanopyAtCamera; }
	const FSkyMood& GetSkyMood() const { return MoodNow; }

	/** The director of a world, if any. */
	static AEnvironmentDirector* Find(const UWorld* World);

protected:
	// --- Components ---------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment")
	TObjectPtr<USceneComponent> Root;

	/** Rain particles, kept above the camera. Assign a Niagara System asset in the Blueprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment")
	TObjectPtr<UNiagaraComponent> RainParticles;

	/**
	 * Weather grading (unbound, above the level's post process volumes): exposure compensation
	 * and saturation of bad weather, on top of the level's own exposure compensation.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment")
	TObjectPtr<UPostProcessComponent> WeatherPostProcess;

	/** Rain sound loop (optional): assign a looping Sound in the Blueprint; volume follows the rain. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment")
	TObjectPtr<UAudioComponent> RainAudio;

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

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "ShroomHunt|Environment|References")
	TObjectPtr<ASkyAtmosphere> SkyAtmosphere;

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

	// --- Bad weather darkening (values at full gloom: storm under a black sky) ------

	/** Exposure compensation (EV) at full gloom; auto exposure would otherwise brighten a dark sky back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Gloom", meta = (ClampMin = "-4", ClampMax = "0"))
	float GloomExposureBiasEV = -1.3f;

	/** Sky atmosphere luminance factor at full gloom (1 = unchanged). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Gloom", meta = (ClampMin = "0", ClampMax = "1"))
	float GloomSkyLuminance = 0.3f;

	/** Sky light intensity factor at full gloom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Gloom", meta = (ClampMin = "0", ClampMax = "1"))
	float GloomSkyLight = 0.45f;

	/** Color saturation at full gloom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Gloom", meta = (ClampMin = "0", ClampMax = "1"))
	float GloomSaturation = 0.65f;

	/** Fog color brightness at full gloom (the fog turns grey). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Gloom", meta = (ClampMin = "0", ClampMax = "1"))
	float GloomFogBrightness = 0.4f;

	// --- Fog & clouds ---------------------------------------------------------------

	/** Fog density multiplier at full weather fog (on top of the level's fog density). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Fog", meta = (ClampMin = "1"))
	float FogDensityMultiplierAtFullFog = 12.0f;

	/** Extra fog multiplier during heavy rain (rain haze). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Fog", meta = (ClampMin = "1"))
	float FogDensityMultiplierInHeavyRain = 3.0f;

	/**
	 * Drive the cloud coverage from the weather through a dynamic instance of the volumetric
	 * cloud material. Off by default: with the engine's m_SimpleVolumetricCloud this crashed the
	 * editor (renderer assertion "UniformExpressionCache should be up to date") when shaders
	 * recompiled. To come back with a project cloud material reading MPC_Environment.CloudCover.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Clouds")
	bool bDriveCloudCoverage = false;

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

	/** Offset of the rain emitter relative to the camera (cm); it is also moved upwind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain")
	FVector RainOffsetFromCamera = FVector(0.0, 0.0, 800.0);

	/** Niagara user parameter (float) receiving the drops falling from leaves in [0, 1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain")
	FName DripIntensityParameter = TEXT("DripIntensity");

	/** Niagara user parameter (vector, cm/s) receiving the horizontal wind drift of the drops. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain")
	FName WindParameter = TEXT("Wind");

	/** Speed at which the drops fall (cm/s), as set in the Niagara system (to move the emitter upwind). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain", meta = (ClampMin = "100"))
	float DropFallSpeed = 1100.0f;

	/** Horizontal drift of the drops relative to the wind speed (1 = drops move with the wind). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain", meta = (ClampMin = "0", ClampMax = "1.5"))
	float WindDrift = 0.8f;

	/** How the canopy changes the rain (interception, drops from leaves, wind shelter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain", meta = (ClampMin = "0", ClampMax = "1"))
	float CanopyInterception = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain", meta = (ClampMin = "0", ClampMax = "1"))
	float CanopyWindReduction = 0.6f;

	/** Data/Forests/<ForestName>.json: canopy under the camera. Outside a forest map the rain is open-sky rain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain")
	FString ForestName = TEXT("F01_LowlandOakBeech");

	/** Rain sound volume at full intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Environment|Rain", meta = (ClampMin = "0"))
	float RainSoundVolume = 1.0f;

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
		FLinearColor FogInscattering = FLinearColor::White;
		FLinearColor FogDirectionalInscattering = FLinearColor::White;
		FLinearColor SkyLuminance = FLinearColor::White;
		float ExposureBias = 0.0f;
		bool bCaptured = false;
	};

	void AutoFindActors();
	void CaptureInitialValues();
	void SetupCloudMaterial();
	void ApplyEnvironment(const FGameDate& Date, double TotalGameSeconds, const FWeatherSample& Weather, float Wetness,
		float LatitudeDeg, float LunarCycleGameDays, bool bInGame);
	void UpdateRain(const FWeatherSample& Weather, float Wetness, float DeltaSeconds);
	float SampleCanopy(const FVector& WorldLocation);
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

	TSharedPtr<const FForestField> Field;
	FTransform FieldToWorld;
	bool bFieldLoaded = false;
	float CanopyAtCamera = 0.0f;
	bool bHasCanopy = false;
	FLocalRain LocalRainNow;
	FSkyMood MoodNow;
};
