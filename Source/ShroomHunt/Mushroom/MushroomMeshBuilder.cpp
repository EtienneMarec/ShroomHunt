#include "Mushroom/MushroomMeshBuilder.h"

#include "Core/MeshAccumulator.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomMath.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Math/RandomStream.h"
#include "Mushroom/MushroomMorphology.h"

using namespace UE::Geometry;

FMushroomMeshDetail FMushroomMeshDetail::Near()
{
	return FMushroomMeshDetail();
}

FMushroomMeshDetail FMushroomMeshDetail::Far()
{
	FMushroomMeshDetail Detail;
	Detail.RadialSegments = 12;
	Detail.CapProfileSegments = 5;
	Detail.StemRings = 4;
	Detail.bGeometricGills = false;
	Detail.WartFraction = 0.5f;
	return Detail;
}

namespace MushroomMeshDetail
{
	/** One point of a 2D profile (R = distance to the axis, Z = height) revolved around Z. */
	struct FProfilePoint
	{
		double R = 0.0;
		double Z = 0.0;
		float V = 0.0f;
		FLinearColor Color = FLinearColor::White;
		EMushroomSurface Surface = EMushroomSurface::CapTop;
	};

	/**
	 * Revolves a profile. The profile must go "top -> outward -> down -> inward" (like the
	 * outline of a mushroom cut in half, read clockwise) so normals point outward
	 * (triangles are written with the right-handed rule, see FMeshAccumulator::AddTriangle).
	 * Points with R ~ 0 become a single pole vertex.
	 */
	void Revolve(FMeshAccumulator& Acc, const TArray<FProfilePoint>& Profile, int32 Segments,
		TFunctionRef<FVector3d(const FProfilePoint&, double Theta)> Place)
	{
		TArray<TArray<int32>> Rings;
		Rings.SetNum(Profile.Num());
		for (int32 K = 0; K < Profile.Num(); ++K)
		{
			const FProfilePoint& P = Profile[K];
			if (P.R < 1e-4)
			{
				Rings[K].Add(Acc.AddVertex(Place(P, 0.0), P.Color));
				continue;
			}
			Rings[K].Reserve(Segments);
			for (int32 J = 0; J < Segments; ++J)
			{
				const double Theta = UE_DOUBLE_TWO_PI * J / Segments;
				Rings[K].Add(Acc.AddVertex(Place(P, Theta), P.Color));
			}
		}

		for (int32 K = 0; K + 1 < Profile.Num(); ++K)
		{
			const TArray<int32>& RingA = Rings[K];
			const TArray<int32>& RingB = Rings[K + 1];
			const EMushroomSurface Surface = Profile[K + 1].Surface;
			const float VA = Profile[K].V;
			const float VB = Profile[K + 1].V;
			for (int32 J = 0; J < Segments; ++J)
			{
				const int32 J1 = (J + 1) % Segments;
				const float U0 = static_cast<float>(J) / Segments;
				const float U1 = static_cast<float>(J + 1) / Segments;
				const int32 A0 = RingA.Num() == 1 ? RingA[0] : RingA[J];
				const int32 A1 = RingA.Num() == 1 ? RingA[0] : RingA[J1];
				const int32 B0 = RingB.Num() == 1 ? RingB[0] : RingB[J];
				const int32 B1 = RingB.Num() == 1 ? RingB[0] : RingB[J1];
				Acc.AddTriangle(A0, B0, A1, FVector2f(U0, VA), FVector2f(U0, VB), FVector2f(U1, VA), Surface);
				Acc.AddTriangle(B0, B1, A1, FVector2f(U0, VB), FVector2f(U1, VB), FVector2f(U1, VA), Surface);
			}
		}
	}

