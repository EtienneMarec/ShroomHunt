#include "Environment/EnvironmentDirector.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestWorld.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Core/ShroomLog.h"
#include "Core/ShroomMath.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameInstance.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ShaderCompiler.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "Time/CalendarMath.h"
#include "Time/CalendarSubsystem.h"
#include "Time/TimeSettings.h"
#include "Vegetation/VegetationDefinition.h"
#include "Weather/SnowModel.h"
#include "Weather/WeatherSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace
{
	using ShroomMath::SmoothStep01;

	TAutoConsoleVariable<float> CVarSeasonPreviewMonth(
		TEXT("sh.Season.PreviewMonth"),
		0.0f,
		TEXT("Shows the foliage of a real month (1 = January ... 12.99 = end of December; 10.8 = late October) ")
		TEXT("instead of the calendar's. 0 = off. Only the foliage: weather and mushrooms keep the calendar."),
		ECVF_Cheat);

	FRotator LightRotationFor(const FCelestialPosition& Position, float NorthYawDeg)
	{
		// Light travels from the body towards the ground, i.e. opposite to the "towards" vector.
		const FVector Towards = CalendarMath::DirectionTowards(Position).RotateAngleAxis(NorthYawDeg, FVector::UpVector);
		return (-Towards).Rotation();
	}

	void WarnIfNotMovable(const ADirectionalLight* Light, const TCHAR* Role)
	{
		if (Light && Light->GetLightComponent() && Light->GetLightComponent()->Mobility != EComponentMobility::Movable)
		{
			UE_LOG(LogShroomEnvironment, Warning, TEXT("%s light '%s' is not Movable: set Mobility = Movable for a dynamic day/night cycle."),
				Role, *Light->GetName());
		}
	}
}

AEnvironmentDirector::AEnvironmentDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	// Spans the whole forest (or drives it): never unloaded by World Partition streaming.
	bIsSpatiallyLoaded = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	RainParticles = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RainParticles"));
	RainParticles->SetupAttachment(Root);
	RainParticles->SetAutoActivate(false);
	RainParticles->SetUsingAbsoluteLocation(true);
	RainParticles->SetUsingAbsoluteRotation(true);

	WeatherPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("WeatherPostProcess"));
	WeatherPostProcess->SetupAttachment(Root);
	WeatherPostProcess->bUnbound = true;
	WeatherPostProcess->Priority = 100.0f; // above the level's volumes; only overrides exposure bias and saturation

	SnowParticles = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SnowParticles"));
	SnowParticles->SetupAttachment(Root);
	SnowParticles->SetAutoActivate(false);
	SnowParticles->SetUsingAbsoluteLocation(true);
	SnowParticles->SetUsingAbsoluteRotation(true);

	SeasonParameters = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Environment/MPC_Season.MPC_Season")));

	RainAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("RainAudio"));
	RainAudio->SetupAttachment(Root);
	RainAudio->SetAutoActivate(false);
	RainAudio->bAllowSpatialization = false; // the rain is all around the player
}

AEnvironmentDirector* AEnvironmentDirector::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AEnvironmentDirector> It(World);
	return It ? *It : nullptr;
}

namespace EnvironmentDirectorLights
{
	/**
	 * Sun first for forward shading, translucency, water and volumetric fog: with two atmosphere
	 * lights of equal priority the engine warns on screen whenever both are above the horizon.
	 */
	void SetForwardShadingPriority(ADirectionalLight* Light, int32 Priority)
	{
		UDirectionalLightComponent* Component = Light ? Cast<UDirectionalLightComponent>(Light->GetLightComponent()) : nullptr;
		if (Component && Component->ForwardShadingPriority != Priority)
		{
			Component->SetForwardShadingPriority(Priority);
		}
	}
}

void AEnvironmentDirector::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	UWorld* World = GetWorld();
	if (!bPreviewInEditor || !World || World->IsGameWorld())
	{
		return;
	}

	AutoFindActors();
	EnvironmentDirectorLights::SetForwardShadingPriority(SunLight, 1);
	EnvironmentDirectorLights::SetForwardShadingPriority(MoonLight, 0);
	const UTimeSettings* Settings = GetDefault<UTimeSettings>();
	const FCalendarConfig Config = Settings->MakeCalendarConfig();
	const int32 MidSeasonDay = Config.SeasonLengthDays[static_cast<int32>(PreviewSeason)] / 2;
	const double Seconds = CalendarMath::EncodeDate(Config, 1, PreviewSeason, MidSeasonDay, PreviewHour);
	const FGameDate Date = CalendarMath::DecodeDate(Config, Seconds);

	// Editor preview only rotates the lights: intensities stay as authored in the level.
	if (SunLight)
	{
		SunLight->SetActorRotation(LightRotationFor(CalendarMath::ComputeSunPosition(Settings->LatitudeDeg, Date.RealDayOfYear, Date.Hour), NorthYawDeg));
	}
	if (MoonLight)
	{
		const float Phase = CalendarMath::MoonPhaseAt(Seconds, Settings->LunarCycleGameDays);
		MoonLight->SetActorRotation(LightRotationFor(CalendarMath::ComputeMoonPosition(Settings->LatitudeDeg, Date.RealDayOfYear, Date.Hour, Phase), NorthYawDeg));
	}
}

