#include "Forest/ForestLandmarkSet.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Core/RuntimeMesh.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestPlaceNames.h"
#include "Forest/ForestWorld.h"
#include "Forest/LandmarkMeshBuilder.h"
#include "Knowledge/KnowledgeSubsystem.h"
#include "Knowledge/KnowledgeText.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Materials/MaterialInterface.h"

namespace ForestLandmarkSetDetail
{
	const TCHAR LeftArrow = 0x2190;
	const TCHAR RightArrow = 0x2192;

	/** Text size (cm) that fits Length cm for Text, at most MaxSize. */
	float FitSize(const FString& Text, float Length, float MaxSize)
	{
		return FMath::Min(MaxSize, Length / FMath::Max(1.0f, 0.55f * Text.Len()));
	}
}

const FName AForestLandmarkSet::ReadableTag(TEXT("ShroomReadable"));
const FName AForestLandmarkSet::CounterId(TEXT("BuyerCounter"));

FName AForestLandmarkSet::GetReadableSource(const UPrimitiveComponent* Component)
{
	return Component && Component->ComponentTags.Num() >= 2 && Component->ComponentTags[0] == ReadableTag ? Component->ComponentTags[1] : NAME_None;
}

AForestLandmarkSet::AForestLandmarkSet()
{
	PrimaryActorTick.bCanEverTick = false;
	// Spans the whole forest (or drives it): never unloaded by World Partition streaming.
	bIsSpatiallyLoaded = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AForestLandmarkSet::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

void AForestLandmarkSet::BeginPlay()
{
	Super::BeginPlay();
	// Meshes are transient: outside the editor (packaged game) the components come back empty.
	TArray<UStaticMeshComponent*> Components;
	GetComponents<UStaticMeshComponent>(Components);
	if (Components.Num() == 0 || Components.ContainsByPredicate([](const UStaticMeshComponent* C) { return C->GetStaticMesh() == nullptr; }))
	{
		TArray<USceneComponent*> Attached;
		Root->GetChildrenComponents(true, Attached);
		for (USceneComponent* Child : Attached)
		{
			Child->DestroyComponent();
		}
		Build();
	}
	if (UKnowledgeSubsystem* Knowledge = GetGameInstance() ? GetGameInstance()->GetSubsystem<UKnowledgeSubsystem>() : nullptr)
	{
		KnowledgeChangedHandle = Knowledge->OnKnowledgeChanged.AddUObject(this, &AForestLandmarkSet::RefreshReadables);
	}
	RefreshReadables();
}

void AForestLandmarkSet::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UKnowledgeSubsystem* Knowledge = GetGameInstance() ? GetGameInstance()->GetSubsystem<UKnowledgeSubsystem>() : nullptr)
	{
		Knowledge->OnKnowledgeChanged.Remove(KnowledgeChangedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AForestLandmarkSet::RefreshReadables()
{
	const UKnowledgeSubsystem* Knowledge = GetGameInstance() ? GetGameInstance()->GetSubsystem<UKnowledgeSubsystem>() : nullptr;
	if (!Knowledge)
	{
		return;
	}
	TArray<UStaticMeshComponent*> Components;
	GetComponents<UStaticMeshComponent>(Components);
	for (UStaticMeshComponent* Component : Components)
	{
		const FName SourceId = GetReadableSource(Component);
		const FKnowledgeSource* Source = Knowledge->FindSource(SourceId);
		if (!Source)
		{
			continue;
		}
		// A page is taken when read; a poster stays on its wall.
		const bool bPresent = Source->Kind != EKnowledgeSourceKind::Page || !Knowledge->HasSource(SourceId);
		Component->SetVisibility(bPresent, true);
		Component->SetCollisionEnabled(bPresent ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

void AForestLandmarkSet::Rebuild()
{
	Meshes.Reset();
#if WITH_EDITOR
	RerunConstructionScripts();
#endif
}

UStaticMesh* AForestLandmarkSet::GetMesh(const FString& Key, TFunctionRef<UStaticMesh*()> Create)
{
	if (const TObjectPtr<UStaticMesh>* Found = Meshes.Find(Key))
	{
		if (*Found)
		{
			return *Found;
		}
	}
	UStaticMesh* Mesh = Create();
	Meshes.Add(Key, Mesh);
	return Mesh;
}

UStaticMeshComponent* AForestLandmarkSet::AddMesh(UStaticMesh* Mesh, const FTransform& WorldTransform, bool bCollision)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
	Component->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	Component->SetupAttachment(Root);
	Component->SetStaticMesh(Mesh);
	Component->SetWorldTransform(WorldTransform);
	if (bCollision)
	{
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}
	else
	{
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Component->RegisterComponent();
	return Component;
}

void AForestLandmarkSet::AddText(USceneComponent* Parent, const FString& Text, const FVector& RelativeLocation, float RelativeYaw, float WorldSize, FColor Color)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	Label->SetupAttachment(Parent);
	Label->SetRelativeLocation(RelativeLocation);
	Label->SetRelativeRotation(FRotator(0.0, RelativeYaw, 0.0));
	Label->SetText(FText::FromString(Text)); // proper nouns from Data/Names, not translated
	Label->SetWorldSize(WorldSize);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(Color);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->RegisterComponent();
}

void AForestLandmarkSet::Build()
{
	using namespace ForestLandmarkSetDetail;

	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(ForestName, Errors);
	FPlaceNameData NameData;
	if (!Field || !ForestPlaceNames::Load(NameData, Errors))
	{
		UE_LOG(LogShroomForest, Warning, TEXT("Forest Landmarks: %s"), *FString::Join(Errors, TEXT("; ")));
		return;
	}
	const TArray<FString> Names = ForestPlaceNames::NameNodes(*Field, NameData);
	const FForestPathNetwork& Network = Field->GetPathNetwork();

	UMaterialInterface* MeshMaterial = Material.LoadSynchronous();
	if (!MeshMaterial)
	{
		MeshMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	}

	const FTransform FieldToWorld = ForestWorld::GetFieldToWorld(GetWorld(), FVector::ZeroVector);
	const FQuat FieldRotation = FieldToWorld.GetRotation();
	auto WorldAt = [&](const FVector2f& PositionM, float HeightM, float YawDeg)
	{
		return FTransform(FieldRotation * FRotator(0.0, YawDeg, 0.0).Quaternion(),
			FieldToWorld.TransformPosition(FVector(PositionM.X, PositionM.Y, HeightM)));
	};
	auto LandmarkMesh = [&](const FForestLandmark& Landmark, const FString& Key)
	{
		return GetMesh(Key, [&]()
		{
			UE::Geometry::FDynamicMesh3 Mesh;
			FLandmarkCollision Collision;
			LandmarkMeshBuilder::Build(Landmark, Mesh, &Collision);
			return ShroomMesh::CreateTransientStaticMesh(Mesh, MeshMaterial, *(TEXT("SM_") + Key), &Collision.Boxes);
		});
	};
	UStaticMesh* BoardMesh = GetMesh(TEXT("SignBoard"), [&]()
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		LandmarkMeshBuilder::BuildSignBoard(Mesh);
		return ShroomMesh::CreateTransientStaticMesh(Mesh, MeshMaterial, TEXT("SM_SignBoard"));
	});

	int32 Built = 0;
	for (const FForestLandmark& Landmark : Field->GetLandmarks())
	{
		FString Key = ForestLandmarks::KindName(Landmark.Kind);
		if (Landmark.Kind == EForestLandmarkKind::Bridge || Landmark.Kind == EForestLandmarkKind::LogPile)
		{
			Key += FString::Printf(TEXT("_%d_%d"), FMath::RoundToInt(Landmark.LengthM * 10.0f), FMath::RoundToInt(Landmark.WidthM * 10.0f));
		}
		UStaticMesh* Mesh = LandmarkMesh(Landmark, Key);
		if (!Mesh)
		{
			continue;
		}
		const FTransform World = WorldAt(Landmark.PositionM, Landmark.GroundM, Landmark.YawDeg);
		UStaticMeshComponent* Component = AddMesh(Mesh, World, true);
		++Built;
		if (!bShowTexts)
		{
			continue;
		}

		const FString Name = Names.IsValidIndex(Landmark.Node) ? Names[Landmark.Node] : FString();
		switch (Landmark.Kind)
		{
		case EForestLandmarkKind::Signpost:
		{
			if (!Name.IsEmpty())
			{
				const float Size = FitSize(Name, 140.0f, 13.0f);
				const double TextX = LandmarkMeshBuilder::SignNameBoardFaceX + 1.0;
				AddText(Component, Name, FVector(TextX, 0.0, LandmarkMeshBuilder::SignNameBoardZ), 0.0f, Size);
				AddText(Component, Name, FVector(-TextX, 0.0, LandmarkMeshBuilder::SignNameBoardZ), 180.0f, Size);
			}
			// Arrows to the neighboring named places, along each path leaving the node.
			int32 Board = 0;
			for (const FForestPath& Path : Network.Paths)
			{
				const bool bFromStart = Path.StartNode == Landmark.Node;
				const bool bFromEnd = Path.EndNode == Landmark.Node;
				if ((!bFromStart && !bFromEnd) || Path.Points.Num() < 4 || Board >= 4)
				{
					continue;
				}
				const int32 Other = bFromStart ? Path.EndNode : Path.StartNode;
				if (!Names.IsValidIndex(Other) || Names[Other].IsEmpty() || Other == Landmark.Node)
				{
					continue;
				}
				const FVector2f From = bFromStart ? Path.Points[0] : Path.Points.Last();
				const FVector2f Toward = bFromStart ? Path.Points[FMath::Min(4, Path.Points.Num() - 1)] : Path.Points[FMath::Max(0, Path.Points.Num() - 5)];
				const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Toward.Y - From.Y, Toward.X - From.X));
				FTransform BoardWorld = WorldAt(Landmark.PositionM, Landmark.GroundM, Yaw);
				BoardWorld.AddToTranslation(FVector(0.0, 0.0, LandmarkMeshBuilder::SignFirstArrowZ - Board * LandmarkMeshBuilder::SignArrowSpacing));
				UStaticMeshComponent* BoardComponent = AddMesh(BoardMesh, BoardWorld, false);
				const int32 Meters = FMath::RoundToInt(Path.GetLengthM() / 10.0f) * 10;
				const FString Label = FString::Printf(TEXT("%s  %d m"), *Names[Other], Meters);
				const float Size = FitSize(Label + TEXT("  "), 95.0f, 9.0f);
				// +Y face: the tip is on the reader's left; -Y face: on the right.
				AddText(BoardComponent, FString::Chr(LeftArrow) + TEXT(" ") + Label, FVector(55.0, 3.0, 0.0), 90.0f, Size);
				AddText(BoardComponent, Label + TEXT(" ") + FString::Chr(RightArrow), FVector(55.0, -3.0, 0.0), -90.0f, Size);
				++Board;
			}
			break;
		}

		case EForestLandmarkKind::ParcelStone:
		{
			TArray<FString> Numbers;
			for (const int32 Number : Landmark.ParcelNumbers)
			{
				Numbers.Add(FString::FromInt(Number));
			}
			const FString Text = FString::Join(Numbers, TEXT(" | "));
			AddText(Component, Text, FVector(21.0, 0.0, 35.0), 0.0f, FitSize(Text, 36.0f, 9.0f));
			AddText(Component, Text, FVector(-21.0, 0.0, 35.0), 180.0f, FitSize(Text, 36.0f, 9.0f));
			break;
		}

		default:
			break;
		}
	}
	// Knowledge sources hanging on their landmark (the first one of its kind).
	FKnowledgeSourceData Sources;
	TMap<FName, FMushroomSpeciesRow> Species;
	FString SpeciesSource;
	int32 Readables = 0;
	if (KnowledgeText::LoadSources(Sources, Errors) && USpeciesDatabaseSubsystem::LoadSpeciesForTools(Species, SpeciesSource))
	{
		for (const FKnowledgeSource& Source : Sources.Sources)
		{
			if (Source.Kind == EKnowledgeSourceKind::Book)
			{
				continue; // sold at the counter, not hung on a landmark
			}
			const FForestLandmark* Host = Field->GetLandmarks().FindByPredicate([&Source](const FForestLandmark& L) { return Source.Landmark == ForestLandmarks::KindName(L.Kind); });
			FTransform Mount;
			FVector2D Size;
			if (!Host || !LandmarkMeshBuilder::GetPaperMount(Host->Kind, Mount, Size))
			{
				UE_LOG(LogShroomForest, Warning, TEXT("Forest Landmarks: no %s to hang %s on"), *Source.Landmark, *Source.Id.ToString());
				continue;
			}
			TArray<FLinearColor> Swatches;
			TArray<FText> SpeciesNames;
			for (const FName Id : Source.Species)
			{
				if (const FMushroomSpeciesRow* Row = Species.Find(Id))
				{
					const FColor A = Row->Cap.Colors.A, B = Row->Cap.Colors.B;
					Swatches.Add(FLinearColor((A.R + B.R) / 510.0f, (A.G + B.G) / 510.0f, (A.B + B.B) / 510.0f));
					SpeciesNames.Add(Row->CommonName);
				}
			}
			const bool bTorn = Source.Kind == EKnowledgeSourceKind::Page;
			UStaticMesh* PaperMesh = GetMesh(TEXT("Paper_") + Source.Id.ToString(), [&]()
			{
				UE::Geometry::FDynamicMesh3 Mesh;
				FLandmarkCollision Collision;
				LandmarkMeshBuilder::BuildPaper(Size, Swatches, bTorn, Mesh, &Collision);
				return ShroomMesh::CreateTransientStaticMesh(Mesh, MeshMaterial, *(TEXT("SM_Paper_") + Source.Id.ToString()), &Collision.Boxes);
			});
			if (!PaperMesh)
			{
				continue;
			}
			UStaticMeshComponent* Paper = AddMesh(PaperMesh, Mount * WorldAt(Host->PositionM, Host->GroundM, Host->YawDeg), true);
			Paper->SetCastShadow(false);
			Paper->ComponentTags = { ReadableTag, Source.Id };
			++Readables;
			if (!bShowTexts)
			{
				continue;
			}
			const FString Title = Source.Title.ToString();
			AddText(Paper, Title, FVector(0.8, 0.0, 0.39 * Size.Y), 0.0f, FitSize(Title, 0.85f * Size.X, 4.0f), FColor(235, 228, 205));
			for (int32 Index = 0; Index < SpeciesNames.Num(); ++Index)
			{
				const float ColumnW = 0.9f * Size.X / SpeciesNames.Num();
				const FString Name = SpeciesNames[Index].ToString();
				AddText(Paper, Name, FVector(0.8, -0.45 * Size.X + ColumnW * (Index + 0.5), -0.3 * Size.Y), 0.0f, FitSize(Name, 0.95f * ColumnW, 3.2f));
			}
		}
	}
	// The buyer's counter (M3): a board by the door of the forester's house.
	if (const FForestLandmark* House = Field->GetLandmarks().FindByPredicate([](const FForestLandmark& L) { return L.Kind == EForestLandmarkKind::ForesterHouse; }))
	{
		FTransform Mount;
		FVector2D Size;
		if (LandmarkMeshBuilder::GetCounterMount(House->Kind, Mount, Size))
		{
			const TArray<FLinearColor> Coins = { FLinearColor(0.75f, 0.6f, 0.2f), FLinearColor(0.55f, 0.35f, 0.15f), FLinearColor(0.3f, 0.45f, 0.25f) };
			UStaticMesh* CounterMesh = GetMesh(TEXT("Paper_Counter"), [&]()
			{
				UE::Geometry::FDynamicMesh3 Mesh;
				FLandmarkCollision Collision;
				LandmarkMeshBuilder::BuildPaper(Size, Coins, false, Mesh, &Collision);
				return ShroomMesh::CreateTransientStaticMesh(Mesh, MeshMaterial, TEXT("SM_Paper_Counter"), &Collision.Boxes);
			});
			if (CounterMesh)
			{
				UStaticMeshComponent* Board = AddMesh(CounterMesh, Mount * WorldAt(House->PositionM, House->GroundM, House->YawDeg), true);
				Board->SetCastShadow(false);
				Board->ComponentTags = { ReadableTag, CounterId };
				if (bShowTexts)
				{
					const FString Title = TEXT("Mushroom buyer - field guides");
					AddText(Board, Title, FVector(0.8, 0.0, 0.39 * Size.Y), 0.0f, FitSize(Title, 0.85f * Size.X, 4.0f), FColor(235, 228, 205));
				}
			}
		}
	}
	UE_LOG(LogShroomForest, Log, TEXT("Forest Landmarks: %d landmarks and %d knowledge sources built for %s"), Built, Readables, *ForestName);
}
