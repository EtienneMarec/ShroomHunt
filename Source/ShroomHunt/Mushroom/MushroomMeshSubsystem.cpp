#include "Mushroom/MushroomMeshSubsystem.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Mushroom/MushroomMeshBuilder.h"
#include "Mushroom/MushroomMeshSettings.h"
#include "Mushroom/MushroomMorphology.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

namespace MushroomMeshSubsystemDetail
{
	const FName MaterialSlot(TEXT("Mushroom"));

	void ConvertLod(const UE::Geometry::FDynamicMesh3& Mesh, FMeshDescription& OutDescription)
	{
		FStaticMeshAttributes Attributes(OutDescription);
		Attributes.Register();
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(&Mesh, OutDescription, true);

		// Runtime builds match sections to materials by slot name.
		TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		for (const FPolygonGroupID GroupId : OutDescription.PolygonGroups().GetElementIDs())
		{
			SlotNames[GroupId] = MaterialSlot;
		}
	}
}

uint32 UMushroomMeshSubsystem::GetVariantSeed(FName SpeciesId, EMushroomStage Stage, int32 Variant)
{
	return ShroomHash::Hash(ShroomHash::HashString(SpeciesId.ToString()), 0x5EEDu, static_cast<uint32>(Stage), static_cast<uint32>(Variant));
}

UStaticMesh* UMushroomMeshSubsystem::GetVariantMesh(const FMushroomSpeciesRow& Species, FName SpeciesId, EMushroomStage Stage, int32 Variant)
{
	const FName Key(*FString::Printf(TEXT("SM_%s_%d_%d"), *SpeciesId.ToString(), static_cast<int32>(Stage), Variant));
	if (const TObjectPtr<UStaticMesh>* Found = Cache.Find(Key))
	{
		return *Found;
	}
	const FMushroomMorphology Morphology = MushroomMorphology::Sample(Species, SpeciesId, GetVariantSeed(SpeciesId, Stage, Variant), Stage);
	UStaticMesh* Mesh = CreateMeshForMorphology(Morphology, Key);
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
	ConvertLod(Near, Descriptions[0]);
	ConvertLod(Far, Descriptions[1]);

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
