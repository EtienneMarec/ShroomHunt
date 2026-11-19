#include "Player/ForagerComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ShroomLog.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Forest/ForestLandmarkSet.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Knowledge/KnowledgeSubsystem.h"
#include "Knowledge/KnowledgeText.h"
#include "Mushroom/MushroomMeshSubsystem.h"
#include "Mycology/MushroomSpawner.h"
#include "Mycology/ObservableCriteria.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Player/ForagingSettings.h"
#include "Player/ForagingSubsystem.h"
#include "UI/EncyclopediaWidget.h"
#include "UI/ForagingWidget.h"

#define LOCTEXT_NAMESPACE "ShroomHunt.Foraging"

UForagerComponent::UForagerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.05f;
}

void UForagerComponent::BeginPlay()
{
	Super::BeginPlay();
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Controller || !Controller->IsLocalController())
	{
		return;
	}
	SetupInput();

	Widget = CreateWidget<UForagingWidget>(Controller, UForagingWidget::StaticClass());
	if (Widget)
	{
		Widget->AddToViewport(1);
	}
	Encyclopedia = CreateWidget<UEncyclopediaWidget>(Controller, UEncyclopediaWidget::StaticClass());
	if (Encyclopedia)
	{
		Encyclopedia->AddToViewport(2);
		Encyclopedia->SetVisibility(ESlateVisibility::Collapsed);
	}
	Specimen = NewObject<UStaticMeshComponent>(Pawn, TEXT("ExaminedSpecimen"));
	Specimen->SetUsingAbsoluteLocation(true);
	Specimen->SetUsingAbsoluteRotation(true);
	Specimen->SetMobility(EComponentMobility::Movable);
	Specimen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Specimen->SetCastShadow(false);
	Specimen->SetupAttachment(Pawn->GetRootComponent());
	Specimen->RegisterComponent();
	Specimen->SetVisibility(false);
	RefreshBasket();
}

void UForagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Widget)
	{
		Widget->RemoveFromParent();
		Widget = nullptr;
	}
	if (Encyclopedia)
	{
		Encyclopedia->RemoveFromParent();
		Encyclopedia = nullptr;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (Controller && Mapping)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer()))
		{
			Input->RemoveMappingContext(Mapping);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UForagerComponent::SetupInput()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UEnhancedInputComponent* Input = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = Controller ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer()) : nullptr;
	if (!Input || !Subsystem)
	{
		UE_LOG(LogShroomSpawn, Warning, TEXT("Forager: no Enhanced Input on the player pawn; foraging keys disabled"));
		return;
	}
	// Input actions built at runtime: nothing to author in the editor.
	const UForagingSettings* Settings = GetDefault<UForagingSettings>();
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		// Enhanced Input actions swallow their keys by default: let the template's
		// look/move actions (lower priority) still receive the mouse and keys.
		Action->bConsumeInput = false;
		return Action;
	};
	ExamineAction = MakeAction(TEXT("IA_Examine"), EInputActionValueType::Boolean);
	PickAction = MakeAction(TEXT("IA_Pick"), EInputActionValueType::Boolean);
	TurnAction = MakeAction(TEXT("IA_TurnSpecimen"), EInputActionValueType::Axis2D);
	EncyclopediaAction = MakeAction(TEXT("IA_Encyclopedia"), EInputActionValueType::Boolean);
	ScrollAction = MakeAction(TEXT("IA_ScrollPage"), EInputActionValueType::Axis1D);
	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Foraging"));
	Mapping->MapKey(ExamineAction, Settings->ExamineKey);
	Mapping->MapKey(PickAction, Settings->PickKey);
	Mapping->MapKey(TurnAction, EKeys::Mouse2D);
	Mapping->MapKey(EncyclopediaAction, Settings->EncyclopediaKey);
	Mapping->MapKey(ScrollAction, EKeys::MouseWheelAxis);
	Subsystem->AddMappingContext(Mapping, 2);

	Input->BindAction(ExamineAction, ETriggerEvent::Started, this, &UForagerComponent::OnExamine);
	Input->BindAction(PickAction, ETriggerEvent::Started, this, &UForagerComponent::OnPick);
	Input->BindAction(TurnAction, ETriggerEvent::Triggered, this, &UForagerComponent::OnTurn);
	Input->BindAction(EncyclopediaAction, ETriggerEvent::Started, this, &UForagerComponent::OnEncyclopedia);
	Input->BindAction(ScrollAction, ETriggerEvent::Triggered, this, &UForagerComponent::OnScroll);
}

