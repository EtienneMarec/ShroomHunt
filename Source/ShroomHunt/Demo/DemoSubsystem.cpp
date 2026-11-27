#include "Demo/DemoSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/ShroomLog.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Engine/World.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestWorld.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Mycology/MushroomSpawner.h"
#include "Vegetation/VegetationObstacles.h"
#include "Time/CalendarMath.h"
#include "Time/CalendarSubsystem.h"
#include "UnrealClient.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Weather/WeatherSubsystem.h"

namespace DemoSubsystemDetail
{
	void SetConsoleValue(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}

	int32 GetConsoleInt(const TCHAR* Name, int32 Fallback)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetInt() : Fallback;
	}
}

TStatId UDemoSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDemoSubsystem, STATGROUP_Tickables);
}

bool UDemoSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDemoSubsystem::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

FSpawnContext UDemoSubsystem::MakeSpawnContext(const AMushroomSpawner* Spawner)
{
	FSpawnContext Context;
	Context.Field = Spawner ? Spawner->GetField() : Field.Get();
	Context.TriggerCache = &TriggerCache;
	TriggerCache.Reset();
	if (const UCalendarSubsystem* Calendar = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UCalendarSubsystem>() : nullptr)
	{
		Context.Calendar = Calendar->GetCalendarConfig();
	}
	if (const TSharedPtr<const FVegetationObstacleGrid> Obstacles = Spawner ? Spawner->GetObstacles() : nullptr)
	{
		Context.IsBlocked = [Obstacles](const FVector2f& P) { return Obstacles->Overlaps(P, 0.1f); };
	}
	TWeakObjectPtr<UWeatherSubsystem> Weather = GetWorld()->GetSubsystem<UWeatherSubsystem>();
	Context.Weather.RainMm = [Weather](double From, double To) { return Weather.IsValid() ? Weather->GetRainfallMm(From, To) : 0.0f; };
	Context.Weather.MeanTemperatureC = [Weather](double From, double To) { return Weather.IsValid() ? Weather->GetMeanTemperatureC(From, To) : 15.0f; };
	return Context;
}

bool UDemoSubsystem::LoadReel(FString& OutReport)
{
	TArray<FString> Errors;
	if (!DemoReel::Load(Reel, Errors))
	{
		OutReport = FString::Join(Errors, TEXT("\n"));
		return false;
	}
	Field = ForestFieldCache::GetField(Reel.ForestName, Errors);
	if (!Field)
	{
		OutReport = FString::Join(Errors, TEXT("\n"));
		return false;
	}
	FieldToWorld = ForestWorld::GetFieldToWorld(GetWorld(), FVector::ZeroVector);
	Placements.Reset();
	AMushroomSpawner* Spawner = AMushroomSpawner::Find(GetWorld());
	for (const FDemoShot& Shot : Reel.Shots)
	{
		if (Shot.Place != EDemoPlace::Mushrooms)
		{
			Placements.Add(DemoReel::FindPlace(*Field, Shot));
			continue;
		}
		// Mushroom shots: the same model, patches, trunks and weather the spawner draws from.
		const FMushroomSpeciesRow* Species = Spawner ? Spawner->FindSpecies(Shot.Species) : nullptr;
		const FDemoMushroomScene Scene = Species ? DemoReel::FindMushrooms(Shot, Reel.Year, *Species, Spawner->GetPatches(), MakeSpawnContext(Spawner)) : FDemoMushroomScene();
		const TSharedPtr<const FVegetationObstacleGrid> Obstacles = Spawner ? Spawner->GetObstacles() : nullptr;
		Placements.Add(DemoReel::PlaceMushroomShot(*Field, Shot, Scene, [&Obstacles](const FVector2f& P, float Radius)
		{
			return Obstacles.IsValid() && Obstacles->Overlaps(P, Radius);
		}));
	}
	return true;
}

