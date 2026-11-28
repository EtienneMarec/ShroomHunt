#include "Forest/LandmarkMeshBuilder.h"

#include "Algo/Reverse.h"
#include "Core/MeshAccumulator.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "DynamicMesh/DynamicMesh3.h"

using namespace UE::Geometry;

namespace LandmarkMeshDetail
{
	constexpr double Cm = 100.0;

	/** UV1 surface ids. M_Vegetation tints id 2 (foliage) with the season: landmarks never use 2 or 3. */
	enum class ESurface : uint8 { Stone = 0, Wood = 1, Plaster = 4, Roof = 5, Metal = 6, Paint = 7 };

	FLinearColor Srgb(double R, double G, double B)
	{
		return FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), 1.0f);
	}

	/** A flat convex polygon (meters), oriented so that its normal points along Outward. */
	void AddFace(FMeshAccumulator& Acc, TArray<FVector3d> Points, const FVector3d& Outward, const FLinearColor& Color, ESurface Surface)
	{
		if (Points.Num() < 3)
		{
			return;
		}
		const FVector3d Normal = FVector3d::CrossProduct(Points[1] - Points[0], Points[2] - Points[0]);
		if (FVector3d::DotProduct(Normal, Outward) < 0.0)
		{
			Algo::Reverse(Points);
		}
		TArray<int32, TInlineAllocator<8>> Ids;
		for (const FVector3d& P : Points)
		{
			Ids.Add(Acc.AddVertex(P * Cm, Color));
		}
		for (int32 Index = 1; Index + 1 < Ids.Num(); ++Index)
		{
			Acc.AddTriangle(Ids[0], Ids[Index], Ids[Index + 1], FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), static_cast<uint8>(Surface));
		}
	}

	/** Axis-aligned box (meters) with flat faces; optionally recorded as collision. */
	void AddBox(FMeshAccumulator& Acc, const FVector3d& Center, const FVector3d& Half, const FLinearColor& Color, ESurface Surface, FLandmarkCollision* Collision = nullptr)
	{
		const FVector3d Axes[3] = { FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ() };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FVector3d U = Axes[(Axis + 1) % 3] * Half[(Axis + 1) % 3];
			const FVector3d V = Axes[(Axis + 2) % 3] * Half[(Axis + 2) % 3];
			for (const double Sign : { 1.0, -1.0 })
			{
				const FVector3d N = Axes[Axis] * Sign;
				const FVector3d C = Center + N * Half[Axis];
				// Slight shading per face orientation makes the shapes readable with flat colors.
				const float Shade = Axis == 2 ? (Sign > 0.0 ? 1.0f : 0.6f) : 0.88f;
				AddFace(Acc, { C - U - V, C + U - V, C + U + V, C - U + V }, N, ShroomMesh::ScaleSRGB(Color, Shade), Surface);
			}
		}
		if (Collision)
		{
			Collision->Boxes.Add(FBox((Center - Half) * Cm, (Center + Half) * Cm));
		}
	}

	/** Closed cylinder between two points (meters). */
	void AddCylinder(FMeshAccumulator& Acc, const FVector3d& A, const FVector3d& B, double Radius, int32 Segments, const FLinearColor& Side, const FLinearColor& Cap, ESurface Surface)
	{
		const FVector3d T = (B - A).GetSafeNormal();
		const FVector3d N = FVector3d::CrossProduct(T, FMath::Abs(T.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX()).GetSafeNormal();
		const FVector3d Bn = FVector3d::CrossProduct(T, N);
		TArray<FVector3d> RingA;
		TArray<FVector3d> RingB;
		for (int32 J = 0; J < Segments; ++J)
		{
			const double Angle = UE_DOUBLE_TWO_PI * J / Segments;
			const FVector3d Offset = (N * FMath::Cos(Angle) + Bn * FMath::Sin(Angle)) * Radius;
			RingA.Add(A + Offset);
			RingB.Add(B + Offset);
		}
		for (int32 J = 0; J < Segments; ++J)
		{
			const int32 J1 = (J + 1) % Segments;
			const FVector3d Outward = ((RingA[J] + RingA[J1]) * 0.5 - A).GetSafeNormal();
			AddFace(Acc, { RingA[J], RingA[J1], RingB[J1], RingB[J] }, Outward, Side, Surface);
		}
		AddFace(Acc, RingA, -T, Cap, Surface);
		AddFace(Acc, RingB, T, Cap, Surface);
	}

	/** Gable roof prism: eaves rectangle at Z0 (half sizes HalfX across, HalfY along the ridge), ridge Height above. */
	void AddGableRoof(FMeshAccumulator& Acc, const FVector3d& Center, double HalfX, double HalfY, double Height, const FLinearColor& Color)
	{
		const double Z = Center.Z;
		const FVector3d E[4] = {
			Center + FVector3d(HalfX, -HalfY, 0), Center + FVector3d(HalfX, HalfY, 0),
			Center + FVector3d(-HalfX, HalfY, 0), Center + FVector3d(-HalfX, -HalfY, 0) };
		const FVector3d R0 = FVector3d(Center.X, Center.Y - HalfY, Z + Height);
		const FVector3d R1 = FVector3d(Center.X, Center.Y + HalfY, Z + Height);
		AddFace(Acc, { E[0], E[1], R1, R0 }, FVector3d(Height, 0, HalfX).GetSafeNormal(), Color, ESurface::Roof);
		AddFace(Acc, { E[2], E[3], R0, R1 }, FVector3d(-Height, 0, HalfX).GetSafeNormal(), Color, ESurface::Roof);
		AddFace(Acc, { E[3], E[0], R0 }, -FVector3d::UnitY(), ShroomMesh::ScaleSRGB(Color, 0.8f), ESurface::Roof);
		AddFace(Acc, { E[1], E[2], R1 }, FVector3d::UnitY(), ShroomMesh::ScaleSRGB(Color, 0.8f), ESurface::Roof);
		AddFace(Acc, { E[0], E[1], E[2], E[3] }, -FVector3d::UnitZ(), ShroomMesh::ScaleSRGB(Color, 0.5f), ESurface::Roof);
	}

	/** Brightness factor of a face from its normal: tops lit, sides a little darker, undersides dark. */
	float FaceShade(const FVector3d& Normal)
	{
		return Normal.Z > 0.5 ? 1.0f : (Normal.Z < -0.5 ? 0.6f : 0.88f);
	}

	/** Color variation in [-1, 1] at a point (meters), stable across panels that share an edge. */
	float Mottle(uint32 Seed, const FVector3d& P, double Scale)
	{
		return ShroomHash::GradientNoise3D(Seed, P * Scale) * 0.7f + ShroomHash::GradientNoise3D(Seed + 17u, P * Scale * 3.1) * 0.3f;
	}

	/**
	 * A flat rectangle (meters) from Corner along U and V, split in cells about CellM wide whose vertices are
	 * mottled (Noise) and darkened near the ground (Grime, over the 0.9 m above GroundZ): flat walls looked
	 * like painted cardboard next to the vegetation.
	 */
	void AddPanel(FMeshAccumulator& Acc, const FVector3d& Corner, const FVector3d& U, const FVector3d& V, const FVector3d& Outward,
		const FLinearColor& Color, ESurface Surface, double CellM, float Noise, float Grime, double GroundZ, uint32 Seed)
	{
		const int32 NU = FMath::Clamp(FMath::CeilToInt(U.Size() / CellM), 1, 48);
		const int32 NV = FMath::Clamp(FMath::CeilToInt(V.Size() / CellM), 1, 48);
		const float Shade = FaceShade(Outward);
		TArray<int32> Ids;
		Ids.SetNum((NU + 1) * (NV + 1));
		for (int32 J = 0; J <= NV; ++J)
		{
			for (int32 I = 0; I <= NU; ++I)
			{
				const FVector3d Pos = Corner + U * (static_cast<double>(I) / NU) + V * (static_cast<double>(J) / NV);
				const float Dirt = Grime * (1.0f - ShroomMath::SmoothStep01(0.0f, 0.9f, static_cast<float>(Pos.Z - GroundZ)));
				const float Factor = Shade * (1.0f + Noise * Mottle(Seed, Pos, 2.3)) * (1.0f - Dirt);
				Ids[J * (NU + 1) + I] = Acc.AddVertex(Pos * Cm, ShroomMesh::ScaleSRGB(Color, Factor));
			}
		}
		const bool bFlip = FVector3d::DotProduct(FVector3d::CrossProduct(U, V), Outward) < 0.0;
		for (int32 J = 0; J < NV; ++J)
		{
			for (int32 I = 0; I < NU; ++I)
			{
				const int32 A = Ids[J * (NU + 1) + I];
				const int32 B = Ids[J * (NU + 1) + I + 1];
				const int32 C = Ids[(J + 1) * (NU + 1) + I + 1];
				const int32 D = Ids[(J + 1) * (NU + 1) + I];
				if (bFlip)
				{
					Acc.AddTriangle(A, C, B, FVector2f(0, 0), FVector2f(1, 1), FVector2f(1, 0), static_cast<uint8>(Surface));
					Acc.AddTriangle(A, D, C, FVector2f(0, 0), FVector2f(0, 1), FVector2f(1, 1), static_cast<uint8>(Surface));
				}
				else
				{
					Acc.AddTriangle(A, B, C, FVector2f(0, 0), FVector2f(1, 0), FVector2f(1, 1), static_cast<uint8>(Surface));
					Acc.AddTriangle(A, C, D, FVector2f(0, 0), FVector2f(1, 1), FVector2f(0, 1), static_cast<uint8>(Surface));
				}
			}
		}
	}

	/** Axis-aligned box (meters) whose faces are mottled panels (see AddPanel); optionally recorded as collision. */
	void AddPanelBox(FMeshAccumulator& Acc, const FVector3d& Center, const FVector3d& Half, const FLinearColor& Color, ESurface Surface,
		double CellM, float Noise, float Grime, double GroundZ, uint32 Seed, FLandmarkCollision* Collision = nullptr)
	{
		const FVector3d Axes[3] = { FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ() };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FVector3d U = Axes[(Axis + 1) % 3] * Half[(Axis + 1) % 3];
			const FVector3d V = Axes[(Axis + 2) % 3] * Half[(Axis + 2) % 3];
			for (const double Sign : { 1.0, -1.0 })
			{
				const FVector3d N = Axes[Axis] * Sign;
				AddPanel(Acc, Center + N * Half[Axis] - U - V, U * 2.0, V * 2.0, N, Color, Surface, CellM, Noise, Grime, GroundZ, Seed);
			}
		}
		if (Collision)
		{
			Collision->Boxes.Add(FBox((Center - Half) * Cm, (Center + Half) * Cm));
		}
	}

	/** Box (meters) along three orthonormal axes, flat faces shaded by orientation. */
	void AddOrientedBox(FMeshAccumulator& Acc, const FVector3d& Center, const FVector3d& X, const FVector3d& Y, const FVector3d& Z,
		const FVector3d& Half, const FLinearColor& Color, ESurface Surface)
	{
		const FVector3d Axes[3] = { X * Half.X, Y * Half.Y, Z * Half.Z };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FVector3d U = Axes[(Axis + 1) % 3];
			const FVector3d V = Axes[(Axis + 2) % 3];
			for (const double Sign : { 1.0, -1.0 })
			{
				const FVector3d N = Axes[Axis] * Sign;
				const FVector3d C = Center + N;
				const FVector3d Out = N.GetSafeNormal();
				AddFace(Acc, { C - U - V, C + U - V, C + U + V, C - U + V }, Out, ShroomMesh::ScaleSRGB(Color, FaceShade(Out)), Surface);
			}
		}
	}

	/** A board (meters) from A to B, Width wide across Side, Thick along the remaining axis. */
	void AddBoard(FMeshAccumulator& Acc, const FVector3d& A, const FVector3d& B, const FVector3d& Side, double Width, double Thick,
		const FLinearColor& Color, ESurface Surface)
	{
		const FVector3d X = (B - A).GetSafeNormal();
		const FVector3d Z = FVector3d::CrossProduct(X, Side).GetSafeNormal();
		const FVector3d Y = FVector3d::CrossProduct(Z, X);
		AddOrientedBox(Acc, (A + B) * 0.5, X, Y, Z, FVector3d(FVector3d::Distance(A, B) * 0.5, Width * 0.5, Thick * 0.5), Color, Surface);
	}

	/** A color a little lighter or darker per piece (planks, tiles, stones). */
	FLinearColor Vary(const FLinearColor& Color, uint32 Seed, uint32 Index, float Amount)
	{
		return ShroomMesh::ScaleSRGB(Color, 1.0f + Amount * ShroomHash::ToSignedFloat(ShroomHash::Hash(Seed, Index)));
	}

	/**
	 * A window in a wall facing Normal (an axis): recessed dark glass, a painted frame with a cross bar, a stone
	 * sill and, if asked, two plank shutters. FaceCenter is on the wall surface, HalfW/HalfH the glass size.
	 */
	void AddWindow(FMeshAccumulator& Acc, const FVector3d& FaceCenter, const FVector3d& Normal, double HalfW, double HalfH,
		bool bShutters, uint32 Seed)
	{
		const FVector3d Up = FVector3d::UnitZ();
		const FVector3d T = FVector3d::CrossProduct(Up, Normal);
		const FLinearColor Frame = Srgb(0.80, 0.78, 0.72);
		const FLinearColor Glass = Srgb(0.10, 0.13, 0.16);
		const FLinearColor Sill = Srgb(0.62, 0.60, 0.56);
		const FLinearColor Shutter = Srgb(0.20, 0.36, 0.26);
		auto Box = [&](double N, double S, double Z, double HN, double HS, double HZ, const FLinearColor& Color, ESurface Surface)
		{
			AddOrientedBox(Acc, FaceCenter + Normal * N + T * S + Up * Z, Normal, T, Up, FVector3d(HN, HS, HZ), Color, Surface);
		};
		Box(-0.02, 0.0, 0.0, 0.03, HalfW, HalfH, Glass, ESurface::Paint);
		Box(0.03, -HalfW - 0.04, 0.0, 0.04, 0.05, HalfH + 0.09, Frame, ESurface::Paint);
		Box(0.03, HalfW + 0.04, 0.0, 0.04, 0.05, HalfH + 0.09, Frame, ESurface::Paint);
		Box(0.03, 0.0, HalfH + 0.05, 0.04, HalfW + 0.09, 0.05, Frame, ESurface::Paint);
		Box(0.03, 0.0, 0.0, 0.02, 0.02, HalfH, Frame, ESurface::Paint);
		Box(0.03, 0.0, HalfH * 0.35, 0.02, HalfW, 0.02, Frame, ESurface::Paint);
		Box(0.07, 0.0, -HalfH - 0.07, 0.09, HalfW + 0.14, 0.05, Sill, ESurface::Stone);
		if (!bShutters)
		{
			return;
		}
		for (const double Side : { -1.0, 1.0 })
		{
			const double S0 = Side * (HalfW + 0.13);
			for (int32 Plank = 0; Plank < 3; ++Plank)
			{
				const double S = S0 + Side * (Plank - 1) * 0.165;
				Box(0.05, S, 0.0, 0.025, 0.078, HalfH + 0.04, Vary(Shutter, Seed, Plank + (Side > 0 ? 10u : 0u), 0.08f), ESurface::Wood);
			}
			for (const double Z : { -HalfH * 0.65, HalfH * 0.65 })
			{
				Box(0.085, S0, Z, 0.012, 0.25, 0.045, ShroomMesh::ScaleSRGB(Shutter, 0.85f), ESurface::Wood);
			}
		}
	}

	/**
	 * One slope of a tiled roof: rows of tile strips from the eave up to the ridge, each strip cut in short
	 * runs of slightly different colors, a little moss on the lower rows; underside closed by a dark soffit.
	 * Eave and Ridge are the middle of the lower and upper edges, Along the ridge direction (unit), HalfLen half
	 * its length.
	 */
	void AddTiledSlope(FMeshAccumulator& Acc, const FVector3d& Eave, const FVector3d& Ridge, const FVector3d& Along, double HalfLen,
		const FLinearColor& Tiles, int32 Rows, uint32 Seed)
	{
		const FVector3d Up = (Ridge - Eave).GetSafeNormal();
		FVector3d Out = FVector3d::CrossProduct(Along, Up).GetSafeNormal();
		if (Out.Z < 0.0)
		{
			Out = -Out;
		}
		const double Slope = FVector3d::Distance(Eave, Ridge);
		const double Step = Slope / Rows;
		const FLinearColor Moss = Srgb(0.30, 0.33, 0.16);
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const FVector3d RowCenter = Eave + Up * (Step * (Row + 0.5)) + Out * 0.045;
			double Y = -HalfLen;
			int32 Run = 0;
			while (Y < HalfLen - 0.01)
			{
				const uint32 H = ShroomHash::Hash(Seed, static_cast<uint32>(Row), static_cast<uint32>(Run));
				const double Length = FMath::Min(HalfLen - Y, 0.6 + 0.9 * ShroomHash::ToUnitFloat(H));
				FLinearColor Color = Vary(Tiles, H, 1u, 0.12f);
				const float MossAmount = ShroomHash::ToUnitFloat(ShroomHash::Hash(H, 2u)) * FMath::Max(0.0f, 0.55f - 0.12f * Row);
				Color = ShroomMesh::LerpSRGB(Color, Moss, MossAmount);
				// Each strip slightly tilted so its lower edge stands proud of the row below.
				const FVector3d StripUp = (Up - Out * 0.08).GetSafeNormal();
				const FVector3d StripOut = FVector3d::CrossProduct(Along, StripUp).GetSafeNormal() * (FVector3d::DotProduct(FVector3d::CrossProduct(Along, StripUp), Out) < 0.0 ? -1.0 : 1.0);
				AddOrientedBox(Acc, RowCenter + Along * (Y + Length * 0.5), Along, StripUp, StripOut,
					FVector3d(Length * 0.5 - 0.005, Step * 0.56, 0.025), Color, ESurface::Roof);
				Y += Length;
				++Run;
			}
		}
		// Soffit (underside) and the boards under the tiles.
		const FVector3d Half = Along * HalfLen;
		AddFace(Acc, { Eave - Half, Eave + Half, Ridge + Half, Ridge - Half }, -Out, Srgb(0.20, 0.15, 0.11), ESurface::Wood);
		AddFace(Acc, { Eave - Half + Out * 0.02, Eave + Half + Out * 0.02, Ridge + Half + Out * 0.02, Ridge - Half + Out * 0.02 }, Out, ShroomMesh::ScaleSRGB(Tiles, 0.5f), ESurface::Roof);
	}

	void BuildHouse(FMeshAccumulator& Acc, FLandmarkCollision* Collision)
	{
		const FLinearColor Stone = Srgb(0.47, 0.45, 0.42);
		const FLinearColor Quoin = Srgb(0.64, 0.61, 0.56);
		const FLinearColor Plaster = Srgb(0.84, 0.78, 0.66);
		const FLinearColor Tiles = Srgb(0.50, 0.22, 0.14);
		const FLinearColor Door = Srgb(0.30, 0.18, 0.09);
		const FLinearColor Timber = Srgb(0.30, 0.21, 0.13);
		constexpr double HalfX = 3.0;  // front wall at +X
		constexpr double HalfY = 4.2;
		constexpr double WallTop = 3.4;
		constexpr double Base = 0.4;  // top of the stone plinth

		// Stone plinth and plaster walls, mottled and grimy at the foot.
		AddPanelBox(Acc, FVector3d(0, 0, -0.8), FVector3d(3.3, 4.5, 1.2), Stone, ESurface::Stone, 0.3, 0.16f, 0.0f, 0.0, 11u, Collision);
		AddPanelBox(Acc, FVector3d(0, 0, (Base + WallTop) * 0.5), FVector3d(HalfX, HalfY, (WallTop - Base) * 0.5), Plaster, ESurface::Plaster,
			0.35, 0.06f, 0.3f, Base, 12u, Collision);
		// Corner quoins: stones alternating long and short on the two faces.
		for (const double SX : { -1.0, 1.0 })
		{
			for (const double SY : { -1.0, 1.0 })
			{
				for (int32 Course = 0; Course < 7; ++Course)
				{
					const bool bLong = (Course % 2) == (SX * SY > 0.0 ? 0 : 1);
					const double A = bLong ? 0.46 : 0.26;
					const double B = bLong ? 0.26 : 0.46;
					const double Z = Base + 0.22 + Course * 0.43;
					AddOrientedBox(Acc, FVector3d(SX * (HalfX - A * 0.5 + 0.025), SY * (HalfY - B * 0.5 + 0.025), Z),
						FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(A * 0.5, B * 0.5, 0.2),
						Vary(Quoin, 31u, static_cast<uint32>(Course * 4 + (SX > 0 ? 1 : 0) + (SY > 0 ? 2 : 0)), 0.08f), ESurface::Stone);
				}
			}
		}

		// Gable walls (plaster) and the tiled roof, with an overhang all round.
		const double Overhang = 0.5;
		const double RoofHalfX = HalfX + Overhang;
		const double RoofHalfY = HalfY + 0.4;
		const double Rise = 2.6;
		const double Eave = WallTop - 0.1;
		const double Apex = Eave + Rise;
		// Where the roof underside meets the wall line (the gables and a frieze close the attic up to it).
		const double RoofAtWall = Eave + Rise * (Overhang / RoofHalfX) - 0.04;
		for (const double SY : { -1.0, 1.0 })
		{
			AddFace(Acc, { FVector3d(HalfX, SY * HalfY, WallTop - 0.01), FVector3d(HalfX, SY * HalfY, RoofAtWall), FVector3d(0, SY * HalfY, Apex - 0.06),
				FVector3d(-HalfX, SY * HalfY, RoofAtWall), FVector3d(-HalfX, SY * HalfY, WallTop - 0.01) },
				FVector3d(0, SY, 0), ShroomMesh::ScaleSRGB(Plaster, 0.86f), ESurface::Plaster);
		}
		for (const double SX : { -1.0, 1.0 })
		{
			AddOrientedBox(Acc, FVector3d(SX * (HalfX - 0.05), 0, (WallTop + RoofAtWall) * 0.5), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(),
				FVector3d(0.05, HalfY, (RoofAtWall - WallTop) * 0.5 + 0.02), ShroomMesh::ScaleSRGB(Plaster, 0.95f), ESurface::Plaster);
		}
		for (const double SX : { -1.0, 1.0 })
		{
			AddTiledSlope(Acc, FVector3d(SX * RoofHalfX, 0, Eave), FVector3d(0, 0, Apex), FVector3d::UnitY(), RoofHalfY, Tiles, 12,
				SX > 0.0 ? 41u : 42u);
			// Bargeboards along the gable edges, gutter along the eave.
			for (const double SY : { -1.0, 1.0 })
			{
				AddBoard(Acc, FVector3d(SX * RoofHalfX, SY * (RoofHalfY + 0.02), Eave - 0.05), FVector3d(0, SY * (RoofHalfY + 0.02), Apex - 0.02),
					FVector3d(0, 1, 0), 0.05, 0.2, Timber, ESurface::Wood);
			}
			AddCylinder(Acc, FVector3d(SX * (RoofHalfX + 0.05), -RoofHalfY, Eave - 0.05), FVector3d(SX * (RoofHalfX + 0.05), RoofHalfY, Eave - 0.05),
				0.06, 8, Srgb(0.42, 0.44, 0.45), Srgb(0.3, 0.3, 0.3), ESurface::Metal);
		}
		AddCylinder(Acc, FVector3d(0, -RoofHalfY - 0.05, Apex + 0.02), FVector3d(0, RoofHalfY + 0.05, Apex + 0.02), 0.13, 8,
			ShroomMesh::ScaleSRGB(Tiles, 0.8f), ShroomMesh::ScaleSRGB(Tiles, 0.7f), ESurface::Roof);
		// Downpipe at a front corner.
		AddCylinder(Acc, FVector3d(RoofHalfX + 0.05, HalfY + 0.15, Eave - 0.05), FVector3d(HalfX + 0.1, HalfY + 0.15, 0.45), 0.045, 6,
			Srgb(0.42, 0.44, 0.45), Srgb(0.3, 0.3, 0.3), ESurface::Metal);

		// Chimney: stone stack, cap and dark flue.
		AddPanelBox(Acc, FVector3d(-1.0, 2.6, 5.4), FVector3d(0.35, 0.35, 1.3), Srgb(0.52, 0.30, 0.22), ESurface::Stone, 0.2, 0.14f, 0.0f, 0.0, 13u);
		AddOrientedBox(Acc, FVector3d(-1.0, 2.6, 6.74), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.43, 0.43, 0.05), Quoin, ESurface::Stone);
		AddOrientedBox(Acc, FVector3d(-1.0, 2.6, 6.8), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.16, 0.16, 0.02), Srgb(0.05, 0.05, 0.05), ESurface::Paint);

		// Front (+X): plank door in a timber frame under a small canopy, a stone step, two shuttered windows.
		for (int32 Plank = 0; Plank < 5; ++Plank)
		{
			const double Y = -0.4 + Plank * 0.2;
			AddOrientedBox(Acc, FVector3d(HalfX + 0.03, Y, Base + 1.05), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(),
				FVector3d(0.03, 0.098, 1.05), Vary(Door, 51u, static_cast<uint32>(Plank), 0.1f), ESurface::Wood);
		}
		for (const double Z : { Base + 0.4, Base + 1.7 })
		{
			AddOrientedBox(Acc, FVector3d(HalfX + 0.07, 0, Z), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.015, 0.45, 0.06),
				ShroomMesh::ScaleSRGB(Door, 0.8f), ESurface::Wood);
		}
		for (const double Y : { -0.57, 0.57 })
		{
			AddOrientedBox(Acc, FVector3d(HalfX + 0.04, Y, Base + 1.1), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.05, 0.07, 1.12), Timber, ESurface::Wood);
		}
		AddOrientedBox(Acc, FVector3d(HalfX + 0.04, 0, Base + 2.25), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.06, 0.66, 0.08), Timber, ESurface::Wood);
		AddOrientedBox(Acc, FVector3d(HalfX + 0.11, 0.2, Base + 1.05), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.03, 0.03, 0.03), Srgb(0.2, 0.2, 0.2), ESurface::Metal);
		{
			// Canopy: a short tiled slab on two brackets.
			const FVector3d Down = FVector3d(0.55, 0, -0.25).GetSafeNormal();
			const FVector3d CanopyUp = FVector3d::CrossProduct(FVector3d::UnitY(), Down).GetSafeNormal() * -1.0;
			AddOrientedBox(Acc, FVector3d(HalfX + 0.35, 0, Base + 2.55), Down, FVector3d::UnitY(), CanopyUp, FVector3d(0.42, 0.85, 0.04), Tiles, ESurface::Roof);
			for (const double Y : { -0.6, 0.6 })
			{
				AddBoard(Acc, FVector3d(HalfX, Y, Base + 2.15), FVector3d(HalfX + 0.55, Y, Base + 2.45), FVector3d(0, 1, 0), 0.06, 0.08, Timber, ESurface::Wood);
			}
		}
		AddPanelBox(Acc, FVector3d(HalfX + 0.4, 0, 0.3), FVector3d(0.4, 0.8, 0.1), Quoin, ESurface::Stone, 0.25, 0.12f, 0.0f, 0.0, 14u);
		for (const double Y : { -2.3, 2.3 })
		{
			AddWindow(Acc, FVector3d(HalfX, Y, 2.05), FVector3d::UnitX(), 0.46, 0.56, true, Y > 0.0 ? 61u : 62u);
		}
		// One window on each side and at the back.
		for (const double SY : { -1.0, 1.0 })
		{
			AddWindow(Acc, FVector3d(0.6, SY * HalfY, 2.05), FVector3d(0, SY, 0), 0.46, 0.56, true, SY > 0.0 ? 63u : 64u);
		}
		AddWindow(Acc, FVector3d(-HalfX, 1.5, 2.05), -FVector3d::UnitX(), 0.46, 0.56, false, 65u);

		// Bench by the door.
		AddOrientedBox(Acc, FVector3d(HalfX + 0.35, -1.65, 0.85), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.2, 0.7, 0.03), Timber, ESurface::Wood);
		for (const double Y : { -2.2, -1.1 })
		{
			AddOrientedBox(Acc, FVector3d(HalfX + 0.35, Y, 0.6), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.16, 0.04, 0.24), ShroomMesh::ScaleSRGB(Timber, 0.85f), ESurface::Wood);
		}
		// Woodpile against the side wall, under a small lean-to roof.
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Log = 0; Log < 6 - Row; ++Log)
			{
				const double Y = -4.6;
				const double X = -2.2 + Log * 0.3 + Row * 0.15;
				const uint32 H = ShroomHash::Hash(71u, static_cast<uint32>(Row), static_cast<uint32>(Log));
				const double Jut = 0.06 * ShroomHash::ToSignedFloat(H);
				AddCylinder(Acc, FVector3d(X, Y - 0.45 + Jut, 0.55 + Row * 0.26), FVector3d(X, Y + 0.02 + Jut, 0.55 + Row * 0.26), 0.13, 7,
					Vary(Srgb(0.32, 0.25, 0.18), H, 3u, 0.12f), Vary(Srgb(0.66, 0.52, 0.34), H, 4u, 0.1f), ESurface::Wood);
			}
		}
		{
			const FVector3d Down = FVector3d(0, -0.8, -0.35).GetSafeNormal();
			const FVector3d LeanUp = FVector3d::CrossProduct(Down, FVector3d::UnitX()).GetSafeNormal();
			AddOrientedBox(Acc, FVector3d(-1.45, -4.62, 1.95), FVector3d::UnitX(), Down, LeanUp.Z < 0.0 ? -LeanUp : LeanUp, FVector3d(1.05, 0.45, 0.025),
				Srgb(0.26, 0.22, 0.18), ESurface::Wood);
		}
	}

	void BuildHut(FMeshAccumulator& Acc, FLandmarkCollision* Collision)
	{
		const FLinearColor Wood = Srgb(0.36, 0.24, 0.14);
		AddBox(Acc, FVector3d(0, 0, -0.4), FVector3d(2.1, 2.6, 0.6), Srgb(0.42, 0.41, 0.39), ESurface::Stone, Collision);
		AddBox(Acc, FVector3d(0, 0, 1.4), FVector3d(2.0, 2.5, 1.2), Wood, ESurface::Wood, Collision);
		// Horizontal planks (darker seams).
		for (int32 Seam = 1; Seam < 6; ++Seam)
		{
			AddBox(Acc, FVector3d(0, 0, 0.2 + Seam * 0.4), FVector3d(2.02, 2.52, 0.02), ShroomMesh::ScaleSRGB(Wood, 0.6f), ESurface::Wood);
		}
		AddGableRoof(Acc, FVector3d(0, 0, 2.6), 2.4, 2.9, 1.3, Srgb(0.22, 0.2, 0.18));
		AddBox(Acc, FVector3d(2.03, -0.8, 1.2), FVector3d(0.05, 0.45, 1.0), Srgb(0.2, 0.12, 0.06), ESurface::Wood);
		AddBox(Acc, FVector3d(2.03, 1.0, 1.7), FVector3d(0.05, 0.45, 0.35), Srgb(0.1, 0.1, 0.1), ESurface::Paint);
		// Bench by the door.
		AddBox(Acc, FVector3d(2.5, 1.0, 0.45), FVector3d(0.2, 0.8, 0.04), Wood, ESurface::Wood);
		AddBox(Acc, FVector3d(2.5, 0.35, 0.2), FVector3d(0.15, 0.04, 0.22), Wood, ESurface::Wood);
		AddBox(Acc, FVector3d(2.5, 1.65, 0.2), FVector3d(0.15, 0.04, 0.22), Wood, ESurface::Wood);
	}

	void BuildTower(FMeshAccumulator& Acc, FLandmarkCollision* Collision)
	{
		const FLinearColor Wood = Srgb(0.46, 0.35, 0.22);
		const FLinearColor Plank = Srgb(0.38, 0.28, 0.17);
		const FLinearColor Dark = Srgb(0.30, 0.22, 0.14);
		const FLinearColor Felt = Srgb(0.17, 0.18, 0.17);
		auto LegAt = [](double Z) { return FMath::Lerp(0.95, 0.65, (Z + 0.3) / 4.85); };
		int32 Leg = 0;
		for (const double SX : { -1.0, 1.0 })
		{
			for (const double SY : { -1.0, 1.0 })
			{
				AddCylinder(Acc, FVector3d(SX * 0.95, SY * 0.95, -0.3), FVector3d(SX * 0.65, SY * 0.65, 4.55), 0.1, 8,
					Vary(Wood, 81u, static_cast<uint32>(Leg++), 0.1f), Srgb(0.62, 0.5, 0.33), ESurface::Wood);
			}
		}
		// Rails at two heights on the four sides, and an X brace on each side.
		const FVector3d Corners[4] = { FVector3d(1, 1, 0), FVector3d(1, -1, 0), FVector3d(-1, -1, 0), FVector3d(-1, 1, 0) };
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const FVector3d C0 = Corners[Side];
			const FVector3d C1 = Corners[(Side + 1) % 4];
			for (const double Z : { 1.4, 3.3 })
			{
				const double R = LegAt(Z);
				AddCylinder(Acc, FVector3d(C0.X * R, C0.Y * R, Z), FVector3d(C1.X * R, C1.Y * R, Z), 0.05, 6, Dark, Dark, ESurface::Wood);
			}
			const double R0 = LegAt(0.3);
			const double R1 = LegAt(3.3);
			AddCylinder(Acc, FVector3d(C0.X * R0, C0.Y * R0, 0.3), FVector3d(C1.X * R1, C1.Y * R1, 3.3), 0.04, 5, Dark, Dark, ESurface::Wood);
			AddCylinder(Acc, FVector3d(C1.X * R0, C1.Y * R0, 0.3), FVector3d(C0.X * R1, C0.Y * R1, 3.3), 0.04, 5, Dark, Dark, ESurface::Wood);
		}
		// Platform: two joists and a floor of planks.
		for (const double Y : { -0.7, 0.7 })
		{
			AddOrientedBox(Acc, FVector3d(0, Y, 4.5), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(), FVector3d(0.95, 0.05, 0.07), Dark, ESurface::Wood);
		}
		for (int32 Board = 0; Board < 7; ++Board)
		{
			AddOrientedBox(Acc, FVector3d(-0.84 + Board * 0.28, 0, 4.61), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(),
				FVector3d(0.13, 0.95, 0.03), Vary(Wood, 82u, static_cast<uint32>(Board), 0.1f), ESurface::Wood);
		}
		// Cabin: dark inside, walls of vertical planks with a wide opening at the front and slits on the sides.
		constexpr double Half = 0.82;
		constexpr double Floor = 4.64;
		constexpr double Top = 6.45;
		AddOrientedBox(Acc, FVector3d(0, 0, (Floor + Top) * 0.5), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(),
			FVector3d(Half - 0.05, Half - 0.05, (Top - Floor) * 0.5), Srgb(0.06, 0.05, 0.04), ESurface::Paint);
		const FVector3d Normals[4] = { FVector3d::UnitX(), FVector3d::UnitY(), -FVector3d::UnitX(), -FVector3d::UnitY() };
		for (int32 Wall = 0; Wall < 4; ++Wall)
		{
			const FVector3d N = Normals[Wall];
			const FVector3d T = FVector3d::CrossProduct(FVector3d::UnitZ(), N);
			// Opening: front 1.2 m wide, sides 0.6 m, none at the back (the ladder).
			const double OpenHalf = Wall == 0 ? 0.6 : (Wall == 2 ? 0.0 : 0.3);
			constexpr double OpenLow = 5.6;
			constexpr double OpenHigh = 6.05;
			constexpr int32 Planks = 12;
			for (int32 Index = 0; Index < Planks; ++Index)
			{
				const double S = -Half + (Index + 0.5) * (2.0 * Half / Planks);
				const FLinearColor Color = Vary(Plank, 90u + Wall, static_cast<uint32>(Index), 0.12f);
				auto Piece = [&](double Z0, double Z1)
				{
					AddOrientedBox(Acc, N * (Half - 0.02) + T * S + FVector3d(0, 0, (Z0 + Z1) * 0.5), N, T, FVector3d::UnitZ(),
						FVector3d(0.02, Half / Planks - 0.006, (Z1 - Z0) * 0.5), Color, ESurface::Wood);
				};
				if (FMath::Abs(S) < OpenHalf)
				{
					Piece(Floor, OpenLow);
					Piece(OpenHigh, Top);
				}
				else
				{
					Piece(Floor, Top);
				}
			}
			if (OpenHalf > 0.0)
			{
				for (const double Z : { OpenLow - 0.03, OpenHigh + 0.03 })
				{
					AddOrientedBox(Acc, N * (Half + 0.015) + FVector3d(0, 0, Z), N, T, FVector3d::UnitZ(), FVector3d(0.02, OpenHalf + 0.06, 0.035), Dark, ESurface::Wood);
				}
			}
			// Corner posts.
			AddOrientedBox(Acc, (N + T) * Half + FVector3d(0, 0, (Floor + Top) * 0.5), FVector3d::UnitX(), FVector3d::UnitY(), FVector3d::UnitZ(),
				FVector3d(0.05, 0.05, (Top - Floor) * 0.5), Dark, ESurface::Wood);
		}
		// Gable roof of tar felt over battens, with an overhang and bargeboards.
		constexpr double RoofHalf = 1.08;
		constexpr double Eave = Top - 0.02;
		constexpr double Apex = Top + 0.5;
		for (const double SX : { -1.0, 1.0 })
		{
			const FVector3d EaveC(SX * RoofHalf, 0, Eave);
			const FVector3d RidgeC(0, 0, Apex);
			const FVector3d Up = (RidgeC - EaveC).GetSafeNormal();
			FVector3d Out = FVector3d::CrossProduct(FVector3d::UnitY(), Up).GetSafeNormal();
			if (Out.Z < 0.0)
			{
				Out = -Out;
			}
			const FVector3d Mid = (EaveC + RidgeC) * 0.5;
			AddOrientedBox(Acc, Mid, Up, FVector3d::UnitY(), Out, FVector3d(FVector3d::Distance(EaveC, RidgeC) * 0.5, RoofHalf, 0.03), Felt, ESurface::Roof);
			for (int32 Batten = 1; Batten < 4; ++Batten)
			{
				const FVector3d P = FMath::Lerp(EaveC, RidgeC, Batten / 4.0) + Out * 0.04;
				AddOrientedBox(Acc, P, FVector3d::UnitY(), Up, Out, FVector3d(RoofHalf, 0.02, 0.015), ShroomMesh::ScaleSRGB(Felt, 0.7f), ESurface::Roof);
			}
			for (const double SY : { -1.0, 1.0 })
			{
				AddBoard(Acc, FVector3d(SX * RoofHalf, SY * (RoofHalf + 0.02), Eave - 0.04), FVector3d(0, SY * (RoofHalf + 0.02), Apex),
					FVector3d(0, 1, 0), 0.04, 0.14, Dark, ESurface::Wood);
			}
		}
		for (const double SY : { -1.0, 1.0 })
		{
			AddFace(Acc, { FVector3d(Half, SY * Half, Top), FVector3d(-Half, SY * Half, Top), FVector3d(0, SY * Half, Apex - 0.03) },
				FVector3d(0, SY, 0), Plank, ESurface::Wood);
		}
		// Ladder at the back.
		for (const double Y : { -0.25, 0.25 })
		{
			AddCylinder(Acc, FVector3d(-1.8, Y, -0.2), FVector3d(-0.85, Y, 4.6), 0.045, 6, Wood, Wood, ESurface::Wood);
		}
		for (int32 Rung = 1; Rung < 14; ++Rung)
		{
			const double T = Rung / 14.0;
			const FVector3d P = FMath::Lerp(FVector3d(-1.8, 0, -0.2), FVector3d(-0.85, 0, 4.6), T);
			AddCylinder(Acc, P + FVector3d(0, -0.25, 0), P + FVector3d(0, 0.25, 0), 0.025, 5, Vary(Wood, 83u, static_cast<uint32>(Rung), 0.1f), Wood, ESurface::Wood);
		}
		if (Collision)
		{
			Collision->Boxes.Add(FBox(FVector3d(-1.0, -1.0, -0.3) * Cm, FVector3d(1.0, 1.0, 6.9) * Cm));
		}
	}

	void BuildCalvary(FMeshAccumulator& Acc, FLandmarkCollision* Collision)
	{
		const FLinearColor Stone = Srgb(0.6, 0.58, 0.54);
		const FLinearColor Iron = Srgb(0.18, 0.18, 0.2);
		AddBox(Acc, FVector3d(0, 0, -0.3), FVector3d(0.9, 0.9, 0.5), Stone, ESurface::Stone, Collision);
		AddBox(Acc, FVector3d(0, 0, 0.35), FVector3d(0.6, 0.6, 0.15), Stone, ESurface::Stone, Collision);
		AddBox(Acc, FVector3d(0, 0, 0.9), FVector3d(0.3, 0.3, 0.4), ShroomMesh::ScaleSRGB(Stone, 0.92f), ESurface::Stone, Collision);
		AddBox(Acc, FVector3d(0, 0, 2.3), FVector3d(0.07, 0.07, 1.0), Iron, ESurface::Metal, Collision);
		AddBox(Acc, FVector3d(0, 0, 2.85), FVector3d(0.06, 0.5, 0.06), Iron, ESurface::Metal);
	}

	void BuildSignpost(FMeshAccumulator& Acc, FLandmarkCollision* Collision)
	{
		const FLinearColor Wood = Srgb(0.45, 0.35, 0.22);
		AddBox(Acc, FVector3d(0, 0, 1.1), FVector3d(0.06, 0.06, 1.45), Wood, ESurface::Wood, Collision);
		AddFace(Acc, { FVector3d(0.07, -0.07, 2.55), FVector3d(0.07, 0.07, 2.55), FVector3d(0, 0, 2.7) }, FVector3d(1, 0, 1), Wood, ESurface::Wood);
		AddFace(Acc, { FVector3d(-0.07, 0.07, 2.55), FVector3d(-0.07, -0.07, 2.55), FVector3d(0, 0, 2.7) }, FVector3d(-1, 0, 1), Wood, ESurface::Wood);
		AddFace(Acc, { FVector3d(0.07, 0.07, 2.55), FVector3d(-0.07, 0.07, 2.55), FVector3d(0, 0, 2.7) }, FVector3d(0, 1, 1), Wood, ESurface::Wood);
		AddFace(Acc, { FVector3d(-0.07, -0.07, 2.55), FVector3d(0.07, -0.07, 2.55), FVector3d(0, 0, 2.7) }, FVector3d(0, -1, 1), Wood, ESurface::Wood);
		// Name boards on both faces of the post (facing +X and -X): one board through the post left the
		// post standing in front of the name on both sides.
		for (const double Side : { 1.0, -1.0 })
		{
			const double BoardX = Side * LandmarkMeshBuilder::SignNameBoardFaceX / Cm;
			AddBox(Acc, FVector3d(BoardX - Side * 0.015, 0, LandmarkMeshBuilder::SignNameBoardZ / Cm), FVector3d(0.015, 0.75, 0.15), Srgb(0.9, 0.88, 0.82), ESurface::Paint);
			AddBox(Acc, FVector3d(BoardX - Side * 0.018, 0, LandmarkMeshBuilder::SignNameBoardZ / Cm), FVector3d(0.012, 0.77, 0.17), Srgb(0.55, 0.12, 0.1), ESurface::Paint);
		}
	}

	void BuildParcelStone(FMeshAccumulator& Acc, FLandmarkCollision* Collision)
	{
		AddBox(Acc, FVector3d(0, 0, 0.1), FVector3d(0.2, 0.2, 0.45), Srgb(0.68, 0.66, 0.6), ESurface::Stone, Collision);
		AddBox(Acc, FVector3d(0, 0, 0.57), FVector3d(0.205, 0.205, 0.03), Srgb(0.92, 0.9, 0.85), ESurface::Paint);
	}

	void BuildBridge(FMeshAccumulator& Acc, double Length, double Width, FLandmarkCollision* Collision)
	{
		const bool bRoad = Width > 4.0;
		const FLinearColor Deck = bRoad ? Srgb(0.5, 0.49, 0.46) : Srgb(0.48, 0.36, 0.22);
		const FLinearColor Rail = bRoad ? Srgb(0.55, 0.53, 0.5) : Srgb(0.38, 0.28, 0.17);
		const double HalfL = Length * 0.5;
		const double HalfW = Width * 0.5;
		AddBox(Acc, FVector3d(0, 0, 0.12), FVector3d(HalfL, HalfW, 0.12), Deck, bRoad ? ESurface::Stone : ESurface::Wood, Collision);
		if (!bRoad)
		{
			// Plank seams.
			for (double X = -HalfL + 0.3; X < HalfL; X += 0.3)
			{
				AddBox(Acc, FVector3d(X, 0, 0.245), FVector3d(0.01, HalfW, 0.005), ShroomMesh::ScaleSRGB(Deck, 0.6f), ESurface::Wood);
			}
		}
		AddBox(Acc, FVector3d(0, -HalfW + 0.2, -0.25), FVector3d(HalfL, 0.15, 0.25), ShroomMesh::ScaleSRGB(Deck, 0.7f), ESurface::Wood);
		AddBox(Acc, FVector3d(0, HalfW - 0.2, -0.25), FVector3d(HalfL, 0.15, 0.25), ShroomMesh::ScaleSRGB(Deck, 0.7f), ESurface::Wood);
		for (const double Side : { -1.0, 1.0 })
		{
			const double Y = Side * (HalfW + (bRoad ? 0.15 : 0.04));
			if (bRoad)
			{
				AddBox(Acc, FVector3d(0, Y, 0.6), FVector3d(HalfL, 0.15, 0.45), Rail, ESurface::Stone, Collision);
				continue;
			}
			AddBox(Acc, FVector3d(0, Y, 1.05), FVector3d(HalfL, 0.04, 0.05), Rail, ESurface::Wood, Collision);
			for (double X = -HalfL + 0.1; X <= HalfL; X += 1.5)
			{
				AddBox(Acc, FVector3d(X, Y, 0.65), FVector3d(0.05, 0.05, 0.45), Rail, ESurface::Wood);
			}
		}
	}

	void BuildLogPile(FMeshAccumulator& Acc, double Length, FLandmarkCollision* Collision)
	{
		const double HalfL = FMath::Max(Length, 2.0) * 0.5;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const int32 Logs = 4 - Row;
			for (int32 Log = 0; Log < Logs; ++Log)
			{
				const double Y = (Log - (Logs - 1) * 0.5) * 0.38;
				const double Z = 0.18 + Row * 0.31;
				const double Jitter = 0.08 * ((Row * 7 + Log * 3) % 5 - 2);
				AddCylinder(Acc, FVector3d(-HalfL + Jitter, Y, Z), FVector3d(HalfL + Jitter, Y, Z), 0.18 - 0.01 * Log, 9,
					Srgb(0.33, 0.29, 0.23), Srgb(0.68, 0.55, 0.36), ESurface::Wood);
			}
		}
		if (Collision)
		{
			Collision->Boxes.Add(FBox(FVector3d(-HalfL - 0.2, -0.8, 0.0) * Cm, FVector3d(HalfL + 0.2, 0.8, 1.0) * Cm));
		}
	}
}

