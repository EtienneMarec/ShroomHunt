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
		const FFieldRaster& CoarseLevel = Field.GetWaterLevel();
		const FFieldRaster& Heights = Field.GetHeights();
		const int32 W = CoarseLevel.Width;
		const int32 R = Heights.Width;
		const float Cell = FForestField::CoarseCellM;
		// Dark, slightly green water albedo (seen through the fallback vertex color material).
		const FLinearColor Color(0.05f, 0.11f, 0.12f, 1.0f);
		FMeshAccumulator Acc;

		// --- Ponds: 1 m grid following the real shore -----------------------------------------
		// Each pond (connected coarse cells sharing one level) is flooded on the fine heightmap:
		// every vertex lower than the level and connected to the pond, then two rings of bank
		// vertices (higher than the level) so that the surface edge is always under the ground.
		{
			TArray<bool> Seen;
			Seen.Init(false, W * W);
			TArray<int32> FineState; // 0 dry, 1 water, 2 bank; per pond, reset in its box
			FineState.Init(0, R * R);
			for (int32 Start = 0; Start < W * W; ++Start)
			{
				if (Seen[Start] || CoarseLevel.Values[Start] <= FForestField::NoWater || Field.GetWaterCells()[Start] != 2)
				{
					continue;
				}
				const float Level = CoarseLevel.Values[Start];
				// Coarse cells of this pond (and its basin / banks) and their bounding box.
				TArray<int32> Coarse = { Start };
				Seen[Start] = true;
				FIntRect Box(Start % W, Start / W, Start % W, Start / W);
				for (int32 K = 0; K < Coarse.Num(); ++K)
				{
					const int32 C = Coarse[K];
					Box.Min = FIntPoint(FMath::Min(Box.Min.X, C % W), FMath::Min(Box.Min.Y, C / W));
					Box.Max = FIntPoint(FMath::Max(Box.Max.X, C % W), FMath::Max(Box.Max.Y, C / W));
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							const int32 X = C % W + DX;
							const int32 Y = C / W + DY;
							if (X >= 0 && Y >= 0 && X < W && Y < W && !Seen[Y * W + X] && FMath::IsNearlyEqual(CoarseLevel.Values[Y * W + X], Level, 1e-3f))
							{
								Seen[Y * W + X] = true;
								Coarse.Add(Y * W + X);
							}
						}
					}
				}
				const int32 X0 = FMath::Max(0, Box.Min.X * 4 - 8);
				const int32 Y0 = FMath::Max(0, Box.Min.Y * 4 - 8);
				const int32 X1 = FMath::Min(R - 1, Box.Max.X * 4 + 8);
				const int32 Y1 = FMath::Min(R - 1, Box.Max.Y * 4 + 8);
				auto Inside = [&](int32 X, int32 Y) { return X >= X0 && Y >= Y0 && X <= X1 && Y <= Y1; };

				// Flood the water from the pond cells' centers.
				TArray<FIntPoint> Queue;
				for (const int32 C : Coarse)
				{
					const FIntPoint P((C % W) * 4, (C / W) * 4);
					if (Field.GetWaterCells()[C] == 2 && Heights.At(P.X, P.Y) < Level && FineState[P.Y * R + P.X] == 0)
					{
						FineState[P.Y * R + P.X] = 1;
						Queue.Add(P);
					}
				}
				// 8-connected: every vertex next to the water is then either water or bank.
				static const FIntPoint Steps[8] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
					FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1) };
				TArray<FIntPoint> Shore;
				for (int32 K = 0; K < Queue.Num(); ++K)
				{
					for (const FIntPoint& Step : Steps)
					{
						const FIntPoint N = Queue[K] + Step;
						if (!Inside(N.X, N.Y) || FineState[N.Y * R + N.X] != 0)
						{
							continue;
						}
						if (Heights.At(N.X, N.Y) < Level)
						{
							FineState[N.Y * R + N.X] = 1;
							Queue.Add(N);
						}
						else
						{
							FineState[N.Y * R + N.X] = 2;
							Shore.Add(N);
						}
					}
				}
				// One more ring of bank around the shore (8-neighbors, only higher ground).
				TArray<FIntPoint> Ring = Shore;
				for (const FIntPoint& P : Shore)
				{
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							const FIntPoint N(P.X + DX, P.Y + DY);
							if (Inside(N.X, N.Y) && FineState[N.Y * R + N.X] == 0 && Heights.At(N.X, N.Y) >= Level)
							{
								FineState[N.Y * R + N.X] = 2;
								Ring.Add(N);
							}
						}
					}
				}

				// Quads where the four corners are water or bank, at least one water.
				TMap<int32, int32> Vertices;
				auto Vertex = [&](int32 X, int32 Y)
				{
					const int32 Key = Y * R + X;
					if (const int32* Found = Vertices.Find(Key))
					{
						return *Found;
					}
					const int32 Id = Acc.AddVertex(FVector3d(X, Y, Level), Color);
					Vertices.Add(Key, Id);
					return Id;
				};
				for (int32 Y = Y0; Y < Y1; ++Y)
				{
					for (int32 X = X0; X < X1; ++X)
					{
						const int32 S[4] = { FineState[Y * R + X], FineState[Y * R + X + 1], FineState[(Y + 1) * R + X + 1], FineState[(Y + 1) * R + X] };
						if (S[0] == 0 || S[1] == 0 || S[2] == 0 || S[3] == 0 || (S[0] != 1 && S[1] != 1 && S[2] != 1 && S[3] != 1))
						{
							continue;
						}
						// Counter-clockwise seen from above: faces up.
						const FVector2f Uv(X / 8.0f, Y / 8.0f);
						const float Step = 1.0f / 8.0f;
						Acc.AddQuad(Vertex(X, Y), Vertex(X + 1, Y), Vertex(X + 1, Y + 1), Vertex(X, Y + 1),
							Uv, Uv + FVector2f(Step, 0), Uv + FVector2f(Step, Step), Uv + FVector2f(0, Step), 0);
					}
				}
				// Reset the box for the next pond.
				for (int32 Y = Y0; Y <= Y1; ++Y)
				{
					for (int32 X = X0; X <= X1; ++X)
					{
						FineState[Y * R + X] = 0;
					}
				}
			}
		}

		// --- Streams: ribbons on the real bed --------------------------------------------------
		// Cross-section of 5 vertices: center and bed edges (+-half width) at the water level, then
		// the outer edges 2.5 m further, on the banks (pulled just under the ground if a bank is
		// low). The level follows the carved bed sampled every meter, lightly smoothed.
		for (const FForestStream& Stream : Field.GetStreams())
		{
			TArray<FVector2f> Points;
			TArray<float> Widths;
			for (int32 Index = 0; Index + 1 < Stream.Points.Num(); ++Index)
			{
				const float Length = FVector2f::Distance(Stream.Points[Index], Stream.Points[Index + 1]);
				const int32 Count = FMath::Max(1, FMath::CeilToInt(Length));
				for (int32 Step = 0; Step < Count; ++Step)
				{
					const float T = static_cast<float>(Step) / Count;
					Points.Add(FMath::Lerp(Stream.Points[Index], Stream.Points[Index + 1], T));
					Widths.Add(FMath::Lerp(Stream.HalfWidths[Index], Stream.HalfWidths[Index + 1], T));
				}
			}
			Points.Add(Stream.Points.Last());
			Widths.Add(Stream.HalfWidths.Last());
			if (Points.Num() < 2)
			{
				continue;
			}
			// The carved bed continues past the first cell: extend upstream, under the bank.
			{
				const FVector2f Back = (Points[0] - Points[1]).GetSafeNormal();
				const FVector2f First = Points[0];
				const float FirstWidth = Widths[0];
				Points.Insert(First + Back * 2.0f, 0);
				Widths.Insert(FirstWidth, 0);
			}

			TArray<float> Bed;
			for (const FVector2f& P : Points)
			{
				Bed.Add(Heights.Sample(P.X, P.Y));
			}
			TArray<float> Levels = Bed;
			for (int32 Index = 0; Index < Bed.Num(); ++Index)
			{
				float Sum = 0.0f;
				int32 N = 0;
				for (int32 K = FMath::Max(0, Index - 2); K <= FMath::Min(Bed.Num() - 1, Index + 2); ++K)
				{
					Sum += Bed[K];
					++N;
				}
				// Never below the bed under the centerline: the water must show.
				Levels[Index] = FMath::Max(Sum / N, Bed[Index]) + 0.25f;
			}

			constexpr int32 Columns = 5;
			TArray<int32> Ids;
			Ids.SetNum(Points.Num() * Columns);
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const FVector2f Dir = (Points[FMath::Min(Index + 1, Points.Num() - 1)] - Points[FMath::Max(Index - 1, 0)]).GetSafeNormal();
				const FVector2f Side(-Dir.Y, Dir.X);
				const float Offsets[Columns] = { Widths[Index] + 2.5f, Widths[Index], 0.0f, -Widths[Index], -(Widths[Index] + 2.5f) };
				for (int32 Column = 0; Column < Columns; ++Column)
				{
					const FVector2f P = Points[Index] + Side * Offsets[Column];
					const bool bOuter = Column == 0 || Column == Columns - 1;
					const float Z = bOuter ? FMath::Min(Levels[Index], Heights.Sample(P.X, P.Y) - 0.15f) : Levels[Index];
					Ids[Index * Columns + Column] = Acc.AddVertex(FVector3d(P.X, P.Y, Z), Color);
				}
			}
			for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
			{
				const float V0 = Index * 0.125f;
				const float V1 = (Index + 1) * 0.125f;
				for (int32 Column = 0; Column + 1 < Columns; ++Column)
				{
					// Left to right, then forward: counter-clockwise seen from above.
					const float U0 = Column * 0.25f;
					const float U1 = (Column + 1) * 0.25f;
					Acc.AddQuad(Ids[Index * Columns + Column], Ids[Index * Columns + Column + 1], Ids[(Index + 1) * Columns + Column + 1], Ids[(Index + 1) * Columns + Column],
						FVector2f(U0, V0), FVector2f(U1, V0), FVector2f(U1, V1), FVector2f(U0, V1), 1);
				}
			}
		}
		Acc.Emit(OutMesh);
	}
}

AForestWater::AForestWater()
{
	PrimaryActorTick.bCanEverTick = false;
	// Spans the whole forest (or drives it): never unloaded by World Partition streaming.
	bIsSpatiallyLoaded = false;
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
