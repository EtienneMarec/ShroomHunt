// Forest debug console commands: sh.Forest.Bake.

#include "Core/ShroomLog.h"
#include "Forest/ForestGenerationLibrary.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace ForestConsoleCommandsDetail
{
	FAutoConsoleCommand CmdForestBake(
		TEXT("sh.Forest.Bake"),
		TEXT("sh.Forest.Bake [ForestName = F01_LowlandOakBeech]: generates the forest field and writes heightmap, weightmaps and debug maps to Saved/ForestBake/<ForestName>."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FString Name = Args.Num() > 0 ? Args[0] : TEXT("F01_LowlandOakBeech");
			UE_LOG(LogShroomDebug, Display, TEXT("%s"), *UForestGenerationLibrary::BakeForest(Name));
		}));
}

#endif // !UE_BUILD_SHIPPING
