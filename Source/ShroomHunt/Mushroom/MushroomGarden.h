#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MushroomGarden.generated.h"

/**
 * Debug "specimen garden": lays out generated mushrooms in a grid
 * (one row per species, columns Young / Mature / Old x variants) to review the
 * procedural shapes. Works in the editor viewport (rebuilt when a property changes)
 * and in game. Drop it in any level; it is not meant for the final forests.
 */
UCLASS(meta = (DisplayName = "Mushroom Specimen Garden"))
class SHROOMHUNT_API AMushroomGarden : public AActor
{
	GENERATED_BODY()

public:
	AMushroomGarden();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Rebuilds the meshes (e.g. after re-importing species data). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ShroomHunt|Garden")
	void Regenerate();

protected:
	/** Species to show; empty = all species. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Garden")
	TArray<FName> Species;

	/** Individuals per stage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Garden", meta = (ClampMin = "1", ClampMax = "16"))
	int32 IndividualsPerStage = 4;

	/**
	 * Off: each cell uses the shared cached variants (what the forest will use).
	 * On: each cell is a unique individual from SeedOffset + cell index (inspection meshes).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Garden")
	bool bUniqueIndividuals = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Garden", meta = (EditCondition = "bUniqueIndividuals"))
	int32 SeedOffset = 0;

	/** Distance between individuals (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Garden", meta = (ClampMin = "5"))
	float Spacing = 35.0f;

	/** Show species and stage labels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Garden")
	bool bShowLabels = true;

	UPROPERTY(VisibleAnywhere, Category = "ShroomHunt|Garden")
	TObjectPtr<USceneComponent> Root;

private:
	void Build();
	void AddLabel(const FText& Text, const FVector& RelativeLocation, float Size);
};
