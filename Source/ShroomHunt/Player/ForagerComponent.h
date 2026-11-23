#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ForagerComponent.generated.h"

class AMushroomSpawner;
class UCounterWidget;
class UEncyclopediaWidget;
class UForagingWidget;
class UInputAction;
class UInputMappingContext;
class UKnowledgeSubsystem;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * Lets the player forage: aim at a mushroom within reach, examine it up close (turn it with
 * the mouse, read its observable criteria), pick it into the basket; read the knowledge
 * sources hanging on landmarks (poster, torn page); open the encyclopedia.
 *
 * Added to the player's pawn at play by the Mushroom Spawner; creates its own input actions
 * (keys in Project Settings > ShroomHunt > Foraging) and its HUD widgets.
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
	enum class ETarget : uint8 { None, Mushroom, Readable, Counter };

	void SetupInput();
	void OnExamine();
	void OnPick();
	void OnTurn(const FInputActionValue& Value);
	void OnEncyclopedia();
	void OnScroll(const FInputActionValue& Value);
	void BeginExamine(uint32 Seed);
	void EndExamine();
	void BeginReading(FName SourceId);
	void EndReading();
	void OpenEncyclopedia();
	void CloseEncyclopedia();
	void OpenCounter();
	void CloseCounter();
	void SetPlayerFrozen(bool bFrozen);
	void RefreshBasket();
	bool GetView(FVector& OutOrigin, FVector& OutDirection) const;
	/** The knowledge source sheet in sight within reach (not hidden by walls), or NAME_None. */
	FName FindReadable(const FVector& Origin, const FVector& Direction, float MaxDistanceCm, float& OutDistanceCm) const;
	AMushroomSpawner* GetSpawner();
	UKnowledgeSubsystem* GetKnowledge() const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Mapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ExamineAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PickAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TurnAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EncyclopediaAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ScrollAction;

	UPROPERTY(Transient)
	TObjectPtr<UForagingWidget> Widget;

	UPROPERTY(Transient)
	TObjectPtr<UEncyclopediaWidget> Encyclopedia;

	/** The buyer's counter (sell, shop), M3. */
	UPROPERTY(Transient)
	TObjectPtr<UCounterWidget> Counter;

	/** The examined specimen, held in front of the camera (its own unique mesh). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Specimen;

	TWeakObjectPtr<AMushroomSpawner> Spawner;
	ETarget Target = ETarget::None;
	uint32 TargetSeed = 0;
	FName TargetSource;
	bool bExamining = false;
	bool bReading = false;
	bool bEncyclopediaOpen = false;
	bool bCounterOpen = false;
	uint32 ExaminedSeed = 0;
	float ExaminedHeightCm = 10.0f;
	FRotator SpecimenRotation = FRotator::ZeroRotator;
};
