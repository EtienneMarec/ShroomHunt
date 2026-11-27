// Demo reel console commands: sh.Demo.Play / Render / Stop / List (D-045).

#include "Core/ShroomLog.h"
#include "Demo/DemoSubsystem.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace DemoConsoleCommandsDetail
{
	void LogDemoLines(const FString& Report)
	{
		TArray<FString> Lines;
		Report.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogShroomDebug, Display, TEXT("%s"), *Line);
		}
	}

	UDemoSubsystem* GetDemo(UWorld* World)
	{
		UDemoSubsystem* Demo = World ? World->GetSubsystem<UDemoSubsystem>() : nullptr;
		if (!Demo)
		{
			UE_LOG(LogShroomDebug, Warning, TEXT("sh.Demo needs a running game (Play or Standalone)"));
		}
		return Demo;
	}

	FAutoConsoleCommandWithWorldAndArgs CmdDemoPlay(
		TEXT("sh.Demo.Play"),
		TEXT("sh.Demo.Play [FirstShot = 0] [Screenshots = 0]: plays the demo reel of Data/Demo/DemoReel.json (camera, date, weather, snow); 1 as second argument also takes a screenshot per shot (Saved/Screenshots)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDemoSubsystem* Demo = GetDemo(World))
			{
				FString Report;
				Demo->Play(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0, Args.Num() > 1 && FCString::Atoi(*Args[1]) != 0, Report);
				LogDemoLines(TEXT("Demo: ") + Report);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdDemoRender(
		TEXT("sh.Demo.Render"),
		TEXT("sh.Demo.Render [FirstShot = 0] [Fps = 30] [WarmupSeconds = 3]: renders the demo reel offline, frame by frame at a fixed time step (smooth whatever the frame rate), as JPEG frames in Saved/DemoFrames; prints the ffmpeg command at the end."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDemoSubsystem* Demo = GetDemo(World))
			{
				FString Report;
				Demo->Render(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0, Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 30,
					Args.Num() > 2 ? FCString::Atof(*Args[2]) : 3.0f, Report);
				LogDemoLines(TEXT("Demo: ") + Report);
			}
		}));

	FAutoConsoleCommandWithWorld CmdDemoStop(
		TEXT("sh.Demo.Stop"),
		TEXT("sh.Demo.Stop: stops the demo reel and gives the camera back to the player."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UDemoSubsystem* Demo = GetDemo(World))
			{
				Demo->Stop();
			}
		}));

	FAutoConsoleCommandWithWorld CmdDemoList(
		TEXT("sh.Demo.List"),
		TEXT("sh.Demo.List: the shots of the demo reel and the places found for them in the forest."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UDemoSubsystem* Demo = GetDemo(World))
			{
				LogDemoLines(Demo->ListPlaces());
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
