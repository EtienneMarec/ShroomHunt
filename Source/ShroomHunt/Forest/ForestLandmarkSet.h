#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ForestLandmarkSet.generated.h"

class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;
class USceneComponent;
class UStaticMeshComponent;

/**
 * All landmarks of a forest (house, hut, hunting towers, calvary, signposts with place names
 * and arrows, parcel stones, bridges, log piles), placed by the forest field and built as
 * placeholder meshes, with the knowledge sources (poster, torn page, Data/Knowledge/Sources.json)
 * hanging on them. Drop one in the forest map: it aligns itself on the level's Landscape.
 */
UCLASS(meta = (DisplayName = "Forest Landmarks"))
class SHROOMHUNT_API AForestLandmarkSet : public AActor
{
	GENERATED_BODY()

public:
	AForestLandmarkSet();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Readable sheets (poster, guide page) carry this component tag followed by their source id. */
	static const FName ReadableTag;

	/** The knowledge source a component shows, or NAME_None. */
	static FName GetReadableSource(const UPrimitiveComponent* Component);

	/** Rebuilds everything (after editing the forest or name JSON files). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "ShroomHunt|Forest")
	void Rebuild();

protected:
	/** Data/Forests/<ForestName>.json */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Forest")
	FString ForestName = TEXT("F01_LowlandOakBeech");

	/** Vertex color material (the vegetation one fits). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Forest")
	TSoftObjectPtr<UMaterialInterface> Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/ShroomHunt/Environment/Vegetation/M_Vegetation.M_Vegetation")));

	/** Place names on signposts, numbers on parcel stones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShroomHunt|Forest")
	bool bShowTexts = true;

	UPROPERTY(VisibleAnywhere, Category = "ShroomHunt|Forest")
	TObjectPtr<USceneComponent> Root;

private:
	void Build();
	UStaticMesh* GetMesh(const FString& Key, TFunctionRef<UStaticMesh*()> Create);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FTransform& WorldTransform, bool bCollision);
	void AddText(USceneComponent* Parent, const FString& Text, const FVector& RelativeLocation, float RelativeYaw, float WorldSize, FColor Color = FColor(45, 28, 22));
	/** Hides the pages already taken by the player. */
	void RefreshReadables();

	FDelegateHandle KnowledgeChangedHandle;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UStaticMesh>> Meshes;
};
