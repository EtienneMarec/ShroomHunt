#include "Mycology/MushroomSpawner.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestWorld.h"
#include "Mushroom/MushroomMeshSettings.h"
#include "Mushroom/MushroomMeshSubsystem.h"
#include "Mushroom/MushroomMorphology.h"
#include "Mycology/ObservableCriteria.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Player/ForagerComponent.h"
#include "Player/ForagingSubsystem.h"
#include "Time/CalendarSubsystem.h"
#include "Vegetation/VegetationObstacles.h"
#include "Vegetation/VegetationScatter.h"
#include "Weather/WeatherSubsystem.h"

AMushroomSpawner::AMushroomSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	// Spans the whole forest (or drives it): never unloaded by World Partition streaming.
	bIsSpatiallyLoaded = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

AMushroomSpawner* AMushroomSpawner::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AMushroomSpawner> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AMushroomSpawner::BeginPlay()
{
	Super::BeginPlay();
	bInitialized = Initialize();
}

bool AMushroomSpawner::Initialize()
{
	TArray<FString> Errors;
	Field = ForestFieldCache::GetField(ForestName, Errors);
	const UGameInstance* GameInstance = GetGameInstance();
	const USpeciesDatabaseSubsystem* Database = GameInstance ? GameInstance->GetSubsystem<USpeciesDatabaseSubsystem>() : nullptr;
	if (!Field || !Database)
	{
		UE_LOG(LogShroomSpawn, Error, TEXT("Mushroom Spawner: no forest field or species database (%s)"), *FString::Join(Errors, TEXT("; ")));
		return false;
	}
	Species.Reset();
	Patches.Reset();
	for (const FName Id : Database->GetAllSpeciesIds())
	{
		if (const FMushroomSpeciesRow* Row = Database->FindSpecies(Id))
		{
			Species.Add(Id, *Row);
			SpawnModel::GeneratePatches(*Row, Id, *Field, Patches);
		}
	}
	// The solid vegetation (same deterministic placement as the PCG vegetation).
	Obstacles = MakeShared<FVegetationObstacleGrid>();
	if (const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors))
	{
		const FBox2d Whole(FVector2d::ZeroVector, FVector2d(Field->GetSizeM(), Field->GetSizeM()));
		TArray<FVegetationInstance> Instances;
		VegetationScatter::Scatter(*Field, *Vegetation, EVegetationLayer::Trees, Whole, Instances);
		VegetationScatter::Scatter(*Field, *Vegetation, EVegetationLayer::Understory, Whole, Instances);
		Obstacles->AddInstances(*Vegetation, Instances);
	}
	FieldToWorld = ForestWorld::GetFieldToWorld(GetWorld(), FVector::ZeroVector);
	UE_LOG(LogShroomSpawn, Log, TEXT("Mushroom Spawner: %d species, %d mycelium patches, %d solid obstacles in %s"), Species.Num(), Patches.Num(), Obstacles->Num(), *ForestName);
	return true;
}

TArray<FName> AMushroomSpawner::GetSpeciesIds() const
{
	TArray<FName> Ids;
	Species.GetKeys(Ids);
	Ids.Sort(FNameLexicalLess());
	return Ids;
}

FSpawnContext AMushroomSpawner::MakeContext()
{
	FSpawnContext Context;
	Context.Field = Field.Get();
	Context.TriggerCache = &TriggerCache;
	if (UForagingSubsystem* Foraging = GetGameInstance() ? GetGameInstance()->GetSubsystem<UForagingSubsystem>() : nullptr)
	{
		TWeakObjectPtr<UForagingSubsystem> WeakForaging = Foraging;
		Context.Exhaustion = [WeakForaging](uint32 PatchId, double Time) { return WeakForaging.IsValid() ? WeakForaging->GetExhaustion(PatchId, Time) : 0.0f; };
		Context.IsPicked = [WeakForaging](uint32 Seed) { return WeakForaging.IsValid() && WeakForaging->IsPicked(Seed); };
	}
	if (Obstacles.IsValid())
	{
		TSharedPtr<FVegetationObstacleGrid> Grid = Obstacles;
		Context.IsBlocked = [Grid](const FVector2f& P) { return Grid->Overlaps(P, 0.1f); };
	}
	if (const UCalendarSubsystem* Calendar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCalendarSubsystem>() : nullptr)
	{
		Context.Calendar = Calendar->GetCalendarConfig();
	}
	TWeakObjectPtr<UWeatherSubsystem> Weather = GetWorld()->GetSubsystem<UWeatherSubsystem>();
	Context.Weather.RainMm = [Weather](double From, double To) { return Weather.IsValid() ? Weather->GetRainfallMm(From, To) : 0.0f; };
	Context.Weather.MeanTemperatureC = [Weather](double From, double To) { return Weather.IsValid() ? Weather->GetMeanTemperatureC(From, To) : 15.0f; };
	return Context;
}