	/** Small pyramid on a surface (wart, volva scale). Base corners CCW around Normal. */
	void AddPyramid(FMeshAccumulator& Acc, const FVector3d& Center, const FVector3d& Normal, double Size, double Height, const FLinearColor& Color, EMushroomSurface Surface)
	{
		const FVector3d N = Normal.GetSafeNormal();
		const FVector3d Reference = FMath::Abs(N.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX();
		const FVector3d T1 = FVector3d::CrossProduct(N, Reference).GetSafeNormal();
		const FVector3d T2 = FVector3d::CrossProduct(N, T1);
		const FVector3d Base = Center - N * (0.15 * Height);
		const double H = 0.5 * Size;

		const int32 B[4] = {
			Acc.AddVertex(Base - T1 * H - T2 * H, Color),
			Acc.AddVertex(Base + T1 * H - T2 * H, Color),
			Acc.AddVertex(Base + T1 * H + T2 * H, Color),
			Acc.AddVertex(Base - T1 * H + T2 * H, Color),
		};
		const int32 Apex = Acc.AddVertex(Center + N * Height, Color);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Acc.AddTriangle(B[Index], B[(Index + 1) % 4], Apex, FVector2f(0, 0), FVector2f(1, 0), FVector2f(0.5f, 1), Surface);
		}
	}

	using ShroomMesh::LerpSRGB;

	/** Height of the cap top above the cap origin at normalized radius U (0 center, 1 margin). */
	double CapTopZ(ECapShape Shape, double U, double H)
	{
		const double U2 = U * U;
		switch (Shape)
		{
		case ECapShape::Hemispherical:   return H * FMath::Sqrt(FMath::Max(0.0, 1.0 - U2));
		case ECapShape::Campanulate:     return H * FMath::Pow(FMath::Max(0.0, 1.0 - FMath::Pow(U, 2.2)), 0.75);
		case ECapShape::Conical:         return H * (1.0 - FMath::Pow(U, 1.2));
		case ECapShape::Convex:          return H * FMath::Pow(FMath::Max(0.0, 1.0 - U2), 0.6);
		case ECapShape::Umbonate:        return H * (0.8 * FMath::Pow(FMath::Max(0.0, 1.0 - U2), 0.6) + 0.3 * FMath::Exp(-FMath::Square(U / 0.22)));
		case ECapShape::Flat:            return H * FMath::Pow(FMath::Max(0.0, 1.0 - FMath::Pow(U, 6.0)), 0.4);
		case ECapShape::Depressed:       return H * (FMath::Pow(FMath::Max(0.0, 1.0 - FMath::Pow(U, 6.0)), 0.35) - 0.5 * FMath::Exp(-FMath::Square(U / 0.4)));
		case ECapShape::Infundibuliform: return H * (FMath::Pow(U, 1.4) - 0.35 * ShroomMath::SmoothStep01(0.86f, 1.0f, static_cast<float>(U)));
		default:                         return H * (1.0 - U2);
		}
	}

	/**
	 * Cap top of an individual: its two current shapes blended by age (the cap opens
	 * continuously), minus the sag of a rotting cap (the margin collapses first).
	 */
	double CapProfileZ(const FMushroomMorphology& M, double U, double H)
	{
		const double Blended = FMath::Lerp(CapTopZ(M.CapShape, U, H), CapTopZ(M.CapShapeB, U, H), static_cast<double>(M.CapShapeBlend));
		const double Radius = H / FMath::Max(static_cast<double>(M.CapHeightRatio), 0.05);
		return Blended - M.CapSag * 0.35 * Radius * FMath::Pow(U, 2.5);
	}

	/** Stem radius at normalized height T (0 base, 1 top) for a nominal radius R. */
	double StemRadius(EStemShape Shape, double T, double R)
	{
		double Radius;
		switch (Shape)
		{
		case EStemShape::Clavate:  Radius = R * (0.72 + 0.5 * FMath::Exp(-FMath::Square((T - 0.25) / 0.35))); break; // barrel/club, widest low
		case EStemShape::Bulbous:  Radius = R * (0.85 + 0.15 * (1.0 - T) + 0.9 * FMath::Exp(-FMath::Square((T - 0.06) / 0.1))); break;
		case EStemShape::Tapering: Radius = R * (1.0 - 0.45 * (1.0 - T)); break;
		case EStemShape::Rooting:  Radius = R * (1.0 - 0.6 * (1.0 - ShroomMath::SmoothStep01(0.0f, 0.25f, static_cast<float>(T)))); break;
		default:                   Radius = R * (1.0 + 0.08 * (1.0 - T)); break;
		}
		// Rounded base.
		return Radius * (0.45 + 0.55 * ShroomMath::SmoothStep01(0.0f, 0.07f, static_cast<float>(T)));
	}

