#include "Forest/LandmarkMeshBuilder.h"

#include "Algo/Reverse.h"
#include "Core/MeshAccumulator.h"
#include "DynamicMesh/DynamicMesh3.h"

using namespace UE::Geometry;

namespace LandmarkMeshDetail
{
	constexpr double Cm = 100.0;

	enum class ESurface : uint8 { Stone = 0, Wood = 1, Plaster = 2, Roof = 3, Metal = 4, Paint = 5 };

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

	void BuildHouse(FMeshAccumulator& Acc, FLandmarkCollision* Collision)
	{
		const FLinearColor Stone = Srgb(0.45, 0.44, 0.42);
		const FLinearColor Plaster = Srgb(0.82, 0.76, 0.62);
		const FLinearColor Tiles = Srgb(0.45, 0.18, 0.12);
		const FLinearColor Door = Srgb(0.26, 0.15, 0.08);
		const FLinearColor Glass = Srgb(0.12, 0.15, 0.18);
		const FLinearColor Shutter = Srgb(0.18, 0.32, 0.22);
		AddBox(Acc, FVector3d(0, 0, -0.8), FVector3d(3.3, 4.5, 1.2), Stone, ESurface::Stone, Collision);
		AddBox(Acc, FVector3d(0, 0, 1.9), FVector3d(3.0, 4.2, 1.5), Plaster, ESurface::Plaster, Collision);
		AddGableRoof(Acc, FVector3d(0, 0, 3.4), 3.5, 4.6, 2.6, Tiles);
		AddBox(Acc, FVector3d(-1.0, 2.6, 5.4), FVector3d(0.35, 0.35, 1.3), Srgb(0.5, 0.25, 0.18), ESurface::Stone);
		// Front (+X): door, two windows with shutters, a step.
		AddBox(Acc, FVector3d(3.03, 0, 1.45), FVector3d(0.05, 0.5, 1.05), Door, ESurface::Wood);
		AddBox(Acc, FVector3d(3.4, 0, 0.3), FVector3d(0.4, 0.8, 0.1), Stone, ESurface::Stone);
		for (const double Y : { -2.3, 2.3 })
		{
			AddBox(Acc, FVector3d(3.03, Y, 2.05), FVector3d(0.05, 0.5, 0.6), Glass, ESurface::Paint);
			AddBox(Acc, FVector3d(3.04, Y - 0.75, 2.05), FVector3d(0.04, 0.25, 0.62), Shutter, ESurface::Wood);
			AddBox(Acc, FVector3d(3.04, Y + 0.75, 2.05), FVector3d(0.04, 0.25, 0.62), Shutter, ESurface::Wood);
		}
		// One window on each side and at the back.
		for (const double Y : { -4.23, 4.23 })
		{
			AddBox(Acc, FVector3d(0.6, Y, 2.05), FVector3d(0.5, 0.05, 0.6), Glass, ESurface::Paint);
		}
		AddBox(Acc, FVector3d(-3.03, 1.5, 2.05), FVector3d(0.05, 0.5, 0.6), Glass, ESurface::Paint);
		// Woodpile against the side wall.
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Log = 0; Log < 4 - Row; ++Log)
			{
				const double Y = -4.6;
				const double X = -1.8 + Log * 0.32 + Row * 0.16;
				AddCylinder(Acc, FVector3d(X, Y - 0.4, 0.55 + Row * 0.27), FVector3d(X, Y + 0.0, 0.55 + Row * 0.27), 0.14, 7, Srgb(0.32, 0.25, 0.18), Srgb(0.62, 0.5, 0.33), ESurface::Wood);
			}
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
		const FLinearColor Wood = Srgb(0.42, 0.32, 0.2);
		const FLinearColor Dark = Srgb(0.3, 0.22, 0.14);
		for (const double SX : { -1.0, 1.0 })
		{
			for (const double SY : { -1.0, 1.0 })
			{
				AddCylinder(Acc, FVector3d(SX * 0.95, SY * 0.95, -0.3), FVector3d(SX * 0.65, SY * 0.65, 4.55), 0.09, 6, Wood, Wood, ESurface::Wood);
			}
		}
		// Cross braces.
		AddCylinder(Acc, FVector3d(0.9, -0.9, 0.5), FVector3d(0.7, 0.7, 3.5), 0.05, 5, Dark, Dark, ESurface::Wood);
		AddCylinder(Acc, FVector3d(-0.9, 0.9, 0.5), FVector3d(-0.7, -0.7, 3.5), 0.05, 5, Dark, Dark, ESurface::Wood);
		AddBox(Acc, FVector3d(0, 0, 4.62), FVector3d(0.9, 0.9, 0.08), Wood, ESurface::Wood);
		AddBox(Acc, FVector3d(0, 0, 5.6), FVector3d(0.82, 0.82, 0.9), Dark, ESurface::Wood);
		AddBox(Acc, FVector3d(0.83, 0, 5.85), FVector3d(0.03, 0.6, 0.22), Srgb(0.05, 0.05, 0.05), ESurface::Paint);
		AddGableRoof(Acc, FVector3d(0, 0, 6.5), 1.05, 1.05, 0.45, Srgb(0.2, 0.2, 0.2));
		// Ladder at the back.
		for (const double Y : { -0.25, 0.25 })
		{
			AddCylinder(Acc, FVector3d(-1.8, Y, -0.2), FVector3d(-0.85, Y, 4.6), 0.04, 5, Wood, Wood, ESurface::Wood);
		}
		for (int32 Rung = 1; Rung < 14; ++Rung)
		{
			const double T = Rung / 14.0;
			const FVector3d P = FMath::Lerp(FVector3d(-1.8, 0, -0.2), FVector3d(-0.85, 0, 4.6), T);
			AddCylinder(Acc, P + FVector3d(0, -0.25, 0), P + FVector3d(0, 0.25, 0), 0.025, 4, Wood, Wood, ESurface::Wood);
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
		// Name board across the post, facing +X and -X.
		AddBox(Acc, FVector3d(0, 0, LandmarkMeshBuilder::SignNameBoardZ / Cm), FVector3d(0.03, 0.75, 0.15), Srgb(0.9, 0.88, 0.82), ESurface::Paint);
		AddBox(Acc, FVector3d(0, 0, LandmarkMeshBuilder::SignNameBoardZ / Cm), FVector3d(0.025, 0.77, 0.17), Srgb(0.55, 0.12, 0.1), ESurface::Paint);
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