void AEnvironmentDirector::BeginPlay()
{
	Super::BeginPlay();

	AutoFindActors();
	CaptureInitialValues();
	SetupCloudMaterial();
	WarnIfNotMovable(SunLight, TEXT("Sun"));
	WarnIfNotMovable(MoonLight, TEXT("Moon"));
	EnvironmentDirectorLights::SetForwardShadingPriority(SunLight, 1);
	EnvironmentDirectorLights::SetForwardShadingPriority(MoonLight, 0);

	if (MoonLight)
	{
		if (UDirectionalLightComponent* MoonComponent = Cast<UDirectionalLightComponent>(MoonLight->GetLightComponent()))
		{
			MoonComponent->SetAtmosphereSunLight(true);
			MoonComponent->SetAtmosphereSunLightIndex(1);
		}
	}

	if (UWeatherSubsystem* Weather = GetWorld()->GetSubsystem<UWeatherSubsystem>())
	{
		Weather->OnLightningStrike.AddDynamic(this, &AEnvironmentDirector::HandleLightningStrike);
	}

	if (!MoonLight && NightAmbientFraction > 0.0f)
	{
		UE_LOG(LogShroomEnvironment, Warning, TEXT("Environment director: no moon light (Atmosphere Sun Light Index 1): nights will be fully dark."));
	}
	if (!SunLight)
	{
		UE_LOG(LogShroomEnvironment, Warning, TEXT("Environment director: no sun found (Directional Light with Atmosphere Sun Light Index 0)."));
	}
}

void AEnvironmentDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UWeatherSubsystem* Weather = World->GetSubsystem<UWeatherSubsystem>())
		{
			Weather->OnLightningStrike.RemoveDynamic(this, &AEnvironmentDirector::HandleLightningStrike);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AEnvironmentDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UGameInstance* GameInstance = GetGameInstance();
	const UCalendarSubsystem* Calendar = GameInstance ? GameInstance->GetSubsystem<UCalendarSubsystem>() : nullptr;
	const UWeatherSubsystem* Weather = GetWorld()->GetSubsystem<UWeatherSubsystem>();
	if (!Calendar || !Weather)
	{
		return;
	}

	// Exponential smoothing so that visuals never pop, even when weather is forced.
	const FWeatherSample& Target = Weather->GetCurrentSample();
	if (!bHasSmoothedWeather)
	{
		SmoothedWeather = Target;
		bHasSmoothedWeather = true;
	}
	else
	{
		const float Alpha = 1.0f - FMath::Exp(-VisualResponse * DeltaSeconds);
		SmoothedWeather = FWeatherSample::Lerp(SmoothedWeather, Target, Alpha);
	}

	LightningFlashRemaining = FMath::Max(0.0f, LightningFlashRemaining - DeltaSeconds);

	ApplyEnvironment(Calendar->GetDate(), Calendar->GetTotalGameSeconds(), SmoothedWeather, Weather->GetWetness(),
		Calendar->GetLatitudeDeg(), Calendar->GetLunarCycleGameDays(), true);
	UpdateSeason(Calendar->GetDate());
	UpdateRain(SmoothedWeather, Weather->GetWetness(), DeltaSeconds);

	OnEnvironmentUpdated(SmoothedWeather, Weather->GetWetness(), Calendar->GetDate());
}

void AEnvironmentDirector::AutoFindActors()
{
	UWorld* World = GetWorld();
	if (!bAutoFindActors || !World)
	{
		return;
	}

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		const UDirectionalLightComponent* Component = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!Component)
		{
			continue;
		}
		const int32 Index = Component->GetAtmosphereSunLightIndex();
		if (!SunLight && Index == 0)
		{
			SunLight = *It;
		}
		else if (!MoonLight && Index == 1)
		{
			MoonLight = *It;
		}
	}
	if (!SkyLight)
	{
		TActorIterator<ASkyLight> It(World);
		SkyLight = It ? *It : nullptr;
	}
	if (!HeightFog)
	{
		TActorIterator<AExponentialHeightFog> It(World);
		HeightFog = It ? *It : nullptr;
	}
	if (!SkyAtmosphere)
	{
		TActorIterator<ASkyAtmosphere> It(World);
		SkyAtmosphere = It ? *It : nullptr;
	}
	if (!VolumetricCloud)
	{
		TActorIterator<AVolumetricCloud> It(World);
		VolumetricCloud = It ? *It : nullptr;
	}
}

