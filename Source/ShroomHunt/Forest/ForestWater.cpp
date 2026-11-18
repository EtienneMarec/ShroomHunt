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

		FMeshAccumulator Acc;
		TMap<int32, int32> Vertices;
		auto Vertex = [&](int32 X, int32 Y)
		{
			const int32 Key = Y * W + X;
			if (const int32* Found = Vertices.Find(Key))
			{
				return *Found;
			}
			const int32 Id = Acc.AddVertex(FVector3d(X * Cell, Y * Cell, Level.At(X, Y)), Color);
			Vertices.Add(Key, Id);
			return Id;
		};
		auto Wet = [&](int32 X, int32 Y) { return Level.At(X, Y) > FForestField::NoWater; };
		for (int32 Y = 0; Y + 1 < W; ++Y)
		{
			for (int32 X = 0; X + 1 < W; ++X)
			{
				if (!Wet(X, Y) || !Wet(X + 1, Y) || !Wet(X + 1, Y + 1) || !Wet(X, Y + 1))
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
