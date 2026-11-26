#include "Environment/GroundTextures.h"

#include "Algo/Accumulate.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Vegetation/VegetationDefinition.h"

namespace GroundTexturesDetail
{
	struct FRgb
	{
		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;

		FRgb operator*(float F) const { return { R * F, G * F, B * F }; }
		FRgb operator+(const FRgb& O) const { return { R + O.R, G + O.G, B + O.B }; }
	};

	FRgb Lerp(const FRgb& A, const FRgb& B, float T)
	{
		return { FMath::Lerp(A.R, B.R, T), FMath::Lerp(A.G, B.G, T), FMath::Lerp(A.B, B.B, T) };
	}

	FRgb ToRgb(const TArray<float>& Values, const FRgb& Fallback)
	{
		return Values.Num() == 3 ? FRgb{ Values[0], Values[1], Values[2] } : Fallback;
	}

	/** Gradient noise on a lattice that wraps every Period cells: tiles seamlessly. Roughly in [-1, 1]. */
	float PeriodicNoise(uint32 Seed, double X, double Y, int32 Period)
	{
		const double FX = FMath::Floor(X);
		const double FY = FMath::Floor(Y);
		const int32 IX = static_cast<int32>(FX);
		const int32 IY = static_cast<int32>(FY);
		const double TX = X - FX;
		const double TY = Y - FY;
		auto Gradient = [Seed, Period](int32 CX, int32 CY, double DX, double DY)
		{
			const uint32 WX = static_cast<uint32>(((CX % Period) + Period) % Period);
			const uint32 WY = static_cast<uint32>(((CY % Period) + Period) % Period);
			const float Angle = UE_TWO_PI * ShroomHash::ToUnitFloat(ShroomHash::Hash(Seed, WX, WY));
			return FMath::Cos(Angle) * DX + FMath::Sin(Angle) * DY;
		};
		auto Fade = [](double T) { return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); };
		const double N00 = Gradient(IX, IY, TX, TY);
		const double N10 = Gradient(IX + 1, IY, TX - 1.0, TY);
		const double N01 = Gradient(IX, IY + 1, TX, TY - 1.0);
		const double N11 = Gradient(IX + 1, IY + 1, TX - 1.0, TY - 1.0);
		const double U = Fade(TX);
		const double V = Fade(TY);
		return static_cast<float>(FMath::Lerp(FMath::Lerp(N00, N10, U), FMath::Lerp(N01, N11, U), V) * 1.4);
	}

	/** Fractal sum of periodic noise at texture coordinates U, V in [0, 1), first octave with BasePeriod cells. */
	float TileFbm(uint32 Seed, double U, double V, int32 BasePeriod, int32 Octaves)
	{
		float Sum = 0.0f;
		float Amplitude = 1.0f;
		float Norm = 0.0f;
		int32 Period = BasePeriod;
		for (int32 O = 0; O < Octaves; ++O)
		{
			Sum += Amplitude * PeriodicNoise(ShroomHash::Hash(Seed, static_cast<uint32>(O)), U * Period, V * Period, Period);
			Norm += Amplitude;
			Amplitude *= 0.5f;
			Period *= 2;
		}
		return Sum / Norm;
	}

	/** Color and height buffers of one tile; every draw wraps around the edges. */
	class FGroundCanvas
	{
	public:
		explicit FGroundCanvas(int32 InSize) : Size(InSize)
		{
			Color.SetNum(Size * Size);
			Height.SetNum(Size * Size);
			Relief.SetNum(Size * Size);
			Covered.Init(false, Size * Size);
		}

		int32 Index(int32 X, int32 Y) const
		{
			return ((Y % Size + Size) % Size) * Size + ((X % Size + Size) % Size);
		}

		/** Calls Shade(DX, DY, Pixel) for every pixel of the square of radius R around (CX, CY), wrapped. */
		template <typename TShade>
		void ForEachAround(double CX, double CY, double R, TShade&& Shade)
		{
			const int32 X0 = FMath::FloorToInt(CX - R);
			const int32 X1 = FMath::CeilToInt(CX + R);
			const int32 Y0 = FMath::FloorToInt(CY - R);
			const int32 Y1 = FMath::CeilToInt(CY + R);
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					Shade(X + 0.5 - CX, Y + 0.5 - CY, Index(X, Y));
				}
			}
		}

		int32 Size;
		TArray<FRgb> Color;
		/** Exaggerated height for the normal map: crisp leaf edges and pebbles in the shading. */
		TArray<float> Height;
		/** Physical height for the displacement and the height blend (units of ReliefRangeCm, 0.4 = ground). */
		TArray<float> Relief;
		/** Physical height of the bare ground, before the objects. */
		TArray<float> GroundRelief;
		TBitArray<> Covered;
		/** Relief units per centimeter (1 / ReliefRangeCm). */
		float PerCm = 0.1f;
	};

	/** Ground level of the relief (the landscape material's displacement Center). */
	constexpr float ReliefGround = 0.4f;

	struct FLeafShape
	{
		FRgb Color;
		double LengthPx = 30.0;
		double WidthRatio = 0.6;
		double Peak = 0.45;
		double Sharpness = 1.0;
		int32 Lobes = 0;
	};

	void DrawLeaf(FGroundCanvas& Canvas, const FLeafShape& Shape, double CX, double CY, double Angle, float Brightness, float Curl, float Lift, float CurlCm)
	{
		const double Exponent = FMath::Loge(0.5) / FMath::Loge(Shape.Peak);
		const double Cos = FMath::Cos(Angle);
		const double Sin = FMath::Sin(Angle);
		const double Length = Shape.LengthPx;
		const double Width = Length * Shape.WidthRatio;
		const double Rib = FMath::Max(0.8, Width * 0.04);
		Canvas.ForEachAround(CX, CY, Length * 0.55, [&](double DX, double DY, int32 Pixel)
		{
			const double A = DX * Cos + DY * Sin;
			const double B = -DX * Sin + DY * Cos;
			const double U = A / Length + 0.5;
			if (U <= 0.0 || U >= 1.0)
			{
				return;
			}
			double Half = 0.5 * Width * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * FMath::Pow(U, Exponent)), Shape.Sharpness);
			if (Shape.Lobes > 0)
			{
				Half *= 1.0 - 0.35 * FMath::Abs(FMath::Sin(U * UE_DOUBLE_PI * Shape.Lobes));
			}
			const double AbsB = FMath::Abs(B);
			if (AbsB > Half || Half < 0.5)
			{
				return;
			}
			const float Edge = static_cast<float>(AbsB / Half);
			float Shade = Brightness * (1.02f - 0.12f * Edge);
			if (Edge > 0.86f)
			{
				Shade *= 0.8f; // dark rim
			}
			if (AbsB < Rib && U > 0.04)
			{
				Shade *= 1.12f; // midrib
			}
			// Lateral veins running forward from the midrib.
			const double Vein = FMath::Frac(U * 7.0 - AbsB / Length * 6.0);
			if (Vein < 0.1 && Edge > 0.15f && Edge < 0.85f)
			{
				Shade *= 0.93f;
			}
			Canvas.Color[Pixel] = Shape.Color * Shade;
			Canvas.Height[Pixel] = Lift + Curl * Edge * Edge;
			// A leaf is a sheet: it adds ~1.5 mm on what lies below, its edges curl up a few mm.
			Canvas.Relief[Pixel] += (0.15f + CurlCm * Edge * Edge) * Canvas.PerCm;
			Canvas.Covered[Pixel] = true;
		});
	}

	void DrawDome(FGroundCanvas& Canvas, double CX, double CY, double RX, double RY, double Angle, const FRgb& Color, float Lift, float Bulge, float Rim, uint32 NoiseSeed, float DomeCm)
	{
		const double Cos = FMath::Cos(Angle);
		const double Sin = FMath::Sin(Angle);
		Canvas.ForEachAround(CX, CY, FMath::Max(RX, RY) + 1.0, [&](double DX, double DY, int32 Pixel)
		{
			const double A = (DX * Cos + DY * Sin) / RX;
			const double B = (-DX * Sin + DY * Cos) / RY;
			const double D2 = A * A + B * B;
			if (D2 >= 1.0)
			{
				return;
			}
			const float Dome = static_cast<float>(FMath::Sqrt(1.0 - D2));
			const float Speckle = 1.0f + 0.12f * PeriodicNoise(NoiseSeed, DX * 0.4, DY * 0.4, 1 << 20);
			const float Shade = (Dome < Rim ? 0.75f : 1.0f) * Speckle;
			Canvas.Color[Pixel] = Color * Shade;
			Canvas.Height[Pixel] = FMath::Max(Canvas.Height[Pixel], Lift + Bulge * Dome);
			Canvas.Relief[Pixel] = FMath::Max(Canvas.Relief[Pixel], Canvas.GroundRelief[Pixel] + DomeCm * Dome * Canvas.PerCm);
			Canvas.Covered[Pixel] = true;
		});
	}

	void DrawStroke(FGroundCanvas& Canvas, double X0, double Y0, double Angle, double Bend, double LengthPx, double WidthPx, const FRgb& Root, const FRgb& Tip, float Lift, float Rise, float LiftCm, float RiseCm)
	{
		const int32 Steps = FMath::Max(2, FMath::CeilToInt(LengthPx * 1.5));
		for (int32 K = 0; K <= Steps; ++K)
		{
			const double S = static_cast<double>(K) / Steps;
			const double A = Angle + Bend * S;
			const double CX = X0 + FMath::Cos(A) * LengthPx * S;
			const double CY = Y0 + FMath::Sin(A) * LengthPx * S;
			const double R = FMath::Max(0.5 * WidthPx * (1.0 - 0.8 * S), 0.45);
			const FRgb Color = Lerp(Root, Tip, static_cast<float>(S));
			const float H = Lift + Rise * static_cast<float>(S);
			const float ReliefCm = LiftCm + RiseCm * static_cast<float>(S);
			Canvas.ForEachAround(CX, CY, R + 0.5, [&](double DX, double DY, int32 Pixel)
			{
				if (DX * DX + DY * DY <= R * R)
				{
					Canvas.Color[Pixel] = Color;
					Canvas.Height[Pixel] = FMath::Max(Canvas.Height[Pixel], H);
					Canvas.Relief[Pixel] = FMath::Max(Canvas.Relief[Pixel], Canvas.GroundRelief[Pixel] + ReliefCm * Canvas.PerCm);
					Canvas.Covered[Pixel] = true;
				}
			});
		}
	}

	/**
	 * Random draws from the hash of a counter: FRandomStream's successive values are correlated (a
	 * linear congruential generator), which showed as a repeating pattern of the scattered objects.
	 */
	struct FGroundRng
	{
		explicit FGroundRng(uint32 InSeed) : Seed(InSeed) {}

		float FRand() { return ShroomHash::ToUnitFloat(ShroomHash::Hash(Seed, Counter++)); }
		float FRandRange(float Min, float Max) { return FMath::Lerp(Min, Max, FRand()); }
		uint32 Next() { return ShroomHash::Hash(Seed, Counter++, 0x9Eu); }

		uint32 Seed;
		uint32 Counter = 0;
	};

	/** Box blur of a wrapping square buffer (separable, Passes x radius): softens the relief steps. */
	void BlurWrapped(TArray<float>& Values, int32 Size, int32 Radius, int32 Passes)
	{
		TArray<float> Temp;
		Temp.SetNumUninitialized(Values.Num());
		const float Norm = 1.0f / (2 * Radius + 1);
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					float Sum = 0.0f;
					for (int32 K = -Radius; K <= Radius; ++K)
					{
						Sum += Values[Y * Size + (X + K + Size) % Size];
					}
					Temp[Y * Size + X] = Sum * Norm;
				}
			}
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					float Sum = 0.0f;
					for (int32 K = -Radius; K <= Radius; ++K)
					{
						Sum += Temp[((Y + K + Size) % Size) * Size + X];
					}
					Values[Y * Size + X] = Sum * Norm;
				}
			}
		}
	}

	int32 Count(float PerM2, float TileM)
	{
		return FMath::Max(0, FMath::RoundToInt(PerM2 * TileM * TileM));
	}
}