void AEnvironmentDirector::CaptureInitialValues()
{
	if (SunLight && SunLight->GetLightComponent())
	{
		Initial.SunIntensity = SunLight->GetLightComponent()->Intensity;
		if (const UDirectionalLightComponent* SunComponent = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
		{
			Initial.SunShadowAmount = SunComponent->ShadowAmount;
		}
	}
	if (MoonLight && MoonLight->GetLightComponent())
	{
		Initial.MoonIntensity = MoonLight->GetLightComponent()->Intensity;
	}
	if (SkyLight && SkyLight->GetLightComponent())
	{
		Initial.SkyLightIntensity = SkyLight->GetLightComponent()->Intensity;
	}
	if (HeightFog && HeightFog->GetComponent())
	{
		Initial.FogDensity = HeightFog->GetComponent()->FogDensity;
		Initial.FogInscattering = HeightFog->GetComponent()->FogInscatteringLuminance;
		Initial.FogDirectionalInscattering = HeightFog->GetComponent()->DirectionalInscatteringLuminance;
	}
	if (SkyAtmosphere && SkyAtmosphere->GetComponent())
	{
		Initial.SkyLuminance = SkyAtmosphere->GetComponent()->SkyLuminanceFactor;
	}
	if (SunClearIntensity >= 0.0f)
	{
		Initial.SunIntensity = SunClearIntensity;
	}
	if (MoonFullIntensity >= 0.0f)
	{
		Initial.MoonIntensity = MoonFullIntensity;
	}
	Initial.bCaptured = true;

	UE_LOG(LogShroomEnvironment, Log, TEXT("Environment references: sun=%s moon=%s skylight=%s fog=%s clouds=%s (sun %.2f, sky %.2f, fog %.4f)"),
		SunLight ? *SunLight->GetName() : TEXT("none"), MoonLight ? *MoonLight->GetName() : TEXT("none"),
		SkyLight ? *SkyLight->GetName() : TEXT("none"), HeightFog ? *HeightFog->GetName() : TEXT("none"),
		VolumetricCloud ? *VolumetricCloud->GetName() : TEXT("none"),
		Initial.SunIntensity, Initial.SkyLightIntensity, Initial.FogDensity);
}

void AEnvironmentDirector::SetupCloudMaterial()
{
	CloudMaterial = nullptr;
	bCloudCoverageFound = false;
	if (!bDriveCloudCoverage)
	{
		return;
	}
	UVolumetricCloudComponent* CloudComponent = VolumetricCloud ? VolumetricCloud->FindComponentByClass<UVolumetricCloudComponent>() : nullptr;
	UMaterialInterface* Material = CloudComponent ? CloudComponent->GetMaterial() : nullptr;
	if (!Material || CloudCoverageParameter.IsNone())
	{
		return;
	}

	CloudMaterial = UMaterialInstanceDynamic::Create(Material, this);
	CloudComponent->SetMaterial(CloudMaterial);

	// The engine cloud material uses material layers: the parameter may live on a layer.
	TArray<FMaterialParameterInfo> Candidates;
	Candidates.Add(FMaterialParameterInfo(CloudCoverageParameter, EMaterialParameterAssociation::GlobalParameter, INDEX_NONE));
	for (int32 LayerIndex = 0; LayerIndex < 8; ++LayerIndex)
	{
		Candidates.Add(FMaterialParameterInfo(CloudCoverageParameter, EMaterialParameterAssociation::LayerParameter, LayerIndex));
	}
	for (const FMaterialParameterInfo& Candidate : Candidates)
	{
		float Value = 0.0f;
		if (CloudMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(Candidate), Value))
		{
			CloudCoverageInfo = Candidate;
			bCloudCoverageFound = true;
			UE_LOG(LogShroomEnvironment, Log, TEXT("Cloud coverage parameter '%s' found (association %d, index %d, initial %.3f)"),
				*CloudCoverageParameter.ToString(), static_cast<int32>(Candidate.Association.GetValue()), Candidate.Index, Value);
			return;
		}
	}
	UE_LOG(LogShroomEnvironment, Warning, TEXT("Cloud coverage parameter '%s' not found on material '%s': clouds will not follow the weather."),
		*CloudCoverageParameter.ToString(), *Material->GetName());
}