AMushroomSpawner* UForagerComponent::GetSpawner()
{
	if (!Spawner.IsValid())
	{
		Spawner = AMushroomSpawner::Find(GetWorld());
	}
	return Spawner.Get();
}

UKnowledgeSubsystem* UForagerComponent::GetKnowledge() const
{
	const UWorld* World = GetWorld();
	return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UKnowledgeSubsystem>() : nullptr;
}

bool UForagerComponent::GetView(FVector& OutOrigin, FVector& OutDirection) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Controller)
	{
		return false;
	}
	FRotator Rotation;
	Controller->GetPlayerViewPoint(OutOrigin, Rotation);
	OutDirection = Rotation.Vector();
	return true;
}

FName UForagerComponent::FindReadable(const FVector& Origin, const FVector& Direction, float MaxDistanceCm, float& OutDistanceCm) const
{
	// A real trace: walls and trees hide the sheets (they have collision, unlike mushrooms).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ForagerReadable), false, GetOwner());
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * MaxDistanceCm, ECC_Visibility, Params))
	{
		return NAME_None;
	}
	OutDistanceCm = Hit.Distance;
	return AForestLandmarkSet::GetReadableSource(Hit.GetComponent());
}

void UForagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AMushroomSpawner* Mushrooms = GetSpawner();
	FVector Origin, Direction;
	if (!Widget || !Mushrooms || !GetView(Origin, Direction))
	{
		return;
	}
	const UForagingSettings* Settings = GetDefault<UForagingSettings>();

	if (bExamining)
	{
		// The specimen floats in front of the camera, centered on its middle.
		if (Mushrooms->FindBySeed(ExaminedSeed) == INDEX_NONE)
		{
			EndExamine(); // it rotted away meanwhile
			return;
		}
		const FRotator View = Direction.Rotation();
		const FQuat Turn = View.Quaternion() * SpecimenRotation.Quaternion();
		const FVector Center = Origin + Direction * Settings->ExamineDistanceCm;
		Specimen->SetWorldLocationAndRotation(Center - Turn.RotateVector(FVector(0.0, 0.0, 0.5 * ExaminedHeightCm)), Turn);
		return;
	}
	if (bReading || bEncyclopediaOpen)
	{
		return;
	}

	// The nearest of the mushroom and the readable sheet in sight.
	const float ReachCm = Settings->ReachM * 100.0f;
	const int32 Index = Mushrooms->FindTarget(Origin, Direction, ReachCm);
	const float MushroomDistance = Index != INDEX_NONE ? FVector::Dist(Origin, Mushrooms->GetWorldLocation(Mushrooms->GetFruitBodies()[Index])) : MAX_flt;
	float ReadableDistance = MAX_flt;
	const FName Source = FindReadable(Origin, Direction, ReachCm, ReadableDistance);
	const UKnowledgeSubsystem* Knowledge = GetKnowledge();
	const FKnowledgeSource* SourceData = Knowledge ? Knowledge->FindSource(Source) : nullptr;

	if (SourceData && ReadableDistance < MushroomDistance)
	{
		Target = ETarget::Readable;
		TargetSource = Source;
		Widget->SetPrompt(FText::Format(LOCTEXT("ReadPrompt", "{0}\n[{1}] Read"), SourceData->Title, Settings->ExamineKey.GetDisplayName()));
		return;
	}
	const FMushroomSpeciesRow* Row = Index != INDEX_NONE ? Mushrooms->FindSpecies(Mushrooms->GetFruitBodies()[Index].SpeciesId) : nullptr;
	if (!Row)
	{
		Target = ETarget::None;
		Widget->SetPrompt(FText::GetEmpty());
		return;
	}
	const FFruitBody& Fruit = Mushrooms->GetFruitBodies()[Index];
	Target = ETarget::Mushroom;
	TargetSeed = Fruit.Seed;
	const EKnowledgeLevel Level = Knowledge ? Knowledge->GetLevel(Fruit.SpeciesId) : EKnowledgeLevel::Unknown;
	Widget->SetPrompt(FText::Format(LOCTEXT("Prompt", "{0}\n[{1}] Examine    [{2}] Pick"),
		KnowledgeText::SpecimenLabel(Level, *Row, Mushrooms->GetMorphology(Fruit)), Settings->ExamineKey.GetDisplayName(), Settings->PickKey.GetDisplayName()));
}