FString UDemoSubsystem::ListPlaces()
{
	FString Report;
	if (!LoadReel(Report))
	{
		return TEXT("FAILED: ") + Report;
	}
	TArray<FString> Lines;
	for (int32 Index = 0; Index < Reel.Shots.Num(); ++Index)
	{
		const FDemoShot& Shot = Reel.Shots[Index];
		const FDemoPlacement& Placement = Placements[Index];
		if (!Placement.bFound)
		{
			Lines.Add(FString::Printf(TEXT("%2d %-28s NOT FOUND"), Index, *Shot.Title));
			continue;
		}
		const FDemoPose Start = DemoReel::Evaluate(*Field, Shot, Placement, 0.0f);
		const FVector World = FieldToWorld.TransformPosition(FVector(Start.Position));
		Lines.Add(FString::Printf(TEXT("%2d %-28s %5.1f s  %s %s day %d %05.2fh  %s | start world (%.0f, %.0f, %.0f)"),
			Index, *Shot.Title, Shot.Seconds, *Shot.Season, Shot.Weather.IsEmpty() ? TEXT("(own weather)") : *Shot.Weather, Shot.Day, Shot.Hour,
			*Placement.Description, World.X, World.Y, World.Z));
	}
	return FString::Join(Lines, TEXT("\n"));
}

bool UDemoSubsystem::Play(int32 FirstShot, bool bInScreenshots, FString& OutReport)
{
	Stop();
	if (!LoadReel(OutReport))
	{
		return false;
	}
	if (!Reel.Shots.IsValidIndex(FirstShot))
	{
		OutReport = FString::Printf(TEXT("no shot %d (the reel has %d)"), FirstShot, Reel.Shots.Num());
		return false;
	}
	APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	if (!Controller)
	{
		OutReport = TEXT("no player controller (start Play first)");
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	Controller->SetViewTargetWithBlend(Camera, 0.0f);
	EnterCinematic();
	bPlaying = true;
	bScreenshots = bInScreenshots;
	StartShot(FirstShot);
	OutReport = FString::Printf(TEXT("playing %d shots from %d (sh.Demo.Stop to stop)"), Reel.Shots.Num() - FirstShot, FirstShot);
	return true;
}

bool UDemoSubsystem::Render(int32 FirstShot, int32 Fps, float WarmupSeconds, FString& OutReport)
{
	if (!Play(FirstShot, false, OutReport))
	{
		return false;
	}
	RenderFps = FMath::Clamp(Fps, 10, 120);
	WarmupFrames = FMath::Max(0, FMath::RoundToInt(WarmupSeconds * RenderFps));
	FramesFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DemoFrames"));
	IFileManager::Get().DeleteDirectory(*FramesFolder, false, true);
	IFileManager::Get().MakeDirectory(*FramesFolder, true);
	FrameNumber = 0;
	PendingWrites = MakeShared<FThreadSafeCounter>();
	// Logical time advances by exactly 1 / Fps per frame, however long a frame takes to render.
	bSavedFixedTimeStep = FApp::UseFixedTimeStep();
	SavedFixedDeltaTime = FApp::GetFixedDeltaTime();
	FApp::SetFixedDeltaTime(1.0 / RenderFps);
	FApp::SetUseFixedTimeStep(true);
	CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UDemoSubsystem::OnFrameCaptured);
	bRendering = true;
	StartShot(FirstShot); // again, now with the warm-up
	OutReport = FString::Printf(TEXT("rendering %d shots from %d at %d fps into %s (sh.Demo.Stop to stop)"), Reel.Shots.Num() - FirstShot, FirstShot, RenderFps, *FramesFolder);
	return true;
}

