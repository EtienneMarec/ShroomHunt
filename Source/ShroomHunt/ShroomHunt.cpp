// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShroomHunt.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "Environment/GroundTextureLibrary.h"
#include "Vegetation/VegetationMeshLibrary.h"
#include "Vegetation/VegetationSettings.h"

/** Game module: in the editor, rebuilds the vegetation meshes and ground textures before the startup map loads (D-040, D-042). */
class FShroomHuntModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		// Broadcast at the end of the engine init, before the editor opens its startup map.
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
		{
			if (GIsEditor && !IsRunningCommandlet() && GetDefault<UVegetationSettings>()->bBakeOnEditorStartup)
			{
				UVegetationMeshLibrary::BakeIfNeeded();
				UGroundTextureLibrary::BakeIfNeeded();
			}
		});
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
#endif
	}

private:
	FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE( FShroomHuntModule, ShroomHunt, "ShroomHunt" );

DEFINE_LOG_CATEGORY(LogShroomHunt)
