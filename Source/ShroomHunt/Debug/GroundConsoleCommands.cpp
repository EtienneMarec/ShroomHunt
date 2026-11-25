// Ground texture debug console commands: sh.Ground.BakeTextures / Preview (D-042).

#include "Core/ShroomLog.h"
#include "Environment/GroundTextureLibrary.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace GroundConsoleCommandsDetail
{
	void LogGroundLines(const FString& Report)
	{
		TArray<FString> Lines;
		Report.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogShroomDebug, Display, TEXT("%s"), *Line);
		}
	}

	FAutoConsoleCommand CmdGroundBake(
		TEXT("sh.Ground.BakeTextures"),
		TEXT("sh.Ground.BakeTextures: draws the ground layer textures of Data/Environment/GroundTextures.json and saves them as T_Ground_<Layer>_D / _N (plus PNG previews in Saved/GroundTextures)."),
		FConsoleCommandDelegate::CreateLambda([]() { LogGroundLines(UGroundTextureLibrary::BakeGroundTextures()); }));

	FAutoConsoleCommand CmdGroundPreview(
		TEXT("sh.Ground.Preview"),
		TEXT("sh.Ground.Preview: writes the ground textures as PNG previews only (Saved/GroundTextures), no asset."),
		FConsoleCommandDelegate::CreateLambda([]() { LogGroundLines(UGroundTextureLibrary::WriteGroundPreviews()); }));
}

#endif // !UE_BUILD_SHIPPING
