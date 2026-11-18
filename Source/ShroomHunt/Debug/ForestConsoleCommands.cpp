// Forest debug console commands: sh.Forest.Bake, sh.Forest.CheckGround, sh.Forest.ExportLandmarks.

#include "Core/MeshAccumulator.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestGenerationLibrary.h"
#include "Forest/ForestWorld.h"
#include "Forest/LandmarkMeshBuilder.h"
#include "Engine/World.h"
#include "LandscapeProxy.h"
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

	FAutoConsoleCommandWithWorldAndArgs CmdForestCheckGround(
		TEXT("sh.Forest.CheckGround"),
		TEXT("sh.Forest.CheckGround [StepM = 8] [ForestName = F01_LowlandOakBeech]: traces the level's landscape every StepM meters and compares it with the forest field heights (what trees, water and landmarks use). Only loaded landscape regions are checked."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float StepM = Args.Num() > 0 ? FMath::Max(1.0f, FCString::Atof(*Args[0])) : 8.0f;
			TArray<FString> Errors;
			const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(Args.Num() > 1 ? Args[1] : TEXT("F01_LowlandOakBeech"), Errors);
			if (!Field || !World)
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("sh.Forest.CheckGround: %s"), Errors.Num() > 0 ? *FString::Join(Errors, TEXT("; ")) : TEXT("no world"));
				return;
			}
			bool bLandscape = false;
			const FTransform FieldToWorld = ForestWorld::GetFieldToWorld(World, FVector::ZeroVector, &bLandscape);
			if (!bLandscape)
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("sh.Forest.CheckGround: no Landscape in this level"));
				return;
			}
			struct FDiff
			{
				FVector2f FieldM;
				FVector World;
				float DeltaM;
			};
			TArray<FDiff> Diffs;
			int32 Missing = 0;
			FCollisionObjectQueryParams Objects(ECC_WorldStatic);
			FCollisionQueryParams Params(TEXT("CheckGround"), false);
			for (float Y = 0.0f; Y <= Field->GetSizeM(); Y += StepM)
			{
				for (float X = 0.0f; X <= Field->GetSizeM(); X += StepM)
				{
					const FVector Expected = FieldToWorld.TransformPosition(FVector(X, Y, Field->GetHeights().Sample(X, Y)));
					TArray<FHitResult> Hits;
					World->LineTraceMultiByObjectType(Hits, Expected + FVector(0, 0, 10000.0), Expected - FVector(0, 0, 10000.0), Objects, Params);
					const FHitResult* Ground = Hits.FindByPredicate([](const FHitResult& Hit) { return Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>(); });
					if (!Ground)
					{
						++Missing;
						continue;
					}
					Diffs.Add({ FVector2f(X, Y), Expected, static_cast<float>((Ground->ImpactPoint.Z - Expected.Z) / 100.0) });
				}
			}
			if (Diffs.Num() == 0)
			{
				UE_LOG(LogShroomDebug, Warning, TEXT("sh.Forest.CheckGround: the landscape was not hit (%d points); is it loaded?"), Missing);
				return;
			}
			double SumAbs = 0.0;
			int32 Over50 = 0;
			for (const FDiff& D : Diffs)
			{
				SumAbs += FMath::Abs(D.DeltaM);
				Over50 += FMath::Abs(D.DeltaM) > 0.5f ? 1 : 0;
			}
			Diffs.Sort([](const FDiff& A, const FDiff& B) { return FMath::Abs(A.DeltaM) > FMath::Abs(B.DeltaM); });
			UE_LOG(LogShroomDebug, Display, TEXT("CheckGround: %d points (%d not loaded), mean |landscape - field| %.2f m, %d points off by more than 0.5 m"),
				Diffs.Num(), Missing, SumAbs / Diffs.Num(), Over50);
			for (int32 Index = 0; Index < FMath::Min(8, Diffs.Num()); ++Index)
			{
				const FDiff& D = Diffs[Index];
				UE_LOG(LogShroomDebug, Display, TEXT("  field (%.0f, %.0f) m = world (%.0f, %.0f, %.0f) cm: landscape is %+.2f m from the field"),
					D.FieldM.X, D.FieldM.Y, D.World.X, D.World.Y, D.World.Z, D.DeltaM);
			}
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
