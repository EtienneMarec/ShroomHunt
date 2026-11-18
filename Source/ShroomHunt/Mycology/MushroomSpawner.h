#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Mycology/SpawnModel.h"
#include "MushroomSpawner.generated.h"

class FForestField;
class UInstancedStaticMeshComponent;
class USceneComponent;

/**
 * Makes the mushrooms of a forest grow (Docs/SpawnModel.md). Drop one in a forest map.
 *
 * At play it builds the mycelium patches of every species from the forest field, then keeps
 * the live fruit bodies up to date as game time passes (re-evaluated every game hour and
 * after a time skip): the set is a pure function of the weather history, so nothing is saved.
 * Fruit bodies are drawn with one instanced mesh component per species x stage x variant.
 */
UCLASS(meta = (DisplayName = "Mushroom Spawner"))
class SHROOMHUNT_API AMushroomSpawner : public AActor
{
	GENERATED_BODY()

public:
	AMushroomSpawner();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Recomputes the fruit bodies now (forgets cached weather triggers). */
	UFUNCTION(BlueprintCallable, Category = "ShroomHunt|Spawn")
	void Refresh();

	const TArray<FMyceliumPatch>& GetPatches() const { return Patches; }
	const TArray<FFruitBody>& GetFruitBodies() const { return FruitBodies; }
	const FMushroomSpeciesRow* FindSpecies(FName SpeciesId) const { return Species.Find(SpeciesId); }
	TArray<FName> GetSpeciesIds() const;
	const FForestField* GetField() const { return Field.Get(); }

	/** Today's mean trigger F_s over the species' lag range (debug). */
	float GetTodayTrigger(FName SpeciesId);

	/** World location of a fruit body (on the ground). */
	FVector GetWorldLocation(const FFruitBody& Fruit) const;

	/** The spawner of a world, if any. */
	static AMushroomSpawner* Find(const UWorld* World);

protected:
	/** Data/Forests/<ForestName>.json */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Spawn")
	FString ForestName = TEXT("F01_LowlandOakBeech");

	/** Mushrooms are not drawn beyond this distance (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Spawn", meta = (ClampMin = "10"))
	float CullDistanceM = 90.0f;

	UPROPERTY(VisibleAnywhere, Category = "ShroomHunt|Spawn")
	TObjectPtr<USceneComponent> Root;

private:
	bool Initialize();
	FSpawnContext MakeContext();
	void Recompute(double NowGameSeconds);
	void RebuildInstances(double NowGameSeconds);

	TSharedPtr<const FForestField> Field;
	TMap<FName, FMushroomSpeciesRow> Species;
	TArray<FMyceliumPatch> Patches;
	TArray<FFruitBody> FruitBodies;
	TMap<uint64, float> TriggerCache;
	FTransform FieldToWorld;
	int64 LastHourIndex = MIN_int64;
	int32 LastOverrideCount = -1;
	bool bInitialized = false;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Instancers;
};
