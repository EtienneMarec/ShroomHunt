#include "Mushroom/MushroomGarden.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Mushroom/MushroomMeshSettings.h"
#include "Mushroom/MushroomMeshSubsystem.h"
#include "Mushroom/MushroomMorphology.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"

#define LOCTEXT_NAMESPACE "MushroomGarden"

AMushroomGarden::AMushroomGarden()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AMushroomGarden::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

void AMushroomGarden::Regenerate()
{
	if (GEngine)
	{
		if (UMushroomMeshSubsystem* Meshes = GEngine->GetEngineSubsystem<UMushroomMeshSubsystem>())
		{
			Meshes->ClearCache();
		}
	}
	RerunConstructionScripts();
}

void AMushroomGarden::AddLabel(const FText& Text, const FVector& RelativeLocation, float Size)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(RelativeLocation);
	Label->SetRelativeRotation(FRotator(0.0, 180.0, 0.0)); // readable from the -X side
	Label->SetText(Text);
	Label->SetWorldSize(Size);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetTextRenderColor(FColor(240, 230, 200));
	Label->RegisterComponent();
}

void AMushroomGarden::Build()
{
	UMushroomMeshSubsystem* Meshes = GEngine ? GEngine->GetEngineSubsystem<UMushroomMeshSubsystem>() : nullptr;
	if (!Meshes)
	{
		return;
	}

	TMap<FName, FMushroomSpeciesRow> AllSpecies;
	FString Source;
	if (!USpeciesDatabaseSubsystem::LoadSpeciesForTools(AllSpecies, Source))
	{
		UE_LOG(LogShroomMesh, Warning, TEXT("Specimen garden: no species data (import Data/Species first)."));
		return;
	}

	TArray<FName> Ids = Species;
	if (Ids.Num() == 0)
	{
		AllSpecies.GetKeys(Ids);
		Ids.Sort(FNameLexicalLess());
	}

	static const EMushroomStage Stages[3] = { EMushroomStage::Young, EMushroomStage::Mature, EMushroomStage::Old };
	static const FText StageNames[3] = { LOCTEXT("Young", "Young"), LOCTEXT("Mature", "Mature"), LOCTEXT("Old", "Old") };
	const float StageGap = Spacing * 0.75f;
	double RowY = 0.0;
	int32 Built = 0;

	for (int32 Row = 0; Row < Ids.Num(); ++Row)
	{
		const FMushroomSpeciesRow* Data = AllSpecies.Find(Ids[Row]);
		if (!Data)
		{
			UE_LOG(LogShroomMesh, Warning, TEXT("Specimen garden: unknown species '%s'"), *Ids[Row].ToString());
			continue;
		}

		// Row depth adapts to the largest cap of the species.
		const double RowDepth = FMath::Max<double>(Spacing, Data->Cap.DiameterCm.Max * 1.4);
		double X = 0.0;
		for (int32 StageIndex = 0; StageIndex < 3; ++StageIndex)
		{
			const EMushroomStage Stage = Stages[StageIndex];
			if (bShowLabels && Row == 0)
			{
				AddLabel(StageNames[StageIndex], FVector(X + 0.5 * Spacing * (IndividualsPerStage - 1), -RowDepth * 0.6, 2.0), 6.0f);
			}
			for (int32 Index = 0; Index < IndividualsPerStage; ++Index)
			{
				UStaticMesh* Mesh = nullptr;
				if (bUniqueIndividuals)
				{
					const uint32 Seed = ShroomHash::Hash(static_cast<uint32>(SeedOffset), static_cast<uint32>(Row), static_cast<uint32>(StageIndex), static_cast<uint32>(Index));
					const FMushroomMorphology Morphology = MushroomMorphology::Sample(*Data, Ids[Row], Seed, Stage);
					Mesh = Meshes->CreateMeshForMorphology(Morphology, FName(*FString::Printf(TEXT("SM_Garden_%s"), *Ids[Row].ToString())));
				}
				else
				{
					const int32 Variants = FMath::Max(1, GetDefault<UMushroomMeshSettings>()->VariantsPerStage);
					Mesh = Meshes->GetVariantMesh(*Data, Ids[Row], Stage, Index % Variants);
				}
				if (!Mesh)
				{
					continue;
				}

				UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
				Component->CreationMethod = EComponentCreationMethod::UserConstructionScript;
				Component->SetupAttachment(Root);
				Component->SetStaticMesh(Mesh);
				Component->SetRelativeLocation(FVector(X, RowY, 0.0));
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Component->RegisterComponent();
				X += Spacing;
				++Built;
			}
			X += StageGap;
		}

		if (bShowLabels)
		{
			AddLabel(Data->CommonName, FVector(-Spacing, RowY, 4.0), 5.0f);
		}
		RowY += RowDepth;
	}

	UE_LOG(LogShroomMesh, Log, TEXT("Specimen garden: %d mushrooms from %s (%d cached meshes)"), Built, *Source, Meshes->GetCachedMeshCount());
}

#undef LOCTEXT_NAMESPACE