void AMushroomSpawner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UCalendarSubsystem* Calendar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCalendarSubsystem>() : nullptr;
	const UWeatherSubsystem* Weather = GetWorld()->GetSubsystem<UWeatherSubsystem>();
	if (!bInitialized || !Calendar)
	{
		return;
	}
	// The player forages with a component added to their pawn (no Blueprint change needed).
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (Pawn && Pawn->IsLocallyControlled() && !Pawn->FindComponentByClass<UForagerComponent>())
	{
		UForagerComponent* Forager = NewObject<UForagerComponent>(Pawn, TEXT("Forager"));
		Pawn->AddInstanceComponent(Forager);
		Forager->RegisterComponent();
	}
	// Forced weather changes the triggers of the coming days: forget the cached ones.
	const int32 Overrides = Weather ? Weather->GetOverrides().Num() : 0;
	if (Overrides != LastOverrideCount)
	{
		LastOverrideCount = Overrides;
		TriggerCache.Reset();
		LastHourIndex = MIN_int64;
	}
	// Stages and births change slowly: re-evaluate once per game hour (or after a time skip).
	const double Now = Calendar->GetTotalGameSeconds();
	const int64 Hour = static_cast<int64>(FMath::FloorToDouble(Now / 3600.0));
	if (Hour != LastHourIndex)
	{
		LastHourIndex = Hour;
		Recompute(Now);
	}
}

void AMushroomSpawner::Refresh()
{
	TriggerCache.Reset();
	LastHourIndex = MIN_int64;
	if (const UCalendarSubsystem* Calendar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCalendarSubsystem>() : nullptr)
	{
		Recompute(Calendar->GetTotalGameSeconds());
	}
}

void AMushroomSpawner::Recompute(double NowGameSeconds)
{
	if (!Field)
	{
		return;
	}
	const double Start = FPlatformTime::Seconds();
	const FSpawnContext Context = MakeContext();
	FruitBodies.Reset();
	for (const FMyceliumPatch& Patch : Patches)
	{
		if (const FMushroomSpeciesRow* Row = Species.Find(Patch.SpeciesId))
		{
			SpawnModel::FruitBodiesAt(*Row, Patch, Context, NowGameSeconds, FruitBodies);
		}
	}
	RebuildInstances(NowGameSeconds);
	UE_LOG(LogShroomSpawn, Verbose, TEXT("Mushroom Spawner: %d fruit bodies at day %.2f (%.1f ms)"), FruitBodies.Num(),
		NowGameSeconds / GameSecondsPerDay, (FPlatformTime::Seconds() - Start) * 1000.0);
}

FVector AMushroomSpawner::GetWorldLocation(const FFruitBody& Fruit) const
{
	const float Height = Field ? Field->GetHeights().Sample(Fruit.PositionM.X, Fruit.PositionM.Y) : 0.0f;
	return FieldToWorld.TransformPosition(FVector(Fruit.PositionM.X, Fruit.PositionM.Y, Height - 0.01));
}

