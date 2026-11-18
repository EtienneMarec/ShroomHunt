#include "Vegetation/VegetationMeshLibrary.h"

#include "Core/MeshAccumulator.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Vegetation/VegetationDefinition.h"
#include "Vegetation/VegetationMeshBuilder.h"
#include "Vegetation/VegetationScatter.h"
#include "Vegetation/VegetationSettings.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace VegetationMeshLibraryDetail
{
	const FName MaterialSlot(TEXT("Vegetation"));

#if WITH_EDITOR
	/** Builds one palette mesh and saves it as a Nanite static mesh asset (created or updated in place). */
	bool BakeOne(const FVegetationDefinition& Definition, const FVegetationMeshSpec& Spec, const FString& PackageName, UMaterialInterface* Material, int32& OutTriangles, FString& OutError)
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		FVegetationCollision Collision;
		if (!VegetationMeshBuilder::Build(Definition, Spec, Mesh, &Collision))
		{
			OutError = TEXT("mesh build failed");
			return false;
		}
		OutTriangles = Mesh.TriangleCount();
		FMeshDescription Description;
		ShroomMesh::ToMeshDescription(Mesh, MaterialSlot, Description);

		const FString AssetName = FPackageName::GetShortName(PackageName);
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		const bool bCreated = StaticMesh == nullptr;
		UPackage* Package = bCreated ? CreatePackage(*PackageName) : StaticMesh->GetPackage();
		if (bCreated)
		{
			StaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}

		StaticMesh->PreEditChange(nullptr);
		StaticMesh->SetNumSourceModels(1);
		FStaticMeshSourceModel& Source = StaticMesh->GetSourceModel(0);
		Source.BuildSettings.bRecomputeNormals = false;
		Source.BuildSettings.bRecomputeTangents = false;
		Source.BuildSettings.bGenerateLightmapUVs = false;
		Source.BuildSettings.bRemoveDegenerates = true;
		StaticMesh->CreateMeshDescription(0, MoveTemp(Description));
		StaticMesh->CommitMeshDescription(0);

		StaticMesh->GetStaticMaterials().Reset();
		StaticMesh->GetStaticMaterials().Add(FStaticMaterial(Material, MaterialSlot, MaterialSlot));

		FMeshNaniteSettings Nanite = StaticMesh->GetNaniteSettings();
		Nanite.bEnabled = true;
		Nanite.FallbackPercentTriangles = 0.3f; // fallback mesh: complex collision, ray tracing
		StaticMesh->SetNaniteSettings(Nanite);

		// Simple collision: one capsule around the trunk / log / stump, nothing for soft plants.
		StaticMesh->CreateBodySetup();
		UBodySetup* Body = StaticMesh->GetBodySetup();
		Body->RemoveSimpleCollision();
		if (Collision.bHasCapsule)
		{
			FKSphylElem Capsule(Collision.Radius, Collision.Length);
			Capsule.Center = Collision.Center;
			Capsule.Rotation = Collision.Rotation;
			Body->AggGeom.SphylElems.Add(Capsule);
			Body->CollisionTraceFlag = CTF_UseDefault;
		}
		else
		{
			Body->CollisionTraceFlag = CTF_UseSimpleAsComplex; // no simple shape: no collision at all
		}
		Body->InvalidatePhysicsData();

		StaticMesh->Build(true);
		StaticMesh->PostEditChange();
		StaticMesh->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(StaticMesh);
		}

		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, StaticMesh, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("cannot save %s"), *Filename);
			return false;
		}
		return true;
	}
#endif
}