	/** All geometry context shared by the parts of one mushroom. */
	struct FBuildContext
	{
		const FMushroomMorphology& M;
		const FMushroomMeshDetail& Detail;
		FMeshAccumulator& Acc;
		FRandomStream Random;

		double CapRadius = 0.0;
		double CapHeight = 0.0;
		double FleshCenter = 0.0;
		double EdgeThickness = 0.0;
		double StemTopRadius = 0.0;
		FVector3d CapOrigin = FVector3d::ZeroVector;
		FQuat4d CapTilt = FQuat4d::Identity;
		FVector3d BendDirection = FVector3d::UnitX();
		double OutlinePhase1 = 0.0;
		double OutlinePhase2 = 0.0;
		double WavePhase = 0.0;

		FBuildContext(const FMushroomMorphology& InM, const FMushroomMeshDetail& InDetail, FMeshAccumulator& InAcc)
			: M(InM), Detail(InDetail), Acc(InAcc), Random(static_cast<int32>(InM.ShapeSeed))
		{
		}

		double Flesh(double U) const
		{
			return FleshCenter * (1.0 - FMath::Pow(U, 1.6)) + EdgeThickness;
		}

		double UndersideZ(double U) const
		{
			return CapProfileZ(M, U, CapHeight) - Flesh(U);
		}

		/** Stem centerline offset at normalized height T (bend grows with T^2). */
		FVector3d StemCenter(double T) const
		{
			return BendDirection * (M.StemBend * M.StemLength * T * T);
		}

		/** Places a cap-local point (radius R, height Z, angle Theta) in mesh space. */
		FVector3d PlaceCap(double R, double Z, double Theta) const
		{
			const double U = CapRadius > 0.0 ? FMath::Clamp(R / CapRadius, 0.0, 1.2) : 0.0;
			const double Outline = 1.0 + M.CapAsymmetry * U * (0.6 * FMath::Sin(2.0 * Theta + OutlinePhase1) + 0.4 * FMath::Sin(3.0 * Theta + OutlinePhase2));
			const double Wave = M.MarginWave * 0.08 * CapRadius * FMath::Pow(U, 4.0) * FMath::Sin(M.MarginLobes * Theta + WavePhase);
			const double Radius = R * Outline;
			const FVector3d Local(Radius * FMath::Cos(Theta), Radius * FMath::Sin(Theta), Z + Wave);
			return CapOrigin + CapTilt.RotateVector(Local);
		}
	};

	void BuildStem(FBuildContext& Ctx)
	{
		const FMushroomMorphology& M = Ctx.M;
		const double R = 0.5 * M.StemDiameter;
		const double L = M.StemLength;
		const int32 Rings = FMath::Max(2, Ctx.Detail.StemRings);

		TArray<FProfilePoint> Profile;
		auto StemColor = [&M](double T)
		{
			FLinearColor Color = LerpSRGB(M.StemColorBase, M.StemColorTop, static_cast<float>(T));
			Color.A = 0.0f;
			if (M.bReticulation && T > 1.0 - M.ReticulationExtent)
			{
				Color = LerpSRGB(Color, M.ReticulationColor, 0.3f);
				Color.A = 1.0f; // material hint: reticulated zone
			}
			return Color;
		};

		Profile.Add({ 0.0, L, 1.0f, StemColor(1.0), EMushroomSurface::Stem });
		for (int32 Index = 0; Index <= Rings; ++Index)
		{
			// Rings get denser towards the base so the rounded foot is smooth.
			const double T = 1.0 - FMath::Sin(0.5 * UE_DOUBLE_PI * Index / Rings);
			Profile.Add({ StemRadius(M.StemShape, T, R), T * L, static_cast<float>(T), StemColor(T), EMushroomSurface::Stem });
		}
		Profile.Add({ 0.0, 0.0, 0.0f, StemColor(0.0), EMushroomSurface::Stem });

		Revolve(Ctx.Acc, Profile, Ctx.Detail.RadialSegments, [&Ctx, L](const FProfilePoint& P, double Theta)
		{
			const double T = L > 0.0 ? P.Z / L : 0.0;
			return Ctx.StemCenter(T) + FVector3d(P.R * FMath::Cos(Theta), P.R * FMath::Sin(Theta), P.Z);
		});
	}