void AMushroomSpawner::RebuildInstances(double NowGameSeconds)
{
	UMushroomMeshSubsystem* Meshes = GEngine ? GEngine->GetEngineSubsystem<UMushroomMeshSubsystem>() : nullptr;
	if (!Meshes)
	{
		return;
	}
	const int32 Variants = FMath::Max(1, GetDefault<UMushroomMeshSettings>()->VariantsPerStage);

	TMap<FString, TArray<FTransform>> Groups;
	TMap<FString, UStaticMesh*> GroupMeshes;
	for (const FFruitBody& Fruit : FruitBodies)
	{
		const FMushroomSpeciesRow* Row = Species.Find(Fruit.SpeciesId);
		if (!Row)
		{
			continue;
		}
		// Continuous growth: the mesh of the nearest growth step, scaled to the exact age.
		const float Age = FMath::Clamp(Fruit.AgeFraction(NowGameSeconds), 0.0f, 1.0f);
		const int32 Step = UMushroomMeshSubsystem::GetAgeStep(Age);
		const float GrowthScale = MushroomMorphology::GrowthFraction(*Row, Age) / MushroomMorphology::GrowthFraction(*Row, UMushroomMeshSubsystem::GetStepAge(Step));
		const int32 Variant = static_cast<int32>(Fruit.Seed % static_cast<uint32>(Variants));
		const FString Key = FString::Printf(TEXT("%s_v%d_age%d"), *Fruit.SpeciesId.ToString(), Variant, Step);
		if (!GroupMeshes.Contains(Key))
		{
			GroupMeshes.Add(Key, Meshes->GetGrowthMesh(*Row, Fruit.SpeciesId, Variant, Step));
		}
		// A slight random lean, like real mushrooms pushing through the litter.
		const float Pitch = 6.0f * ShroomHash::ToSignedFloat(ShroomHash::Hash(Fruit.Seed, 11u));
		const float Roll = 6.0f * ShroomHash::ToSignedFloat(ShroomHash::Hash(Fruit.Seed, 12u));
		const FQuat Rotation = FieldToWorld.GetRotation() * FRotator(Pitch, Fruit.YawDeg, Roll).Quaternion();
		Groups.FindOrAdd(Key).Add(FTransform(Rotation, GetWorldLocation(Fruit), FVector(Fruit.Scale * GrowthScale)));
	}

	// One component per mushroom, reused from update to update (a few hundred at most).
	// Instanced meshes are not used: vertex colors (the mushroom albedo) did not show on them.
	const float CullCm = CullDistanceM * 100.0f;
	int32 Used = 0;
	for (TPair<FString, TArray<FTransform>>& Group : Groups)
	{
		UStaticMesh* Mesh = GroupMeshes.FindRef(Group.Key);
		if (!Mesh)
		{
			continue;
		}
		for (const FTransform& Transform : Group.Value)
		{
			if (!MushroomComponents.IsValidIndex(Used))
			{
				UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
				Component->SetupAttachment(Root);
				Component->SetUsingAbsoluteLocation(true);
				Component->SetUsingAbsoluteRotation(true);
				Component->SetUsingAbsoluteScale(true);
				Component->SetMobility(EComponentMobility::Movable);
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); // picking comes with M1g
				Component->SetCastShadow(true);
				Component->LDMaxDrawDistance = CullCm;
				Component->SetCachedMaxDrawDistance(CullCm);
				Component->RegisterComponent();
				MushroomComponents.Add(Component);
			}
			UStaticMeshComponent* Component = MushroomComponents[Used++];
			Component->SetStaticMesh(Mesh);
			Component->SetWorldTransform(Transform);
			Component->SetVisibility(true);
		}
	}
	for (int32 Index = Used; Index < MushroomComponents.Num(); ++Index)
	{
		MushroomComponents[Index]->SetVisibility(false);
	}
}

float AMushroomSpawner::GetTodayTrigger(FName SpeciesId)
{
	const FMushroomSpeciesRow* Row = Species.Find(SpeciesId);
	const UCalendarSubsystem* Calendar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCalendarSubsystem>() : nullptr;
	if (!Row || !Calendar)
	{
		return 0.0f;
	}
	const FSpawnContext Context = MakeContext();
	const int32 Day = FMath::FloorToInt(Calendar->GetTotalGameSeconds() / GameSecondsPerDay);
	const double Tick = SpawnModel::GrowthTickSeconds(Day);
	float Sum = 0.0f;
	for (int32 Step = 0; Step <= 4; ++Step)
	{
		Sum += SpawnModel::Trigger(*Row, Context, Tick, FMath::Lerp(Row->LagDays.Min, Row->LagDays.Max, Step / 4.0f));
	}
	return Sum / 5.0f;
}

