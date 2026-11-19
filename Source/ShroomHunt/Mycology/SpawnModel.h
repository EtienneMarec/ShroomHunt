#pragma once

#include "CoreMinimal.h"
#include "Mycology/MushroomSpeciesTypes.h"
#include "Time/TimeTypes.h"

class FForestField;
struct FForestSample;

/** A persistent "good spot" where the mycelium of a species lives (Docs/SpawnModel.md section 3). */
struct SHROOMHUNT_API FMyceliumPatch
{
	FName SpeciesId;
	uint32 Id = 0;
	FVector2f PositionM = FVector2f::ZeroVector;
	float RadiusM = 5.0f;
	/** Static suitability at the patch, in [0, 1]. */
	float Strength = 0.0f;
};

/** One mushroom (fruit body) standing in the forest. */
struct SHROOMHUNT_API FFruitBody
{
	FName SpeciesId;
	uint32 PatchId = 0;
	/** Individual seed (shape variant, morphology of the inspected specimen). */
	uint32 Seed = 0;
	FVector2f PositionM = FVector2f::ZeroVector;
	float YawDeg = 0.0f;
	float Scale = 1.0f;
	double BirthGameSeconds = 0.0;
	double LifetimeGameSeconds = 1.0;

	/** 0 at birth, 1 when it rots away. */
	float AgeFraction(double NowGameSeconds) const { return static_cast<float>((NowGameSeconds - BirthGameSeconds) / FMath::Max(LifetimeGameSeconds, 1.0)); }
	bool IsAlive(double NowGameSeconds) const { return NowGameSeconds >= BirthGameSeconds && NowGameSeconds < BirthGameSeconds + LifetimeGameSeconds; }
	EMushroomStage StageAt(double NowGameSeconds, const FMushroomSpeciesRow& Species) const;
};

/** Rain and temperature over game-time windows (the weather model in game, synthetic weather in tests). */
struct SHROOMHUNT_API FSpawnWeather
{
	TFunction<float(double FromGameSeconds, double ToGameSeconds)> RainMm;
	TFunction<float(double FromGameSeconds, double ToGameSeconds)> MeanTemperatureC;
};

/** Everything the dynamic part of the model reads. */
struct SHROOMHUNT_API FSpawnContext
{
	const FForestField* Field = nullptr;
	FCalendarConfig Calendar;
	FSpawnWeather Weather;
	/**
	 * Optional memo of trigger values (species, day, lag bucket -> F). The trigger integrates the
	 * weather over several days; patches of one species share it. Clear it when the weather
	 * changes (debug overrides).
	 */
	TMap<uint64, float>* TriggerCache = nullptr;
	/** Optional: true where something solid stands (tree trunk, log, stump); no mushroom there. */
	TFunction<bool(const FVector2f& PositionM)> IsBlocked;
	/** Optional: patch exhaustion E in [0, 1] at a game time, from the player's past picks. */
	TFunction<float(uint32 PatchId, double GameSeconds)> Exhaustion;
	/** Optional: true for a fruit body the player already picked (by its seed). */
	TFunction<bool(uint32 FruitSeed)> IsPicked;
};

/** Diagnostics of one flush roll (debug HUD and tests). */
struct SHROOMHUNT_API FFlushRoll
{
	int32 Day = 0;
	float Trigger = 0.0f;
	float Probability = 0.0f;
	bool bFlush = false;
	int32 Fruits = 0;
};

/**
 * The mushroom spawn model of Docs/SpawnModel.md, as pure deterministic functions:
 * static suitability -> persistent mycelium patches -> daily flush roll from lagged weather ->
 * fruit bodies. Live fruit bodies are a function of (forest, weather, time): nothing is
 * stored, the same weather always gives the same mushrooms.
 */
namespace SpawnModel
{
	/** Hour of the daily growth tick. */
	inline constexpr double GrowthTickHour = 4.0;

	// --- Static suitability (section 2) -------------------------------------------------------

	/** Host factor H: tree hosts (mycorrhizal, parasitic), dead wood (saprotrophs) or open grass. */
	SHROOMHUNT_API float HostFactor(const FMushroomSpeciesRow& Species, const FForestField& Field, const FForestSample& Sample);

	/** S_s(p) in [0, 1]; 0 on water, path surfaces and landmark clearances. */
	SHROOMHUNT_API float Suitability(const FMushroomSpeciesRow& Species, FName SpeciesId, const FForestField& Field, float XM, float YM);

	// --- Patches (section 3) ------------------------------------------------------------------

	/** Deterministic patches of one species over the whole forest. */
	SHROOMHUNT_API void GeneratePatches(const FMushroomSpeciesRow& Species, FName SpeciesId, const FForestField& Field, TArray<FMyceliumPatch>& Out);

	// --- Dynamic trigger and flushes (sections 4 and 5) ------------------------------------------

	/** Game seconds of the growth tick of game day Day. */
	SHROOMHUNT_API double GrowthTickSeconds(int32 Day);

	/** F_s(t): phenology x lagged rain x lagged temperature, for a lag in real (biological) days. */
	SHROOMHUNT_API float Trigger(const FMushroomSpeciesRow& Species, const FSpawnContext& Context, double TickGameSeconds, float LagRealDays);

	/** P_flush = (1 - exp(-k S F)) (1 - E). */
	SHROOMHUNT_API float FlushProbability(const FMushroomSpeciesRow& Species, float Strength, float TriggerValue, float Exhaustion);

	/** Poisson draw from a uniform hash (inverse transform; Lambda up to ~50). */
	SHROOMHUNT_API int32 PoissonFromUniform(float Lambda, float U);

	/**
	 * The patch's roll on game day Day, ignoring cooldown. A flush also needs no raw flush in the
	 * cooldown days before it: cooldown without stored history (stateless, so any time can be
	 * evaluated directly and the answer never changes).
	 */
	SHROOMHUNT_API FFlushRoll RawRoll(const FMushroomSpeciesRow& Species, const FMyceliumPatch& Patch, const FSpawnContext& Context, int32 Day);

	/** Fruit bodies of a patch alive at NowGameSeconds. Rolls (if given) receives every effective flush considered. */
	SHROOMHUNT_API void FruitBodiesAt(const FMushroomSpeciesRow& Species, const FMyceliumPatch& Patch, const FSpawnContext& Context, double NowGameSeconds,
		TArray<FFruitBody>& Out, TArray<FFlushRoll>* Rolls = nullptr);
}