void UDemoSubsystem::OnFrameCaptured(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (!bRendering)
	{
		return;
	}
	// Compress and write on a worker thread; wait if too many frames are queued (memory).
	while (PendingWrites->GetValue() > 6)
	{
		FPlatformProcess::Sleep(0.005f);
	}
	PendingWrites->Increment();
	const FString Path = FramesFolder / FString::Printf(TEXT("frame_%05d.jpg"), FrameNumber++);
	Async(EAsyncExecution::ThreadPool, [Pixels, Width, Height, Path, Pending = PendingWrites]()
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Jpeg = Module.CreateImageWrapper(EImageFormat::JPEG);
		if (Jpeg.IsValid() && Jpeg->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8))
		{
			FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(95), *Path);
		}
		Pending->Decrement();
	});
}

void UDemoSubsystem::EnterCinematic()
{
	using namespace DemoSubsystemDetail;
	if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
	{
		Controller->SetCinematicMode(true, true, true, true, true);
	}
	SavedDebugHud = GetConsoleInt(TEXT("sh.Debug.HUD"), 1);
	SetConsoleValue(TEXT("sh.Debug.HUD"), 0.0f);
	if (GEngine)
	{
		GEngine->ClearOnScreenDebugMessages();
	}
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), true);
	for (UUserWidget* Widget : Widgets)
	{
		if (Widget && Widget->IsVisible())
		{
			Widget->SetVisibility(ESlateVisibility::Hidden);
			HiddenWidgets.Add(Widget);
		}
	}
}

void UDemoSubsystem::LeaveCinematic()
{
	using namespace DemoSubsystemDetail;
	for (const TWeakObjectPtr<UUserWidget>& Widget : HiddenWidgets)
	{
		if (Widget.IsValid())
		{
			Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
	HiddenWidgets.Reset();
	SetConsoleValue(TEXT("sh.Debug.HUD"), static_cast<float>(SavedDebugHud));
	SetConsoleValue(TEXT("sh.Season.PreviewSnowCm"), -1.0f);
	if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
	{
		Controller->SetCinematicMode(false, false, false, true, true);
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StopCameraFade();
		}
		if (APawn* Pawn = Controller->GetPawn())
		{
			Controller->SetViewTargetWithBlend(Pawn, 0.0f);
		}
	}
}

void UDemoSubsystem::Stop()
{
	if (!bPlaying)
	{
		return;
	}
	bPlaying = false;
	bWarmingUp = false;
	if (bRendering)
	{
		bRendering = false;
		UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
		FApp::SetUseFixedTimeStep(bSavedFixedTimeStep);
		FApp::SetFixedDeltaTime(SavedFixedDeltaTime);
		while (PendingWrites.IsValid() && PendingWrites->GetValue() > 0)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		UE_LOG(LogShroomDebug, Display, TEXT("Demo: %d frames in %s. Encode them with:\n  ffmpeg -framerate %d -i \"%s/frame_%%05d.jpg\" -c:v libx264 -pix_fmt yuv420p -crf 16 ShroomHunt_demo.mp4"),
			FrameNumber, *FramesFolder, RenderFps, *FramesFolder);
	}
	LeaveCinematic();
	if (UWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UWeatherSubsystem>() : nullptr)
	{
		Weather->ClearForcedWeather();
	}
	if (const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UCalendarSubsystem* Calendar = GameInstance->GetSubsystem<UCalendarSubsystem>())
		{
			Calendar->SetTimePaused(false);
			Calendar->SetDebugTimeMultiplier(1.0f);
		}
	}
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	UE_LOG(LogShroomDebug, Display, TEXT("Demo: stopped"));
}