void AEnvironmentDirector::ApplyEnvironment(const FGameDate& Date, double TotalGameSeconds, const FWeatherSample& Weather, float Wetness,
	float LatitudeDeg, float LunarCycleGameDays, bool bInGame)
{
	const FCelestialPosition Sun = CalendarMath::ComputeSunPosition(LatitudeDeg, Date.RealDayOfYear, Date.Hour);
	const float MoonPhase = CalendarMath::MoonPhaseAt(TotalGameSeconds, LunarCycleGameDays);
	const FCelestialPosition Moon = CalendarMath::ComputeMoonPosition(LatitudeDeg, Date.RealDayOfYear, Date.Hour, MoonPhase);

	// 0 at night, 1 in full daylight, with a twilight ramp around the horizon.
	const float Daylight = SmoothStep01(-6.0f, 8.0f, Sun.ElevationDeg);
	const float Overcast = FMath::Clamp(Weather.CloudCover, 0.0f, 1.0f);
	const float Fog = FMath::Clamp(Weather.FogDensity, 0.0f, 1.0f);
	const float Flash = LightningFlashRemaining > 0.0f ? 1.0f : 0.0f;

	// Bad weather darkens the sky, the ambient light and the fog, and fights auto exposure.
	FSkyMoodParams MoodParams;
	MoodParams.ExposureBiasEV = GloomExposureBiasEV;
	MoodParams.SkyLuminance = GloomSkyLuminance;
	MoodParams.SkyLight = GloomSkyLight;
	MoodParams.Saturation = GloomSaturation;
	MoodParams.FogBrightness = GloomFogBrightness;
	MoodParams.OvercastShadowAmount = OvercastShadowAmount;
	MoodNow = SkyMood::Compute(Overcast, Weather.PrecipitationMmPerHour / RainRateForFullIntensity, Weather.StormIntensity, MoodParams);

	if (SunLight && SunLight->GetLightComponent())
	{
		SunLight->SetActorRotation(LightRotationFor(Sun, NorthYawDeg));
		const float Direct = Daylight * MoodNow.SunFactor * (1.0f - 0.5f * Fog);
		SunLight->GetLightComponent()->SetIntensity(Initial.SunIntensity * Direct);
		// Under clouds the sun is a faint bright patch at most: faint shadows, diffuse light.
		UDirectionalLightComponent* SunComponent = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent());
		if (bInGame && SunComponent)
		{
			const float Shadows = Initial.SunShadowAmount * MoodNow.ShadowAmount;
			if (!FMath::IsNearlyEqual(SunComponent->ShadowAmount, Shadows, 0.005f))
			{
				SunComponent->SetShadowAmount(Shadows);
			}
		}
	}

	if (MoonLight && MoonLight->GetLightComponent())
	{
		const float Night = 1.0f - Daylight;
		const float AboveHorizon = SmoothStep01(-2.0f, 6.0f, Moon.ElevationDeg);
		const float MoonFactor = Moon.Illumination * AboveHorizon * Night * (1.0f - 0.85f * Overcast);

		// Night ambient floor (starlight / sky glow): keeps moonless nights readable.
		const float AmbientFactor = NightAmbientFraction * Night * (1.0f - 0.4f * Overcast);
		const float Total = MoonFactor + AmbientFactor * (1.0f - MoonFactor);

		// Light comes from the moon when it dominates, otherwise from a fixed high direction.
		// Blending the directions avoids any shadow jump when the moon rises or sets.
		FCelestialPosition AmbientPosition;
		AmbientPosition.ElevationDeg = NightAmbientElevationDeg;
		AmbientPosition.AzimuthDeg = NightAmbientAzimuthDeg;
		const float MoonWeight = MoonFactor / FMath::Max(MoonFactor + AmbientFactor, KINDA_SMALL_NUMBER);
		const FVector Towards = FMath::Lerp(CalendarMath::DirectionTowards(AmbientPosition), CalendarMath::DirectionTowards(Moon), MoonWeight).GetSafeNormal();
		const FVector TowardsWorld = (Towards.IsNearlyZero() ? FVector::UpVector : Towards).RotateAngleAxis(NorthYawDeg, FVector::UpVector);

		MoonLight->SetActorRotation((-TowardsWorld).Rotation());
		MoonLight->GetLightComponent()->SetIntensity(Initial.MoonIntensity * Total);
	}

	if (SkyLight && SkyLight->GetLightComponent())
	{
		const float DayNight = FMath::Lerp(SkyLightNightFraction, 1.0f, SmoothStep01(-10.0f, 10.0f, Sun.ElevationDeg));
		SkyLight->GetLightComponent()->SetIntensity(Initial.SkyLightIntensity * DayNight * MoodNow.SkyLightFactor * (1.0f + 6.0f * Flash));
	}

	if (bInGame)
	{
		if (SkyAtmosphere && SkyAtmosphere->GetComponent())
		{
			SkyAtmosphere->GetComponent()->SetSkyLuminanceFactor(Initial.SkyLuminance * MoodNow.SkyLuminanceFactor);
		}
		if (WeatherPostProcess)
		{
			FPostProcessSettings& Grading = WeatherPostProcess->Settings;
			Grading.bOverride_AutoExposureBias = true;
			const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
			Grading.AutoExposureBias = GetLevelExposureBias(Camera ? Camera->GetCameraLocation() : GetActorLocation()) + MoodNow.ExposureBiasEV;
			Grading.bOverride_ColorSaturation = true;
			Grading.ColorSaturation = FVector4(MoodNow.Saturation, MoodNow.Saturation, MoodNow.Saturation, 1.0);
		}
	}

	if (HeightFog && HeightFog->GetComponent())
	{
		const float RainHaze = FMath::Clamp(Weather.PrecipitationMmPerHour / RainRateForFullIntensity, 0.0f, 1.0f);
		const float Multiplier = FMath::Lerp(1.0f, FogDensityMultiplierAtFullFog, Fog) * FMath::Lerp(1.0f, FogDensityMultiplierInHeavyRain, RainHaze);
		HeightFog->GetComponent()->SetFogDensity(Initial.FogDensity * Multiplier);
		if (bInGame)
		{
			// The fog turns grey and darker under heavy clouds.
			const auto Grey = [&](const FLinearColor& Color)
			{
				const float Luminance = Color.GetLuminance();
				return FMath::Lerp(Color, FLinearColor(Luminance, Luminance, Luminance), MoodNow.Gloom) * MoodNow.FogBrightness;
			};
			HeightFog->GetComponent()->SetFogInscatteringColor(Grey(Initial.FogInscattering));
			HeightFog->GetComponent()->SetDirectionalInscatteringColor(Grey(Initial.FogDirectionalInscattering) * MoodNow.SunFactor);
		}
	}

	if (CloudMaterial && bCloudCoverageFound)
	{
		// Pushing a dynamic material instance parameter every frame while the engine (re)compiles
		// that material's shaders in the background can trip a renderer assertion (stale uniform
		// expression cache, seen with the volumetric cloud material). Weather changes slowly:
		// only push real changes, and never while shaders are compiling.
		const float Coverage = FMath::Lerp(CloudCoverageClear, CloudCoverageOvercast, Overcast);
		float Current = 0.0f;
		const bool bKnown = CloudMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(CloudCoverageInfo), Current);
		const bool bShadersCompiling = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
		if (!bShadersCompiling && (!bKnown || FMath::Abs(Current - Coverage) > 0.005f))
		{
			CloudMaterial->SetScalarParameterValueByInfo(CloudCoverageInfo, Coverage);
		}
	}

	if (bInGame)
	{
		SetEnvironmentParameter(TEXT("TimeOfDay"), Date.Hour / 24.0f);
		SetEnvironmentParameter(TEXT("SunElevation"), Sun.ElevationDeg);
		SetEnvironmentParameter(TEXT("CloudCover"), Overcast);
		SetEnvironmentParameter(TEXT("RainIntensity"), FMath::Clamp(Weather.PrecipitationMmPerHour / RainRateForFullIntensity, 0.0f, 1.5f));
		SetEnvironmentParameter(TEXT("Wetness"), Wetness);
		SetEnvironmentParameter(TEXT("FogDensity"), Fog);
		SetEnvironmentParameter(TEXT("WindSpeed"), Weather.WindSpeed);
		SetEnvironmentParameter(TEXT("TemperatureC"), Weather.TemperatureC);
		SetEnvironmentParameter(TEXT("SeasonProgress"), static_cast<float>(Date.Season) + Date.SeasonProgress);
	}
}