double AMushroomSpawner::GetNowGameSeconds() const
{
	const UCalendarSubsystem* Calendar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCalendarSubsystem>() : nullptr;
	return Calendar ? Calendar->GetTotalGameSeconds() : 0.0;
}

FMushroomMorphology AMushroomSpawner::GetMorphology(const FFruitBody& Fruit) const
{
	const FMushroomSpeciesRow* Row = Species.Find(Fruit.SpeciesId);
	if (!Row)
	{
		return FMushroomMorphology();
	}
	const int32 Variants = FMath::Max(1, GetDefault<UMushroomMeshSettings>()->VariantsPerStage);
	const int32 Variant = static_cast<int32>(Fruit.Seed % static_cast<uint32>(Variants));
	const float Age = FMath::Clamp(Fruit.AgeFraction(GetNowGameSeconds()), 0.0f, 1.0f);
	return MushroomMorphology::SampleAtAge(*Row, Fruit.SpeciesId, UMushroomMeshSubsystem::GetVariantSeed(Fruit.SpeciesId, Variant), Age);
}

int32 AMushroomSpawner::FindBySeed(uint32 Seed) const
{
	return FruitBodies.IndexOfByPredicate([Seed](const FFruitBody& Fruit) { return Fruit.Seed == Seed; });
}

int32 AMushroomSpawner::FindTarget(const FVector& Origin, const FVector& Direction, float MaxDistanceCm) const
{
	const double Now = GetNowGameSeconds();
	int32 Best = INDEX_NONE;
	double BestT = MaxDistanceCm;
	for (int32 Index = 0; Index < FruitBodies.Num(); ++Index)
	{
		const FFruitBody& Fruit = FruitBodies[Index];
		const FVector Ground = GetWorldLocation(Fruit);
		if (FVector::DistSquared(Ground, Origin) > FMath::Square(MaxDistanceCm + 50.0))
		{
			continue;
		}
		const FMushroomSpeciesRow* Row = Species.Find(Fruit.SpeciesId);
		if (!Row)
		{
			continue;
		}
		// A sphere around the whole mushroom, from its typical size and growth.
		const float Growth = MushroomMorphology::GrowthFraction(*Row, FMath::Clamp(Fruit.AgeFraction(Now), 0.0f, 1.0f)) * Fruit.Scale;
		const float Height = (Row->Stem.LengthCm.Lerp(0.6f) + 0.3f * Row->Cap.DiameterCm.Lerp(0.6f)) * Growth;
		const float Radius = FMath::Max3(0.5f * Row->Cap.DiameterCm.Lerp(0.6f) * Growth, 0.5f * Height, 4.0f);
		const FVector Center = Ground + FVector(0.0, 0.0, 0.5 * Height);
		const FVector ToCenter = Center - Origin;
		const double Along = FVector::DotProduct(ToCenter, Direction);
		const double Miss = (ToCenter - Direction * Along).SizeSquared();
		if (Along > 0.0 && Along < BestT && Miss < FMath::Square(Radius * 1.25))
		{
			BestT = Along;
			Best = Index;
		}
	}
	return Best;
}

bool AMushroomSpawner::PickFruit(uint32 Seed)
{
	const int32 Index = FindBySeed(Seed);
	UForagingSubsystem* Foraging = GetGameInstance() ? GetGameInstance()->GetSubsystem<UForagingSubsystem>() : nullptr;
	if (Index == INDEX_NONE || !Foraging)
	{
		return false;
	}
	const FFruitBody& Fruit = FruitBodies[Index];
	const double Now = GetNowGameSeconds();
	FBasketItem Item;
	Item.SpeciesId = Fruit.SpeciesId;
	Item.Seed = Fruit.Seed;
	Item.PatchId = Fruit.PatchId;
	Item.AgeAtPick = FMath::Clamp(Fruit.AgeFraction(Now), 0.0f, 1.0f);
	Item.PickGameSeconds = Now;
	Item.WeightGrams = ObservableCriteria::EstimateWeightGrams(GetMorphology(Fruit)) * Fruit.Scale * Fruit.Scale * Fruit.Scale;
	Foraging->RecordPick(Item);
	Recompute(Now);
	return true;
}