void UDemoSubsystem::StartShot(int32 Index)
{
	using namespace DemoSubsystemDetail;
	ShotIndex = Index;
	ShotTime = 0.0f;
	bScreenshotTaken = false;
	bFadingOut = false;
	const FDemoShot& Shot = Reel.Shots[Index];

	// End the previous shot's forced weather before moving in time, so it leaves only the few
	// minutes actually played in the rain history (the mushrooms of later shots depend on it).
	UWeatherSubsystem* Weather = GetWorld()->GetSubsystem<UWeatherSubsystem>();
	if (Weather)
	{
		Weather->ClearForcedWeather();
	}
	// Date and time speed.
	if (const UGameInstance* GameInstance = GetWorld()->GetGameInstance())
	{
		if (UCalendarSubsystem* Calendar = GameInstance->GetSubsystem<UCalendarSubsystem>())
		{
			const FCalendarConfig& Config = Calendar->GetCalendarConfig();
			const int64 SeasonValue = StaticEnum<ESeason>()->GetValueByNameString(Shot.Season);
			const ESeason Season = SeasonValue == INDEX_NONE ? ESeason::Summer : static_cast<ESeason>(SeasonValue);
			const int32 Day = FMath::Clamp(Shot.Day, 1, FMath::Max(Config.SeasonLengthDays[static_cast<int32>(Season)], 1));
			const double Date = Placements[Index].GameSeconds >= 0.0 ? Placements[Index].GameSeconds : CalendarMath::EncodeDate(Config, Reel.Year, Season, Day, Shot.Hour);
			Calendar->SetTotalGameSeconds(Date);
			Calendar->SetTimePaused(Shot.TimeScale <= 0.0f);
			Calendar->SetDebugTimeMultiplier(FMath::Max(Shot.TimeScale, 0.01f));
		}
	}
	// Weather and snow.
	if (Weather)
	{
		const int64 State = Shot.Weather.IsEmpty() ? INDEX_NONE : StaticEnum<EWeatherState>()->GetValueByNameString(Shot.Weather);
		if (State == INDEX_NONE)
		{
			Weather->ClearForcedWeather();
		}
		else
		{
			Weather->ForceWeather(static_cast<EWeatherState>(State), 48.0f);
		}
	}
	SetConsoleValue(TEXT("sh.Season.PreviewSnowCm"), Shot.SnowCm);
	if (AMushroomSpawner* Spawner = AMushroomSpawner::Find(GetWorld()))
	{
		Spawner->Refresh(); // new date and weather: the mushrooms of this day, at once
		if (Shot.Place == EDemoPlace::Mushrooms && Placements[Index].bFound)
		{
			const FVector2f Target(Placements[Index].Target.X, Placements[Index].Target.Y);
			int32 InView = 0;
			for (const FFruitBody& Fruit : Spawner->GetFruitBodies())
			{
				InView += Fruit.SpeciesId == Shot.Species && FVector2f::Distance(Fruit.PositionM, Target) < 1.5f ? 1 : 0;
			}
			UE_LOG(LogShroomDebug, Display, TEXT("Demo: %d %s standing in view"), InView, *Shot.Species.ToString());
		}
	}

	if (Camera)
	{
		Camera->GetCameraComponent()->SetFieldOfView(Shot.FovDeg);
	}
	WarmupFramesLeft = bRendering && Placements[Index].bFound ? WarmupFrames : 0;
	WarmupSecondsLeft = Placements[Index].bFound ? MaxStreamingWaitSeconds : 0.0f;
	bWarmingUp = Placements[Index].bFound;
	if (bWarmingUp)
	{
		// Black and still while the place loads; the motion starts after the warm-up.
		if (const UGameInstance* GameInstance = GetWorld()->GetGameInstance())
		{
			if (UCalendarSubsystem* Calendar = GameInstance->GetSubsystem<UCalendarSubsystem>())
			{
				Calendar->SetTimePaused(true);
			}
		}
		if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (Controller->PlayerCameraManager)
			{
				Controller->PlayerCameraManager->StartCameraFade(1.0f, 1.0f, 0.0f, FLinearColor::Black, false, true);
			}
		}
		PlaceCamera(0.0f);
	}
	else
	{
		BeginShotMotion();
	}
	UE_LOG(LogShroomDebug, Display, TEXT("Demo: shot %d \"%s\" (%s)"), Index, *Shot.Title,
		Placements[Index].bFound ? *Placements[Index].Description : TEXT("place NOT FOUND: skipped"));
	if (!Placements[Index].bFound)
	{
		ShotTime = Shot.Seconds; // skipped at the next tick
	}
}