FString UVegetationMeshLibrary::BakeVegetationMeshes(const FString& Folder)
{
#if WITH_EDITOR
	using namespace VegetationMeshLibraryDetail;
	TArray<FString> Errors;
	const TSharedPtr<const FVegetationDefinition> Definition = ForestFieldCache::GetVegetation(Errors);
	if (!Definition)
	{
		return TEXT("FAILED\n") + FString::Join(Errors, TEXT("\n"));
	}

	const UVegetationSettings* Settings = GetDefault<UVegetationSettings>();
	UMaterialInterface* Material = Settings->VegetationMaterial.LoadSynchronous();
	if (!Material)
	{
		Material = Settings->FallbackMaterial.LoadSynchronous();
		UE_LOG(LogShroomVegetation, Warning, TEXT("Vegetation material %s not found, using %s (re-bake once it exists)"),
			*Settings->VegetationMaterial.ToString(), *GetNameSafe(Material));
	}

	const double Start = FPlatformTime::Seconds();
	int32 Baked = 0;
	int64 Triangles = 0;
	for (const FVegetationMeshSpec& Spec : VegetationData::ListMeshes(*Definition))
	{
		FString PackageName = Settings->GetGeneratedPackageName(Spec);
		if (!Folder.IsEmpty())
		{
			FString CleanFolder = Folder;
			CleanFolder.RemoveFromEnd(TEXT("/"));
			PackageName = CleanFolder / FPackageName::GetShortName(PackageName);
		}
		int32 MeshTriangles = 0;
		FString Error;
		if (BakeOne(*Definition, Spec, PackageName, Material, MeshTriangles, Error))
		{
			++Baked;
			Triangles += MeshTriangles;
			UE_LOG(LogShroomVegetation, Log, TEXT("Baked %s (%d triangles)"), *PackageName, MeshTriangles);
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("%s: %s"), *PackageName, *Error));
		}
	}
	const FString Summary = FString::Printf(TEXT("%d meshes baked (%lld triangles, material %s) in %.1f s"),
		Baked, Triangles, *GetNameSafe(Material), FPlatformTime::Seconds() - Start);
	if (Errors.Num() > 0)
	{
		return TEXT("FAILED: ") + Summary + TEXT("\n") + FString::Join(Errors, TEXT("\n"));
	}
	return TEXT("OK: ") + Summary;
#else
	return TEXT("FAILED: baking meshes needs the editor");
#endif
}

FString UVegetationMeshLibrary::ScatterReport(const FString& ForestName)
{
	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(ForestName, Errors);
	const TSharedPtr<const FVegetationDefinition> Definition = ForestFieldCache::GetVegetation(Errors);
	if (!Field || !Definition)
	{
		return TEXT("FAILED\n") + FString::Join(Errors, TEXT("\n"));
	}

	const double Start = FPlatformTime::Seconds();
	const double SizeM = Field->GetSizeM();
	const FBox2d Bounds(FVector2d::ZeroVector, FVector2d(SizeM, SizeM));
	TArray<FVegetationInstance> Instances;
	VegetationScatter::Scatter(*Field, *Definition, EVegetationLayer::Trees, Bounds, Instances);
	const int32 TreeCount = Instances.Num();
	VegetationScatter::Scatter(*Field, *Definition, EVegetationLayer::Understory, Bounds, Instances);
	const double Seconds = FPlatformTime::Seconds() - Start;

	TMap<FString, int32> Counts;
	for (const FVegetationInstance& Instance : Instances)
	{
		Counts.FindOrAdd(VegetationData::KindName(Instance.Kind))++;
		if (!Instance.Genus.IsNone())
		{
			Counts.FindOrAdd(FString::Printf(TEXT("%s / %s"), VegetationData::KindName(Instance.Kind), *Instance.Genus.ToString()))++;
		}
	}
	Counts.KeySort(TLess<FString>());
	const double Hectares = SizeM * SizeM / 10000.0;
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("OK: %s, %d trees + %d understory instances on %.0f ha in %.2f s"),
		*ForestName, TreeCount, Instances.Num() - TreeCount, Hectares, Seconds));
	for (const TPair<FString, int32>& Pair : Counts)
	{
		Lines.Add(FString::Printf(TEXT("%-24s %7d  (%.1f / ha)"), *Pair.Key, Pair.Value, Pair.Value / Hectares));
	}
	return FString::Join(Lines, TEXT("\n"));
}
