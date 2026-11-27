#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Mushroom/MushroomMorphology.h"
#include "Mycology/SpawnModel.h"
#include "MushroomSpawner.generated.h"

class FForestField;
class FVegetationObstacleGrid;
class UMaterialParameterCollection;
class UStaticMeshComponent;
class USceneComponent;

/**
 * Makes the mushrooms of a forest grow (Docs/SpawnModel.md). Drop one in a forest map.
 *
 * At play it builds the mycelium patches of every species from the forest field, then keeps
 * the live fruit bodies up to date as game time passes (re-evaluated every game hour and
 * after a time skip): the set is a pure function of the weather history, so nothing is saved.
 * Each fruit body is drawn by its own static mesh component (a reused pool): the meshes are
 * shared per species x stage x variant, and a mushroom will be a target for picking (M1g).
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
	/** Solid vegetation (trunks, logs, stumps) of the whole forest. */
	TSharedPtr<const FVegetationObstacleGrid> GetObstacles() const { return Obstacles; }
	/** Low leafy cover (bracken, brambles, shrubs) of the whole forest: what hides mushrooms from a low camera. */
	TSharedPtr<const FVegetationObstacleGrid> GetCover() const { return Cover; }

	/** Snow hollows written to MPC_Season (SnowHollow0..N-1); must match Tools/Python/add_snow_hollows.py. */
	static constexpr int32 SnowHollowCount = 8;

	/** Today's mean trigger F_s over the species' lag range (debug). */
	float GetTodayTrigger(FName SpeciesId);

	/** World location of a fruit body (on the ground). */
	FVector GetWorldLocation(const FFruitBody& Fruit) const;

	/**
	 * The standing mushroom hit by a ray (camera), within MaxDistanceCm, or INDEX_NONE.
	 * Each mushroom is a sphere around its cap and stem.
	 */
	int32 FindTarget(const FVector& Origin, const FVector& Direction, float MaxDistanceCm) const;

	/** Index of the standing fruit body with this seed, or INDEX_NONE (indices change at each update). */
	int32 FindBySeed(uint32 Seed) const;

	/** Exact shape of a standing mushroom now (the drawn individual at its exact age). */
	FMushroomMorphology GetMorphology(const FFruitBody& Fruit) const;

	/** Picks a mushroom: into the basket, gone from the forest, its patch exhausted for a while. */
	bool PickFruit(uint32 Seed);

	double GetNowGameSeconds() const;

	/** The spawner of a world, if any. */
	static AMushroomSpawner* Find(const UWorld* World);

protected:
	/** Data/Forests/<ForestName>.json */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Spawn")
	FString ForestName = TEXT("F01_LowlandOakBeech");

	/** Mushrooms are not drawn beyond this distance (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Spawn", meta = (ClampMin = "10"))
	float CullDistanceM = 90.0f;

	/**
	 * The collection the landscape reads the snow hollows from: the mushrooms nearest to the camera
	 * (world position, radius in cm) push the snow layer down around them (add_snow_hollows.py).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Spawn")
	TSoftObjectPtr<UMaterialParameterCollection> SeasonParameters = TSoftObjectPtr<UMaterialParameterCollection>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Environment/MPC_Season.MPC_Season")));

	/** Only mushrooms this close to the camera get a snow hollow (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Spawn", meta = (ClampMin = "1"))
	float SnowHollowDistanceM = 25.0f;

	UPROPERTY(VisibleAnywhere, Category = "ShroomHunt|Spawn")
	TObjectPtr<USceneComponent> Root;

private:
	bool Initialize();
	FSpawnContext MakeContext();
	void Recompute(double NowGameSeconds);
	void RebuildInstances(double NowGameSeconds);
	void UpdateSnowHollows();

	TSharedPtr<const FForestField> Field;
	/** Tree trunks, logs and stumps: no mushroom grows inside them. */
	TSharedPtr<FVegetationObstacleGrid> Obstacles;
	TSharedPtr<FVegetationObstacleGrid> Cover;
	/** Standing mushrooms: world position (cm) and footprint radius (cm, W), for the snow hollows. */
	TArray<FVector4> HollowSources;
	TArray<FVector4> WrittenHollows;
	bool bWarnedHollows = false;
	TMap<FName, FMushroomSpeciesRow> Species;
	TArray<FMyceliumPatch> Patches;
	TArray<FFruitBody> FruitBodies;
	TMap<uint64, float> TriggerCache;
	FTransform FieldToWorld;
	int64 LastHourIndex = MIN_int64;
	int32 LastOverrideCount = -1;
	bool bInitialized = false;

	/** One component per standing mushroom; extra ones are hidden and reused later. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MushroomComponents;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> SeasonCollection;
};
