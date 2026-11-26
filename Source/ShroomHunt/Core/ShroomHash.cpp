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

	float GradientNoise3D(uint32 Seed, const FVector3d& P)
	{
		const FVector3d Floor(FMath::FloorToDouble(P.X), FMath::FloorToDouble(P.Y), FMath::FloorToDouble(P.Z));
		const int32 IX = static_cast<int32>(Floor.X);
		const int32 IY = static_cast<int32>(Floor.Y);
		const int32 IZ = static_cast<int32>(Floor.Z);
		const FVector3f D(P - Floor);

		// Classic improved-Perlin gradients: the 12 edge directions of a cube.
		auto Corner = [Seed](int32 CX, int32 CY, int32 CZ, float OX, float OY, float OZ)
		{
			switch (Hash(Seed, static_cast<uint32>(CX), static_cast<uint32>(CY), static_cast<uint32>(CZ)) % 12u)
			{
			case 0:  return OX + OY;
			case 1:  return -OX + OY;
			case 2:  return OX - OY;
			case 3:  return -OX - OY;
			case 4:  return OX + OZ;
			case 5:  return -OX + OZ;
			case 6:  return OX - OZ;
			case 7:  return -OX - OZ;
			case 8:  return OY + OZ;
			case 9:  return -OY + OZ;
			case 10: return OY - OZ;
			default: return -OY - OZ;
			}
		};

		const float TX = Fade(D.X);
		const float TY = Fade(D.Y);
		const float TZ = Fade(D.Z);
		auto Plane = [&](int32 DZ)
		{
			const float OZ = D.Z - DZ;
			const float Bottom = FMath::Lerp(Corner(IX, IY, IZ + DZ, D.X, D.Y, OZ), Corner(IX + 1, IY, IZ + DZ, D.X - 1.0f, D.Y, OZ), TX);
			const float Top = FMath::Lerp(Corner(IX, IY + 1, IZ + DZ, D.X, D.Y - 1.0f, OZ), Corner(IX + 1, IY + 1, IZ + DZ, D.X - 1.0f, D.Y - 1.0f, OZ), TX);
			return FMath::Lerp(Bottom, Top, TY);
		};
		return FMath::Clamp(FMath::Lerp(Plane(0), Plane(1), TZ), -1.0f, 1.0f);
	}

	void Cellular3D(uint32 Seed, const FVector3d& P, double& OutF1, double& OutF2)
	{
		const int32 IX = FMath::FloorToInt32(P.X);
		const int32 IY = FMath::FloorToInt32(P.Y);
		const int32 IZ = FMath::FloorToInt32(P.Z);
		double F1 = UE_DOUBLE_BIG_NUMBER;
		double F2 = UE_DOUBLE_BIG_NUMBER;
		for (int32 CZ = IZ - 1; CZ <= IZ + 1; ++CZ)
		{
			for (int32 CY = IY - 1; CY <= IY + 1; ++CY)
			{
				for (int32 CX = IX - 1; CX <= IX + 1; ++CX)
				{
					const uint32 H = Hash(Seed, static_cast<uint32>(CX), static_cast<uint32>(CY), static_cast<uint32>(CZ));
					const FVector3d Feature(CX + ToUnitFloat(H), CY + ToUnitFloat(Mix(H ^ 0x68bc21ebU)), CZ + ToUnitFloat(Mix(H ^ 0x02e5be93U)));
					const double Distance = FVector3d::Distance(P, Feature);
					if (Distance < F1)
					{
						F2 = F1;
						F1 = Distance;
					}
					else if (Distance < F2)
					{
						F2 = Distance;
					}
				}
			}
		}
		OutF1 = F1;
		OutF2 = F2;
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
