#include "Player/ForagerComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ShroomLog.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Mushroom/MushroomMeshSubsystem.h"
#include "Mycology/MushroomSpawner.h"
#include "Mycology/ObservableCriteria.h"
#include "Player/ForagingSettings.h"
#include "Player/ForagingSubsystem.h"
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
	ExamineAction = NewObject<UInputAction>(this, TEXT("IA_Examine"));
	ExamineAction->ValueType = EInputActionValueType::Boolean;
	PickAction = NewObject<UInputAction>(this, TEXT("IA_Pick"));
	PickAction->ValueType = EInputActionValueType::Boolean;
	TurnAction = NewObject<UInputAction>(this, TEXT("IA_TurnSpecimen"));
	TurnAction->ValueType = EInputActionValueType::Axis2D;
	// Enhanced Input actions swallow their keys by default: let the template's
	// look/move actions (lower priority) still receive the mouse, E and F.
	ExamineAction->bConsumeInput = false;
	PickAction->bConsumeInput = false;
	TurnAction->bConsumeInput = false;
	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Foraging"));
	Mapping->MapKey(ExamineAction, Settings->ExamineKey);
	Mapping->MapKey(PickAction, Settings->PickKey);
	Mapping->MapKey(TurnAction, EKeys::Mouse2D);
	Subsystem->AddMappingContext(Mapping, 2);

	Input->BindAction(ExamineAction, ETriggerEvent::Started, this, &UForagerComponent::OnExamine);
	Input->BindAction(PickAction, ETriggerEvent::Started, this, &UForagerComponent::OnPick);
	Input->BindAction(TurnAction, ETriggerEvent::Triggered, this, &UForagerComponent::OnTurn);
}

AMushroomSpawner* UForagerComponent::GetSpawner()
{
	if (!Spawner.IsValid())
	{
		Spawner = AMushroomSpawner::Find(GetWorld());
	}
	return Spawner.Get();
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

	const int32 Index = Mushrooms->FindTarget(Origin, Direction, Settings->ReachM * 100.0f);
	bHasTarget = Index != INDEX_NONE;
	if (!bHasTarget)
	{
		Widget->SetPrompt(FText::GetEmpty());
		return;
	}
	const FFruitBody& Fruit = Mushrooms->GetFruitBodies()[Index];
	TargetSeed = Fruit.Seed;
	Widget->SetPrompt(FText::Format(LOCTEXT("Prompt", "{0}\n[{1}] Examine    [{2}] Pick"),
		ObservableCriteria::VagueLabel(Mushrooms->GetMorphology(Fruit)), Settings->ExamineKey.GetDisplayName(), Settings->PickKey.GetDisplayName()));
}

void UForagerComponent::OnExamine()
{
	if (bExamining)
	{
		EndExamine();
	}
	else if (bHasTarget)
	{
		BeginExamine(TargetSeed);
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

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
		{
			Controller->SetIgnoreMoveInput(true);
			Controller->SetIgnoreLookInput(true);
		}
	}
	const UForagingSettings* Settings = GetDefault<UForagingSettings>();
	Widget->SetPrompt(FText::GetEmpty());
	Widget->ShowExamine(ObservableCriteria::VagueLabel(Morphology), ObservableCriteria::Describe(Morphology, *Row),
		FText::Format(LOCTEXT("ExamineHint", "Mouse: turn    [{0}] Pick    [{1}] Put back"), Settings->PickKey.GetDisplayName(), Settings->ExamineKey.GetDisplayName()));
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
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
		{
			Controller->SetIgnoreMoveInput(false);
			Controller->SetIgnoreLookInput(false);
		}
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
	if (!Mushrooms)
	{
		return;
	}
	const uint32 Seed = bExamining ? ExaminedSeed : TargetSeed;
	if (!bExamining && !bHasTarget)
	{
		return;
	}
	if (bExamining)
	{
		EndExamine();
	}
	if (Mushrooms->PickFruit(Seed))
	{
		bHasTarget = false;
		RefreshBasket();
	}
}

void UForagerComponent::RefreshBasket()
{
	const UForagingSubsystem* Foraging = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UForagingSubsystem>() : nullptr;
	if (Widget && Foraging)
	{
		Widget->SetBasket(FText::Format(LOCTEXT("Basket", "Basket: {0} mushrooms, {1} g"),
			FText::AsNumber(Foraging->GetBasket().Num()), FText::AsNumber(FMath::RoundToInt(Foraging->GetBasketWeightGrams()))));
	}
}

#undef LOCTEXT_NAMESPACE