void UForagerComponent::SetPlayerFrozen(bool bFrozen)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
		{
			Controller->SetIgnoreMoveInput(bFrozen);
			Controller->SetIgnoreLookInput(bFrozen);
		}
	}
}

void UForagerComponent::OnExamine()
{
	if (bEncyclopediaOpen)
	{
		return;
	}
	if (bReading)
	{
		EndReading();
	}
	else if (bExamining)
	{
		EndExamine();
	}
	else if (Target == ETarget::Mushroom)
	{
		BeginExamine(TargetSeed);
	}
	else if (Target == ETarget::Readable)
	{
		BeginReading(TargetSource);
	}
}

void UForagerComponent::BeginExamine(uint32 Seed)
{
	AMushroomSpawner* Mushrooms = GetSpawner();
	UMushroomMeshSubsystem* Meshes = GEngine ? GEngine->GetEngineSubsystem<UMushroomMeshSubsystem>() : nullptr;
	const int32 Index = Mushrooms ? Mushrooms->FindBySeed(Seed) : INDEX_NONE;
	if (Index == INDEX_NONE || !Meshes || !Specimen)
	{
		return;
	}
	const FFruitBody& Fruit = Mushrooms->GetFruitBodies()[Index];
	const FMushroomSpeciesRow* Row = Mushrooms->FindSpecies(Fruit.SpeciesId);
	if (!Row)
	{
		return;
	}
	// The exact individual at its exact age, in full detail.
	const FMushroomMorphology Morphology = Mushrooms->GetMorphology(Fruit);
	Specimen->SetStaticMesh(Meshes->CreateMeshForMorphology(Morphology, TEXT("SM_ExaminedSpecimen")));
	Specimen->SetWorldScale3D(FVector(Fruit.Scale));
	Specimen->SetVisibility(true);
	ExaminedHeightCm = Morphology.GetApproximateHeight() * Fruit.Scale;
	ExaminedSeed = Seed;
	SpecimenRotation = FRotator(-25.0, 180.0, 0.0); // slightly from above, facing the player
	bExamining = true;
	SetPlayerFrozen(true);

	// Examining is how a species is confirmed against a source the player owns.
	UKnowledgeSubsystem* Knowledge = GetKnowledge();
	const bool bNewlyIdentified = Knowledge && Knowledge->RecordObservation(Fruit.SpeciesId, Mushrooms->GetNowGameSeconds());
	const EKnowledgeLevel Level = Knowledge ? Knowledge->GetLevel(Fruit.SpeciesId) : EKnowledgeLevel::Unknown;
	TArray<FText> Lines;
	if (Level == EKnowledgeLevel::Identified)
	{
		Lines.Add(FText::Format(LOCTEXT("ExamineIdentified", "{0}: {1}"), FText::FromString(Row->ScientificName), KnowledgeText::EdibilityName(Row->Edibility)));
		if (bNewlyIdentified)
		{
			const TArray<const FKnowledgeSource*> Sources = Knowledge->GetState().SourcesTeaching(Fruit.SpeciesId);
			Lines.Add(FText::Format(LOCTEXT("ExamineMatch", "It matches \"{0}\": you now recognize it."), Sources.Num() > 0 ? Sources[0]->Title : FText::GetEmpty()));
		}
	}
	else
	{
		Lines.Add(LOCTEXT("ExamineUnknown", "You do not know this species: no guide you own describes it."));
	}
	Lines.Add(FText::GetEmpty());
	Lines.Append(ObservableCriteria::Describe(Morphology, *Row));

	const UForagingSettings* Settings = GetDefault<UForagingSettings>();
	Widget->SetPrompt(FText::GetEmpty());
	Widget->ShowExamine(KnowledgeText::SpecimenLabel(Level, *Row, Morphology), Lines,
		FText::Format(LOCTEXT("ExamineHint", "Mouse: turn    [{0}] Pick    [{1}] Put back"), Settings->PickKey.GetDisplayName(), Settings->ExamineKey.GetDisplayName()));
	RefreshBasket(); // basket lines name the species identified now
}

