#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ForagerComponent.generated.h"

class AMushroomSpawner;
class UForagingWidget;
class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * Lets the player forage: aim at a mushroom within reach, examine it up close (turn it with
 * the mouse, read its observable criteria), pick it into the basket.
 *
 * Added to the player's pawn at play by the Mushroom Spawner; creates its own input actions
 * (keys in Project Settings > ShroomHunt > Foraging) and its HUD widget.
 */
UCLASS(ClassGroup = (ShroomHunt), meta = (BlueprintSpawnableComponent))
class SHROOMHUNT_API UForagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UForagerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool IsExamining() const { return bExamining; }

private:
	void SetupInput();
	void OnExamine();
	void OnPick();
	void OnTurn(const FInputActionValue& Value);
	void BeginExamine(uint32 Seed);
	void EndExamine();
	void RefreshBasket();
	bool GetView(FVector& OutOrigin, FVector& OutDirection) const;
	AMushroomSpawner* GetSpawner();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Mapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ExamineAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PickAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TurnAction;

	UPROPERTY(Transient)
	TObjectPtr<UForagingWidget> Widget;

	/** The examined specimen, held in front of the camera (its own unique mesh). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Specimen;

	TWeakObjectPtr<AMushroomSpawner> Spawner;
	uint32 TargetSeed = 0;
	bool bHasTarget = false;
	bool bExamining = false;
	uint32 ExaminedSeed = 0;
	float ExaminedHeightCm = 10.0f;
	FRotator SpecimenRotation = FRotator::ZeroRotator;
};
