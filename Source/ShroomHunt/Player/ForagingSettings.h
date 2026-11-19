#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "ForagingSettings.generated.h"

/** Foraging controls and rules. Edit in Project Settings > ShroomHunt > Foraging. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Foraging"))
class SHROOMHUNT_API UForagingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** How far the player can reach a mushroom (m). */
	UPROPERTY(Config, EditAnywhere, Category = "Interaction", meta = (ClampMin = "0.5", Units = "m"))
	float ReachM = 2.5f;

	/** Distance of the examined specimen in front of the camera (cm). */
	UPROPERTY(Config, EditAnywhere, Category = "Interaction", meta = (ClampMin = "10"))
	float ExamineDistanceCm = 40.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Controls")
	FKey ExamineKey = EKeys::E;

	UPROPERTY(Config, EditAnywhere, Category = "Controls")
	FKey PickKey = EKeys::F;

	/** Exhaustion added to a mycelium patch by each mushroom picked there (Docs/SpawnModel.md section 5). */
	UPROPERTY(Config, EditAnywhere, Category = "Rules", meta = (ClampMin = "0", ClampMax = "1"))
	float ExhaustionPerPick = 0.15f;

	/** Exhaustion decays exponentially with this time constant (game days). */
	UPROPERTY(Config, EditAnywhere, Category = "Rules", meta = (ClampMin = "0.1"))
	float ExhaustionRecoveryGameDays = 3.0f;

	virtual FName GetCategoryName() const override { return TEXT("ShroomHunt"); }
};
