#include "Vegetation/VegetationMeshBuilder.h"

#include "Core/MeshAccumulator.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Math/RandomStream.h"

using namespace UE::Geometry;

namespace VegetationMeshDetail
{
	constexpr double CmPerM = 100.0;
	using ShroomMesh::LerpSRGB;
	using ShroomMesh::ScaleSRGB;

	int32 AddVertexM(FMeshAccumulator& Acc, const FVector3d& PositionM, const FLinearColor& Color)
	{
		return Acc.AddVertex(PositionM * CmPerM, Color);
	}

	FLinearColor Srgb(double R, double G, double B)
	{
		return FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), 1.0f);
	}

	// --- Unit icosphere (foliage clumps, bramble mounds) ----------------------------

	struct FUnitSphere
	{
		TArray<FVector3d> Vertices;
		TArray<FIntVector> Triangles;
	};

	FUnitSphere MakeSphere(int32 Level)
	{
		const double P = (1.0 + FMath::Sqrt(5.0)) * 0.5;
		FUnitSphere S;
		const FVector3d Base[12] = {
			FVector3d(-1, P, 0), FVector3d(1, P, 0), FVector3d(-1, -P, 0), FVector3d(1, -P, 0),
			FVector3d(0, -1, P), FVector3d(0, 1, P), FVector3d(0, -1, -P), FVector3d(0, 1, -P),
			FVector3d(P, 0, -1), FVector3d(P, 0, 1), FVector3d(-P, 0, -1), FVector3d(-P, 0, 1) };
		for (const FVector3d& V : Base)
		{
			S.Vertices.Add(V.GetSafeNormal());
		}
		const int32 Faces[20][3] = {
			{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
			{3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1} };
		for (const auto& F : Faces)
		{
			S.Triangles.Add(FIntVector(F[0], F[1], F[2]));
		}
		for (int32 L = 0; L < Level; ++L)
		{
			TMap<uint64, int32> Midpoints;
			auto Midpoint = [&S, &Midpoints](int32 A, int32 B)
			{
				const uint64 Key = (static_cast<uint64>(FMath::Min(A, B)) << 32) | static_cast<uint64>(FMath::Max(A, B));
				if (const int32* Found = Midpoints.Find(Key))
				{
					return *Found;
				}
				const int32 Index = S.Vertices.Add(((S.Vertices[A] + S.Vertices[B]) * 0.5).GetSafeNormal());
				Midpoints.Add(Key, Index);
				return Index;
			};
			TArray<FIntVector> Next;
			Next.Reserve(S.Triangles.Num() * 4);
			for (const FIntVector& T : S.Triangles)
			{
				const int32 AB = Midpoint(T.X, T.Y);
				const int32 BC = Midpoint(T.Y, T.Z);
				const int32 CA = Midpoint(T.Z, T.X);
				Next.Add(FIntVector(T.X, AB, CA));
				Next.Add(FIntVector(T.Y, BC, AB));
				Next.Add(FIntVector(T.Z, CA, BC));
				Next.Add(FIntVector(AB, BC, CA));
			}
			S.Triangles = MoveTemp(Next);
		}
		// Right-handed outward orientation for FMeshAccumulator::AddTriangle.
		for (FIntVector& T : S.Triangles)
		{
			const FVector3d& A = S.Vertices[T.X];
			const FVector3d& B = S.Vertices[T.Y];
			const FVector3d& C = S.Vertices[T.Z];
			if (FVector3d::DotProduct(FVector3d::CrossProduct(B - A, C - A), A + B + C) < 0.0)
			{
				Swap(T.Y, T.Z);
			}
		}
		return S;
	}

	const FUnitSphere& GetSphere(int32 Level)
	{
		static const FUnitSphere Spheres[3] = { MakeSphere(0), MakeSphere(1), MakeSphere(2) };
		return Spheres[FMath::Clamp(Level, 0, 2)];
	}

	/** Seam-free noise of a unit direction in [-1, 1] (two crossed 2D gradient noises). */
	float DirectionNoise(uint32 Seed, const FVector3d& D, double Frequency)
	{
		const float A = ShroomHash::GradientNoise2D(Seed, D.X * Frequency + 11.3, D.Y * Frequency - 4.1);
		const float B = ShroomHash::GradientNoise2D(Seed ^ 0x51u, D.Y * Frequency + 2.7, D.Z * Frequency + 7.9);
		return FMath::Clamp((A + B) * 0.75f, -1.0f, 1.0f);
	}

	/** A deformed sphere: Position maps a unit direction to a point (m), Color colors it. */
	void AddBlob(FMeshAccumulator& Acc, int32 Level, TFunctionRef<FVector3d(const FVector3d&)> Position,
		TFunctionRef<FLinearColor(const FVector3d& Direction, const FVector3d& PositionM)> Color, EVegetationSurface Surface)
	{
		const FUnitSphere& S = GetSphere(Level);
		const int32 First = Acc.GetVertexCount();
		TArray<FVector2f, TInlineAllocator<162>> Uvs;
		for (const FVector3d& D : S.Vertices)
		{
			const FVector3d P = Position(D);
			AddVertexM(Acc, P, Color(D, P));
			Uvs.Add(FVector2f(static_cast<float>(FMath::Atan2(D.Y, D.X) / UE_DOUBLE_TWO_PI + 0.5),
				static_cast<float>(FMath::Acos(FMath::Clamp(D.Z, -1.0, 1.0)) / UE_DOUBLE_PI)));
		}
		for (const FIntVector& T : S.Triangles)
		{
			Acc.AddTriangle(First + T.X, First + T.Y, First + T.Z, Uvs[T.X], Uvs[T.Y], Uvs[T.Z], Surface);
		}
	}

	/**
	 * A generalized cylinder along Path (m). Rings use a parallel-transport frame (N, B = T x N)
	 * so that the angle runs counter-clockwise around the tangent and normals point outward.
	 * Caps are separate vertices (hard edge), colored from CapCenter to CapRim.
	 */
	void AddTube(FMeshAccumulator& Acc, const TArray<FVector3d>& Path, int32 Segments,
		TFunctionRef<double(int32 Ring, double Theta)> Radius,
		TFunctionRef<FLinearColor(int32 Ring, double Theta, const FVector3d& RadialDir)> Color, EVegetationSurface Surface,
		bool bCapStart, bool bCapEnd, const FLinearColor& CapCenter, const FLinearColor& CapRim, EVegetationSurface CapSurface)
	{
		const int32 NumRings = Path.Num();
		if (NumRings < 2 || Segments < 3)
		{
			return;
		}

		TArray<FVector3d> Tangents;
		Tangents.SetNum(NumRings);
		for (int32 K = 0; K < NumRings; ++K)
		{
			Tangents[K] = (Path[FMath::Min(K + 1, NumRings - 1)] - Path[FMath::Max(K - 1, 0)]).GetSafeNormal();
		}

		FVector3d N = FVector3d::CrossProduct(Tangents[0], FMath::Abs(Tangents[0].Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX()).GetSafeNormal();
		TArray<TArray<int32>> Rings;
		TArray<TArray<FVector3d>> RingPositions;
		Rings.SetNum(NumRings);
		RingPositions.SetNum(NumRings);
		for (int32 K = 0; K < NumRings; ++K)
		{
			const FVector3d& T = Tangents[K];
			const FVector3d Projected = N - T * FVector3d::DotProduct(T, N);
			if (Projected.SizeSquared() > 1e-12)
			{
				N = Projected.GetSafeNormal();
			}
			const FVector3d B = FVector3d::CrossProduct(T, N);
			for (int32 J = 0; J < Segments; ++J)
			{
				const double Theta = UE_DOUBLE_TWO_PI * J / Segments;
				const FVector3d Radial = N * FMath::Cos(Theta) + B * FMath::Sin(Theta);
				const FVector3d P = Path[K] + Radial * FMath::Max(Radius(K, Theta), 1e-4);
				RingPositions[K].Add(P);
				Rings[K].Add(AddVertexM(Acc, P, Color(K, Theta, Radial)));
			}
		}

		for (int32 K = 0; K + 1 < NumRings; ++K)
		{
			const float V0 = static_cast<float>(K) / (NumRings - 1);
			const float V1 = static_cast<float>(K + 1) / (NumRings - 1);
			for (int32 J = 0; J < Segments; ++J)
			{
				const int32 J1 = (J + 1) % Segments;
				const float U0 = static_cast<float>(J) / Segments;
				const float U1 = static_cast<float>(J + 1) / Segments;
				Acc.AddQuad(Rings[K][J], Rings[K][J1], Rings[K + 1][J1], Rings[K + 1][J],
					FVector2f(U0, V0), FVector2f(U1, V0), FVector2f(U1, V1), FVector2f(U0, V1), static_cast<uint8>(Surface));
			}
		}

		auto AddCap = [&](int32 Ring, bool bEnd)
		{
			const int32 Center = AddVertexM(Acc, Path[Ring], CapCenter);
			TArray<int32, TInlineAllocator<24>> Rim;
			for (const FVector3d& P : RingPositions[Ring])
			{
				Rim.Add(AddVertexM(Acc, P, CapRim));
			}
			for (int32 J = 0; J < Segments; ++J)
			{
				const int32 J1 = (J + 1) % Segments;
				const double A0 = UE_DOUBLE_TWO_PI * J / Segments;
				const double A1 = UE_DOUBLE_TWO_PI * (J + 1) / Segments;
				const FVector2f UvC(0.5f, 0.5f);
				const FVector2f Uv0(0.5f + 0.5f * static_cast<float>(FMath::Cos(A0)), 0.5f + 0.5f * static_cast<float>(FMath::Sin(A0)));
				const FVector2f Uv1(0.5f + 0.5f * static_cast<float>(FMath::Cos(A1)), 0.5f + 0.5f * static_cast<float>(FMath::Sin(A1)));
				if (bEnd)
				{
					Acc.AddTriangle(Center, Rim[J], Rim[J1], UvC, Uv0, Uv1, CapSurface);
				}
				else
				{
					Acc.AddTriangle(Center, Rim[J1], Rim[J], UvC, Uv1, Uv0, CapSurface);
				}
			}
		};
		if (bCapStart)
		{
			AddCap(0, false);
		}
		if (bCapEnd)
		{
			AddCap(NumRings - 1, true);
		}
	}

	FVector3d QuadraticBezier(const FVector3d& A, const FVector3d& Control, const FVector3d& B, double T)
	{
		const double U = 1.0 - T;
		return A * (U * U) + Control * (2.0 * U * T) + B * (T * T);
	}

	// --- Trees ---------------------------------------------------------------------

	struct FClump
	{
		FVector3d Center = FVector3d::ZeroVector;
		double Radius = 1.0;
		/** Relative height in the crown (0 base - 1 top). */
		double CrownT = 0.5;
		FLinearColor Tint = FLinearColor::Green;
		bool bTop = false;
	};

	void BuildTree(const FTreeArchetype& Arch, const FTreeFormDefinition& Form, uint32 Seed, FMeshAccumulator& Acc, FVegetationCollision* OutCollision)
	{
		const FTreeShape Shape = FTreeShape::Sample(Arch, Form, Seed);
		FRandomStream Rng(static_cast<int32>(ShroomHash::Hash(Seed, 0xC1u)));
		const double H = Shape.HeightM;
		const double R0 = Shape.TrunkRadiusM;
		const double TrunkTop = H * 0.93;
		const uint32 NoiseSeed = ShroomHash::Hash(Seed, 0xBA4Cu);

		auto Axis = [&](double Z)
		{
			const double T = Z / H;
			const double Wobble = 0.006 * H;
			return FVector3d(
				Shape.LeanX * H * T * T + Wobble * ShroomHash::GradientNoise2D(NoiseSeed, Z / (0.25 * H), 0.5),
				Shape.LeanY * H * T * T + Wobble * ShroomHash::GradientNoise2D(NoiseSeed ^ 0x77u, Z / (0.25 * H), 3.5),
				Z);
		};
		const double FlareLength = 2.5 * R0;
		const double BreastZ = FMath::Min(1.3, TrunkTop * 0.3);
		const double TaperNorm = (1.08 - 0.96 * BreastZ / TrunkTop) * (1.0 + 0.5 * FMath::Exp(-BreastZ / FlareLength));
		auto TrunkRadius = [&](double Z)
		{
			const double Taper = 1.08 - 0.96 * FMath::Clamp(Z / TrunkTop, 0.0, 1.0);
			const double Flare = 1.0 + 0.5 * FMath::Exp(-FMath::Max(Z, 0.0) / FlareLength);
			return FMath::Max(R0 * Taper * Flare / TaperNorm, 0.004);
		};

		// Trunk: rings denser near the base where the flare changes quickly.
		const int32 TrunkRings = FMath::Clamp(FMath::RoundToInt(H / 0.6), 6, 48);
		TArray<FVector3d> TrunkPath;
		TArray<double> TrunkZ;
		for (int32 K = 0; K <= TrunkRings; ++K)
		{
			const double Z = -0.15 + (TrunkTop + 0.15) * FMath::Pow(static_cast<double>(K) / TrunkRings, 1.4);
			TrunkZ.Add(Z);
			TrunkPath.Add(Axis(Z));
		}

		const FLinearColor Bark = VegetationData::ToColor(Arch.Bark, Srgb(0.4, 0.38, 0.34));
		const bool bPatches = Arch.BarkPatch.Num() == 3 && Arch.BarkPatchShare > 0.0f;
		const FLinearColor Patch = VegetationData::ToColor(Arch.BarkPatch, Bark);
		const FLinearColor Moss = Srgb(0.20, 0.30, 0.08);
		const double FlutePhase = Rng.FRandRange(0.0f, UE_TWO_PI);

		auto Ridge = [&](double Theta, double Z)
		{
			return ShroomHash::GradientNoise2D(NoiseSeed ^ 0x3Eu, FMath::Cos(Theta) * 6.0 + Z * 0.15, FMath::Sin(Theta) * 6.0 - Z * 0.1);
		};
		auto BarkColor = [&](double Theta, double Z, const FVector3d& Radial, double Lighten)
		{
			const float Grain = ShroomHash::GradientNoise2D(NoiseSeed ^ 0x9Au, FMath::Cos(Theta) * 2.0 + Z * 0.8, FMath::Sin(Theta) * 2.0 + Z * 0.5);
			float Factor = 1.0f + Arch.BarkContrast * 2.0f * Grain + 2.5f * Arch.BarkRidges * Ridge(Theta, Z);
			FLinearColor C = ScaleSRGB(Bark, Factor * static_cast<float>(Lighten));
			if (bPatches)
			{
				// Horizontal dark marks (high frequency along the trunk) and a dark, fissured base.
				const float Mark = ShroomHash::GradientNoise2D(NoiseSeed ^ 0xB1u, FMath::Cos(Theta) * 1.5 + Z * 0.2, FMath::Sin(Theta) * 1.5 + Z * 2.5) * 0.5f + 0.5f;
				const float Base = 1.0f - ShroomMath::SmoothStep01(0.3f, 1.6f, static_cast<float>(Z / FMath::Max(H / 20.0, 0.1)));
				const float PatchAlpha = FMath::Max(Mark < Arch.BarkPatchShare ? 0.85f : 0.0f, Base * 0.8f);
				C = LerpSRGB(C, Patch, PatchAlpha);
			}
			// Moss on the lower trunk, mostly on the north side (+X).
			const float MossAlpha = (1.0f - ShroomMath::SmoothStep01(0.2f, 1.8f, static_cast<float>(Z))) * FMath::Clamp(static_cast<float>(0.3 + 0.7 * Radial.X), 0.0f, 1.0f) * 0.6f;
			return LerpSRGB(C, Moss, MossAlpha);
		};

		AddTube(Acc, TrunkPath, Shape.RadialSegments,
			[&](int32 Ring, double Theta)
			{
				const double Z = TrunkZ[Ring];
				const double Flute = Arch.Fluting * FMath::Cos(5.0 * Theta + FlutePhase) * (1.0 - FMath::Clamp(Z / H, 0.0, 1.0));
				return TrunkRadius(Z) * (1.0 + Arch.BarkRidges * Ridge(Theta, Z) + Flute);
			},
			[&](int32 Ring, double Theta, const FVector3d& Radial) { return BarkColor(Theta, TrunkZ[Ring], Radial, 1.0); },
			EVegetationSurface::Bark, false, true, Bark, Bark, EVegetationSurface::Bark);

		// Crown: clumps spread in the genus envelope, outer ones fed by a branch.
		const double CrownBase = Shape.ClearBoleM;
		const double CrownDepth = FMath::Max(H - CrownBase, 0.5);
		const double CrownR = Shape.CrownRadiusM;
		const double Peak = FMath::Clamp(Arch.CrownPeak, 0.05f, 0.95f);
		const double Exponent = FMath::Max(Arch.CrownExponent, 0.5f);
		auto Envelope = [&](double T)
		{
			const double U = FMath::Clamp(T < Peak ? (Peak - T) / Peak : (T - Peak) / (1.0 - Peak), 0.0, 1.0);
			return CrownR * FMath::Pow(FMath::Max(0.0, 1.0 - FMath::Pow(U, Exponent)), 1.0 / Exponent);
		};
		const double ClumpR = FMath::Min(Arch.ClumpScale * CrownR * 0.42, CrownDepth * 0.32);
		const FLinearColor Foliage = VegetationData::ToColor(Arch.Foliage, Srgb(0.2, 0.35, 0.1));

		TArray<FClump> Clumps;
		{
			FClump& Top = Clumps.AddDefaulted_GetRef();
			Top.Radius = ClumpR * 0.9;
			Top.Center = Axis(H - Top.Radius * 0.85);
			Top.CrownT = 1.0;
			Top.bTop = true;
		}
		const int32 Outer = FMath::Max(Shape.Clumps - 1, 1);
		for (int32 Index = 0; Index < Outer; ++Index)
		{
			FClump& C = Clumps.AddDefaulted_GetRef();
			const double T = 0.06 + 0.86 * (Index + 0.25 + 0.5 * Rng.FRand()) / Outer;
			const double Azimuth = Index * 2.39996 + Rng.FRandRange(-0.4f, 0.4f);
			C.Radius = ClumpR * Rng.FRandRange(0.8f, 1.15f);
			// Mostly on the outer shell of the envelope (a crown is hollow-ish, leaves seek light).
			const double Rho = FMath::Max(0.0, Envelope(T) - C.Radius * 0.55) * Rng.FRandRange(0.82f, 1.0f);
			const double Z = CrownBase + T * CrownDepth - Arch.Droop * Rho * 0.6;
			C.Center = Axis(Z) + FVector3d(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0.0) * Rho;
			C.CrownT = T;
		}
		for (FClump& C : Clumps)
		{
			const float Shift = Arch.FoliageVariation * Rng.FRandRange(-1.0f, 1.0f);
			const float GreenShift = Arch.FoliageVariation * 0.5f * Rng.FRandRange(-1.0f, 1.0f);
			C.Tint = FLinearColor(
				FMath::Clamp(Foliage.R * (1.0f + Shift), 0.0f, 1.0f),
				FMath::Clamp(Foliage.G * (1.0f + Shift + GreenShift), 0.0f, 1.0f),
				FMath::Clamp(Foliage.B * (1.0f + Shift), 0.0f, 1.0f), 1.0f);
		}

		// Branches (drawn before the foliage, hidden inside it at the tips).
		const int32 BranchSegments = FMath::Max(4, Shape.RadialSegments / 2);
		for (const FClump& C : Clumps)
		{
			const FVector3d AxisPoint = Axis(C.Center.Z);
			const double Rho = FVector2d(C.Center.X - AxisPoint.X, C.Center.Y - AxisPoint.Y).Size();
			if (C.bTop || Rho < 0.25 * CrownR)
			{
				continue;
			}
			const double Angle = FMath::DegreesToRadians(Arch.BranchAngleDeg + 20.0 * (C.CrownT - 0.5) + Rng.FRandRange(-8.0f, 8.0f));
			const double AttachZ = FMath::Clamp(C.Center.Z - Rho * FMath::Tan(Angle), FMath::Max(CrownBase - 0.5, 0.5), FMath::Max(C.Center.Z - 0.1, 0.6));
			const FVector3d Start = Axis(FMath::Min(AttachZ, TrunkTop - 0.2));
			const FVector3d ToClump = C.Center - Start;
			const FVector3d End = C.Center - ToClump.GetSafeNormal() * C.Radius * 0.35;
			const double Length = (End - Start).Size();
			const FVector3d Control = (Start + End) * 0.5 + FVector3d(0, 0, (0.12 - 0.25 * Arch.Droop) * Length);
			TArray<FVector3d> Path;
			for (int32 K = 0; K <= 5; ++K)
			{
				Path.Add(QuadraticBezier(Start, Control, End, K / 5.0));
			}
			const double BaseRadius = FMath::Max(TrunkRadius(Start.Z) * 0.55, 0.006);
			AddTube(Acc, Path, BranchSegments,
				[&](int32 Ring, double) { return BaseRadius * FMath::Lerp(1.0, 0.35, Ring / 5.0); },
				[&](int32 Ring, double Theta, const FVector3d& Radial) { return BarkColor(Theta, Path[Ring].Z, Radial, 1.08); },
				EVegetationSurface::Bark, false, true, Bark, Bark, EVegetationSurface::Bark);
		}

		// Foliage clumps and their smaller sub-clumps.
		const uint32 LeafSeed = ShroomHash::Hash(Seed, 0x1EAFu);
		auto AddClump = [&](const FVector3d& Center, double Radius, int32 Level, const FLinearColor& Tint, double CrownT, uint32 BlobSeed)
		{
			AddBlob(Acc, Level,
				[&](const FVector3d& D)
				{
					const double Bump = 1.0 + Arch.Lumpiness * DirectionNoise(BlobSeed, D, 1.6);
					return Center + FVector3d(D.X, D.Y, D.Z * 0.82) * (Radius * Bump);
				},
				[&](const FVector3d& D, const FVector3d& P)
				{
					const FVector3d AxisP = Axis(FMath::Clamp(P.Z, 0.0, H));
					const double Outside = FMath::Clamp(FVector2d(P.X - AxisP.X, P.Y - AxisP.Y).Size() / FMath::Max(CrownR, 0.1), 0.0, 1.0);
					const float Ao = FMath::Lerp(0.6f, 1.0f, static_cast<float>(0.5 + 0.5 * D.Z))
						* FMath::Lerp(0.78f, 1.0f, static_cast<float>(Outside))
						* FMath::Lerp(0.85f, 1.0f, static_cast<float>(CrownT));
					return ScaleSRGB(Tint, Ao * (1.0f + 0.06f * DirectionNoise(BlobSeed ^ 0x2u, D, 4.0)));
				},
				EVegetationSurface::Foliage);
		};
		for (int32 Index = 0; Index < Clumps.Num(); ++Index)
		{
			const FClump& C = Clumps[Index];
			const uint32 BlobSeed = ShroomHash::Hash(LeafSeed, static_cast<uint32>(Index));
			AddClump(C.Center, C.Radius, Shape.ClumpDetail, C.Tint, C.CrownT, BlobSeed);

			const FVector3d AxisPoint = Axis(C.Center.Z);
			const FVector3d Outward = FVector3d(C.Center.X - AxisPoint.X, C.Center.Y - AxisPoint.Y, 0.0).GetSafeNormal();
			const int32 SubClumps = Shape.ClumpDetail >= 2 ? 3 : 1;
			for (int32 Sub = 0; Sub < SubClumps; ++Sub)
			{
				const FVector3d Random(Rng.FRandRange(-1.0f, 1.0f), Rng.FRandRange(-1.0f, 1.0f), Rng.FRandRange(-0.5f, 1.0f));
				const FVector3d Dir = (Outward + FVector3d(0, 0, 0.6) + Random * 0.8).GetSafeNormal();
				AddClump(C.Center + Dir * C.Radius * 0.85, C.Radius * Rng.FRandRange(0.38f, 0.5f), Shape.ClumpDetail - 1,
					ScaleSRGB(C.Tint, Rng.FRandRange(0.95f, 1.08f)), C.CrownT, ShroomHash::Hash(BlobSeed, static_cast<uint32>(Sub) + 1u));
			}
		}

		if (OutCollision && Form.Kind != EVegetationKind::Sapling)
		{
			const double Radius = R0 * 1.25;
			const double TotalLength = FMath::Max(FMath::Min(CrownBase, 3.5), 2.0 * Radius + 0.1);
			OutCollision->bHasCapsule = true;
			OutCollision->Radius = static_cast<float>(Radius * CmPerM);
			OutCollision->Length = static_cast<float>((TotalLength - 2.0 * Radius) * CmPerM);
			OutCollision->Center = FVector(0.0, 0.0, TotalLength * 0.5 * CmPerM);
			OutCollision->Rotation = FRotator::ZeroRotator;
		}
	}

	// --- Understory ------------------------------------------------------------------

	void BuildBracken(uint32 Seed, FMeshAccumulator& Acc)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		const int32 Fronds = Rng.RandRange(7, 12);
		const double Size = Rng.FRandRange(0.7f, 1.1f);
		const FLinearColor Green = LerpSRGB(Srgb(0.20, 0.34, 0.07), Srgb(0.28, 0.40, 0.10), Rng.FRand());
		const FLinearColor Stem = Srgb(0.25, 0.27, 0.10);
		const FVector3d Up(0, 0, 1);
		constexpr int32 Steps = 10;

		for (int32 F = 0; F < Fronds; ++F)
		{
			const double Azimuth = F * 2.39996 + Rng.FRandRange(-0.3f, 0.3f);
			const FVector3d Dir(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0.0);
			const FVector3d Side(-Dir.Y, Dir.X, 0.0);
			const FVector3d Base = Dir * Rng.FRandRange(0.0f, 0.12f);
			const double Length = Size * Rng.FRandRange(0.8f, 1.2f);
			const double Height = Size * Rng.FRandRange(0.75f, 1.05f);
			const double Width = Length * 0.32;

			// Rows of (left, middle, right) points, the middle raised along the rachis.
			TArray<FVector3d> Rows[3];
			TArray<FLinearColor> RowColors;
			for (int32 I = 0; I <= Steps; ++I)
			{
				const double S = static_cast<double>(I) / Steps;
				const FVector3d Spine = Base + Dir * (Length * 0.9 * FMath::Pow(S, 1.3)) + Up * (Height * (2.0 * S - 1.25 * S * S));
				double W = S < 0.25 ? 0.012 : Width * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * (S - 0.25) / 0.75), 0.8);
				W *= (I % 2 == 1) ? 0.75 : 1.0;
				W = FMath::Max(W, 0.004);
				Rows[0].Add(Spine + Side * W - Up * 0.12 * W);
				Rows[1].Add(Spine + Up * 0.15 * W);
				Rows[2].Add(Spine - Side * W - Up * 0.12 * W);
				RowColors.Add(S < 0.25 ? Stem : ScaleSRGB(Green, static_cast<float>(0.82 + 0.25 * S)));
			}
			for (int32 Face = 0; Face < 2; ++Face)
			{
				const bool bBack = Face == 1;
				int32 Ids[3][Steps + 1];
				for (int32 Row = 0; Row < 3; ++Row)
				{
					for (int32 I = 0; I <= Steps; ++I)
					{
						Ids[Row][I] = AddVertexM(Acc, Rows[Row][I], bBack ? ScaleSRGB(RowColors[I], 0.8f) : RowColors[I]);
					}
				}
				for (int32 I = 0; I < Steps; ++I)
				{
					const float V0 = static_cast<float>(I) / Steps;
					const float V1 = static_cast<float>(I + 1) / Steps;
					for (int32 Row = 0; Row < 2; ++Row)
					{
						// Seen from above, left -> middle -> right then forward is counter-clockwise.
						const int32 A = Ids[Row][I];
						const int32 B = Ids[Row + 1][I];
						const int32 C = Ids[Row + 1][I + 1];
						const int32 D = Ids[Row][I + 1];
						const float U0 = Row * 0.5f;
						const float U1 = U0 + 0.5f;
						if (bBack)
						{
							Acc.AddQuad(A, D, C, B, FVector2f(U0, V0), FVector2f(U0, V1), FVector2f(U1, V1), FVector2f(U1, V0), static_cast<uint8>(EVegetationSurface::Frond));
						}
						else
						{
							Acc.AddQuad(A, B, C, D, FVector2f(U0, V0), FVector2f(U1, V0), FVector2f(U1, V1), FVector2f(U0, V1), static_cast<uint8>(EVegetationSurface::Frond));
						}
					}
				}
			}
		}
	}

	void BuildBramble(uint32 Seed, FMeshAccumulator& Acc)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		const FLinearColor Leaf = LerpSRGB(Srgb(0.12, 0.20, 0.06), Srgb(0.16, 0.24, 0.07), Rng.FRand());
		const FLinearColor Red = Srgb(0.24, 0.14, 0.08);
		const int32 Mounds = Rng.RandRange(2, 4);
		for (int32 M = 0; M < Mounds; ++M)
		{
			const double A = Rng.FRandRange(0.0f, UE_TWO_PI);
			const double Radius = Rng.FRandRange(0.45f, 0.85f);
			const FVector3d Center = FVector3d(FMath::Cos(A), FMath::Sin(A), 0.0) * Rng.FRandRange(0.0f, 0.5f) + FVector3d(0, 0, Radius * 0.2);
			const uint32 BlobSeed = ShroomHash::Hash(Seed, static_cast<uint32>(M));
			AddBlob(Acc, 2,
				[&](const FVector3d& D)
				{
					const double Bump = 1.0 + 0.3 * DirectionNoise(BlobSeed, D, 2.2);
					return Center + FVector3d(D.X, D.Y, D.Z * 0.55) * (Radius * Bump);
				},
				[&](const FVector3d& D, const FVector3d&)
				{
					const float Mottle = DirectionNoise(BlobSeed ^ 0x5u, D, 5.0) * 0.5f + 0.5f;
					const FLinearColor C = LerpSRGB(Leaf, Red, Mottle > 0.8f ? 0.5f : 0.0f);
					return ScaleSRGB(C, FMath::Lerp(0.6f, 1.0f, static_cast<float>(0.5 + 0.5 * D.Z)));
				},
				EVegetationSurface::Foliage);
		}

		// Arching canes rooting at their tips.
		const int32 Canes = Rng.RandRange(4, 7);
		for (int32 C = 0; C < Canes; ++C)
		{
			const double A = C * 2.39996 + Rng.FRandRange(-0.3f, 0.3f);
			const FVector3d Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			const double Length = Rng.FRandRange(1.2f, 1.8f);
			const double Height = Rng.FRandRange(0.6f, 0.9f);
			TArray<FVector3d> Path;
			for (int32 K = 0; K <= 8; ++K)
			{
				const double S = K / 8.0;
				Path.Add(Dir * (0.1 + Length * S) + FVector3d(0, 0, Height * FMath::Sin(UE_DOUBLE_PI * S) - 0.05));
			}
			AddTube(Acc, Path, 4, [](int32, double) { return 0.008; },
				[&](int32, double, const FVector3d&) { return Red; },
				EVegetationSurface::Cane, false, false, Red, Red, EVegetationSurface::Cane);
		}
	}

	void BuildLog(uint32 Seed, FMeshAccumulator& Acc, FVegetationCollision* OutCollision)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		const double Length = Rng.FRandRange(3.5f, 7.0f);
		const double Radius = Rng.FRandRange(0.15f, 0.3f);
		const double Bend = Rng.FRandRange(-0.15f, 0.15f);
		const uint32 NoiseSeed = ShroomHash::Hash(Seed, 0x106u);
		const FLinearColor Bark = Srgb(0.30, 0.27, 0.22);
		const FLinearColor Moss = Srgb(0.20, 0.32, 0.08);
		constexpr int32 Rings = 12;

		TArray<FVector3d> Path;
		for (int32 K = 0; K <= Rings; ++K)
		{
			const double S = static_cast<double>(K) / Rings;
			Path.Add(FVector3d((S - 0.5) * Length, Bend * FMath::Sin(UE_DOUBLE_PI * S), Radius * 0.8));
		}
		AddTube(Acc, Path, 12,
			[&](int32 Ring, double Theta)
			{
				const double S = static_cast<double>(Ring) / Rings;
				return Radius * (1.0 - 0.15 * S) * (1.0 + 0.06 * ShroomHash::GradientNoise2D(NoiseSeed, FMath::Cos(Theta) * 2.0 + S * 6.0, FMath::Sin(Theta) * 2.0));
			},
			[&](int32 Ring, double Theta, const FVector3d& Radial)
			{
				const double S = static_cast<double>(Ring) / Rings;
				const float Grain = ShroomHash::GradientNoise2D(NoiseSeed ^ 0x2u, FMath::Cos(Theta) * 3.0 + S * 10.0, FMath::Sin(Theta) * 3.0);
				const float MossAlpha = ShroomMath::SmoothStep01(0.1f, 0.8f, static_cast<float>(Radial.Z)) * FMath::Clamp(0.6f + 0.6f * Grain, 0.0f, 1.0f);
				return LerpSRGB(ScaleSRGB(Bark, 1.0f + 0.15f * Grain), Moss, MossAlpha);
			},
			EVegetationSurface::Bark, true, true, Srgb(0.58, 0.47, 0.32), Srgb(0.40, 0.31, 0.20), EVegetationSurface::Wood);

		if (OutCollision)
		{
			OutCollision->bHasCapsule = true;
			OutCollision->Radius = static_cast<float>(Radius * 0.9 * CmPerM);
			OutCollision->Length = static_cast<float>(FMath::Max(Length - 2.0 * Radius * 0.9, 0.0) * CmPerM);
			OutCollision->Center = FVector(0.0, 0.0, Radius * 0.8 * CmPerM);
			OutCollision->Rotation = FRotator(90.0, 0.0, 0.0); // capsule axis (Z) along X
		}
	}

	void BuildStump(uint32 Seed, FMeshAccumulator& Acc, FVegetationCollision* OutCollision)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		const double Height = Rng.FRandRange(0.25f, 0.6f);
		const double Radius = Rng.FRandRange(0.2f, 0.38f);
		const uint32 NoiseSeed = ShroomHash::Hash(Seed, 0x57u);
		const FLinearColor Bark = Srgb(0.33, 0.30, 0.25);
		const FLinearColor Moss = Srgb(0.20, 0.32, 0.08);
		constexpr int32 Rings = 6;

		TArray<FVector3d> Path;
		for (int32 K = 0; K <= Rings; ++K)
		{
			Path.Add(FVector3d(0.0, 0.0, -0.1 + (Height + 0.1) * K / Rings));
		}
		AddTube(Acc, Path, 14,
			[&](int32 Ring, double Theta)
			{
				const double Z = Path[Ring].Z;
				return Radius * (1.0 + 0.4 * FMath::Exp(-FMath::Max(Z, 0.0) / 0.12)) * (1.0 + 0.05 * ShroomHash::GradientNoise2D(NoiseSeed, FMath::Cos(Theta) * 3.0, FMath::Sin(Theta) * 3.0 + Z));
			},
			[&](int32 Ring, double Theta, const FVector3d&)
			{
				const float Grain = ShroomHash::GradientNoise2D(NoiseSeed ^ 0x3u, FMath::Cos(Theta) * 3.0, FMath::Sin(Theta) * 3.0 + Path[Ring].Z * 4.0);
				const float MossAlpha = (1.0f - ShroomMath::SmoothStep01(0.0f, Height * 0.8f, static_cast<float>(Path[Ring].Z))) * FMath::Clamp(0.5f + Grain, 0.0f, 1.0f);
				return LerpSRGB(ScaleSRGB(Bark, 1.0f + 0.15f * Grain), Moss, MossAlpha);
			},
			EVegetationSurface::Bark, false, true, Srgb(0.62, 0.52, 0.36), Srgb(0.42, 0.33, 0.21), EVegetationSurface::Wood);

		if (OutCollision)
		{
			OutCollision->bHasCapsule = true;
			OutCollision->Radius = static_cast<float>(Radius * CmPerM);
			OutCollision->Length = static_cast<float>(FMath::Max(Height - 2.0 * Radius, 0.0) * CmPerM);
			OutCollision->Center = FVector(0.0, 0.0, Height * 0.5 * CmPerM);
			OutCollision->Rotation = FRotator::ZeroRotator;
		}
	}
}

