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
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace VegetationMeshLibraryDetail
{
	const FName MaterialSlot(TEXT("Vegetation"));

#if WITH_EDITOR
	FString GetBakeStampPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("VegetationBake.txt");
	}

	/** What the baked meshes depend on: the vegetation data, the builder version, the material and the folder. */
	FString ComputeBakeStamp()
	{
		FString Json;
		FFileHelper::LoadFileToString(Json, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Vegetation"), TEXT("Vegetation.json")));
		Json.ReplaceInline(TEXT("\r"), TEXT("")); // same stamp whatever the line endings of the checkout
		const UVegetationSettings* Settings = GetDefault<UVegetationSettings>();
		return FString::Printf(TEXT("builder %d, data %08x, material %s, folder %s"), VegetationMeshBuilder::BuilderVersion,
			FCrc::StrCrc32(*Json), *Settings->VegetationMaterial.ToString(), *Settings->GeneratedMeshFolder);
	}

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
	const TArray<FVegetationMeshSpec> Specs = VegetationData::ListMeshes(*Definition);
	FScopedSlowTask Progress(static_cast<float>(Specs.Num()), NSLOCTEXT("ShroomHunt", "BakeVegetation", "Building the vegetation meshes"));
	Progress.MakeDialog();
	for (const FVegetationMeshSpec& Spec : Specs)
	{
		Progress.EnterProgressFrame(1.0f, FText::FromString(Spec.GetKey()));
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
	if (Folder.IsEmpty())
	{
		FFileHelper::SaveStringToFile(ComputeBakeStamp(), *GetBakeStampPath());
	}
	return TEXT("OK: ") + Summary;
#else
	return TEXT("FAILED: baking meshes needs the editor");
#endif
}

bool UVegetationMeshLibrary::IsBakeUpToDate(FString& OutReason)
{
#if WITH_EDITOR
	using namespace VegetationMeshLibraryDetail;
	TArray<FString> Errors;
	const TSharedPtr<const FVegetationDefinition> Definition = ForestFieldCache::GetVegetation(Errors);
	if (!Definition)
	{
		// Nothing sensible to bake: the data errors are reported where the data is used.
		OutReason = TEXT("vegetation data invalid: ") + FString::Join(Errors, TEXT("; "));
		return true;
	}
	const UVegetationSettings* Settings = GetDefault<UVegetationSettings>();
	for (const FVegetationMeshSpec& Spec : VegetationData::ListMeshes(*Definition))
	{
		const FString PackageName = Settings->GetGeneratedPackageName(Spec);
		if (!FPackageName::DoesPackageExist(PackageName))
		{
			OutReason = PackageName + TEXT(" is missing");
			return false;
		}
	}
	FString Stamp;
	if (!FFileHelper::LoadFileToString(Stamp, *GetBakeStampPath()) || Stamp != ComputeBakeStamp())
	{
		OutReason = TEXT("the vegetation data or the builder changed since the last bake");
		return false;
	}
	return true;
#else
	return true;
#endif
}

void UVegetationMeshLibrary::BakeIfNeeded()
{
	FString Reason;
	if (IsBakeUpToDate(Reason))
	{
		return;
	}
	UE_LOG(LogShroomVegetation, Display, TEXT("Rebuilding the vegetation meshes: %s"), *Reason);
	UE_LOG(LogShroomVegetation, Display, TEXT("%s"), *BakeVegetationMeshes(FString()).Left(400));
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
	const int32 UnderstoryCount = Instances.Num() - TreeCount;
	// Ground debris (millions of leaves): counted on a 1 ha sample in the middle, scaled up.
	const FBox2d Sample(FVector2d(0.5 * SizeM - 50.0), FVector2d(0.5 * SizeM + 50.0));
	TArray<FVegetationInstance> Ground;
	VegetationScatter::Scatter(*Field, *Definition, EVegetationLayer::Ground, Sample, Ground);
	const double GroundScale = (SizeM * SizeM) / Sample.GetArea();
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
	for (const FVegetationInstance& Instance : Ground)
	{
		Counts.FindOrAdd(FString(TEXT("~")) + VegetationData::KindName(Instance.Kind)) += FMath::RoundToInt(GroundScale);
	}
	Lines.Add(FString::Printf(TEXT("OK: %s, %d trees + %d understory instances on %.0f ha, ~%.0f ground debris (estimated from 1 ha, generated around the player) in %.2f s"),
		*ForestName, TreeCount, UnderstoryCount, Hectares, Ground.Num() * GroundScale, Seconds));
	for (const TPair<FString, int32>& Pair : Counts)
	{
		Lines.Add(FString::Printf(TEXT("%-24s %7d  (%.1f / ha)"), *Pair.Key, Pair.Value, Pair.Value / Hectares));
	}
	return FString::Join(Lines, TEXT("\n"));
}
