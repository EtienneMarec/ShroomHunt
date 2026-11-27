#pragma once

#include "CoreMinimal.h"
#include "Demo/DemoReel.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoSubsystem.generated.h"

class ACameraActor;
class AMushroomSpawner;
struct FSpawnContext;
class FForestField;
class UUserWidget;

/**
 * Plays the demo reel in a running game (D-045): for each shot of Data/Demo/DemoReel.json it sets
 * the date, the weather and the snow, finds the place in the forest field, flies a camera along the
 * shot with fades in between, and hides the player, the HUD and the UI. Record the screen meanwhile
 * (Docs/Milestones/V1.md, step V1-5). Console: sh.Demo.Play [first shot] [screenshots],
 * sh.Demo.Stop, sh.Demo.List.
 */
UCLASS()
class SHROOMHUNT_API UDemoSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Starts the reel at FirstShot; with bScreenshots, takes a screenshot in the middle of each shot. */
	bool Play(int32 FirstShot, bool bScreenshots, FString& OutReport);

	/**
	 * Renders the reel offline, frame by frame: a fixed time step of 1 / Fps whatever the real frame
	 * rate, every frame saved as a JPEG in Saved/DemoFrames, and WarmupSeconds of black frames (not
	 * saved, time stopped) at the start of each shot so streaming and the runtime ground catch up.
	 * Encode the frames with ffmpeg afterwards (the command is printed at the end).
	 */
	bool Render(int32 FirstShot, int32 Fps, float WarmupSeconds, FString& OutReport);
	bool IsRendering() const { return bRendering; }
	void Stop();
	bool IsPlaying() const { return bPlaying; }

	/** Places found for every shot (field meters and world centimeters), for checking and tuning. */
	FString ListPlaces();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	bool LoadReel(FString& OutReport);
	/** Spawn model inputs as the Mushroom Spawner sees them (weather of this world, trunks). */
	FSpawnContext MakeSpawnContext(const AMushroomSpawner* Spawner);
	void StartShot(int32 Index);
	void EnterCinematic();
	void LeaveCinematic();
	/** Camera (and the hidden pawn under it) at time T in [0, 1] of the current shot. */
	void PlaceCamera(float T);
	/** Starts the motion of the current shot: game time speed and fade in. */
	void BeginShotMotion();
	void OnFrameCaptured(int32 Width, int32 Height, const TArray<FColor>& Pixels);

	FDemoReel Reel;
	TArray<FDemoPlacement> Placements;
	TSharedPtr<const FForestField> Field;
	FTransform FieldToWorld;
	TMap<uint64, float> TriggerCache;

	bool bPlaying = false;
	bool bScreenshots = false;
	bool bScreenshotTaken = false;
	bool bFadingOut = false;
	int32 ShotIndex = 0;
	float ShotTime = 0.0f;
	int32 SavedDebugHud = 1;

	bool bRendering = false;
	int32 RenderFps = 30;
	int32 WarmupFrames = 0;
	int32 WarmupFramesLeft = 0;
	int32 FrameNumber = 0;
	FString FramesFolder;
	FDelegateHandle CaptureHandle;
	TSharedPtr<FThreadSafeCounter> PendingWrites;
	bool bSavedFixedTimeStep = false;
	double SavedFixedDeltaTime = 1.0 / 30.0;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	TArray<TWeakObjectPtr<UUserWidget>> HiddenWidgets;
};