float AEnvironmentDirector::GetLevelExposureBias(const FVector& Location) const
{
	// The level's own exposure compensation, read every frame (World Partition streams volumes in
	// and out): the highest-priority enabled volume around the camera that sets it, else the
	// engine default (not 0). Our own weather grading is skipped.
	float Bias = FPostProcessSettings().AutoExposureBias;
	float BestPriority = -MAX_flt;
	const IInterface_PostProcessVolume* Own = WeatherPostProcess;
	for (IInterface_PostProcessVolume& Volume : GetWorld()->GetPostProcessVolumeIterator())
	{
		if (&Volume == Own)
		{
			continue;
		}
		const FPostProcessVolumeProperties Properties = Volume.GetProperties();
		if (!Properties.bIsEnabled || !Properties.Settings || !Properties.Settings->bOverride_AutoExposureBias || Properties.Priority <= BestPriority)
		{
			continue;
		}
		if (Properties.bIsUnbound || Volume.EncompassesPoint(Location, 0.0f, nullptr))
		{
			BestPriority = Properties.Priority;
			Bias = Properties.Settings->AutoExposureBias;
		}
	}
	return Bias;
}

bool AEnvironmentDirector::EnsureField()
{
	if (!bFieldLoaded)
	{
		bFieldLoaded = true;
		TArray<FString> Errors;
		Field = ForestName.IsEmpty() ? nullptr : ForestFieldCache::GetField(ForestName, Errors);
		bool bFoundLandscape = false;
		FieldToWorld = ForestWorld::GetFieldToWorld(GetWorld(), FVector::ZeroVector, &bFoundLandscape);
		if (!Field || !bFoundLandscape)
		{
			Field.Reset(); // not a forest map: open-sky rain
		}
	}
	return Field.IsValid();
}

