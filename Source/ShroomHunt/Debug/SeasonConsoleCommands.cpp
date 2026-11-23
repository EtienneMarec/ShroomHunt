// Season debug console commands: sh.Season.Status (the preview month is the CVar sh.Season.PreviewMonth).

#include "Core/ShroomLog.h"
#include "Engine/World.h"
#include "Environment/EnvironmentDirector.h"
#include "Environment/SeasonModel.h"
#include "HAL/IConsoleManager.h"
#include "Weather/WeatherSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace SeasonConsoleCommandsDetail
{
	FAutoConsoleCommandWithWorld CmdSeasonStatus(
		TEXT("sh.Season.Status"),
		TEXT("sh.Season.Status: foliage of each tree genus (leaves, autumn coloring, spring freshness), snow, frost and dryness."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			const AEnvironmentDirector* Director = AEnvironmentDirector::Find(World);
			if (!Director)
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("sh.Season.Status needs a map with an Environment Director (Play)"));
				return;
			}
			UE_LOG(LogShroomDebug, Display, TEXT("Season at month %.2f:"), Director->GetSeasonMonth());
			for (const FTreePhenology& Phenology : Director->GetPhenologies())
			{
				if (const FFoliageState* Foliage = Director->GetFoliage(Phenology.Genus))
				{
					UE_LOG(LogShroomDebug, Display, TEXT("  %-9s leaves %.2f  coloring %.2f  fresh %.2f  fresh litter %.2f  color (%.2f, %.2f, %.2f)"),
						*Phenology.Genus.ToString(), Foliage->LeafAmount, Foliage->Coloring, Foliage->Freshness, Foliage->FreshLitter,
						Foliage->Color.R, Foliage->Color.G, Foliage->Color.B);
				}
			}
			const FFoliageState& Forest = Director->GetForestFoliage();
			UE_LOG(LogShroomDebug, Display, TEXT("  forest    leaves %.2f  coloring %.2f  fresh %.2f  fresh litter %.2f"),
				Forest.LeafAmount, Forest.Coloring, Forest.Freshness, Forest.FreshLitter);
			if (const UWeatherSubsystem* Weather = World->GetSubsystem<UWeatherSubsystem>())
			{
				UE_LOG(LogShroomDebug, Display, TEXT("  ground    snow %.1f mm (cover %.2f)  frost %.2f  dryness %.2f; falling as snow %.0f%%, at camera %.2f"),
					Weather->GetSnowWaterMm(), Weather->GetSnowCover(), Weather->GetFrost(), Weather->GetDryness(),
					Weather->GetSnowFraction() * 100.0f, Director->GetSnowAtCamera());
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