	void BuildRing(FBuildContext& Ctx)
	{
		const FMushroomMorphology& M = Ctx.M;
		if (M.Ring != ERingType::Skirt && M.Ring != ERingType::Fragile)
		{
			return;
		}
		const double L = M.StemLength;
		const double H = M.RingHeight * L;
		const double T = M.RingHeight;
		const double Rs = StemRadius(M.StemShape, T, 0.5 * M.StemDiameter);
		const double Spread = M.Ring == ERingType::Skirt ? 1.0 : 0.55;
		const double Drop = (M.Ring == ERingType::Skirt ? 0.1 : 0.05) * L;
		const FLinearColor Color(M.RingColor.R, M.RingColor.G, M.RingColor.B, 0.0f);

		// Hanging skirt: outer surface going down and out, lip, inner surface back up to the stem.
		TArray<FProfilePoint> Profile;
		Profile.Add({ Rs * 1.0, H + 0.005 * L, 0.0f, Color, EMushroomSurface::Ring });
		Profile.Add({ Rs * (1.0 + 0.35 * Spread), H - 0.3 * Drop, 0.3f, Color, EMushroomSurface::Ring });
		Profile.Add({ Rs * (1.0 + 0.75 * Spread), H - 0.9 * Drop, 0.6f, Color, EMushroomSurface::Ring });
		Profile.Add({ Rs * (1.0 + 0.8 * Spread), H - Drop, 0.7f, Color, EMushroomSurface::Ring });
		Profile.Add({ Rs * (1.0 + 0.68 * Spread), H - 0.9 * Drop, 0.8f, Color, EMushroomSurface::Ring });
		Profile.Add({ Rs * (1.0 + 0.25 * Spread), H - 0.3 * Drop, 0.9f, Color, EMushroomSurface::Ring });
		Profile.Add({ Rs * 0.98, H - 0.08 * Drop, 1.0f, Color, EMushroomSurface::Ring });

		Revolve(Ctx.Acc, Profile, Ctx.Detail.RadialSegments, [&Ctx, T](const FProfilePoint& P, double Theta)
		{
			return Ctx.StemCenter(T) + FVector3d(P.R * FMath::Cos(Theta), P.R * FMath::Sin(Theta), P.Z);
		});
	}