FTreeShape FTreeShape::Sample(const FTreeArchetype& Archetype, const FTreeFormDefinition& Form, uint32 Seed)
{
	FRandomStream Rng(static_cast<int32>(ShroomHash::Hash(Seed, 0x5A3Eu)));
	FTreeShape S;
	const float SizeJitter = Rng.FRandRange(0.9f, 1.1f);
	S.HeightM = Archetype.HeightM * Form.Height * SizeJitter;
	S.TrunkRadiusM = Archetype.TrunkRadiusM * Form.TrunkRadius * FMath::Sqrt(SizeJitter) * Rng.FRandRange(0.9f, 1.12f);
	S.ClearBoleM = S.HeightM * FMath::Clamp(Archetype.ClearBole * Form.ClearBole * Rng.FRandRange(0.88f, 1.12f), 0.05f, 0.8f);
	S.CrownRadiusM = Archetype.CrownRadiusM * Form.Crown * Rng.FRandRange(0.85f, 1.15f);
	const float LeanAngle = Rng.FRandRange(0.0f, UE_TWO_PI);
	const float LeanAmount = Rng.FRandRange(0.0f, 0.04f);
	S.LeanX = LeanAmount * FMath::Cos(LeanAngle);
	S.LeanY = LeanAmount * FMath::Sin(LeanAngle);
	S.Clumps = FMath::Max(3, Form.Clumps + Rng.RandRange(-1, 2));
	S.RadialSegments = S.TrunkRadiusM > 0.2f ? 14 : (S.TrunkRadiusM > 0.06f ? 10 : 6);
	S.ClumpDetail = Form.Kind == EVegetationKind::Sapling ? 1 : 2;
	return S;
}

