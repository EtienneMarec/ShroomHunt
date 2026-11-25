#include "Vegetation/VegetationMeshBuilder.h"

#include "Core/MeshAccumulator.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Math/RandomStream.h"
#include "Vegetation/TreeGrowth.h"

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


	// --- Trees (stylized, grown from a skeleton: D-039) ----------------------------------

	/**
	 * One leaf blade, both faces: base, rows of (left edge, raised midrib, right edge), tip.
	 * The outline follows the genus (widest point and tip from LeafTip, a sinus for lobed oak
	 * leaves), the tip bends down.
	 */
	void AddLeafBlade(FMeshAccumulator& Acc, const FTreeArchetype& Arch, const FTreeLeaf& Leaf, const FLinearColor& Front, const FLinearColor& Back)
	{
		// Evergreen leaves (holly) use the Frond surface: the seasonal material neither colors nor drops them.
		const EVegetationSurface Surface = Arch.bEvergreen ? EVegetationSurface::Frond : EVegetationSurface::Foliage;
		const FVector3d Side = FVector3d::CrossProduct(Leaf.Normal, Leaf.Axis).GetSafeNormal();
		const double Tip = FMath::Clamp(Arch.LeafTip, -1.0f, 1.0f);
		const double Peak = 0.45 - 0.17 * Tip;
		const double Exponent = FMath::Loge(0.5) / FMath::Loge(Peak);
		const double Sharpness = 0.7 + 0.5 * FMath::Max(Tip, 0.0);
		const bool bLobed = Arch.LeafLobes > 0;
		// One row (a folded diamond: 4 triangles per face) for plain leaves, two for lobed ones.
		const double PlainRows[1] = { Peak };
		static const double LobedRows[2] = { 0.38, 0.72 };
		const double* RowT = bLobed ? LobedRows : PlainRows;
		const int32 Rows = bLobed ? 2 : 1;
		constexpr double Curl = 0.12;
		const double Fold = 0.08 * Leaf.Width;

		auto At = [&](double T, double Y, double Lift)
		{
			return Leaf.Base + Leaf.Axis * (Leaf.Length * T) + Side * Y + Leaf.Normal * (Lift - Curl * Leaf.Length * T * T);
		};
		auto HalfWidth = [&](int32 Row)
		{
			const double T = RowT[Row];
			const double W = 0.5 * Leaf.Width * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * FMath::Pow(T, Exponent)), Sharpness);
			return bLobed && Row == 1 ? W * 0.75 : W;
		};
		auto Uv = [&](double T, double Y) { return FVector2f(static_cast<float>(T), static_cast<float>(0.5 + Y / FMath::Max(Leaf.Width, 1e-3))); };

		for (int32 Face = 0; Face < 2; ++Face)
		{
			const bool bBack = Face == 1;
			const FLinearColor& Color = bBack ? Back : Front;
			const int32 Base = AddVertexM(Acc, At(0.0, 0.0, 0.0), Color);
			const int32 TipId = AddVertexM(Acc, At(1.0, 0.0, 0.0), Color);
			int32 L[2], M[2], R[2];
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const double W = HalfWidth(Row);
				L[Row] = AddVertexM(Acc, At(RowT[Row], W, 0.0), Color);
				M[Row] = AddVertexM(Acc, At(RowT[Row], 0.0, Fold), Color);
				R[Row] = AddVertexM(Acc, At(RowT[Row], -W, 0.0), Color);
			}
			// Seen from the normal side (axis forward, Side to the left) these run counter-clockwise.
			auto Tri = [&](int32 A, int32 B, int32 C, const FVector2f& UA, const FVector2f& UB, const FVector2f& UC)
			{
				if (bBack)
				{
					Acc.AddTriangle(A, C, B, UA, UC, UB, Surface);
				}
				else
				{
					Acc.AddTriangle(A, B, C, UA, UB, UC, Surface);
				}
			};
			const FVector2f UvBase = Uv(0.0, 0.0);
			const FVector2f UvTip = Uv(1.0, 0.0);
			Tri(Base, M[0], L[0], UvBase, Uv(RowT[0], 0.0), Uv(RowT[0], HalfWidth(0)));
			Tri(Base, R[0], M[0], UvBase, Uv(RowT[0], -HalfWidth(0)), Uv(RowT[0], 0.0));
			for (int32 Row = 0; Row + 1 < Rows; ++Row)
			{
				const double T0 = RowT[Row];
				const double T1 = RowT[Row + 1];
				const double W0 = HalfWidth(Row);
				const double W1 = HalfWidth(Row + 1);
				Tri(M[Row], M[Row + 1], L[Row + 1], Uv(T0, 0.0), Uv(T1, 0.0), Uv(T1, W1));
				Tri(M[Row], L[Row + 1], L[Row], Uv(T0, 0.0), Uv(T1, W1), Uv(T0, W0));
				Tri(R[Row], R[Row + 1], M[Row + 1], Uv(T0, -W0), Uv(T1, -W1), Uv(T1, 0.0));
				Tri(R[Row], M[Row + 1], M[Row], Uv(T0, -W0), Uv(T1, 0.0), Uv(T0, 0.0));
			}
			const int32 Last = Rows - 1;
			const double TL = RowT[Last];
			const double WL = HalfWidth(Last);
			Tri(M[Last], TipId, L[Last], Uv(TL, 0.0), UvTip, Uv(TL, WL));
			Tri(R[Last], TipId, M[Last], Uv(TL, -WL), UvTip, Uv(TL, 0.0));
		}
	}

	void BuildTree(const FTreeArchetype& Arch, const FTreeFormDefinition& Form, uint32 Seed, FMeshAccumulator& Acc, FVegetationCollision* OutCollision)
	{
		const FTreeShape Shape = FTreeShape::Sample(Arch, Form, Seed);
		const FTreeSkeleton Skeleton = TreeGrowth::Grow(Arch, Form, Shape, Seed);
		FRandomStream Rng(static_cast<int32>(ShroomHash::Hash(Seed, 0xC1u)));
		const double H = Shape.HeightM;
		const uint32 NoiseSeed = ShroomHash::Hash(Seed, 0xBA4Cu);

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

		// Wood: trunk and branches as tapered tubes (they start inside their parent: no joint caps).
		for (const FTreeBranch& Branch : Skeleton.Branches)
		{
			const double BaseRadius = Branch.Radii[0];
			const int32 Segments = Branch.Level == 0 ? Shape.RadialSegments
				: (BaseRadius > 0.12 ? 8 : (BaseRadius > 0.05 ? 6 : (BaseRadius > 0.018 ? 4 : 3)));
			const bool bTrunk = Branch.Level == 0;
			AddTube(Acc, Branch.Points, Segments,
				[&](int32 Ring, double Theta)
				{
					const double R = Branch.Radii[Ring];
					if (!bTrunk)
					{
						return R;
					}
					const double Z = Branch.Points[Ring].Z;
					const double Flute = Arch.Fluting * FMath::Cos(5.0 * Theta + FlutePhase) * (1.0 - FMath::Clamp(Z / H, 0.0, 1.0));
					return R * (1.0 + Arch.BarkRidges * Ridge(Theta, Z) + Flute);
				},
				[&](int32 Ring, double Theta, const FVector3d& Radial) { return BarkColor(Theta, Branch.Points[Ring].Z, Radial, 1.0 + 0.04 * Branch.Level); },
				EVegetationSurface::Bark, false, true, Bark, Bark, EVegetationSurface::Bark);
		}

		// Foliage: dark masses inside the crown, then the leaves. Vertex alpha = per leaf random (leaf fall order).
		const FLinearColor Foliage = VegetationData::ToColor(Arch.Foliage, Srgb(0.2, 0.35, 0.1));
		const uint32 LeafSeed = ShroomHash::Hash(Seed, 0x1EAFu);
		auto Tint = [&](uint32 Key, float Light)
		{
			const float Shift = Arch.FoliageVariation * ShroomHash::ToSignedFloat(ShroomHash::Hash(LeafSeed, Key, 1u));
			const float GreenShift = Arch.FoliageVariation * 0.5f * ShroomHash::ToSignedFloat(ShroomHash::Hash(LeafSeed, Key, 2u));
			const FLinearColor C(
				FMath::Clamp(Foliage.R * (1.0f + Shift), 0.0f, 1.0f),
				FMath::Clamp(Foliage.G * (1.0f + Shift + GreenShift), 0.0f, 1.0f),
				FMath::Clamp(Foliage.B * (1.0f + Shift), 0.0f, 1.0f), 1.0f);
			return ScaleSRGB(C, FMath::Lerp(0.58f, 1.1f, Light));
		};
		for (int32 Index = 0; Index < Skeleton.Fills.Num(); ++Index)
		{
			const FTreeFill& Fill = Skeleton.Fills[Index];
			const uint32 BlobSeed = ShroomHash::Hash(LeafSeed, static_cast<uint32>(Index), 0xF111u);
			FLinearColor Color = Tint(0x10000u + Index, Fill.Light);
			Color.A = Fill.Random;
			AddBlob(Acc, 1,
				[&](const FVector3d& D)
				{
					const double Bump = 1.0 + 0.3 * DirectionNoise(BlobSeed, D, 1.8);
					return Fill.Center + FVector3d(D.X, D.Y, D.Z * 0.75) * (Fill.Radius * Bump);
				},
				[&](const FVector3d& D, const FVector3d&)
				{
					FLinearColor C = ScaleSRGB(Color, FMath::Lerp(0.7f, 1.0f, static_cast<float>(0.5 + 0.5 * D.Z)));
					C.A = Fill.Random;
					return C;
				},
				EVegetationSurface::Foliage);
		}
		for (int32 Index = 0; Index < Skeleton.Leaves.Num(); ++Index)
		{
			const FTreeLeaf& Leaf = Skeleton.Leaves[Index];
			FLinearColor Front = Tint(static_cast<uint32>(Index), Leaf.Light);
			// The underside is paler and duller.
			FLinearColor Back = LerpSRGB(ScaleSRGB(Front, 0.85f), Srgb(0.42, 0.46, 0.30), 0.25f);
			Front.A = Leaf.Random;
			Back.A = Leaf.Random;
			AddLeafBlade(Acc, Arch, Leaf, Front, Back);
		}

		const bool bCollides = Form.Kind == EVegetationKind::Pole || Form.Kind == EVegetationKind::Mature || Form.Kind == EVegetationKind::Old || Form.Kind == EVegetationKind::Young;
		if (OutCollision && bCollides)
		{
			const double Radius = Shape.TrunkRadiusM * 1.25;
			const double TotalLength = FMath::Max(FMath::Min(Shape.ClearBoleM, 3.5), 2.0 * Radius + 0.1);
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

	// --- Ground debris (V1-3) ------------------------------------------------------------

	/**
	 * One fallen leaf of a genus (each variant a different shape and color: flat, curled,
	 * tilted, decayed). Placed one by one around the player (PCG runtime generation), on top
	 * of the leaf carpet of the ground material. Double-sided blade, tip toward +X.
	 */
	void BuildLeaf(const FTreeArchetype& Arch, uint32 Seed, FMeshAccumulator& Acc)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		const FLinearColor Base = VegetationData::ToColor(Arch.Litter, Srgb(0.45, 0.30, 0.14));
		const FLinearColor Decayed = Srgb(0.17, 0.12, 0.07);
		constexpr int32 MaxOutline = 16;
		const int32 Outline = Arch.LeafLobes > 0 ? 16 : 10;

		{
			const double Length = Arch.LeafSizeCm / 100.0 * Rng.FRandRange(0.85f, 1.15f);
			const double Width = Length * (Arch.LeafLobes > 0 ? 0.62 : Rng.FRandRange(0.5f, 0.62f));
			const FVector3d Center = FVector3d::ZeroVector;
			const FVector3d U(1.0, 0.0, 0.0);
			const FVector3d V(0.0, 1.0, 0.0);
			const double Height = 0.006;
			const double TiltU = Rng.FRandRange(-0.15f, 0.15f);
			const double TiltV = Rng.FRandRange(-0.15f, 0.15f);
			const double Curl = Rng.FRandRange(0.1f, 0.35f);

			FLinearColor Color = ScaleSRGB(Base, Rng.FRandRange(0.75f, 1.15f));
			const float Fate = Rng.FRand();
			if (Fate < 0.2f)
			{
				Color = LerpSRGB(Color, Decayed, Rng.FRandRange(0.3f, 0.7f));
			}
			else if (Fate > 0.9f)
			{
				Color = ScaleSRGB(Color, 1.25f);
			}

			auto PointAt = [&](double X, double Y)
			{
				const double Edge = FMath::Square(Y / (0.5 * Width));
				const double Z = Height + TiltU * X + TiltV * Y + Curl * Edge * Width * 0.25;
				return Center + U * X + V * Y + FVector3d(0.0, 0.0, Z);
			};
			TArray<FVector3d, TInlineAllocator<MaxOutline>> Rim;
			for (int32 K = 0; K < Outline; ++K)
			{
				const double T = UE_DOUBLE_TWO_PI * K / Outline;
				// Egg-shaped blade, tip toward +U; oak leaves get lobes.
				double Half = 0.5 * Width * FMath::Sin(T) * (1.0 - 0.25 * FMath::Cos(T));
				if (Arch.LeafLobes > 0)
				{
					Half *= 1.0 - 0.35 * FMath::Abs(FMath::Sin(T * Arch.LeafLobes));
				}
				Rim.Add(PointAt(0.5 * Length * FMath::Cos(T), Half));
			}
			for (int32 Face = 0; Face < 2; ++Face)
			{
				const bool bBack = Face == 1;
				const FLinearColor FaceColor = bBack ? ScaleSRGB(Color, 0.7f) : Color;
				const int32 Middle = AddVertexM(Acc, PointAt(0.0, 0.0), ScaleSRGB(FaceColor, 0.92f));
				TArray<int32, TInlineAllocator<MaxOutline>> Ids;
				for (const FVector3d& P : Rim)
				{
					Ids.Add(AddVertexM(Acc, P, FaceColor));
				}
				for (int32 K = 0; K < Outline; ++K)
				{
					const int32 K1 = (K + 1) % Outline;
					const FVector2f UvC(0.5f, 0.5f);
					const FVector2f Uv0(0.5f + 0.5f * FMath::Cos(UE_TWO_PI * K / Outline), 0.5f + 0.5f * FMath::Sin(UE_TWO_PI * K / Outline));
					const FVector2f Uv1(0.5f + 0.5f * FMath::Cos(UE_TWO_PI * K1 / Outline), 0.5f + 0.5f * FMath::Sin(UE_TWO_PI * K1 / Outline));
					// The rim runs counter-clockwise seen from above: (center, k, k+1) faces up.
					if (bBack)
					{
						Acc.AddTriangle(Middle, Ids[K1], Ids[K], UvC, Uv1, Uv0, static_cast<uint8>(EVegetationSurface::Litter));
					}
					else
					{
						Acc.AddTriangle(Middle, Ids[K], Ids[K1], UvC, Uv0, Uv1, static_cast<uint8>(EVegetationSurface::Litter));
					}
				}
			}
		}
	}

	/** A few dead twigs lying on the ground, some with a side branch. */
	void BuildTwigs(uint32 Seed, FMeshAccumulator& Acc)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		const FLinearColor Bark = Srgb(0.29, 0.24, 0.18);
		const FLinearColor Wood = Srgb(0.55, 0.45, 0.31);
		const int32 Count = Rng.RandRange(2, 4);
		for (int32 T = 0; T < Count; ++T)
		{
			const double Length = Rng.FRandRange(0.3f, 0.7f);
			const double Radius = Rng.FRandRange(0.007f, 0.016f);
			const double Angle = Rng.FRandRange(0.0f, UE_TWO_PI);
			const FVector3d Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			const FVector3d Side(-Dir.Y, Dir.X, 0.0);
			const FVector3d Start = FVector3d(Rng.FRandRange(-0.2f, 0.2f), Rng.FRandRange(-0.2f, 0.2f), 0.0) - Dir * (0.5 * Length);
			const double Bend = Rng.FRandRange(-0.08f, 0.08f);
			const FLinearColor Tint = ScaleSRGB(Bark, Rng.FRandRange(0.8f, 1.2f));

			auto AddStick = [&](const FVector3d& From, const FVector3d& Direction, const FVector3d& Normal, double StickLength, double StickRadius, double StickBend)
			{
				TArray<FVector3d> Path;
				for (int32 K = 0; K <= 5; ++K)
				{
					const double S = K / 5.0;
					Path.Add(From + Direction * (StickLength * S) + Normal * (StickBend * FMath::Sin(UE_DOUBLE_PI * S)) + FVector3d(0.0, 0.0, StickRadius));
				}
				AddTube(Acc, Path, 5, [&](int32 Ring, double) { return StickRadius * (1.0 - 0.5 * Ring / 5.0); },
					[&](int32, double, const FVector3d&) { return Tint; },
					EVegetationSurface::Bark, true, true, Wood, Wood, EVegetationSurface::Wood);
			};
			AddStick(Start, Dir, Side, Length, Radius, Bend);
			if (Rng.FRand() < 0.6f)
			{
				const double Along = Rng.FRandRange(0.3f, 0.7f);
				const double Branch = Rng.FRandRange(0.5f, 0.9f) * (Rng.FRand() < 0.5f ? -1.0 : 1.0);
				const FVector3d BranchDir = (Dir + Side * Branch).GetSafeNormal();
				AddStick(Start + Dir * (Length * Along), BranchDir, FVector3d(-BranchDir.Y, BranchDir.X, 0.0),
					Length * Rng.FRandRange(0.25f, 0.45f), Radius * 0.6, Bend * 0.5);
			}
		}
	}

	/** One to three stones half sunk in the ground, mossy on top. */
	void BuildStones(uint32 Seed, FMeshAccumulator& Acc)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		const FLinearColor Rock = Srgb(0.43, 0.42, 0.39);
		const FLinearColor Moss = Srgb(0.20, 0.31, 0.08);
		const int32 Count = Rng.RandRange(1, 3);
		for (int32 S = 0; S < Count; ++S)
		{
			const double Radius = Rng.FRandRange(0.06f, 0.22f) * (S == 0 ? 1.0 : 0.6);
			const FVector3d Stretch(1.0, Rng.FRandRange(0.65f, 0.95f), Rng.FRandRange(0.45f, 0.65f));
			const double Angle = Rng.FRandRange(0.0f, UE_TWO_PI);
			const FVector3d Center = FVector3d(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * (S == 0 ? 0.0 : Rng.FRandRange(0.15f, 0.3f))
				+ FVector3d(0.0, 0.0, Radius * Stretch.Z * 0.3);
			const uint32 BlobSeed = ShroomHash::Hash(Seed, static_cast<uint32>(S), 0x570u);
			const FLinearColor Tint = ScaleSRGB(Rock, Rng.FRandRange(0.8f, 1.15f));
			const float MossShare = Rng.FRandRange(0.0f, 0.8f);
			AddBlob(Acc, Radius > 0.12 ? 2 : 1,
				[&](const FVector3d& D)
				{
					const double Bump = 1.0 + 0.22 * DirectionNoise(BlobSeed, D, 1.6);
					return Center + FVector3d(D.X * Stretch.X, D.Y * Stretch.Y, D.Z * Stretch.Z) * (Radius * Bump);
				},
				[&](const FVector3d& D, const FVector3d&)
				{
					const float Grain = DirectionNoise(BlobSeed ^ 0x9u, D, 4.0);
					const float MossAlpha = MossShare * ShroomMath::SmoothStep01(0.3f, 0.9f, static_cast<float>(D.Z)) * FMath::Clamp(0.6f + Grain, 0.0f, 1.0f);
					return LerpSRGB(ScaleSRGB(Tint, 1.0f + 0.12f * Grain), Moss, MossAlpha);
				},
				EVegetationSurface::Wood);
		}
	}

	/** Low, bumpy moss cushions. */
	void BuildMoss(uint32 Seed, FMeshAccumulator& Acc)
	{
		FRandomStream Rng(static_cast<int32>(Seed));
		const FLinearColor Dark = Srgb(0.15, 0.26, 0.06);
		const FLinearColor Light = Srgb(0.31, 0.43, 0.12);
		const int32 Count = Rng.RandRange(2, 5);
		for (int32 M = 0; M < Count; ++M)
		{
			const double Radius = Rng.FRandRange(0.12f, 0.35f);
			const double Angle = Rng.FRandRange(0.0f, UE_TWO_PI);
			const FVector3d Center = FVector3d(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Rng.FRandRange(0.0f, 0.35f) - FVector3d(0.0, 0.0, Radius * 0.05);
			const uint32 BlobSeed = ShroomHash::Hash(Seed, static_cast<uint32>(M), 0x3055u);
			AddBlob(Acc, 2,
				[&](const FVector3d& D)
				{
					const double Bump = 1.0 + 0.3 * DirectionNoise(BlobSeed, D, 3.5);
					return Center + FVector3d(D.X, D.Y, D.Z * 0.28) * (Radius * Bump);
				},
				[&](const FVector3d& D, const FVector3d&)
				{
					const float Mottle = DirectionNoise(BlobSeed ^ 0x7u, D, 6.0) * 0.5f + 0.5f;
					return ScaleSRGB(LerpSRGB(Dark, Light, Mottle), FMath::Lerp(0.55f, 1.0f, static_cast<float>(0.5 + 0.5 * D.Z)));
				},
				EVegetationSurface::Foliage);
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
	// Wide spread of crown bases: some trees keep low branches (more foliage at eye level).
	S.ClearBoleM = S.HeightM * FMath::Clamp(Archetype.ClearBole * Form.ClearBole * Rng.FRandRange(0.7f, 1.12f), 0.02f, 0.8f);
	S.CrownRadiusM = Archetype.CrownRadiusM * Form.Crown * Rng.FRandRange(0.85f, 1.15f);
	const float LeanAngle = Rng.FRandRange(0.0f, UE_TWO_PI);
	const float LeanAmount = Rng.FRandRange(0.0f, 0.04f);
	S.LeanX = LeanAmount * FMath::Cos(LeanAngle);
	S.LeanY = LeanAmount * FMath::Sin(LeanAngle);
	const int32 ClumpJitter = Rng.RandRange(-1, 2);
	S.Clumps = Form.Clumps > 0 ? FMath::Max(0, Form.Clumps + ClumpJitter) : 0;
	S.RadialSegments = S.TrunkRadiusM > 0.2f ? 14 : (S.TrunkRadiusM > 0.06f ? 10 : 6);
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
		if (Spec.Kind == EVegetationKind::Leaf)
		{
			const FTreeArchetype* Archetype = Definition.FindTree(Spec.Genus);
			if (!Archetype)
			{
				return false;
			}
			BuildLeaf(*Archetype, Seed, Acc);
		}
		else if (VegetationData::IsShrubKind(Spec.Kind))
		{
			const FTreeArchetype* Archetype = Definition.FindShrubFor(Spec.Kind);
			if (!Archetype)
			{
				return false;
			}
			// A shrub is its archetype at full size: a neutral form of its own kind (leaf budget, no collision).
			FTreeFormDefinition Form;
			Form.Kind = Spec.Kind;
			Form.Clumps = 0;
			Form.Variants = Definition.GetVariantCount(Spec.Kind);
			BuildTree(*Archetype, Form, Seed, Acc, nullptr);
		}
		else if (VegetationData::IsTreeKind(Spec.Kind))
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
			case EVegetationKind::Twigs: BuildTwigs(Seed, Acc); break;
			case EVegetationKind::Stones: BuildStones(Seed, Acc); break;
			case EVegetationKind::Moss: BuildMoss(Seed, Acc); break;
			default: return false;
			}
		}
		Acc.Emit(OutMesh);
		return OutMesh.TriangleCount() > 0;
	}
}