	void BuildVolva(FBuildContext& Ctx)
	{
		const FMushroomMorphology& M = Ctx.M;
		const double L = M.StemLength;
		const double R = 0.5 * M.StemDiameter;
		const FLinearColor Color(M.VolvaColor.R, M.VolvaColor.G, M.VolvaColor.B, 0.0f);

		if (M.Volva == EVolvaType::Friable)
		{
			// Concentric rows of scales around the bulb.
			const int32 Rows = 3;
			const int32 PerRow = FMath::Max(4, FMath::RoundToInt(12 * Ctx.Detail.WartFraction));
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const double T = 0.04 + 0.045 * Row;
				const double Rs = StemRadius(M.StemShape, T, R);
				for (int32 Index = 0; Index < PerRow; ++Index)
				{
					const double Theta = UE_DOUBLE_TWO_PI * (Index + 0.5 * Row + Ctx.Random.FRandRange(-0.2f, 0.2f)) / PerRow;
					const FVector3d Outward(FMath::Cos(Theta), FMath::Sin(Theta), 0.15);
					const FVector3d Center = Ctx.StemCenter(T) + FVector3d(Rs * FMath::Cos(Theta), Rs * FMath::Sin(Theta), T * L);
					const double Size = Rs * 0.35 * Ctx.Random.FRandRange(0.7f, 1.2f);
					AddPyramid(Ctx.Acc, Center, Outward, Size, Size * 0.5, Color, EMushroomSurface::Volva);
				}
			}
		}
		else if (M.Volva == EVolvaType::Sac)
		{
			// Membranous cup around the base: lip, outer wall down, bottom, inner wall back up.
			const double Rb = StemRadius(M.StemShape, 0.08, R);
			const double Top = 0.16 * L;
			TArray<FProfilePoint> Profile;
			Profile.Add({ Rb * 1.38, Top, 0.0f, Color, EMushroomSurface::Volva });
			Profile.Add({ Rb * 1.5, Top - 0.01 * L, 0.1f, Color, EMushroomSurface::Volva });
			Profile.Add({ Rb * 1.45, 0.05 * L, 0.5f, Color, EMushroomSurface::Volva });
			Profile.Add({ Rb * 1.2, -0.005 * L, 0.8f, Color, EMushroomSurface::Volva });
			Profile.Add({ Rb * 1.05, 0.01 * L, 0.9f, Color, EMushroomSurface::Volva });
			Profile.Add({ Rb * 1.3, Top - 0.015 * L, 1.0f, Color, EMushroomSurface::Volva });
			Revolve(Ctx.Acc, Profile, Ctx.Detail.RadialSegments, [](const FProfilePoint& P, double Theta)
			{
				return FVector3d(P.R * FMath::Cos(Theta), P.R * FMath::Sin(Theta), P.Z);
			});
		}
		// Bulb volva: already given by the bulbous stem shape.
	}

	void BuildCap(FBuildContext& Ctx)
	{
		const FMushroomMorphology& M = Ctx.M;
		const double R = Ctx.CapRadius;
		const int32 Segments = FMath::Max(3, Ctx.Detail.CapProfileSegments);

		auto TopColor = [&M](double U)
		{
			FLinearColor Color = LerpSRGB(M.CapColorCenter, M.CapColorMargin, static_cast<float>(FMath::Pow(U, 1.5)));
			Color.A = 0.0f;
			return Color;
		};
		FLinearColor UnderColor = M.UndersideColor;
		UnderColor.A = 0.0f;

		TArray<FProfilePoint> Profile;

		// Top surface: center to margin, denser near the margin.
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const double U = FMath::Sin(0.5 * UE_DOUBLE_PI * Index / Segments);
			Profile.Add({ U * R, CapProfileZ(M, U, Ctx.CapHeight), static_cast<float>(0.5 * U), TopColor(U), EMushroomSurface::CapTop });
		}

		// Rim.
		const double MarginZ = CapProfileZ(M, 1.0, Ctx.CapHeight);
		double UndersideStartU = 1.0;
		if (M.Margin == EMarginType::Inrolled)
		{
			Profile.Add({ R * 0.99, MarginZ - 0.08 * R, 0.52f, TopColor(1.0), EMushroomSurface::CapTop });
			Profile.Add({ R * 0.93, MarginZ - 0.13 * R, 0.55f, TopColor(1.0), EMushroomSurface::CapTop });
			UndersideStartU = 0.9;
		}
		else
		{
			Profile.Add({ R * 1.005, MarginZ - 0.5 * Ctx.EdgeThickness, 0.52f, TopColor(1.0), EMushroomSurface::CapTop });
		}

		// Underside: margin back to the stem.
		const double StemU = FMath::Clamp(Ctx.StemTopRadius / R, 0.05, 0.6);
		const int32 UnderSegments = FMath::Max(2, Segments / 2);
		for (int32 Index = 0; Index <= UnderSegments; ++Index)
		{
			const double Alpha = static_cast<double>(Index) / UnderSegments;
			const double U = FMath::Lerp(UndersideStartU, StemU, Alpha);
			Profile.Add({ U * R, Ctx.UndersideZ(U), static_cast<float>(0.55 + 0.45 * Alpha), UnderColor, EMushroomSurface::CapUnderside });
		}
		if (M.Attachment == EGillAttachment::Decurrent)
		{
			// The fertile surface runs down onto the stem.
			Profile.Add({ Ctx.StemTopRadius * 1.02, Ctx.UndersideZ(StemU) - 0.12 * R, 1.0f, UnderColor, EMushroomSurface::CapUnderside });
		}
		// Close the cap inside the stem so no gap shows where the tilted cap meets the stem.
		Profile.Add({ Ctx.StemTopRadius * 0.5, Ctx.UndersideZ(StemU) + 0.05 * R, 1.0f, UnderColor, EMushroomSurface::CapUnderside });

		Revolve(Ctx.Acc, Profile, Ctx.Detail.RadialSegments, [&Ctx](const FProfilePoint& P, double Theta)
		{
			return Ctx.PlaceCap(P.R, P.Z, Theta);
		});
	}

	void BuildGills(FBuildContext& Ctx)
	{
		const FMushroomMorphology& M = Ctx.M;
		if (!Ctx.Detail.bGeometricGills || M.GillCount <= 0 || (M.UndersideType != EUndersideType::Gills && M.UndersideType != EUndersideType::Ridges))
		{
			return;
		}

		const bool bRidges = M.UndersideType == EUndersideType::Ridges;
		const double R = Ctx.CapRadius;
		const double StemU = FMath::Clamp(Ctx.StemTopRadius / R, 0.05, 0.6) * 1.05;
		const int32 Total = bRidges ? M.GillCount : M.GillCount * 2; // gills: long + short (lamellulae) alternately
		const double Thickness = (bRidges ? 0.03 : 0.012) * R;
		const double MaxDepth = (bRidges ? 0.3 : 0.6) * Ctx.FleshCenter + 0.02 * R;
		const int32 Steps = 6;
		FLinearColor Color = M.UndersideColor;
		Color.A = 0.0f;
		const FLinearColor EdgeColor = LerpSRGB(Color, FLinearColor(Color.R * 0.85f, Color.G * 0.85f, Color.B * 0.85f, 0.0f), 0.6f);

		for (int32 Fin = 0; Fin < Total; ++Fin)
		{
			const bool bShort = !bRidges && (Fin % 2 == 1);
			const double U0 = bShort ? FMath::Lerp(StemU, 0.96, 0.45) : StemU;
			const double Theta0 = UE_DOUBLE_TWO_PI * (Fin + Ctx.Random.FRandRange(-0.15f, 0.15f)) / Total;
			const double Phase = Ctx.Random.FRandRange(0.0f, 6.28f);

			TArray<int32> Top[2];
			TArray<int32> Bottom[2];
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const double S = static_cast<double>(Step) / Steps;
				const double U = FMath::Lerp(U0, 0.96, S);
				const double Theta = Theta0 + (bRidges ? 0.06 * FMath::Sin(3.0 * UE_DOUBLE_PI * S + Phase) : 0.0);
				const double Z = Ctx.UndersideZ(U);
				const double Profile = FMath::Pow(FMath::Sin(UE_DOUBLE_PI * (0.12 + 0.88 * S)), 0.6);
				const double Depth = MaxDepth * Profile * (bRidges ? (1.3 - 0.6 * S) : 1.0) * (bShort ? 0.85 : 1.0);
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const double Offset = (Side == 0 ? -0.5 : 0.5) * Thickness;
					const double ThetaOffset = Offset / FMath::Max(U * R, 0.01);
					Top[Side].Add(Ctx.Acc.AddVertex(Ctx.PlaceCap(U * R, Z + 0.01 * R, Theta + ThetaOffset), Color));
					Bottom[Side].Add(Ctx.Acc.AddVertex(Ctx.PlaceCap(U * R, Z - Depth, Theta + ThetaOffset), EdgeColor));
				}
			}

			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const float S0 = static_cast<float>(Step) / Steps;
				const float S1 = static_cast<float>(Step + 1) / Steps;
				// Side 1 faces +tangent (counter-clockwise neighbour), side 0 faces -tangent: opposite windings.
				Ctx.Acc.AddTriangle(Top[1][Step], Top[1][Step + 1], Bottom[1][Step], FVector2f(S0, 0), FVector2f(S1, 0), FVector2f(S0, 1), EMushroomSurface::Gills);
				Ctx.Acc.AddTriangle(Top[1][Step + 1], Bottom[1][Step + 1], Bottom[1][Step], FVector2f(S1, 0), FVector2f(S1, 1), FVector2f(S0, 1), EMushroomSurface::Gills);
				Ctx.Acc.AddTriangle(Top[0][Step], Bottom[0][Step], Top[0][Step + 1], FVector2f(S0, 0), FVector2f(S0, 1), FVector2f(S1, 0), EMushroomSurface::Gills);
				Ctx.Acc.AddTriangle(Top[0][Step + 1], Bottom[0][Step], Bottom[0][Step + 1], FVector2f(S1, 0), FVector2f(S0, 1), FVector2f(S1, 1), EMushroomSurface::Gills);
			}
		}
	}

	void BuildWarts(FBuildContext& Ctx)
	{
		const FMushroomMorphology& M = Ctx.M;
		const int32 Count = FMath::RoundToInt(M.WartCount * Ctx.Detail.WartFraction);
		if (!M.bWarts || Count <= 0)
		{
			return;
		}
		const double R = Ctx.CapRadius;
		const FLinearColor Color(M.WartColor.R, M.WartColor.G, M.WartColor.B, 0.0f);
		constexpr double GoldenAngle = 2.39996322972865332;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			// Sunflower distribution over the cap top, with jitter.
			const double U = 0.93 * FMath::Sqrt((Index + 0.5) / Count) * Ctx.Random.FRandRange(0.95f, 1.03f);
			const double Theta = Index * GoldenAngle + Ctx.Random.FRandRange(-0.2f, 0.2f);
			const double Du = 0.01;
			const double Dt = 0.01;
			auto Surface = [&Ctx, R](double SampleU, double SampleTheta)
			{
				return Ctx.PlaceCap(SampleU * R, CapProfileZ(Ctx.M, SampleU, Ctx.CapHeight), SampleTheta);
			};
			const FVector3d Center = Surface(U, Theta);
			const FVector3d AlongU = Surface(FMath::Min(U + Du, 1.0), Theta) - Surface(FMath::Max(U - Du, 0.0), Theta);
			const FVector3d AlongTheta = Surface(FMath::Max(U, 0.02), Theta + Dt) - Surface(FMath::Max(U, 0.02), Theta - Dt);
			FVector3d Normal = FVector3d::CrossProduct(AlongU, AlongTheta).GetSafeNormal();
			if (Normal.IsNearlyZero())
			{
				Normal = Ctx.CapTilt.RotateVector(FVector3d::UnitZ());
			}
			const double Size = R * 0.07 * (1.25 - 0.6 * U) * Ctx.Random.FRandRange(0.7f, 1.3f);
			AddPyramid(Ctx.Acc, Center, Normal, Size, Size * 0.45, Color, EMushroomSurface::Warts);
		}
	}
}