void UForagerComponent::EndExamine()
{
	bExamining = false;
	if (Specimen)
	{
		Specimen->SetVisibility(false);
		Specimen->SetStaticMesh(nullptr);
	}
	if (Widget)
	{
		Widget->HideExamine();
	}
	SetPlayerFrozen(false);
}

void UForagerComponent::BeginReading(FName SourceId)
{
	UKnowledgeSubsystem* Knowledge = GetKnowledge();
	AMushroomSpawner* Mushrooms = GetSpawner();
	const FKnowledgeSource* Source = Knowledge ? Knowledge->FindSource(SourceId) : nullptr;
	if (!Source || !Mushrooms || !Widget)
	{
		return;
	}
	TArray<FText> Lines = KnowledgeText::SourceLines(*Source, [Mushrooms](FName Id) { return Mushrooms->FindSpecies(Id); });
	if (!Knowledge->HasSource(SourceId))
	{
		// Species examined before are recognized at once.
		for (const FName Id : Knowledge->AcquireSource(SourceId))
		{
			if (const FMushroomSpeciesRow* Row = Mushrooms->FindSpecies(Id))
			{
				Lines.Add(FText::Format(LOCTEXT("ReadRecognize", "You recognize the {0} you examined before."), Row->CommonName));
			}
		}
		Lines.Add(Source->Kind == EKnowledgeSourceKind::Page ? LOCTEXT("ReadTakePage", "You take the page with you: its species are in your encyclopedia.")
			: LOCTEXT("ReadLearnPoster", "Its species are now in your encyclopedia."));
	}
	bReading = true;
	Target = ETarget::None;
	SetPlayerFrozen(true);
	const UForagingSettings* Settings = GetDefault<UForagingSettings>();
	Widget->SetPrompt(FText::GetEmpty());
	Widget->ShowExamine(Source->Title, Lines, FText::Format(LOCTEXT("ReadHint", "[{0}] Close"), Settings->ExamineKey.GetDisplayName()));
	RefreshBasket();
}

void UForagerComponent::EndReading()
{
	bReading = false;
	if (Widget)
	{
		Widget->HideExamine();
	}
	SetPlayerFrozen(false);
}

void UForagerComponent::OnEncyclopedia()
{
	if (bEncyclopediaOpen)
	{
		CloseEncyclopedia();
	}
	else if (!bExamining && !bReading)
	{
		OpenEncyclopedia();
	}
}

void UForagerComponent::OpenEncyclopedia()
{
	const UKnowledgeSubsystem* Knowledge = GetKnowledge();
	const UGameInstance* GameInstance = GetWorld()->GetGameInstance();
	const USpeciesDatabaseSubsystem* Database = GameInstance ? GameInstance->GetSubsystem<USpeciesDatabaseSubsystem>() : nullptr;
	if (!Encyclopedia || !Knowledge || !Database)
	{
		return;
	}
	// Species taught by an owned source or examined at least once; the others stay hidden.
	const FKnowledgeState& State = Knowledge->GetState();
	TArray<UEncyclopediaWidget::FPage> Pages;
	int32 UnknownNumber = 0;
	for (const FName Id : Database->GetAllSpeciesIds())
	{
		const FMushroomSpeciesRow* Row = Database->FindSpecies(Id);
		const bool bTaught = State.IsTaught(Id);
		if (!Row || (!bTaught && State.GetLevel(Id) == EKnowledgeLevel::Unknown))
		{
			continue;
		}
		const KnowledgeText::FEntry Entry = KnowledgeText::EncyclopediaEntry(Id, *Row, State, bTaught ? 0 : ++UnknownNumber);
		Pages.Add({ Entry.Title, Entry.Lines });
	}
	const int32 SpeciesCount = Pages.Num();
	if (Pages.Num() == 0)
	{
		Pages.Add({ LOCTEXT("EncyclopediaEmptyTitle", "Nothing yet"),
			{ LOCTEXT("EncyclopediaEmpty", "Examine mushrooms, and read the poster by the door of the forester's house.") } });
	}
	const UForagingSettings* Settings = GetDefault<UForagingSettings>();
	Encyclopedia->SetPages(FText::Format(LOCTEXT("EncyclopediaHeader", "Encyclopedia ({0} {0}|plural(one=species,other=species))"), FText::AsNumber(SpeciesCount)), Pages,
		FText::Format(LOCTEXT("EncyclopediaHint", "Mouse wheel: scroll    [{0}] Close"), Settings->EncyclopediaKey.GetDisplayName()));
	Encyclopedia->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (Widget)
	{
		Widget->SetPrompt(FText::GetEmpty());
	}
	bEncyclopediaOpen = true;
	Target = ETarget::None;
	SetPlayerFrozen(true);
}