namespace LandmarkMeshBuilder
{
	bool Build(const FForestLandmark& Landmark, FDynamicMesh3& OutMesh, FLandmarkCollision* OutCollision)
	{
		using namespace LandmarkMeshDetail;
		if (OutCollision)
		{
			OutCollision->Boxes.Reset();
		}
		FMeshAccumulator Acc;
		switch (Landmark.Kind)
		{
		case EForestLandmarkKind::ForesterHouse: BuildHouse(Acc, OutCollision); break;
		case EForestLandmarkKind::HuntingHut: BuildHut(Acc, OutCollision); break;
		case EForestLandmarkKind::HuntingTower: BuildTower(Acc, OutCollision); break;
		case EForestLandmarkKind::Calvary: BuildCalvary(Acc, OutCollision); break;
		case EForestLandmarkKind::Signpost: BuildSignpost(Acc, OutCollision); break;
		case EForestLandmarkKind::ParcelStone: BuildParcelStone(Acc, OutCollision); break;
		case EForestLandmarkKind::Bridge: BuildBridge(Acc, FMath::Max(Landmark.LengthM, 3.0f), FMath::Max(Landmark.WidthM, 1.2f), OutCollision); break;
		case EForestLandmarkKind::LogPile: BuildLogPile(Acc, Landmark.LengthM, OutCollision); break;
		}
		Acc.Emit(OutMesh);
		return OutMesh.TriangleCount() > 0;
	}

