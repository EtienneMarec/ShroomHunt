#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ForestWater.generated.h"

class FForestField;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

namespace ForestWaterMesh
{
	/**
	 * Water surface of a forest in field units (meters): one quad per 4 m cell whose four
	 * corners carry a water level (ponds flat at their spill level, streams sloping just above
	 * their bed). Edges extend under the banks; the landscape hides them.
	 */
	SHROOMHUNT_API void Build(const FForestField& Field, UE::Geometry::FDynamicMesh3& OutMesh);
}

/**
 * Ponds and streams of a forest. Drop one in the forest map: it aligns itself on the
 * level's Landscape and rebuilds when a property changes.
 */
UCLASS(meta = (DisplayName = "Forest Water"))
class SHROOMHUNT_API AForestWater : public AActor
{
	GENERATED_BODY()

public:
	AForestWater();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Rebuilds the surface (after editing the forest JSON or moving the landscape). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ShroomHunt|Forest")
	void Rebuild();

protected:
	/** Data/Forests/<ForestName>.json */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Forest")
	FString ForestName = TEXT("F01_LowlandOakBeech");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Forest")
	TSoftObjectPtr<UMaterialInterface> WaterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Environment/Water/M_Water.M_Water")));

	UPROPERTY(VisibleAnywhere, Category = "ShroomHunt|Forest")
	TObjectPtr<UStaticMeshComponent> Surface;

private:
	void Build();
};