void UForagerComponent::CloseEncyclopedia()
{
	bEncyclopediaOpen = false;
	if (Encyclopedia)
	{
		Encyclopedia->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetPlayerFrozen(false);
}

void UForagerComponent::OnScroll(const FInputActionValue& Value)
{
	if (bEncyclopediaOpen && Encyclopedia)
	{
		Encyclopedia->Scroll(Value.Get<float>());
	}
}

void UForagerComponent::OnTurn(const FInputActionValue& Value)
{
	if (!bExamining)
	{
		return;
	}
	const FVector2D Delta = Value.Get<FVector2D>();
	SpecimenRotation.Yaw += Delta.X * 2.0;
	SpecimenRotation.Pitch = FMath::Clamp(SpecimenRotation.Pitch + Delta.Y * 2.0, -80.0, 80.0);
}

void UForagerComponent::OnPick()
{
	AMushroomSpawner* Mushrooms = GetSpawner();
	if (!Mushrooms || bReading || bEncyclopediaOpen || (!bExamining && Target != ETarget::Mushroom))
	{
		return;
	}
	const uint32 Seed = bExamining ? ExaminedSeed : TargetSeed;
	if (bExamining)
	{
		EndExamine();
	}
	const int32 Index = Mushrooms->FindBySeed(Seed);
	const FName SpeciesId = Index != INDEX_NONE ? Mushrooms->GetFruitBodies()[Index].SpeciesId : NAME_None;
	if (Mushrooms->PickFruit(Seed))
	{
		if (UKnowledgeSubsystem* Knowledge = GetKnowledge())
		{
			Knowledge->RecordPick(SpeciesId);
		}
		Target = ETarget::None;
		RefreshBasket();
	}
}

void UForagerComponent::RefreshBasket()
{
	const UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	const UForagingSubsystem* Foraging = GameInstance ? GameInstance->GetSubsystem<UForagingSubsystem>() : nullptr;
	const UKnowledgeSubsystem* Knowledge = GetKnowledge();
	AMushroomSpawner* Mushrooms = GetSpawner();
	if (!Widget || !Foraging)
	{
		return;
	}
	// One line per identified species, the rest counted as unknown.
	TArray<FName> Order;
	TMap<FName, int32> Counts;
	int32 UnknownCount = 0;
	for (const FBasketItem& Item : Foraging->GetBasket())
	{
		if (Knowledge && Knowledge->GetLevel(Item.SpeciesId) == EKnowledgeLevel::Identified)
		{
			Order.AddUnique(Item.SpeciesId);
			++Counts.FindOrAdd(Item.SpeciesId);
		}
		else
		{
			++UnknownCount;
		}
	}
	TArray<FText> Lines;
	Lines.Add(FText::Format(LOCTEXT("Basket", "Basket: {0} {0}|plural(one=mushroom,other=mushrooms), {1} g"),
		FText::AsNumber(Foraging->GetBasket().Num()), FText::AsNumber(FMath::RoundToInt(Foraging->GetBasketWeightGrams()))));
	for (const FName Id : Order)
	{
		const FMushroomSpeciesRow* Row = Mushrooms ? Mushrooms->FindSpecies(Id) : nullptr;
		Lines.Add(FText::Format(LOCTEXT("BasketSpecies", "{0} x {1}"), FText::AsNumber(Counts[Id]), Row ? Row->CommonName : FText::FromName(Id)));
	}
	if (UnknownCount > 0 && Order.Num() > 0)
	{
		Lines.Add(FText::Format(LOCTEXT("BasketUnknown", "{0} x unknown"), FText::AsNumber(UnknownCount)));
	}
	Widget->SetBasket(FText::Join(FText::FromString(TEXT("\n")), Lines));
}

#undef LOCTEXT_NAMESPACE