	bool GetPaperMount(EForestLandmarkKind Kind, FTransform& OutLocal, FVector2D& OutSizeCm)
	{
		switch (Kind)
		{
		case EForestLandmarkKind::ForesterHouse:
			// Front wall (plaster face at X = 3.0 m), between the door and the right window.
			OutLocal = FTransform(FVector(301.0, 92.0, 178.0));
			OutSizeCm = FVector2D(52.0, 72.0);
			return true;
		case EForestLandmarkKind::HuntingHut:
			// Front planks (X = 2.02 m), between the door and the window, slightly askew.
			OutLocal = FTransform(FRotator(0.0, 0.0, 4.0), FVector(203.0, 8.0, 138.0));
			OutSizeCm = FVector2D(24.0, 32.0);
			return true;
		default:
			return false;
		}
	}

	bool GetCounterMount(EForestLandmarkKind Kind, FTransform& OutLocal, FVector2D& OutSizeCm)
	{
		if (Kind != EForestLandmarkKind::ForesterHouse)
		{
			return false;
		}
		// Front wall, mirror of the poster: between the door and the left window.
		OutLocal = FTransform(FVector(301.0, -92.0, 170.0));
		OutSizeCm = FVector2D(56.0, 44.0);
		return true;
	}

	void BuildPaper(const FVector2D& SizeCm, TConstArrayView<FLinearColor> Swatches, bool bTorn, FDynamicMesh3& OutMesh, FLandmarkCollision* OutCollision)
	{
		using namespace LandmarkMeshDetail;
		FMeshAccumulator Acc;
		const double W = SizeCm.X / Cm;
		const double H = SizeCm.Y / Cm;
		const FLinearColor Paper = bTorn ? Srgb(0.80, 0.74, 0.58) : Srgb(0.90, 0.87, 0.76);
		// The sheet; a torn one is made of strips with a ragged bottom.
		const double Ragged[6] = { 0.0, 0.07, 0.02, 0.11, 0.04, 0.14 };
		const int32 Strips = bTorn ? 6 : 1;
		for (int32 Strip = 0; Strip < Strips; ++Strip)
		{
			const double Y0 = -0.5 * W + W * Strip / Strips;
			const double Y1 = -0.5 * W + W * (Strip + 1) / Strips;
			const double Bottom = -0.5 * H + (bTorn ? Ragged[Strip] * H : 0.0);
			AddBox(Acc, FVector3d(0, 0.5 * (Y0 + Y1), 0.5 * (Bottom + 0.5 * H)), FVector3d(0.003, 0.5 * (Y1 - Y0), 0.5 * (0.5 * H - Bottom)), Paper, ESurface::Paint);
		}
		// Title band.
		AddBox(Acc, FVector3d(0.0035, 0, 0.5 * H - 0.11 * H), FVector3d(0.001, 0.44 * W, 0.07 * H), Srgb(0.20, 0.30, 0.20), ESurface::Paint);
		// One stylized mushroom (cap over a pale stem) per species.
		const int32 Count = FMath::Max(1, Swatches.Num());
		for (int32 Index = 0; Index < Swatches.Num(); ++Index)
		{
			const double ColumnW = 0.9 * W / Count;
			const double Y = -0.45 * W + ColumnW * (Index + 0.5);
			AddBox(Acc, FVector3d(0.0035, Y, -0.06 * H), FVector3d(0.001, 0.08 * ColumnW, 0.11 * H), Srgb(0.92, 0.90, 0.84), ESurface::Paint);
			AddBox(Acc, FVector3d(0.0045, Y, 0.07 * H), FVector3d(0.001, 0.32 * ColumnW, 0.06 * H), Swatches[Index], ESurface::Paint);
		}
		Acc.Emit(OutMesh);
		if (OutCollision)
		{
			OutCollision->Boxes.Reset();
			OutCollision->Boxes.Add(FBox(FVector(-1.0, -0.5 * SizeCm.X, -0.5 * SizeCm.Y), FVector(1.0, 0.5 * SizeCm.X, 0.5 * SizeCm.Y)));
		}
	}