void UDemoSubsystem::Tick(float DeltaTime)
{
	if (!bPlaying || !Field || !Camera)
	{
		return;
	}
	const FDemoShot& Shot = Reel.Shots[ShotIndex];
	if (bWarmingUp)
	{
		// Black and still until the trees and the landscape around the camera are streamed in (World
		// Partition loads asynchronously after the jump) and, when rendering, the warm-up frames are
		// done; never longer than MaxStreamingWaitSeconds of real time.
		PlaceCamera(0.0f);
		WarmupFramesLeft = FMath::Max(0, WarmupFramesLeft - 1);
		WarmupSecondsLeft -= FApp::GetDeltaTime() > 0.0 && !bRendering ? static_cast<float>(FApp::GetDeltaTime()) : static_cast<float>(1.0 / RenderFps);
		const UWorldPartitionSubsystem* Streaming = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
		const bool bStreamed = !Streaming || Streaming->IsStreamingCompleted();
		if ((bStreamed && WarmupFramesLeft == 0) || WarmupSecondsLeft <= 0.0f)
		{
			if (!bStreamed)
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("Demo: shot %d starts before World Partition finished streaming"), ShotIndex);
			}
			bWarmingUp = false;
			BeginShotMotion();
		}
		return;
	}
	ShotTime += DeltaTime;
	if (ShotTime >= Shot.Seconds)
	{
		if (Reel.Shots.IsValidIndex(ShotIndex + 1))
		{
			StartShot(ShotIndex + 1);
			return;
		}
		Stop();
		return;
	}

	PlaceCamera(ShotTime / Shot.Seconds);
	APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	if (bRendering)
	{
		FScreenshotRequest::RequestScreenshot(false); // caught by OnFrameCaptured at the end of this frame
	}
	if (bScreenshots && !bScreenshotTaken && ShotTime >= 0.5f * Shot.Seconds)
	{
		bScreenshotTaken = true;
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Demo_%02d"), ShotIndex), false, false);
	}
	if (!bFadingOut && ShotTime >= Shot.Seconds - Reel.FadeSeconds && Controller && Controller->PlayerCameraManager)
	{
		bFadingOut = true;
		Controller->PlayerCameraManager->StartCameraFade(0.0f, 1.0f, Reel.FadeSeconds, FLinearColor::Black, false, true);
	}
}

void UDemoSubsystem::PlaceCamera(float T)
{
	const FDemoShot& Shot = Reel.Shots[ShotIndex];
	const FDemoPose Pose = DemoReel::Evaluate(*Field, Shot, Placements[ShotIndex], T);
	const FVector Position = FieldToWorld.TransformPosition(FVector(Pose.Position));
	const FVector LookAt = FieldToWorld.TransformPosition(FVector(Pose.LookAt));
	Camera->SetActorLocationAndRotation(Position, (LookAt - Position).Rotation());
	// The hidden pawn follows the camera: the runtime ground debris and the mushrooms are generated around it.
	const APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	if (APawn* Pawn = Controller ? Controller->GetPawn() : nullptr)
	{
		const FVector Ground = FieldToWorld.TransformPosition(FVector(Pose.Position.X, Pose.Position.Y, Pose.Position.Z - Shot.EyeM + 1.0));
		Pawn->SetActorLocation(Ground, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UDemoSubsystem::BeginShotMotion()
{
	const FDemoShot& Shot = Reel.Shots[ShotIndex];
	if (const UGameInstance* GameInstance = GetWorld()->GetGameInstance())
	{
		if (UCalendarSubsystem* Calendar = GameInstance->GetSubsystem<UCalendarSubsystem>())
		{
			Calendar->SetTimePaused(Shot.TimeScale <= 0.0f);
		}
	}
	if (APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, Reel.FadeSeconds, FLinearColor::Black, false, false);
		}
	}
}
