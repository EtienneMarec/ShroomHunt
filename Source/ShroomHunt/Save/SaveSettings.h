#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SaveSettings.generated.h"

/** Save game behavior (Project Settings > ShroomHunt > Save). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Save"))
class SHROOMHUNT_API USaveSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Save slot used by the autosave and the debug commands without a slot name. */
	UPROPERTY(Config, EditAnywhere, Category = "Save")
	FString SlotName = TEXT("Autosave");

	/** Loads the slot when the first forest map starts (Play in the editor included). */
	UPROPERTY(Config, EditAnywhere, Category = "Save")
	bool bAutoLoad = true;

	/** Saves when a new game day starts (midnight). */
	UPROPERTY(Config, EditAnywhere, Category = "Save")
	bool bAutoSaveEachDay = true;

	/** Saves when the game world closes (quitting, or Stop in the editor). */
	UPROPERTY(Config, EditAnywhere, Category = "Save")
	bool bSaveOnQuit = true;

	virtual FName GetCategoryName() const override { return TEXT("ShroomHunt"); }
};
