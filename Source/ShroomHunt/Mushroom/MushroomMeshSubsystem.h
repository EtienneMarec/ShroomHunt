#pragma once

#include "CoreMinimal.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Subsystems/EngineSubsystem.h"
#include "MushroomMeshSubsystem.generated.h"

class UMaterialInterface;
class UStaticMesh;
struct FMushroomMorphology;

/**
 * Creates mushroom UStaticMeshes at runtime and caches the per-species variants.
 *
 * Engine subsystem so it works both in the editor (specimen garden preview) and in game.
 * World mushrooms use cached variants (few meshes, instanced rendering); the specimen the
 * player inspects gets a unique mesh built from its exact morphology.
 */
UCLASS()
class SHROOMHUNT_API UMushroomMeshSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	/** Cached variant mesh (built on first request). */
	UStaticMesh* GetVariantMesh(const FMushroomSpeciesRow& Species, FName SpeciesId, EMushroomStage Stage, int32 Variant);

	/** Seed used for a cached variant (stable across sessions). */
	static uint32 GetVariantSeed(FName SpeciesId, EMushroomStage Stage, int32 Variant);

	/** Builds a new, uncached mesh for one exact individual (near + far LOD). */
	UStaticMesh* CreateMeshForMorphology(const FMushroomMorphology& Morphology, FName MeshName);

	/** Drops all cached meshes (after a data reload). */
	void ClearCache();

	int32 GetCachedMeshCount() const { return Cache.Num(); }

	/** The mushroom material, or the vertex-color fallback when it does not exist yet. */
	UMaterialInterface* GetMaterial();

private:
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UStaticMesh>> Cache;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CachedMaterial;
};
