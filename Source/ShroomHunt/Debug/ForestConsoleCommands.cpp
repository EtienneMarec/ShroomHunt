// Forest debug console commands: sh.Forest.Bake, sh.Forest.ExportLandmarks.

#include "Core/MeshAccumulator.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestGenerationLibrary.h"
#include "Forest/LandmarkMeshBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

#if !UE_BUILD_SHIPPING

namespace ForestConsoleCommandsDetail
{
	FAutoConsoleCommand CmdForestBake(
		TEXT("sh.Forest.Bake"),
		TEXT("sh.Forest.Bake [ForestName = F01_LowlandOakBeech]: generates the forest field and writes heightmap, weightmaps and debug maps to Saved/ForestBake/<ForestName>."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FString Name = Args.Num() > 0 ? Args[0] : TEXT("F01_LowlandOakBeech");
			UE_LOG(LogShroomDebug, Display, TEXT("%s"), *UForestGenerationLibrary::BakeForest(Name));
		}));

	FAutoConsoleCommand CmdForestExportLandmarks(
		TEXT("sh.Forest.ExportLandmarks"),
		TEXT("sh.Forest.ExportLandmarks [ForestName = F01_LowlandOakBeech]: writes one OBJ per landmark kind to Saved/LandmarkDebug (preview with Tools/Python/render_mushroom_preview.py --grid)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			TArray<FString> Errors;
			const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(Args.Num() > 0 ? Args[0] : TEXT("F01_LowlandOakBeech"), Errors);
			if (!Field)
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("%s"), *FString::Join(Errors, TEXT("; ")));
				return;
			}
			const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("LandmarkDebug"));
			TSet<EForestLandmarkKind> Done;
			for (const FForestLandmark& Landmark : Field->GetLandmarks())
			{
				if (Done.Contains(Landmark.Kind))
				{
					continue;
				}
				Done.Add(Landmark.Kind);
				UE::Geometry::FDynamicMesh3 Mesh;
				LandmarkMeshBuilder::Build(Landmark, Mesh);
				ShroomMesh::WriteObj(Mesh, Folder / FString::Printf(TEXT("%s_0.obj"), ForestLandmarks::KindName(Landmark.Kind)), ForestLandmarks::KindName(Landmark.Kind));
			}
			UE::Geometry::FDynamicMesh3 Board;
			LandmarkMeshBuilder::BuildSignBoard(Board);
			ShroomMesh::WriteObj(Board, Folder / TEXT("SignBoard_0.obj"), TEXT("SignBoard"));
			UE_LOG(LogShroomDebug, Display, TEXT("Wrote %d landmark OBJ files to %s"), Done.Num() + 1, *Folder);
		}));
}

#endif // !UE_BUILD_SHIPPING