namespace VegetationMeshBuilder
{
	// Keep these draws in the order of BuildLog / BuildStump (a test compares them with the meshes).
	void GetLogSize(uint32 Seed, float& OutLengthM, float& OutRadiusM)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		OutLengthM = Rng.FRandRange(3.5f, 7.0f);
		OutRadiusM = Rng.FRandRange(0.15f, 0.3f);
	}

	float GetStumpRadius(uint32 Seed)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		Rng.FRandRange(0.25f, 0.6f); // height
		return Rng.FRandRange(0.2f, 0.38f) * 1.4f;
	}

	bool Build(const FVegetationDefinition& Definition, const FVegetationMeshSpec& Spec, FDynamicMesh3& OutMesh, FVegetationCollision* OutCollision)
	{
		using namespace VegetationMeshDetail;
		if (OutCollision)
		{
			*OutCollision = FVegetationCollision();
		}
		FMeshAccumulator Acc;
		const uint32 Seed = Spec.GetSeed();
		if (VegetationData::IsTreeKind(Spec.Kind))
		{
			const FTreeArchetype* Archetype = Definition.FindTree(Spec.Genus);
			const FTreeFormDefinition* Form = Definition.FindForm(Spec.Kind);
			if (!Archetype || !Form)
			{
				return false;
			}
			BuildTree(*Archetype, *Form, Seed, Acc, OutCollision);
		}
		else
		{
			switch (Spec.Kind)
			{
			case EVegetationKind::Bracken: BuildBracken(Seed, Acc); break;
			case EVegetationKind::Bramble: BuildBramble(Seed, Acc); break;
			case EVegetationKind::Log: BuildLog(Seed, Acc, OutCollision); break;
			case EVegetationKind::Stump: BuildStump(Seed, Acc, OutCollision); break;
			default: return false;
			}
		}
		Acc.Emit(OutMesh);
		return OutMesh.TriangleCount() > 0;
	}
}
