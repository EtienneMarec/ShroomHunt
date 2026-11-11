#pragma once

#include "CoreMinimal.h"

/**
 * Deterministic hashing and noise helpers.
 *
 * Every procedural system derives its randomness from these functions so that
 * the same seed always produces the same forest, weather and mushrooms, on any
 * machine and in any order of evaluation. Never use FMath::Rand* for generation.
 */
namespace ShroomHash
{
	/** Integer finalizer ("lowbias32", Chris Wellons): good avalanche, cheap. */
	FORCEINLINE uint32 Mix(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
	}

	/** Combines a running hash with one more value. */
	FORCEINLINE uint32 Combine(uint32 Seed, uint32 Value)
	{
		return Mix(Seed ^ (Value + 0x9e3779b9U + (Seed << 6) + (Seed >> 2)));
	}

	/** Hash(Seed) */
	FORCEINLINE uint32 Hash(uint32 Seed)
	{
		return Mix(Seed);
	}

	/** Hash(Seed, A, B, ...): order-sensitive hash of any number of 32-bit values. */
	template <typename... TRest>
	FORCEINLINE uint32 Hash(uint32 Seed, uint32 First, TRest... Rest)
	{
		return Hash(Combine(Seed, First), static_cast<uint32>(Rest)...);
	}

	/** Splits a 64-bit value into two 32-bit hash inputs. */
	FORCEINLINE uint32 Hash64(uint32 Seed, int64 Value)
	{
		const uint64 U = static_cast<uint64>(Value);
		return Hash(Seed, static_cast<uint32>(U & 0xffffffffULL), static_cast<uint32>(U >> 32));
	}

	/** Stable string hash (FNV-1a over UTF-16 code units). Use for ids such as species names. */
	SHROOMHUNT_API uint32 HashString(FStringView Text);

	/** Maps a hash to [0, 1). */
	FORCEINLINE float ToUnitFloat(uint32 H)
	{
		return static_cast<float>(H >> 8) * (1.0f / 16777216.0f);
	}

	/** Maps a hash to [-1, 1). */
	FORCEINLINE float ToSignedFloat(uint32 H)
	{
		return ToUnitFloat(H) * 2.0f - 1.0f;
	}

	/** Quintic fade 6t^5 - 15t^4 + 10t^3 (C2-continuous interpolation). */
	FORCEINLINE float Fade(float T)
	{
		return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
	}

	/** 1D value noise in [-1, 1], period 1 between lattice points. */
	SHROOMHUNT_API float ValueNoise1D(uint32 Seed, double X);

	/** 2D value noise in [-1, 1]. */
	SHROOMHUNT_API float ValueNoise2D(uint32 Seed, double X, double Y);

	/** Fractal sum of 1D value noise, normalized to [-1, 1]. */
	SHROOMHUNT_API float Fbm1D(uint32 Seed, double X, int32 Octaves, float Lacunarity = 2.0f, float Gain = 0.5f);

	/** Fractal sum of 2D value noise, normalized to [-1, 1]. */
	SHROOMHUNT_API float Fbm2D(uint32 Seed, double X, double Y, int32 Octaves, float Lacunarity = 2.0f, float Gain = 0.5f);
}
