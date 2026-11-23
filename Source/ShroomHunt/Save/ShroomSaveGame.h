#pragma once

#include "CoreMinimal.h"
#include "Economy/EconomyTypes.h"
#include "GameFramework/SaveGame.h"
#include "Knowledge/KnowledgeTypes.h"
#include "Player/ForagingSubsystem.h"
#include "Weather/WeatherTypes.h"
#include "ShroomSaveGame.generated.h"

/**
 * One save (M4, D-004): only what the player changed. Forests, patches, weather and mushroom
 * shapes are recomputed from their seeds; the weather overrides are kept because they are
 * part of the rain history the mushrooms depend on.
 */
UCLASS()
class SHROOMHUNT_API UShroomSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Version 1: M4 first version. Bump it when a field changes meaning and convert in USaveSubsystem. */
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY()
	int32 SaveVersion = CurrentVersion;

	/** Real time of the save (for a load menu later). */
	UPROPERTY()
	FDateTime SavedAtUtc;

	// --- Time and weather ---------------------------------------------------------------

	UPROPERTY()
	double TotalGameSeconds = 0.0;

	UPROPERTY()
	TArray<FWeatherOverride> WeatherOverrides;

	// --- Player ---------------------------------------------------------------------------

	/** Map package of the player when saved (the player is put back only on the same map). */
	UPROPERTY()
	FString MapName;

	UPROPERTY()
	bool bHasPlayerTransform = false;

	UPROPERTY()
	FTransform PlayerTransform;

	UPROPERTY()
	FRotator ViewRotation = FRotator::ZeroRotator;

	// --- Domains ---------------------------------------------------------------------------

	UPROPERTY()
	FForagingSaveData Foraging;

	UPROPERTY()
	FKnowledgeSaveData Knowledge;

	UPROPERTY()
	FEconomyState Economy;
};
