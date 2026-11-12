#include "Environment/EnvironmentDirector.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Core/ShroomLog.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/GameInstance.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraComponent.h"
#include "Time/CalendarMath.h"
#include "Time/CalendarSubsystem.h"
#include "Time/TimeSettings.h"
#include "Weather/WeatherSubsystem.h"

namespace
{
	float SmoothStep01(float Edge0, float Edge1, float X)
	{
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

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

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	RainParticles = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RainParticles"));
	RainParticles->SetupAttachment(Root);
	RainParticles->SetAutoActivate(false);
	RainParticles->SetUsingAbsoluteLocation(true);
	RainParticles->SetUsingAbsoluteRotation(true);
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
	UpdateRain(SmoothedWeather);

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

	if (SunLight && SunLight->GetLightComponent())
	{
		SunLight->SetActorRotation(LightRotationFor(Sun, NorthYawDeg));
		const float Direct = Daylight * (1.0f - 0.8f * Overcast) * (1.0f - 0.5f * Fog);
		SunLight->GetLightComponent()->SetIntensity(Initial.SunIntensity * Direct);
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
		SkyLight->GetLightComponent()->SetIntensity(Initial.SkyLightIntensity * DayNight * (1.0f - 0.25f * Overcast) * (1.0f + 6.0f * Flash));
	}

	if (HeightFog && HeightFog->GetComponent())
	{
		const float RainHaze = FMath::Clamp(Weather.PrecipitationMmPerHour / RainRateForFullIntensity, 0.0f, 1.0f);
		const float Multiplier = FMath::Lerp(1.0f, FogDensityMultiplierAtFullFog, Fog) * FMath::Lerp(1.0f, FogDensityMultiplierInHeavyRain, RainHaze);
		HeightFog->GetComponent()->SetFogDensity(Initial.FogDensity * Multiplier);
	}

	if (CloudMaterial && bCloudCoverageFound)
	{
		CloudMaterial->SetScalarParameterValueByInfo(CloudCoverageInfo, FMath::Lerp(CloudCoverageClear, CloudCoverageOvercast, Overcast));
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

void AEnvironmentDirector::UpdateRain(const FWeatherSample& Weather)
{
	if (!RainParticles || !RainParticles->GetAsset())
	{
		return;
	}

	const float Intensity = FMath::Clamp(Weather.PrecipitationMmPerHour / RainRateForFullIntensity, 0.0f, 1.5f);
	const bool bShouldRain = Intensity > 0.01f;
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

	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		RainParticles->SetWorldLocation(Camera->GetCameraLocation() + RainOffsetFromCamera);
	}
	RainParticles->SetVariableFloat(RainIntensityParameter, Intensity);
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