	void BuildSignBoard(FDynamicMesh3& OutMesh)
	{
		using namespace LandmarkMeshDetail;
		FMeshAccumulator Acc;
		const FLinearColor Board = Srgb(0.9, 0.88, 0.82);
		AddBox(Acc, FVector3d(0.55, 0, 0), FVector3d(0.5, 0.025, 0.12), Board, ESurface::Paint);
		// Arrow tip.
		const FVector3d Tip(1.22, 0, 0);
		for (const double Side : { -1.0, 1.0 })
		{
			AddFace(Acc, { FVector3d(1.05, Side * 0.025, 0.12), FVector3d(1.05, Side * 0.025, -0.12), Tip + FVector3d(0, Side * 0.025, 0) },
				FVector3d(0, Side, 0), Board, ESurface::Paint);
		}
		AddFace(Acc, { FVector3d(1.05, 0.025, 0.12), FVector3d(1.05, -0.025, 0.12), Tip + FVector3d(0, -0.025, 0), Tip + FVector3d(0, 0.025, 0) }, FVector3d(1, 0, 1), Board, ESurface::Paint);
		AddFace(Acc, { FVector3d(1.05, -0.025, -0.12), FVector3d(1.05, 0.025, -0.12), Tip + FVector3d(0, 0.025, 0), Tip + FVector3d(0, -0.025, 0) }, FVector3d(1, 0, -1), Board, ESurface::Paint);
		Acc.Emit(OutMesh);
	}
}