float AEnvironmentDirector::SampleCanopy(const FVector& WorldLocation)
{
	if (!EnsureField())
	{
		return 0.0f;
	}
	const FVector FieldM = FieldToWorld.InverseTransformPosition(WorldLocation);
	const FForestSample Sample = Field->Sample(static_cast<float>(FieldM.X), static_cast<float>(FieldM.Y));
	// Bare winter branches still catch a little rain; the leaves catch the rest.
	float Leaves = 1.0f;
	if (FoliageNow.Num() > 0)
	{
		Leaves = 0.0f;
		float Total = 0.0f;
		for (int32 Genus = 0; Genus < Sample.StandWeights.Num(); ++Genus)
		{
			const int32 Index = FieldGenusToPhenology.IsValidIndex(Genus) ? FieldGenusToPhenology[Genus] : INDEX_NONE;
			const float Amount = FoliageNow.IsValidIndex(Index) ? FoliageNow[Index].LeafAmount : 1.0f; // evergreens, unknown genera
			Leaves += Sample.StandWeights[Genus] * Amount;
			Total += Sample.StandWeights[Genus];
		}
		Leaves = Total > 0.0f ? Leaves / Total : 1.0f;
	}
	const float Cover = BareBranchCanopy + (1.0f - BareBranchCanopy) * Leaves;
	return FMath::Clamp(Sample.Canopy * Cover, 0.0f, 1.0f);
}

const FFoliageState* AEnvironmentDirector::GetFoliage(FName Genus) const
{
	const int32 Index = Phenologies.IndexOfByPredicate([Genus](const FTreePhenology& P) { return P.Genus == Genus; });
	return FoliageNow.IsValidIndex(Index) ? &FoliageNow[Index] : nullptr;
}

