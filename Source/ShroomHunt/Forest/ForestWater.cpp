#include "Forest/ForestWater.h"

#include "Components/StaticMeshComponent.h"
#include "Core/MeshAccumulator.h"
#include "Core/RuntimeMesh.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestWorld.h"
#include "Materials/MaterialInterface.h"

namespace ForestWaterMesh
{
	void Build(const FForestField& Field, UE::Geometry::FDynamicMesh3& OutMesh)
	{
		const FFieldRaster& Level = Field.GetWaterLevel();
		const int32 W = Level.Width;
		const float Cell = FForestField::CoarseCellM;
		// Dark, slightly green water albedo (seen through the fallback vertex color material).
		const FLinearColor Color(0.05f, 0.11f, 0.12f, 1.0f);

		const FFieldRaster& Heights = Field.GetHeights();
		// "Skirt": an edge vertex standing above the ground (the fine terrain dips between the 4 m
		// nodes, or a stream bank is low) is pulled just below it, so the edge is never visible.
		auto EdgeZ = [&Heights](float X, float Y, float WaterZ)
		{
			return FMath::Min(WaterZ, Heights.Sample(X, Y) - 0.15f);
		};

		FMeshAccumulator Acc;
		auto Wet = [&](int32 X, int32 Y) { return Level.At(X, Y) > FForestField::NoWater; };
		auto HasQuad = [&](int32 X, int32 Y)
		{
			return X >= 0 && Y >= 0 && X + 1 < W && Y + 1 < W && Wet(X, Y) && Wet(X + 1, Y) && Wet(X + 1, Y + 1) && Wet(X, Y + 1);
		};
		TMap<int32, int32> Vertices;
		auto Vertex = [&](int32 X, int32 Y)
		{
			const int32 Key = Y * W + X;
			if (const int32* Found = Vertices.Find(Key))
			{
				return *Found;
			}
			const bool bEdge = !HasQuad(X - 1, Y - 1) || !HasQuad(X, Y - 1) || !HasQuad(X - 1, Y) || !HasQuad(X, Y);
			const float Z = bEdge ? EdgeZ(X * Cell, Y * Cell, Level.At(X, Y)) : Level.At(X, Y);
			const int32 Id = Acc.AddVertex(FVector3d(X * Cell, Y * Cell, Z), Color);
			Vertices.Add(Key, Id);
			return Id;
		};
		for (int32 Y = 0; Y + 1 < W; ++Y)
		{
			for (int32 X = 0; X + 1 < W; ++X)
			{
				if (!HasQuad(X, Y))
				{
					continue;
				}
				// Counter-clockwise seen from above: faces up.
				const FVector2f Uv(X * Cell / 8.0f, Y * Cell / 8.0f);
				const float Step = Cell / 8.0f;
				Acc.AddQuad(Vertex(X, Y), Vertex(X + 1, Y), Vertex(X + 1, Y + 1), Vertex(X, Y + 1),
					Uv, Uv + FVector2f(Step, 0), Uv + FVector2f(Step, Step), Uv + FVector2f(0, Step), 0);
			}
		}
		// Streams: a ribbon along each centerline, 3 m wider than the carved bed (its banks rise
		// gradually) so its edges end inside the banks. Resampled every 1 m.
		for (const FForestStream& Stream : Field.GetStreams())
		{
			TArray<FVector2f> Points;
			TArray<float> Levels;
			TArray<float> Widths;
			for (int32 Index = 0; Index + 1 < Stream.Points.Num(); ++Index)
			{
				const float Length = FVector2f::Distance(Stream.Points[Index], Stream.Points[Index + 1]);
				const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length));
				for (int32 Step = 0; Step < Steps; ++Step)
				{
					const float T = static_cast<float>(Step) / Steps;
					Points.Add(FMath::Lerp(Stream.Points[Index], Stream.Points[Index + 1], T));
					Levels.Add(FMath::Lerp(Stream.Levels[Index], Stream.Levels[Index + 1], T));
					Widths.Add(FMath::Lerp(Stream.HalfWidths[Index], Stream.HalfWidths[Index + 1], T) + 3.0f);
				}
			}
			Points.Add(Stream.Points.Last());
			Levels.Add(Stream.Levels.Last());
			Widths.Add(Stream.HalfWidths.Last() + 3.0f);
			// The carved bed continues a few meters past the first cell: extend upstream, under the bank.
			if (Points.Num() >= 2)
			{
				const FVector2f Back = (Points[0] - Points[1]).GetSafeNormal();
				Points.Insert(Points[0] + Back * 3.0f, 0);
				const float FirstLevel = Levels[0];
				const float FirstWidth = Widths[0];
				Levels.Insert(FirstLevel, 0);
				Widths.Insert(FirstWidth, 0);
			}

			TArray<int32> Left;
			TArray<int32> Right;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const FVector2f Dir = (Points[FMath::Min(Index + 1, Points.Num() - 1)] - Points[FMath::Max(Index - 1, 0)]).GetSafeNormal();
				const FVector2f Side(-Dir.Y, Dir.X);
				const FVector2f L = Points[Index] + Side * Widths[Index];
				const FVector2f R = Points[Index] - Side * Widths[Index];
				Left.Add(Acc.AddVertex(FVector3d(L.X, L.Y, EdgeZ(L.X, L.Y, Levels[Index])), Color));
				Right.Add(Acc.AddVertex(FVector3d(R.X, R.Y, EdgeZ(R.X, R.Y, Levels[Index])), Color));
			}
			for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
			{
				// Left, right, then forward: counter-clockwise seen from above.
				const float V0 = Index * 0.125f;
				const float V1 = (Index + 1) * 0.125f;
				Acc.AddQuad(Left[Index], Right[Index], Right[Index + 1], Left[Index + 1],
					FVector2f(0, V0), FVector2f(0.5f, V0), FVector2f(0.5f, V1), FVector2f(0, V1), 1);
			}
		}
		Acc.Emit(OutMesh);
	}
}

AForestWater::AForestWater()
{
	PrimaryActorTick.bCanEverTick = false;
	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	RootComponent = Surface;
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetCastShadow(false);
}

void AForestWater::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

void AForestWater::BeginPlay()
{
	Super::BeginPlay();
	if (!Surface->GetStaticMesh())
	{
		Build(); // the mesh is transient: rebuilt when the level is loaded outside the editor
	}
}

void AForestWater::Rebuild()
{
	Build();
}

void AForestWater::Build()
{
	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(ForestName, Errors);
	if (!Field)
	{
		UE_LOG(LogShroomForest, Warning, TEXT("Forest Water: %s"), *FString::Join(Errors, TEXT("; ")));
		return;
	}
	UE::Geometry::FDynamicMesh3 Mesh;
	ForestWaterMesh::Build(*Field, Mesh);
	UMaterialInterface* Material = WaterMaterial.LoadSynchronous();
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	}
	Surface->SetStaticMesh(Mesh.TriangleCount() > 0 ? ShroomMesh::CreateTransientStaticMesh(Mesh, Material, TEXT("SM_ForestWater")) : nullptr);

	// The mesh is in field meters: place it with the landscape's transform (100 cm per meter).
	Surface->SetWorldTransform(ForestWorld::GetFieldToWorld(GetWorld(), FVector::ZeroVector));
}
