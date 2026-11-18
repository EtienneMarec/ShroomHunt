#include "Mycology/MushroomSpawner.h"

#include "Components/InstancedStaticMeshComponent.h"
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
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Time/CalendarSubsystem.h"
#include "Weather/WeatherSubsystem.h"

AMushroomSpawner::AMushroomSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
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
	FieldToWorld = ForestWorld::GetFieldToWorld(GetWorld(), FVector::ZeroVector);
	UE_LOG(LogShroomSpawn, Log, TEXT("Mushroom Spawner: %d species, %d mycelium patches in %s"), Species.Num(), Patches.Num(), *ForestName);
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
	static const TCHAR* StageNames[3] = { TEXT("Young"), TEXT("Mature"), TEXT("Old") };

	TMap<FString, TArray<FTransform>> Groups;
	TMap<FString, UStaticMesh*> GroupMeshes;
	for (const FFruitBody& Fruit : FruitBodies)
	{
		const FMushroomSpeciesRow* Row = Species.Find(Fruit.SpeciesId);
		if (!Row)
		{
			continue;
		}
		const EMushroomStage Stage = Fruit.StageAt(NowGameSeconds, *Row);
		const int32 Variant = static_cast<int32>(Fruit.Seed % static_cast<uint32>(Variants));
		const FString Key = FString::Printf(TEXT("%s_%s_%d"), *Fruit.SpeciesId.ToString(), StageNames[static_cast<int32>(Stage)], Variant);
		if (!GroupMeshes.Contains(Key))
		{
			GroupMeshes.Add(Key, Meshes->GetVariantMesh(*Row, Fruit.SpeciesId, Stage, Variant));
		}
		// A slight random lean, like real mushrooms pushing through the litter.
		const float Pitch = 6.0f * ShroomHash::ToSignedFloat(ShroomHash::Hash(Fruit.Seed, 11u));
		const float Roll = 6.0f * ShroomHash::ToSignedFloat(ShroomHash::Hash(Fruit.Seed, 12u));
		const FQuat Rotation = FieldToWorld.GetRotation() * FRotator(Pitch, Fruit.YawDeg, Roll).Quaternion();
		Groups.FindOrAdd(Key).Add(FTransform(Rotation, GetWorldLocation(Fruit), FVector(Fruit.Scale)));
	}

	// Empty the instancers that have nothing to draw now, fill or create the others.
	for (TPair<FString, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : Instancers)
	{
		if (Pair.Value && !Groups.Contains(Pair.Key))
		{
			Pair.Value->ClearInstances();
		}
	}
	const int32 CullCm = FMath::RoundToInt(CullDistanceM * 100.0f);
	for (TPair<FString, TArray<FTransform>>& Group : Groups)
	{
		UStaticMesh* Mesh = GroupMeshes.FindRef(Group.Key);
		if (!Mesh)
		{
			continue;
		}
		TObjectPtr<UInstancedStaticMeshComponent>& Instancer = Instancers.FindOrAdd(Group.Key);
		if (!Instancer)
		{
			Instancer = NewObject<UInstancedStaticMeshComponent>(this, *(TEXT("ISM_") + Group.Key));
			Instancer->SetupAttachment(Root);
			Instancer->SetUsingAbsoluteLocation(true);
			Instancer->SetUsingAbsoluteRotation(true);
			Instancer->SetUsingAbsoluteScale(true);
			Instancer->SetStaticMesh(Mesh);
			Instancer->SetCollisionEnabled(ECollisionEnabled::NoCollision); // interaction comes with M1g
			Instancer->SetCullDistances(CullCm * 3 / 4, CullCm);
			Instancer->RegisterComponent();
		}
		Instancer->ClearInstances();
		Instancer->AddInstances(Group.Value, false, true);
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