namespace GroundTextures
{
	FString GetDefaultPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Environment"), TEXT("GroundTextures.json")));
	}

	bool Parse(const FString& JsonText, FGroundTexturesDefinition& Out, TArray<FString>& OutErrors)
	{
		const int32 ErrorsBefore = OutErrors.Num();
		FText FailReason;
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &Out, 0, 0, false, &FailReason))
		{
			OutErrors.Add(TEXT("GroundTextures.json: ") + FailReason.ToString());
			return false;
		}
		if (!FMath::IsPowerOfTwo(Out.Resolution) || Out.Resolution < 64 || Out.Resolution > 4096)
		{
			OutErrors.Add(TEXT("GroundTextures.json: Resolution must be a power of two in [64, 4096]"));
		}
		if (Out.Layers.Num() == 0)
		{
			OutErrors.Add(TEXT("GroundTextures.json: Layers is empty"));
		}
		TSet<FName> Names;
		for (const FGroundLayerStyle& Style : Out.Layers)
		{
			const FString Field = TEXT("GroundTextures.json: Layers.") + Style.Layer.ToString();
			bool bDuplicate = false;
			Names.Add(Style.Layer, &bDuplicate);
			if (Style.Layer.IsNone() || bDuplicate)
			{
				OutErrors.Add(Field + TEXT(": missing or duplicate Layer name"));
			}
			if (Style.Base.Num() != 3 || Style.TileM < 0.2f)
			{
				OutErrors.Add(Field + TEXT(": Base [r, g, b] and TileM >= 0.2 required"));
			}
			for (const TArray<float>* Color : { &Style.Mottle, &Style.Pebble, &Style.Blade, &Style.MossTip })
			{
				if (Color->Num() != 0 && Color->Num() != 3)
				{
					OutErrors.Add(Field + TEXT(": colors are [r, g, b]"));
				}
			}
			if (Style.LeavesPerM2 < 0.0f || Style.PebblesPerM2 < 0.0f || Style.BladesPerM2 < 0.0f || Style.MossBumpsPerM2 < 0.0f
				|| Style.TwigsPerM2 < 0.0f || Style.Puddles < 0.0f || Style.Puddles > 0.9f)
			{
				OutErrors.Add(Field + TEXT(": densities >= 0 and Puddles in [0, 0.9] required"));
			}
		}
		return OutErrors.Num() == ErrorsBefore;
	}

	bool Load(FGroundTexturesDefinition& Out, TArray<FString>& OutErrors)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
		{
			OutErrors.Add(GetDefaultPath() + TEXT(": cannot read file"));
			return false;
		}
		return Parse(Text, Out, OutErrors);
	}

	FGroundTextureImage Generate(const FGroundTexturesDefinition& Definition, const FGroundLayerStyle& Style, const FVegetationDefinition& Vegetation)
	{
		using namespace GroundTexturesDetail;
		const int32 Size = Definition.Resolution;
		const double PxPerM = Size / FMath::Max(Style.TileM, 0.2f);
		const uint32 Seed = ShroomHash::Hash(ShroomHash::HashString(Style.Layer.ToString()), 0x6A0Du);
		FGroundRng Rng(Seed);
		FGroundCanvas Canvas(Size);
		const float GroundPerCm = 1.0f / FMath::Max(Definition.ReliefRangeCm, 0.5f);
		// Objects (leaves, pebbles, blades, bumps) get the layer's stylized exaggeration.
		Canvas.PerCm = GroundPerCm * FMath::Max(Style.ReliefScale, 0.0f);

		// Ground: base color mottled by large noise, fine grain, gentle relief.
		const FRgb Base = ToRgb(Style.Base, { 0.3f, 0.2f, 0.1f });
		const FRgb Mottle = ToRgb(Style.Mottle, Base);
		const int32 LargePeriod = FMath::Max(2, FMath::RoundToInt(Style.TileM / 0.8f));
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const double U = (X + 0.5) / Size;
				const double V = (Y + 0.5) / Size;
				const float Large = TileFbm(Seed ^ 0x11u, U, V, LargePeriod, 3) * 0.5f + 0.5f;
				const float Fine = TileFbm(Seed ^ 0x22u, U, V, LargePeriod * 16, 3);
				const float MottleAlpha = Style.MottleAmount * ShroomMath::SmoothStep01(0.35f, 0.7f, Large);
				const int32 Pixel = Y * Size + X;
				Canvas.Color[Pixel] = Lerp(Base, Mottle, MottleAlpha) * (1.0f + Style.Grain * Fine);
				Canvas.Height[Pixel] = 0.25f + 0.12f * Large + 0.04f * Fine;
				// Gentle mounds and hollows (leaf piles), a few millimeters of grain.
				Canvas.Relief[Pixel] = ReliefGround + (Style.ReliefCm * (2.0f * Large - 1.0f) + 0.15f * Fine) * GroundPerCm;
			}
		}
		Canvas.GroundRelief = Canvas.Relief;

		// Moss cushions: many small bumps, tips lighter.
		const FRgb MossTip = ToRgb(Style.MossTip, Base * 1.3f);
		for (int32 I = 0, N = Count(Style.MossBumpsPerM2, Style.TileM); I < N; ++I)
		{
			const double R = PxPerM * Rng.FRandRange(0.006f, 0.016f);
			const FRgb Color = Lerp(Base, MossTip, Rng.FRandRange(0.3f, 1.0f));
			DrawDome(Canvas, Rng.FRand() * Size, Rng.FRand() * Size, R, R * Rng.FRandRange(0.8f, 1.2f), 0.0, Color, 0.3f, 0.25f, 0.25f, Rng.Next(),
				static_cast<float>(R / PxPerM * 100.0 * 0.8));
		}

		// Grass blades seen from above: tapered strokes, dark at the root.
		const FRgb Blade = ToRgb(Style.Blade, { 0.3f, 0.42f, 0.12f });
		for (int32 I = 0, N = Count(Style.BladesPerM2, Style.TileM); I < N; ++I)
		{
			const double Length = PxPerM * Style.BladeLengthCm / 100.0 * Rng.FRandRange(0.6f, 1.3f);
			const FRgb Tip = Blade * Rng.FRandRange(0.8f, 1.25f);
			const FRgb Root = Lerp(Base, Tip, 0.35f);
			DrawStroke(Canvas, Rng.FRand() * Size, Rng.FRand() * Size, Rng.FRandRange(0.0f, UE_TWO_PI), Rng.FRandRange(-0.6f, 0.6f),
				Length, FMath::Max(1.6, PxPerM * 0.004), Root, Tip, 0.35f, 0.4f, 0.1f, 0.6f);
		}

		// Pebbles: rounded domes with a dark rim.
		const FRgb Pebble = ToRgb(Style.Pebble, { 0.5f, 0.48f, 0.44f });
		for (int32 I = 0, N = Count(Style.PebblesPerM2, Style.TileM); I < N; ++I)
		{
			const double R = PxPerM * Style.PebbleSizeCm / 200.0 * Rng.FRandRange(0.5f, 1.4f);
			const FRgb Color = Lerp(Pebble, Base, Rng.FRandRange(0.0f, 0.35f)) * Rng.FRandRange(0.8f, 1.2f);
			DrawDome(Canvas, Rng.FRand() * Size, Rng.FRand() * Size, R, R * Rng.FRandRange(0.6f, 0.95f), Rng.FRandRange(0.0f, UE_TWO_PI),
				Color, 0.3f, 0.6f, 0.18f, Rng.Next(), static_cast<float>(R / PxPerM * 100.0 * 0.7));
		}

		// Twigs: thin, slightly bent brown strokes.
		for (int32 I = 0, N = Count(Style.TwigsPerM2, Style.TileM); I < N; ++I)
		{
			const FRgb Wood = FRgb{ 0.30f, 0.23f, 0.16f } * Rng.FRandRange(0.8f, 1.2f);
			DrawStroke(Canvas, Rng.FRand() * Size, Rng.FRand() * Size, Rng.FRandRange(0.0f, UE_TWO_PI), Rng.FRandRange(-0.3f, 0.3f),
				PxPerM * Rng.FRandRange(0.08f, 0.3f), FMath::Max(1.5, PxPerM * Rng.FRandRange(0.004f, 0.009f)), Wood, Wood * 1.1f, 0.6f, 0.0f, 0.5f, 0.0f);
		}

		// Fallen leaves of the stand's genera, the latest on top.
		TArray<FLeafShape> Shapes;
		TArray<float> Weights;
		for (const TPair<FName, float>& Entry : Definition.LeafGenera)
		{
			if (const FTreeArchetype* Tree = Vegetation.FindTree(Entry.Key))
			{
				FLeafShape& Shape = Shapes.AddDefaulted_GetRef();
				Shape.Color = ToRgb(Tree->Litter, { 0.45f, 0.30f, 0.14f });
				Shape.LengthPx = PxPerM * Tree->LeafSizeCm / 100.0 * Style.LeafScale;
				Shape.WidthRatio = FMath::Clamp(Tree->LeafWidth, 0.3f, 0.9f);
				Shape.Peak = 0.45 - 0.17 * FMath::Clamp(Tree->LeafTip, -1.0f, 1.0f);
				Shape.Sharpness = 0.7 + 0.5 * FMath::Max(Tree->LeafTip, 0.0f);
				Shape.Lobes = Tree->LeafLobes;
				Weights.Add(FMath::Max(Entry.Value, 0.0f));
			}
		}
		const FRgb Decayed{ 0.17f, 0.12f, 0.07f };
		const int32 LeafCount = Shapes.Num() > 0 ? Count(Style.LeavesPerM2, Style.TileM) : 0;
		for (int32 I = 0; I < LeafCount; ++I)
		{
			float Pick = Rng.FRand() * Algo::Accumulate(Weights, 0.0f);
			int32 Which = 0;
			while (Which + 1 < Weights.Num() && (Pick -= Weights[Which]) > 0.0f)
			{
				++Which;
			}
			FLeafShape Shape = Shapes[Which];
			Shape.LengthPx *= Rng.FRandRange(0.8f, 1.2f);
			// Hue and brightness variation; some leaves dark and decayed.
			Shape.Color = FRgb{ Shape.Color.R * Rng.FRandRange(0.9f, 1.12f), Shape.Color.G * Rng.FRandRange(0.88f, 1.08f), Shape.Color.B };
			if (Rng.FRand() < Style.LeafDecay)
			{
				Shape.Color = Lerp(Shape.Color, Decayed, Rng.FRandRange(0.35f, 0.75f));
			}
			// Older leaves lie lower in the pile: darker, flatter.
			const float Depth = static_cast<float>(I) / FMath::Max(LeafCount, 1);
			DrawLeaf(Canvas, Shape, Rng.FRand() * Size, Rng.FRand() * Size, Rng.FRandRange(0.0f, UE_TWO_PI),
				Rng.FRandRange(0.8f, 1.15f) * FMath::Lerp(0.78f, 1.0f, Depth), Rng.FRandRange(0.03f, 0.12f), 0.4f + 0.25f * Depth, Rng.FRandRange(0.1f, 0.4f));
		}

		// Puddles: a flat, dark water film over the low spots.
		if (Style.Puddles > 0.0f)
		{
			const float Threshold = 0.5f - Style.Puddles; // the noise is roughly uniform around 0.5
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const float N = TileFbm(Seed ^ 0x33u, (X + 0.5) / Size, (Y + 0.5) / Size, LargePeriod, 3) * 0.5f + 0.5f;
					const float Water = 1.0f - ShroomMath::SmoothStep01(Threshold, Threshold + 0.06f, N);
					if (Water <= 0.0f)
					{
						continue;
					}
					const int32 Pixel = Y * Size + X;
					Canvas.Color[Pixel] = Lerp(Canvas.Color[Pixel], Canvas.Color[Pixel] * 0.5f + FRgb{ 0.02f, 0.025f, 0.03f }, Water * 0.85f);
					Canvas.Height[Pixel] = FMath::Lerp(Canvas.Height[Pixel], 0.2f, Water);
					// Flat water about a centimeter below the ground.
					Canvas.Relief[Pixel] = FMath::Lerp(Canvas.Relief[Pixel], ReliefGround - 1.0f * Canvas.PerCm, Water);
				}
			}
		}

		// The displacement turns sharp steps into walls with stretched texture: soften the physical relief
		// over about a centimeter (the normal map keeps the crisp edges).
		const int32 BlurRadius = FMath::Max(1, FMath::RoundToInt(0.004 * PxPerM));
		BlurWrapped(Canvas.Relief, Size, BlurRadius, 2);

		// Every layer's mean surface at ground level: no step where two layers meet, and a fair height
		// blend (litter, the highest pile, otherwise won every border and hid the grass).
		double Mean = 0.0;
		for (const float R : Canvas.Relief)
		{
			Mean += R;
		}
		const float Shift = ReliefGround - static_cast<float>(Mean / Canvas.Relief.Num());
		for (float& R : Canvas.Relief)
		{
			R += Shift;
		}

		FGroundTextureImage Image;
		Image.Size = Size;
		Image.CoveredShare = static_cast<float>(Canvas.Covered.CountSetBits()) / Canvas.Covered.Num();
		Image.Albedo.SetNum(Size * Size);
		Image.Normal.SetNum(Size * Size);
		const float Strength = 6.0f * Style.NormalStrength * Size / 1024.0f;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 Pixel = Y * Size + X;
				const FRgb& C = Canvas.Color[Pixel];
				const float H = FMath::Clamp(Canvas.Relief[Pixel], 0.0f, 1.0f);
				Image.Albedo[Pixel] = FColor(
					static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(C.R * 255.0f), 0, 255)),
					static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(C.G * 255.0f), 0, 255)),
					static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(C.B * 255.0f), 0, 255)),
					static_cast<uint8>(FMath::RoundToInt(H * 255.0f)));
				// Tangent space, DirectX convention (green toward +V, i.e. down the image).
				const float DX = (Canvas.Height[Canvas.Index(X + 1, Y)] - Canvas.Height[Canvas.Index(X - 1, Y)]) * 0.5f;
				const float DY = (Canvas.Height[Canvas.Index(X, Y + 1)] - Canvas.Height[Canvas.Index(X, Y - 1)]) * 0.5f;
				const FVector3f N = FVector3f(-DX * Strength, -DY * Strength, 1.0f).GetSafeNormal();
				Image.Normal[Pixel] = FColor(
					static_cast<uint8>(FMath::RoundToInt((N.X * 0.5f + 0.5f) * 255.0f)),
					static_cast<uint8>(FMath::RoundToInt((N.Y * 0.5f + 0.5f) * 255.0f)),
					static_cast<uint8>(FMath::RoundToInt((N.Z * 0.5f + 0.5f) * 255.0f)), 255);
			}
		}
		return Image;
	}

	TArray<FColor> MakePreview(const FGroundTextureImage& Image)
	{
		TArray<FColor> Out;
		const int32 Size = Image.Size;
		Out.SetNum(Size * Size);
		const FVector3f Light = FVector3f(-0.5f, -0.5f, 0.7f).GetSafeNormal();
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 Source = ((Y * 2) % Size) * Size + (X * 2) % Size;
				const FColor& A = Image.Albedo[Source];
				const FColor& NC = Image.Normal[Source];
				const FVector3f N(NC.R / 127.5f - 1.0f, NC.G / 127.5f - 1.0f, NC.B / 127.5f - 1.0f);
				const float Shade = 0.4f + 0.7f * FMath::Max(0.0f, FVector3f::DotProduct(N, Light));
				Out[Y * Size + X] = FColor(
					static_cast<uint8>(FMath::Clamp(A.R * Shade, 0.0f, 255.0f)),
					static_cast<uint8>(FMath::Clamp(A.G * Shade, 0.0f, 255.0f)),
					static_cast<uint8>(FMath::Clamp(A.B * Shade, 0.0f, 255.0f)), 255);
			}
		}
		return Out;
	}
}