void AEnvironmentDirector::UpdateSeason(const FGameDate& Date)
{
	if (!bSeasonLoaded)
	{
		bSeasonLoaded = true;
		SeasonCollection = SeasonParameters.LoadSynchronous();
		if (!SeasonCollection)
		{
			UE_LOG(LogShroomEnvironment, Warning, TEXT("Environment director: no season collection at %s (run Tools/Python/create_season_collection.py)."),
				*SeasonParameters.ToString());
		}
		TArray<FString> Errors;
		if (const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors))
		{
			for (const FTreeArchetype& Tree : Vegetation->Trees)
			{
				Phenologies.Add(SeasonModel::FromArchetype(Tree));
			}
		}
		ForestWeights.Init(1.0f, Phenologies.Num());
		if (EnsureField())
		{
			// Forest composition on a coarse grid: weights of the placeholder averages.
			ForestWeights.Init(0.0f, Phenologies.Num());
			const TArray<FName>& Genera = Field->GetGenera();
			FieldGenusToPhenology.Init(INDEX_NONE, Genera.Num());
			for (int32 Genus = 0; Genus < Genera.Num(); ++Genus)
			{
				const FName Name = Genera[Genus];
				FieldGenusToPhenology[Genus] = Phenologies.IndexOfByPredicate([Name](const FTreePhenology& P) { return P.Genus == Name; });
			}
			constexpr int32 Grid = 24;
			const float Size = Field->GetSizeM();
			for (int32 Y = 0; Y < Grid; ++Y)
			{
				for (int32 X = 0; X < Grid; ++X)
				{
					const FForestSample Sample = Field->Sample((X + 0.5f) * Size / Grid, (Y + 0.5f) * Size / Grid);
					for (int32 Genus = 0; Genus < Sample.StandWeights.Num(); ++Genus)
					{
						const int32 Index = FieldGenusToPhenology.IsValidIndex(Genus) ? FieldGenusToPhenology[Genus] : INDEX_NONE;
						if (ForestWeights.IsValidIndex(Index))
						{
							ForestWeights[Index] += Sample.StandWeights[Genus] * Sample.Canopy;
						}
					}
				}
			}
		}
	}

	const float Preview = CVarSeasonPreviewMonth.GetValueOnGameThread();
	SeasonMonth = Preview >= 1.0f ? FMath::Clamp(Preview, 1.0f, 12.999f) : Date.Month;
	const uint32 Seed = Field ? static_cast<uint32>(Field->GetDefinition().Seed) : 0u;
	FoliageNow.SetNum(Phenologies.Num());
	for (int32 Index = 0; Index < Phenologies.Num(); ++Index)
	{
		const FTreePhenology& P = Phenologies[Index];
		FoliageNow[Index] = SeasonModel::Evaluate(P, SeasonMonth, SeasonModel::YearShiftMonths(Seed, Date.Year, P.Genus));
		const FString Genus = P.Genus.ToString();
		SetSeasonParameter(*(Genus + TEXT("LeafAmount")), FoliageNow[Index].LeafAmount);
		SetSeasonParameter(*(Genus + TEXT("Coloring")), FoliageNow[Index].Coloring);
		SetSeasonParameter(*(Genus + TEXT("Freshness")), FoliageNow[Index].Freshness);
		SetSeasonVector(*(Genus + TEXT("FoliageColor")), FoliageNow[Index].Color);
	}
	ForestFoliage = SeasonModel::Blend(FoliageNow, ForestWeights);
	SetSeasonParameter(TEXT("LeafAmount"), ForestFoliage.LeafAmount);
	SetSeasonParameter(TEXT("Coloring"), ForestFoliage.Coloring);
	SetSeasonParameter(TEXT("Freshness"), ForestFoliage.Freshness);
	SetSeasonParameter(TEXT("FreshLitter"), ForestFoliage.FreshLitter);
	SetSeasonVector(TEXT("FoliageColor"), ForestFoliage.Color);
	SetSeasonParameter(TEXT("Month"), SeasonMonth);

	if (const UWeatherSubsystem* Weather = GetWorld()->GetSubsystem<UWeatherSubsystem>())
	{
		// Gradual whitening and thaw: exponential approach over GroundVisualSeconds of real time.
		const float Alpha = GroundVisualSeconds > 0.0f ? 1.0f - FMath::Exp(-GetWorld()->GetDeltaSeconds() / GroundVisualSeconds) : 1.0f;
		auto Follow = [Alpha](float& Shown, float Target)
		{
			Shown = Shown < 0.0f ? Target : FMath::Lerp(Shown, Target, Alpha);
			return Shown;
		};
		SetSeasonParameter(TEXT("SnowCover"), Follow(ShownSnowCover, Weather->GetSnowCover()));
		SetSeasonParameter(TEXT("Frost"), Follow(ShownFrost, Weather->GetFrost()));
		// Fresh snow is about ten times thicker than its water (1 mm of water ~ 1 cm of snow): drives the
		// ground displacement (Nanite tessellation of the landscape, V1-2b).
		SetSeasonParameter(TEXT("SnowDepthCm"), Follow(ShownSnowDepthCm, FMath::Clamp(Weather->GetSnowWaterMm(), 0.0f, 40.0f)));
		SetSeasonParameter(TEXT("Dryness"), Weather->GetDryness());
	}
	SetSeasonParameter(TEXT("SnowFalling"), SnowAtCamera);
}

void AEnvironmentDirector::SetSeasonParameter(FName Name, float Value) const
{
	if (SeasonCollection)
	{
		if (UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(SeasonCollection))
		{
			Instance->SetScalarParameterValue(Name, Value);
		}
	}
}

void AEnvironmentDirector::SetSeasonVector(FName Name, const FLinearColor& Value) const
{
	if (SeasonCollection)
	{
		if (UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(SeasonCollection))
		{
			Instance->SetVectorParameterValue(Name, Value);
		}
	}
}

void AEnvironmentDirector::UpdateSnowParticles(const FWeatherSample& Weather, const FVector& CameraLocation)
{
	if (!SnowParticles || !SnowParticles->GetAsset())
	{
		return;
	}
	const bool bShouldSnow = SnowAtCamera > 0.01f;
	if (bShouldSnow != SnowParticles->IsActive())
	{
		if (bShouldSnow)
		{
			SnowParticles->Activate(true);
		}
		else
		{
			SnowParticles->Deactivate();
		}
	}
	if (!bShouldSnow)
	{
		return;
	}
	const FVector2D Heading = Weather.GetWindDirection();
	const FVector WindDirection = FieldToWorld.GetRotation().RotateVector(FVector(Heading.X, Heading.Y, 0.0)).GetSafeNormal2D();
	// Flakes follow the wind almost fully, but are slowed down under the trees.
	const FVector Wind = WindDirection * (Weather.WindSpeed * 100.0f * LocalRainNow.WindFactor);
	const float FallSeconds = FMath::Max(0.0f, static_cast<float>(SnowOffsetFromCamera.Z)) / SnowFallSpeed;
	// Slow flakes would come from far upwind: keep the emitter within 15 m.
	const FVector Upwind = (Wind * FallSeconds).GetClampedToMaxSize(1500.0);
	SnowParticles->SetWorldLocation(CameraLocation + SnowOffsetFromCamera - Upwind);
	SnowParticles->SetVariableFloat(SnowIntensityParameter, SnowAtCamera);
	SnowParticles->SetVariableVec3(WindParameter, Wind);
}

