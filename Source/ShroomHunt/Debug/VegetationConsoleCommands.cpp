// Vegetation debug console commands: sh.Vegetation.BakeMeshes / Stats / ExportObj.

#include "Core/MeshAccumulator.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Forest/ForestFieldCache.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Vegetation/VegetationDefinition.h"
#include "Vegetation/VegetationMeshBuilder.h"
#include "Vegetation/VegetationMeshLibrary.h"

#if !UE_BUILD_SHIPPING

namespace VegetationConsoleCommandsDetail
{
	void LogLines(const FString& Report)
	{
		TArray<FString> Lines;
		Report.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogShroomDebug, Display, TEXT("%s"), *Line);
		}
	}

	FAutoConsoleCommand CmdVegetationBake(
		TEXT("sh.Vegetation.BakeMeshes"),
		TEXT("sh.Vegetation.BakeMeshes [ContentFolder]: builds the placeholder trees and understory of Data/Vegetation/Vegetation.json and saves them as Nanite static meshes (default folder: Project Settings > ShroomHunt > Vegetation)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			LogLines(UVegetationMeshLibrary::BakeVegetationMeshes(Args.Num() > 0 ? Args[0] : FString()));
		}));

	FAutoConsoleCommand CmdVegetationStats(
		TEXT("sh.Vegetation.Stats"),
		TEXT("sh.Vegetation.Stats [ForestName = F01_LowlandOakBeech]: number of trees and understory instances per kind and genus for the whole forest."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			LogLines(UVegetationMeshLibrary::ScatterReport(Args.Num() > 0 ? Args[0] : TEXT("F01_LowlandOakBeech")));
		}));

	FAutoConsoleCommand CmdVegetationExportObj(
		TEXT("sh.Vegetation.ExportObj"),
		TEXT("sh.Vegetation.ExportObj: writes every vegetation mesh as an OBJ file to Saved/VegetationDebug (preview with Tools/Python/render_mushroom_preview.py --grid)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			TArray<FString> Errors;
			const TSharedPtr<const FVegetationDefinition> Definition = ForestFieldCache::GetVegetation(Errors);
			if (!Definition)
			{
				LogLines(FString::Join(Errors, TEXT("\n")));
				return;
			}
			const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("VegetationDebug"));
			int32 Written = 0;
			for (const FVegetationMeshSpec& Spec : VegetationData::ListMeshes(*Definition))
			{
				UE::Geometry::FDynamicMesh3 Mesh;
				if (VegetationMeshBuilder::Build(*Definition, Spec, Mesh))
				{
					Written += ShroomMesh::WriteObj(Mesh, Folder / (Spec.GetKey() + TEXT(".obj")),
						FString::Printf(TEXT("%s - ShroomHunt placeholder vegetation (cm, Z up), %d triangles"), *Spec.GetKey(), Mesh.TriangleCount())) ? 1 : 0;
				}
			}
			UE_LOG(LogShroomDebug, Display, TEXT("Wrote %d vegetation OBJ files to %s"), Written, *Folder);
		}));
}

#endif // !UE_BUILD_SHIPPING
