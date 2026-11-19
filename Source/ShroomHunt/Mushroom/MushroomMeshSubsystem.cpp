#include "Mushroom/MushroomMeshSubsystem.h"

#include "Core/MeshAccumulator.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Mushroom/MushroomMeshBuilder.h"
#include "Mushroom/MushroomMeshSettings.h"
#include "Mushroom/MushroomMorphology.h"
#include "UObject/Package.h"

namespace MushroomMeshSubsystemDetail
{
	const FName MaterialSlot(TEXT("Mushroom"));
}

uint32 UMushroomMeshSubsystem::GetVariantSeed(FName SpeciesId, int32 Variant)
{
	return ShroomHash::Hash(ShroomHash::HashString(SpeciesId.ToString()), 0x5EEDu, static_cast<uint32>(Variant));
}

int32 UMushroomMeshSubsystem::GetAgeStep(float Age)
{
	const int32 Steps = FMath::Max(2, GetDefault<UMushroomMeshSettings>()->AgeSteps);
	return FMath::Clamp(FMath::FloorToInt(Age * Steps), 0, Steps - 1);
}

float UMushroomMeshSubsystem::GetStepAge(int32 AgeStep)
{
	const int32 Steps = FMath::Max(2, GetDefault<UMushroomMeshSettings>()->AgeSteps);
	return (FMath::Clamp(AgeStep, 0, Steps - 1) + 0.5f) / Steps;
}

UStaticMesh* UMushroomMeshSubsystem::GetVariantMesh(const FMushroomSpeciesRow& Species, FName SpeciesId, EMushroomStage Stage, int32 Variant)
{
	const FName Key(*FString::Printf(TEXT("SM_%s_v%d_stage%d"), *SpeciesId.ToString(), Variant, static_cast<int32>(Stage)));
	if (const TObjectPtr<UStaticMesh>* Found = Cache.Find(Key))
	{
		return *Found;
	}
	UStaticMesh* Mesh = CreateMeshForMorphology(MushroomMorphology::Sample(Species, SpeciesId, GetVariantSeed(SpeciesId, Variant), Stage), Key);
	Cache.Add(Key, Mesh);
	return Mesh;
}

UStaticMesh* UMushroomMeshSubsystem::GetGrowthMesh(const FMushroomSpeciesRow& Species, FName SpeciesId, int32 Variant, int32 AgeStep)
{
	const FName Key(*FString::Printf(TEXT("SM_%s_v%d_age%d"), *SpeciesId.ToString(), Variant, AgeStep));
	if (const TObjectPtr<UStaticMesh>* Found = Cache.Find(Key))
	{
		return *Found;
	}
	UStaticMesh* Mesh = CreateMeshForMorphology(MushroomMorphology::SampleAtAge(Species, SpeciesId, GetVariantSeed(SpeciesId, Variant), GetStepAge(AgeStep)), Key);
	Cache.Add(Key, Mesh);
	return Mesh;
}

UStaticMesh* UMushroomMeshSubsystem::CreateMeshForMorphology(const FMushroomMorphology& Morphology, FName MeshName)
{
	using namespace MushroomMeshSubsystemDetail;
	const double StartTime = FPlatformTime::Seconds();

	UE::Geometry::FDynamicMesh3 Near;
	UE::Geometry::FDynamicMesh3 Far;
	MushroomMeshBuilder::Build(Morphology, FMushroomMeshDetail::Near(), Near);
	MushroomMeshBuilder::Build(Morphology, FMushroomMeshDetail::Far(), Far);

	TArray<FMeshDescription> Descriptions;
	Descriptions.SetNum(2);
	ShroomMesh::ToMeshDescription(Near, MaterialSlot, Descriptions[0]);
	ShroomMesh::ToMeshDescription(Far, MaterialSlot, Descriptions[1]);

	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), MeshName), RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(GetMaterial(), MaterialSlot, MaterialSlot));

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;
	Params.bBuildSimpleCollision = true; // box collision for interaction traces
	Params.bCommitMeshDescription = false;
	Params.bMarkPackageDirty = false;
	Params.bAllowCpuAccess = false;
	TArray<const FMeshDescription*> DescriptionPointers = { &Descriptions[0], &Descriptions[1] };
	if (!Mesh->BuildFromMeshDescriptions(DescriptionPointers, Params))
	{
		UE_LOG(LogShroomMesh, Error, TEXT("Failed to build mushroom mesh %s"), *MeshName.ToString());
		return nullptr;
	}
	if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 1)
	{
		Mesh->GetRenderData()->ScreenSize[1].Default = GetDefault<UMushroomMeshSettings>()->FarLodScreenSize;
	}

	UE_LOG(LogShroomMesh, Verbose, TEXT("Built %s: %d / %d triangles in %.2f ms"), *MeshName.ToString(),
		Near.TriangleCount(), Far.TriangleCount(), (FPlatformTime::Seconds() - StartTime) * 1000.0);
	return Mesh;
}

void UMushroomMeshSubsystem::ClearCache()
{
	UE_LOG(LogShroomMesh, Log, TEXT("Clearing %d cached mushroom meshes"), Cache.Num());
	Cache.Reset();
	CachedMaterial = nullptr;
}

UMaterialInterface* UMushroomMeshSubsystem::GetMaterial()
{
	if (!CachedMaterial)
	{
		const UMushroomMeshSettings* Settings = GetDefault<UMushroomMeshSettings>();
		CachedMaterial = Settings->MushroomMaterial.LoadSynchronous();
		if (!CachedMaterial)
		{
			CachedMaterial = Settings->FallbackMaterial.LoadSynchronous();
			UE_LOG(LogShroomMesh, Log, TEXT("Mushroom material not found, using fallback %s"), *GetNameSafe(CachedMaterial));
		}
	}
	return CachedMaterial;
}