void AEnvironmentDirector::UpdateRain(const FWeatherSample& Weather, float Wetness, float DeltaSeconds)
{
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector CameraLocation = Camera ? Camera->GetCameraLocation() : FVector::ZeroVector;

	// Canopy under the camera, smoothed over about a second while walking.
	const float Canopy = SampleCanopy(CameraLocation);
	CanopyAtCamera = bHasCanopy ? FMath::Lerp(CanopyAtCamera, Canopy, 1.0f - FMath::Exp(-1.5f * DeltaSeconds)) : Canopy;
	bHasCanopy = true;

	FLocalRainParams Params;
	Params.CanopyInterception = CanopyInterception;
	Params.CanopyWindReduction = CanopyWindReduction;
	const float Precipitation = FMath::Clamp(Weather.PrecipitationMmPerHour / RainRateForFullIntensity, 0.0f, 1.5f);
	// Cold air: the precipitation falls as snow (sleet in between), through bare branches.
	const float SnowShare = SnowModel::SnowFraction(Weather.TemperatureC);
	const float Intensity = Precipitation * (1.0f - SnowShare);
	// Flakes are slow and light: a small rate of snow water looks like a lot of snow.
	SnowAtCamera = FMath::Clamp(3.0f * Precipitation * SnowShare * (1.0f - 0.5f * CanopyAtCamera), 0.0f, 1.5f);
	LocalRainNow = LocalRain::Compute(Intensity, Wetness, CanopyAtCamera, Params);
	UpdateSnowParticles(Weather, CameraLocation);

	// Sound: rain on the leaves sounds louder under the trees.
	if (RainAudio && RainAudio->Sound)
	{
		const float Volume = RainSoundVolume * FMath::Clamp(Intensity * (0.75f + 0.35f * CanopyAtCamera) + 0.25f * LocalRainNow.Drip, 0.0f, 1.5f);
		if (Volume > 0.01f && !RainAudio->IsPlaying())
		{
			RainAudio->FadeIn(2.0f, Volume);
		}
		else if (Volume <= 0.01f && RainAudio->IsPlaying())
		{
			RainAudio->FadeOut(3.0f, 0.0f);
		}
		RainAudio->SetVolumeMultiplier(FMath::Max(Volume, 0.0f));
	}

	if (!RainParticles || !RainParticles->GetAsset())
	{
		return;
	}
	const bool bShouldRain = LocalRainNow.Direct > 0.01f || LocalRainNow.Drip > 0.01f;
	if (bShouldRain != RainParticles->IsActive())
	{
		if (bShouldRain)
		{
			RainParticles->Activate(true);
		}
		else
		{
			RainParticles->Deactivate();
		}
	}
	if (!bShouldRain)
	{
		return;
	}

	// Wind drift in world space (the heading is in the forest field frame).
	const FVector2D Heading = Weather.GetWindDirection();
	const FVector WindDirection = FieldToWorld.GetRotation().RotateVector(FVector(Heading.X, Heading.Y, 0.0)).GetSafeNormal2D();
	const FVector Wind = WindDirection * (Weather.WindSpeed * 100.0f * WindDrift * LocalRainNow.WindFactor);
	// Drops spawned upwind land around the camera.
	const float FallSeconds = FMath::Max(0.0f, static_cast<float>(RainOffsetFromCamera.Z)) / DropFallSpeed;
	RainParticles->SetWorldLocation(CameraLocation + RainOffsetFromCamera - Wind * FallSeconds);
	RainParticles->SetVariableFloat(RainIntensityParameter, LocalRainNow.Direct);
	RainParticles->SetVariableFloat(DripIntensityParameter, LocalRainNow.Drip);
	RainParticles->SetVariableVec3(WindParameter, Wind);
}

void AEnvironmentDirector::SetEnvironmentParameter(FName Name, float Value) const
{
	if (!EnvironmentParameters)
	{
		return;
	}
	if (UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(EnvironmentParameters))
	{
		Instance->SetScalarParameterValue(Name, Value);
	}
}

void AEnvironmentDirector::HandleLightningStrike(float Distance01)
{
	// Close strikes flash longer.
	LightningFlashRemaining = FMath::Lerp(0.2f, 0.08f, Distance01);
	OnLightning(Distance01);
}
