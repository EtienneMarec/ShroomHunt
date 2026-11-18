// Mushroom mesh debug console commands: sh.Mesh.Stats / sh.Mesh.ClearCache.

#include "Core/MeshAccumulator.h"
#include "Core/ShroomLog.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Mushroom/MushroomMeshBuilder.h"
#include "Mushroom/MushroomMeshSubsystem.h"
#include "Mushroom/MushroomMorphology.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace MeshConsoleCommandsDetail
{
	UMushroomMeshSubsystem* FindMeshes()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UMushroomMeshSubsystem>() : nullptr;
	}

	FAutoConsoleCommand CmdMeshStats(
		TEXT("sh.Mesh.Stats"),
		TEXT("sh.Mesh.Stats: number of cached procedural mushroom meshes."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (const UMushroomMeshSubsystem* Meshes = FindMeshes())
			{
				UE_LOG(LogShroomDebug, Display, TEXT("%d cached mushroom meshes, material %s"), Meshes->GetCachedMeshCount(),
					*GetNameSafe(const_cast<UMushroomMeshSubsystem*>(Meshes)->GetMaterial()));
			}
		}));

	/** Writes one individual as an OBJ with per-vertex sRGB colors. */
	bool ExportObj(const FMushroomMorphology& Morphology, const FMushroomMeshDetail& Detail, const FString& Path)
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		MushroomMeshBuilder::Build(Morphology, Detail, Mesh);
		return ShroomMesh::WriteObj(Mesh, Path, FString::Printf(TEXT("%s stage %d seed %u - ShroomHunt procedural mushroom (cm, Z up)"),
			*Morphology.SpeciesId.ToString(), static_cast<int32>(Morphology.Stage), Morphology.Seed));
	}

	FAutoConsoleCommand CmdMeshExportObj(
		TEXT("sh.Mesh.ExportObj"),
		TEXT("sh.Mesh.ExportObj [seed]: writes every species x stage as OBJ files to Saved/MushroomDebug (for review in a 3D viewer)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			TMap<FName, FMushroomSpeciesRow> Species;
			FString Source;
			if (!USpeciesDatabaseSubsystem::LoadSpeciesForTools(Species, Source))
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("No species data"));
				return;
			}
			const uint32 Seed = Args.Num() > 0 ? static_cast<uint32>(FCString::Atoi(*Args[0])) : 1u;
			const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MushroomDebug"));
			static const EMushroomStage Stages[3] = { EMushroomStage::Young, EMushroomStage::Mature, EMushroomStage::Old };
			int32 Written = 0;
			for (const TPair<FName, FMushroomSpeciesRow>& Pair : Species)
			{
				for (const EMushroomStage Stage : Stages)
				{
					const FMushroomMorphology M = MushroomMorphology::Sample(Pair.Value, Pair.Key, Seed, Stage);
					const FString Name = FString::Printf(TEXT("%s_%d"), *Pair.Key.ToString(), static_cast<int32>(Stage));
					Written += ExportObj(M, FMushroomMeshDetail::Near(), Folder / (Name + TEXT(".obj"))) ? 1 : 0;
					Written += ExportObj(M, FMushroomMeshDetail::Far(), Folder / (Name + TEXT("_far.obj"))) ? 1 : 0;
				}
			}
			UE_LOG(LogShroomDebug, Display, TEXT("Exported %d OBJ files to %s"), Written, *Folder);
		}));

	FAutoConsoleCommand CmdMeshClear(
		TEXT("sh.Mesh.ClearCache"),
		TEXT("sh.Mesh.ClearCache: drops cached mushroom meshes (they are rebuilt on demand, e.g. after sh.Species.Reload)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UMushroomMeshSubsystem* Meshes = FindMeshes())
			{
				Meshes->ClearCache();
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
