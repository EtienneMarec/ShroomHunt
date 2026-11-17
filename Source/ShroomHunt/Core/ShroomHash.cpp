#include "Core/ShroomHash.h"

namespace ShroomHash
{
	uint32 HashString(FStringView Text)
	{
		uint32 H = 2166136261U;
		for (const TCHAR C : Text)
		{
			H ^= static_cast<uint32>(C);
			H *= 16777619U;
		}
		return Mix(H);
	}

	float ValueNoise1D(uint32 Seed, double X)
	{
		const double Floor = FMath::FloorToDouble(X);
		const int64 I = static_cast<int64>(Floor);
		const float T = Fade(static_cast<float>(X - Floor));
		const float A = ToSignedFloat(Hash64(Seed, I));
		const float B = ToSignedFloat(Hash64(Seed, I + 1));
		return FMath::Lerp(A, B, T);
	}

	float ValueNoise2D(uint32 Seed, double X, double Y)
	{
		const double FloorX = FMath::FloorToDouble(X);
		const double FloorY = FMath::FloorToDouble(Y);
		const int32 IX = static_cast<int32>(FloorX);
		const int32 IY = static_cast<int32>(FloorY);
		const float TX = Fade(static_cast<float>(X - FloorX));
		const float TY = Fade(static_cast<float>(Y - FloorY));

		auto Corner = [Seed](int32 CX, int32 CY)
		{
			return ToSignedFloat(Hash(Seed, static_cast<uint32>(CX), static_cast<uint32>(CY)));
		};

		const float Bottom = FMath::Lerp(Corner(IX, IY), Corner(IX + 1, IY), TX);
		const float Top = FMath::Lerp(Corner(IX, IY + 1), Corner(IX + 1, IY + 1), TX);
		return FMath::Lerp(Bottom, Top, TY);
	}

	float GradientNoise2D(uint32 Seed, double X, double Y)
	{
		const double FloorX = FMath::FloorToDouble(X);
		const double FloorY = FMath::FloorToDouble(Y);
		const int32 IX = static_cast<int32>(FloorX);
		const int32 IY = static_cast<int32>(FloorY);
		const float DX = static_cast<float>(X - FloorX);
		const float DY = static_cast<float>(Y - FloorY);

		// Dot product with a hashed unit gradient at each corner.
		auto Corner = [Seed](int32 CX, int32 CY, float OX, float OY)
		{
			const float Angle = ToUnitFloat(Hash(Seed, static_cast<uint32>(CX), static_cast<uint32>(CY))) * UE_TWO_PI;
			return FMath::Cos(Angle) * OX + FMath::Sin(Angle) * OY;
		};

		const float TX = Fade(DX);
		const float TY = Fade(DY);
		const float Bottom = FMath::Lerp(Corner(IX, IY, DX, DY), Corner(IX + 1, IY, DX - 1.0f, DY), TX);
		const float Top = FMath::Lerp(Corner(IX, IY + 1, DX, DY - 1.0f), Corner(IX + 1, IY + 1, DX - 1.0f, DY - 1.0f), TX);
		// Raw range is about [-0.71, 0.71].
		return FMath::Clamp(1.41f * FMath::Lerp(Bottom, Top, TY), -1.0f, 1.0f);
	}

	float GradientFbm2D(uint32 Seed, double X, double Y, int32 Octaves, float Lacunarity, float Gain)
	{
		float Sum = 0.0f;
		float Amplitude = 1.0f;
		float Norm = 0.0f;
		double Frequency = 1.0;
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			// Rotate each octave a little so lattice directions do not line up.
			const double Angle = 0.6 * Octave;
			const double RX = X * FMath::Cos(Angle) - Y * FMath::Sin(Angle);
			const double RY = X * FMath::Sin(Angle) + Y * FMath::Cos(Angle);
			Sum += Amplitude * GradientNoise2D(Hash(Seed, static_cast<uint32>(Octave)), RX * Frequency, RY * Frequency);
			Norm += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}
		return Norm > 0.0f ? Sum / Norm : 0.0f;
	}

	float Fbm1D(uint32 Seed, double X, int32 Octaves, float Lacunarity, float Gain)
	{
		float Sum = 0.0f;
		float Amplitude = 1.0f;
		float Norm = 0.0f;
		double Frequency = 1.0;
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			Sum += Amplitude * ValueNoise1D(Hash(Seed, static_cast<uint32>(Octave)), X * Frequency);
			Norm += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}
		return Norm > 0.0f ? Sum / Norm : 0.0f;
	}

	float Fbm2D(uint32 Seed, double X, double Y, int32 Octaves, float Lacunarity, float Gain)
	{
		float Sum = 0.0f;
		float Amplitude = 1.0f;
		float Norm = 0.0f;
		double Frequency = 1.0;
		for (int32 Octave = 0; Octave < Octaves; ++Octave)
		{
			Sum += Amplitude * ValueNoise2D(Hash(Seed, static_cast<uint32>(Octave)), X * Frequency, Y * Frequency);
			Norm += Amplitude;
			Amplitude *= Gain;
			Frequency *= Lacunarity;
		}
		return Norm > 0.0f ? Sum / Norm : 0.0f;
	}
}
