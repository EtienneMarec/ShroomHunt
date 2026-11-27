#pragma once

#include "CoreMinimal.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "Vegetation/VegetationDefinition.h"
#include "PCGForestScatter.generated.h"

/**
 * PCG node "Forest Scatter": outputs the trees or the understory of a forest as points.
 *
 * The points come from VegetationScatter (deterministic, driven by the forest field: stands,
 * age classes, moisture, pH, canopy, water), restricted to the bounds of the PCG component or
 * partition that runs the graph. Each point carries the attributes:
 *   Mesh  (soft object path) - feed a Static Mesh Spawner with Mesh Selector "By Attribute";
 *   Kind  (string)           - Pole, Mature, Old, Sapling, Bracken, Bramble, Log, Stump;
 *   Genus (string)           - Fagus, Quercus... for trees and saplings.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class SHROOMHUNT_API UPCGForestScatterSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ForestScatter")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return TArray<FPCGPinProperties>(); }
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override { return Super::DefaultPointOutputPinProperties(); }
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

public:
	/** Data/Forests/<ForestName>.json */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString ForestName = TEXT("F01_LowlandOakBeech");

	/** Trees (with collision, never culled) or understory (no collision, culled at distance). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EVegetationLayer Layer = EVegetationLayer::Trees;

	/**
	 * Place the forest on the level's Landscape: field point (x m, y m, height h m) goes to the
	 * landscape's local position (x, y, h), i.e. vertex x, y (1 m per vertex) and the height the
	 * imported heightmap encodes. Follows the landscape's location, rotation and scale.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bAlignToLandscape = true;

	/**
	 * Used only when bAlignToLandscape is off: world location (cm) of the field's corner (0, 0, height 0).
	 * With bAlignToLandscape and no Landscape found, the node outputs nothing (a silent fallback once
	 * placed a whole generation 504 m away from the landscape: floating trees).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector ForestOrigin = FVector::ZeroVector;

	/** Name of the soft object path attribute read by the mesh spawner. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName MeshAttribute = TEXT("Mesh");
};

class FPCGForestScatterElement : public IPCGElement
{
public:
	/** The output also depends on Data/ JSON files, which PCG cannot track: never reuse cached results. */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

	/** Finding the Landscape iterates the world's actors: game thread only (the scatter itself takes ~10 ms). */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