namespace MushroomMeshBuilder
{
	void Build(const FMushroomMorphology& Morphology, const FMushroomMeshDetail& Detail, FDynamicMesh3& OutMesh)
	{
		using namespace MushroomMeshDetail;

		FMeshAccumulator Acc;
		FBuildContext Ctx(Morphology, Detail, Acc);
		const FMushroomMorphology& M = Morphology;

		Ctx.CapRadius = FMath::Max(0.25, 0.5 * static_cast<double>(M.CapDiameter));
		Ctx.CapHeight = M.CapHeightRatio * Ctx.CapRadius;
		Ctx.FleshCenter = M.CapFleshRatio * Ctx.CapRadius;
		Ctx.EdgeThickness = 0.03 * Ctx.CapRadius;
		Ctx.StemTopRadius = StemRadius(M.StemShape, 1.0, 0.5 * M.StemDiameter);
		Ctx.OutlinePhase1 = Ctx.Random.FRandRange(0.0f, 6.28f);
		Ctx.OutlinePhase2 = Ctx.Random.FRandRange(0.0f, 6.28f);
		Ctx.WavePhase = Ctx.Random.FRandRange(0.0f, 6.28f);

		const double BendAzimuth = FMath::DegreesToRadians(static_cast<double>(M.StemBendAzimuthDeg));
		Ctx.BendDirection = FVector3d(FMath::Cos(BendAzimuth), FMath::Sin(BendAzimuth), 0.0);

		// Cap sits so that its underside meets the stem top (decurrent: a bit higher).
		const double StemU = FMath::Clamp(Ctx.StemTopRadius / Ctx.CapRadius, 0.05, 0.6);
		double CapLift = -Ctx.UndersideZ(StemU);
		if (M.Attachment == EGillAttachment::Decurrent)
		{
			CapLift += 0.12 * Ctx.CapRadius;
		}
		Ctx.CapOrigin = Ctx.StemCenter(1.0) + FVector3d(0.0, 0.0, M.StemLength + CapLift);
		const double TiltAzimuth = FMath::DegreesToRadians(static_cast<double>(M.CapTiltAzimuthDeg));
		const FVector3d TiltAxis(-FMath::Sin(TiltAzimuth), FMath::Cos(TiltAzimuth), 0.0);
		Ctx.CapTilt = FQuat4d(TiltAxis, FMath::DegreesToRadians(static_cast<double>(M.CapTiltDeg)));

		BuildStem(Ctx);
		BuildRing(Ctx);
		BuildVolva(Ctx);
		BuildCap(Ctx);
		BuildGills(Ctx);
		BuildWarts(Ctx);

		Acc.Emit(OutMesh);
	}
}
