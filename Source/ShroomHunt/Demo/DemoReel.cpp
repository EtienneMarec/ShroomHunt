#include "Demo/DemoReel.h"

#include "Forest/ForestField.h"
#include "Forest/ForestLandmarks.h"
#include "Forest/ForestPaths.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Core/ShroomHash.h"
#include "Mycology/SpawnModel.h"
#include "Time/CalendarMath.h"
#include "Time/TimeTypes.h"
#include "Vegetation/VegetationObstacles.h"
#include "Vegetation/VegetationScatter.h"

namespace DemoReelDetail
{
	constexpr int32 Directions = 16;
	/** Rise shots: least distance from the forest's boundary (m). */
	constexpr double RiseMarginM = 160.0;
	/** Mushroom close-ups: least distance from the forest's boundary (m). */
	constexpr double MushroomMarginM = 60.0;

	double Ground(const FForestField& Field, double X, double Y)
	{
		const double Size = Field.GetSizeM();
		return Field.GetHeights().Sample(static_cast<float>(FMath::Clamp(X, 0.0, Size - 1.0)), static_cast<float>(FMath::Clamp(Y, 0.0, Size - 1.0)));
	}

	FVector3d OnGround(const FForestField& Field, const FVector2d& P, double Above)
	{
		return FVector3d(P.X, P.Y, Ground(Field, P.X, P.Y) + Above);
	}

	FVector2d Dir(double Angle)
	{
		return FVector2d(FMath::Cos(Angle), FMath::Sin(Angle));
	}

	bool Inside(const FForestField& Field, const FVector2d& P, double Margin)
	{
		const double Size = Field.GetSizeM();
		return P.X >= Margin && P.Y >= Margin && P.X <= Size - Margin && P.Y <= Size - Margin;
	}

	struct FCandidate
	{
		FVector2d Position;
		double Score = 0.0;
	};

	/** Scans the field on a grid, keeps scored candidates, and returns the Pick-th best among places at least Spacing apart. */
	bool PickCandidate(const FForestField& Field, double Step, double Spacing, int32 Pick,
		TFunctionRef<bool(const FVector2d&, const FForestSample&, double&)> Score, FVector2d& OutPosition)
	{
		TArray<FCandidate> Candidates;
		const double Size = Field.GetSizeM();
		for (double Y = 20.0; Y < Size - 20.0; Y += Step)
		{
			for (double X = 20.0; X < Size - 20.0; X += Step)
			{
				const FVector2d P(X, Y);
				double S = 0.0;
				if (Score(P, Field.Sample(static_cast<float>(X), static_cast<float>(Y)), S))
				{
					Candidates.Add({ P, S });
				}
			}
		}
		Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
		TArray<FVector2d> Chosen;
		for (const FCandidate& C : Candidates)
		{
			if (Chosen.ContainsByPredicate([&](const FVector2d& Q) { return FVector2d::Distance(Q, C.Position) < Spacing; }))
			{
				continue;
			}
			Chosen.Add(C.Position);
			if (Chosen.Num() > Pick)
			{
				OutPosition = C.Position;
				return true;
			}
		}
		if (Chosen.Num() > 0)
		{
			OutPosition = Chosen.Last();
			return true;
		}
		return false;
	}

	/** Direction (radians) in which Value is highest at Distance from P. */
	double BestDirection(const FForestField& Field, const FVector2d& P, double Distance, TFunctionRef<double(const FForestSample&)> Value)
	{
		double Best = -1e30;
		double BestAngle = 0.0;
		for (int32 K = 0; K < Directions; ++K)
		{
			const double Angle = UE_DOUBLE_TWO_PI * K / Directions;
			const FVector2d Q = P + Dir(Angle) * Distance;
			if (!Inside(Field, Q, 5.0))
			{
				continue;
			}
			const double V = Value(Field.Sample(static_cast<float>(Q.X), static_cast<float>(Q.Y)));
			if (V > Best)
			{
				Best = V;
				BestAngle = Angle;
			}
		}
		return BestAngle;
	}

	/** Position along a polyline at arc length D (clamped), and the direction there. */
	FVector3d AlongTrack(const TArray<FVector3d>& Track, double D)
	{
		if (Track.Num() == 0)
		{
			return FVector3d::ZeroVector;
		}
		double Walked = 0.0;
		for (int32 K = 1; K < Track.Num(); ++K)
		{
			const double Length = FVector2d::Distance(FVector2d(Track[K - 1].X, Track[K - 1].Y), FVector2d(Track[K].X, Track[K].Y));
			if (Walked + Length >= D)
			{
				return FMath::Lerp(Track[K - 1], Track[K], Length > 1e-6 ? (D - Walked) / Length : 0.0);
			}
			Walked += Length;
		}
		return Track.Last();
	}

	double Ease(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Height of a low camera at P looking at mushrooms at Center: EyeM above them, never in a bank. */
	double MushroomCameraZ(const FForestField& Field, const FVector2d& P, const FVector2d& Center, double EyeM)
	{
		return FMath::Max(Ground(Field, Center.X, Center.Y) + EyeM, Ground(Field, P.X, P.Y) + 0.25);
	}
}

namespace DemoReel
{
	FString GetDefaultPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Demo"), TEXT("DemoReel.json")));
	}

	bool Parse(const FString& JsonText, FDemoReel& Out, TArray<FString>& OutErrors)
	{
		FText Fail;
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &Out, 0, 0, false, &Fail))
		{
			OutErrors.Add(TEXT("DemoReel.json: ") + Fail.ToString());
			return false;
		}
		const int32 Before = OutErrors.Num();
		for (int32 Index = 0; Index < Out.Shots.Num(); ++Index)
		{
			const FDemoShot& Shot = Out.Shots[Index];
			const FString Field = FString::Printf(TEXT("DemoReel.json: Shots[%d] (%s)"), Index, *Shot.Title);
			if (Shot.Seconds <= 0.0f || Shot.EyeM <= 0.0f || Shot.FovDeg < 10.0f || Shot.FovDeg > 120.0f)
			{
				OutErrors.Add(Field + TEXT(": Seconds and EyeM > 0, FovDeg in [10, 120] required"));
			}
			if (!StaticEnum<ESeason>() || StaticEnum<ESeason>()->GetValueByNameString(Shot.Season) == INDEX_NONE)
			{
				OutErrors.Add(Field + TEXT(": Season must be Spring, Summer, Autumn or Winter"));
			}
			if (Shot.Hour < 0.0f || Shot.Hour >= 24.0f || Shot.Day < 1)
			{
				OutErrors.Add(Field + TEXT(": Hour in [0, 24) and Day >= 1 required"));
			}
			if (Shot.Place == EDemoPlace::OldStand && Shot.Genus.IsNone())
			{
				OutErrors.Add(Field + TEXT(": OldStand needs a Genus"));
			}
			if (Shot.Place == EDemoPlace::Landmark && Shot.Landmark.IsEmpty())
			{
				OutErrors.Add(Field + TEXT(": Landmark needs a Landmark kind"));
			}
			if (Shot.Place == EDemoPlace::Mushrooms && (Shot.Species.IsNone() || Shot.DistanceM <= 0.2f || Shot.MinFruits < 1))
			{
				OutErrors.Add(Field + TEXT(": Mushrooms needs a Species, DistanceM > 0.2 and MinFruits >= 1"));
			}
		}
		return OutErrors.Num() == Before;
	}

	bool Load(FDemoReel& Out, TArray<FString>& OutErrors)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
		{
			OutErrors.Add(GetDefaultPath() + TEXT(": cannot read file"));
			return false;
		}
		return Parse(Text, Out, OutErrors);
	}

	FDemoPlacement FindPlace(const FForestField& Field, const FDemoShot& Shot, const FVegetationDefinition* Vegetation)
	{
		using namespace DemoReelDetail;
		FDemoPlacement Out;
		const FForestPathNetwork& Network = Field.GetPathNetwork();

		auto PathWalk = [&](EForestPathRank Rank)
		{
			TArray<const FForestPath*> Paths;
			for (const FForestPath& Path : Network.Paths)
			{
				if (Path.Rank == Rank && Path.Points.Num() >= 2)
				{
					Paths.Add(&Path);
				}
			}
			Paths.Sort([](const FForestPath& A, const FForestPath& B) { return A.GetLengthM() > B.GetLengthM(); });
			if (Paths.Num() == 0)
			{
				return;
			}
			const FForestPath& Path = *Paths[Shot.Pick % Paths.Num()];
			// Start a little into the path, away from the junction.
			const int32 First = FMath::Min(Path.Points.Num() / 6, Path.Points.Num() - 2);
			for (int32 K = First; K < Path.Points.Num(); ++K)
			{
				const float Height = Path.Heights.IsValidIndex(K) ? Path.Heights[K] : static_cast<float>(Ground(Field, Path.Points[K].X, Path.Points[K].Y));
				Out.Track.Add(FVector3d(Path.Points[K].X, Path.Points[K].Y, Height));
			}
			Out.bFound = Out.Track.Num() >= 2;
			Out.Description = FString::Printf(TEXT("path of %.0f m from (%.0f, %.0f)"), Path.GetLengthM(), Out.Track[0].X, Out.Track[0].Y);
		};

		auto Stand = [&](const FVector2d& P, const FVector2d& Look, double LookUp)
		{
			Out.Camera = OnGround(Field, P, 0.0);
			Out.Target = OnGround(Field, Look, Shot.EyeM + LookUp);
			Out.bFound = true;
			Out.Description = FString::Printf(TEXT("camera (%.0f, %.0f) looking at (%.0f, %.0f)"), P.X, P.Y, Look.X, Look.Y);
		};

		FVector2d P;
		switch (Shot.Place)
		{
		case EDemoPlace::Ride: PathWalk(EForestPathRank::Ride); break;
		case EDemoPlace::Road: PathWalk(EForestPathRank::Road); break;
		case EDemoPlace::Footpath: PathWalk(EForestPathRank::Footpath); break;
		case EDemoPlace::Clearing:
			if (PickCandidate(Field, 6.0, 80.0, Shot.Pick, [&](const FVector2d& Q, const FForestSample& S, double& Score)
				{
					// A rising camera sees far: keep it well inside the forest, or the edge of the world shows.
					if (S.bWater || S.Canopy > 0.12f || S.PathDistanceM < 6.0f || (Shot.Kind == EDemoShotKind::Rise && !Inside(Field, Q, RiseMarginM)))
					{
						return false;
					}
					// A gap in the forest, not an empty clear-cut: open around the camera (8 m), trees
					// on most of a ring 28 m away (wide clear-cuts showed bare ground and no tree).
					double Open = 0.0;
					double Trees = 0.0;
					for (int32 K = 0; K < 8; ++K)
					{
						const FVector2d Near = Q + Dir(UE_DOUBLE_TWO_PI * K / 8) * 8.0;
						const FVector2d Far = Q + Dir(UE_DOUBLE_TWO_PI * K / 8) * 28.0;
						Open += 1.0 - Field.Sample(static_cast<float>(Near.X), static_cast<float>(Near.Y)).Canopy;
						Trees += Field.Sample(static_cast<float>(Far.X), static_cast<float>(Far.Y)).Canopy;
					}
					Score = Open + 1.5 * Trees;
					// Seen from above, a smaller gap is enough (and far from the boundary there are few big ones).
					return Trees >= (Shot.Kind == EDemoShotKind::Rise ? 2.5 : 4.0);
				}, P))
			{
				// Look toward the forest edge (where the canopy is densest 30 m away); a rising camera also
				// looks toward the middle of the forest, away from its boundary.
				const FVector2d ToMiddle = (FVector2d(Field.GetSizeM() * 0.5, Field.GetSizeM() * 0.5) - P).GetSafeNormal();
				double BestScore = -1e30;
				double Angle = 0.0;
				for (int32 K = 0; K < Directions; ++K)
				{
					const double A = UE_DOUBLE_TWO_PI * K / Directions;
					const FVector2d Q = P + Dir(A) * 30.0;
					const double Inward = Shot.Kind == EDemoShotKind::Rise ? 1.5 * FVector2d::DotProduct(Dir(A), ToMiddle) : 0.0;
					const double Score = Field.Sample(static_cast<float>(Q.X), static_cast<float>(Q.Y)).Canopy + Inward;
					if (Score > BestScore)
					{
						BestScore = Score;
						Angle = A;
					}
				}
				Stand(P, P + Dir(Angle) * 30.0, 3.0);
			}
			break;
		case EDemoPlace::Pond:
			if (PickCandidate(Field, 4.0, 80.0, Shot.Pick, [&](const FVector2d& Q, const FForestSample& S, double& Score)
				{
					if (!S.bWater)
					{
						return false;
					}
					int32 Water = 0;
					for (int32 K = 0; K < 8; ++K)
					{
						const FVector2d R = Q + Dir(UE_DOUBLE_TWO_PI * K / 8) * 8.0;
						Water += Field.Sample(static_cast<float>(R.X), static_cast<float>(R.Y)).bWater ? 1 : 0;
					}
					Score = Water;
					return Water >= 5;
				}, P))
			{
				// Walk out from the center to the first dry ground, then 3 m further onto the bank.
				const double Angle = BestDirection(Field, P, 15.0, [](const FForestSample& S) { return S.bWater ? -1.0 : 1.0 - S.Canopy; });
				FVector2d Bank = P;
				for (double D = 1.0; D < 60.0; D += 1.0)
				{
					Bank = P + Dir(Angle) * D;
					if (!Field.Sample(static_cast<float>(Bank.X), static_cast<float>(Bank.Y)).bWater)
					{
						Bank += Dir(Angle) * 3.0;
						break;
					}
				}
				Stand(Bank, P, -Shot.EyeM + 0.3);
			}
			break;
		case EDemoPlace::OldStand:
		{
			const int32 GenusIndex = Field.GetGenera().IndexOfByKey(Shot.Genus);
			auto Score = [&](EStandAgeClass Wanted, const FForestSample& S, double& Value)
			{
				if (S.bWater || S.AgeClass != Wanted || S.PathDistanceM < 8.0f || S.Canopy < 0.6f || !S.StandWeights.IsValidIndex(GenusIndex))
				{
					return false;
				}
				Value = S.StandWeights[GenusIndex] * S.Canopy;
				return S.StandWeights[GenusIndex] > 0.45f;
			};
			bool bFound = GenusIndex != INDEX_NONE && PickCandidate(Field, 8.0, 60.0, Shot.Pick,
				[&](const FVector2d&, const FForestSample& S, double& Value) { return Score(EStandAgeClass::Old, S, Value); }, P);
			if (!bFound && GenusIndex != INDEX_NONE)
			{
				bFound = PickCandidate(Field, 8.0, 60.0, Shot.Pick,
					[&](const FVector2d&, const FForestSample& S, double& Value) { return Score(EStandAgeClass::Mature, S, Value); }, P);
			}
			if (bFound)
			{
				const double Angle = BestDirection(Field, P, 20.0, [&](const FForestSample& S) { return S.StandWeights.IsValidIndex(GenusIndex) ? S.StandWeights[GenusIndex] * S.Canopy : 0.0; });
				Stand(P, P + Dir(Angle) * 20.0, 4.0);
			}
			break;
		}
		case EDemoPlace::YoungStand:
			if (PickCandidate(Field, 8.0, 60.0, Shot.Pick, [&](const FVector2d&, const FForestSample& S, double& Score)
				{
					const bool bYoung = S.AgeClass == EStandAgeClass::Regeneration || S.AgeClass == EStandAgeClass::Pole;
					Score = S.Canopy;
					return bYoung && !S.bWater && S.PathDistanceM > 2.5f && S.PathDistanceM < 8.0f;
				}, P))
			{
				const double Angle = BestDirection(Field, P, 15.0, [](const FForestSample& S)
					{
						return (S.AgeClass == EStandAgeClass::Regeneration || S.AgeClass == EStandAgeClass::Pole) && S.PathDistanceM > 8.0f ? 1.0 + S.Canopy : 0.0;
					});
				Stand(P, P + Dir(Angle) * 15.0, 0.5);
			}
			break;
		case EDemoPlace::Landmark:
		{
			TArray<const FForestLandmark*> Matching;
			for (const FForestLandmark& Mark : Field.GetLandmarks())
			{
				if (Shot.Landmark.Equals(ForestLandmarks::KindName(Mark.Kind), ESearchCase::IgnoreCase))
				{
					Matching.Add(&Mark);
				}
			}
			if (Matching.Num() > 0)
			{
				const FForestLandmark& Mark = *Matching[Shot.Pick % Matching.Num()];
				const FVector2d Where(Mark.PositionM.X, Mark.PositionM.Y);
				const double Yaw = FMath::DegreesToRadians(Mark.YawDeg);
				const double Near = 6.0 + Mark.LengthM * 0.5;
				const double Far = FMath::Max(Near, static_cast<double>(Shot.DistanceM));
				// Low crowns and shrubs around the landmark (the canopy alone said nothing of a thicket in front).
				FVegetationObstacleGrid ViewCover;
				if (Vegetation)
				{
					TArray<FVegetationInstance> Instances;
					const FBox2d Around(Where - FVector2d(Far + 5.0), Where + FVector2d(Far + 5.0));
					VegetationScatter::Scatter(Field, *Vegetation, EVegetationLayer::Trees, Around, Instances);
					VegetationScatter::Scatter(Field, *Vegetation, EVegetationLayer::Understory, Around, Instances);
					ViewCover.AddViewCover(*Vegetation, Instances);
				}
				// Seen whole, from a distance: the most open line of sight (low canopy between the camera
				// and the landmark), the front side and the farthest distance preferred.
				double BestScore = -1e30;
				FVector2d Best = Where + Dir(Yaw) * Near;
				double BestSide = 0.0;
				for (int32 K = 0; K < 16; ++K)
				{
					const double Angle = Yaw + UE_DOUBLE_TWO_PI * K / 16;
					for (const double Fraction : { 1.0, 0.75, 0.5 })
					{
						const double Distance = FMath::Max(Near, Far * Fraction);
						const FVector2d Camera = Where + Dir(Angle) * Distance;
						if (!Inside(Field, Camera, 5.0) || Field.Sample(static_cast<float>(Camera.X), static_cast<float>(Camera.Y)).bWater
							|| ViewCover.Overlaps(FVector2f(Camera), 0.6f))
						{
							continue;
						}
						double Open = 0.0;
						for (int32 Step = 0; Step < 8; ++Step)
						{
							const FVector2d Q = FMath::Lerp(Where + Dir(Angle) * Near * 0.5, Camera, (Step + 0.5) / 8.0);
							Open += (1.0 - Field.Sample(static_cast<float>(Q.X), static_cast<float>(Q.Y)).Canopy) / 8.0;
						}
						int32 Hidden = 0;
						for (int32 Step = 0; Step < 16; ++Step)
						{
							Hidden += ViewCover.Overlaps(FVector2f(FMath::Lerp(Where + Dir(Angle) * Near * 0.5, Camera, (Step + 0.5) / 16.0)), 0.2f) ? 1 : 0;
						}
						Open -= 2.0 * Hidden / 16.0;
						// A side asked for (ViewDeg) outweighs a slightly more open view.
						const double Side = Shot.ViewDeg != 0.0f ? 0.6 * FMath::Cos(Angle - Yaw - FMath::DegreesToRadians(static_cast<double>(Shot.ViewDeg))) : 0.15 * FMath::Cos(Angle - Yaw);
						const double Score = Open + Side + 0.1 * Distance / Far;
						if (Score > BestScore)
						{
							BestScore = Score;
							Best = Camera;
							BestSide = 360.0 * K / 16;
						}
					}
				}
				Stand(Best, Where, -Shot.EyeM + (Shot.LookAtM >= 0.0f ? Shot.LookAtM : 1.4));
				Out.Description += FString::Printf(TEXT(", %.0f deg from its front"), BestSide > 180.0 ? BestSide - 360.0 : BestSide);
			}
			break;
		}
		case EDemoPlace::Base:
			for (const FForestPathNode& Node : Network.Nodes)
			{
				if (Node.Kind == EForestPathNodeKind::Base)
				{
					const FVector2d Where(Node.PositionM.X, Node.PositionM.Y);
					const FVector2d Center(Field.GetSizeM() * 0.5, Field.GetSizeM() * 0.5);
					const FVector2d Away = (Where - Center).GetSafeNormal();
					Stand(Where + Away * 14.0, Where, -Shot.EyeM + 2.5);
					break;
				}
			}
			break;
		case EDemoPlace::Mushrooms:
			// Needs the spawn model: FindMushrooms, then PlaceMushroomShot.
			break;
		}
		if (Out.bFound)
		{
			Out.Description = FString(StaticEnum<EDemoPlace>()->GetNameStringByValue(static_cast<int64>(Shot.Place))) + TEXT(": ") + Out.Description;
		}
		return Out;
	}

	FDemoPose Evaluate(const FForestField& Field, const FDemoShot& Shot, const FDemoPlacement& Placement, float T)
	{
		using namespace DemoReelDetail;
		FDemoPose Pose;
		const double Time = FMath::Clamp(static_cast<double>(T), 0.0, 1.0);
		switch (Shot.Kind)
		{
		case EDemoShotKind::Walk:
		{
			const double D = Time * Shot.Seconds * Shot.SpeedMps;
			const FVector3d Here = AlongTrack(Placement.Track, D);
			const FVector3d Ahead = AlongTrack(Placement.Track, D + 8.0);
			// A light step rhythm (~0.75 m per step).
			const double Bob = 0.025 * FMath::Sin(D * UE_DOUBLE_TWO_PI / 0.75);
			Pose.Position = Here + FVector3d(0, 0, Shot.EyeM + Bob);
			Pose.LookAt = Ahead + FVector3d(0, 0, Shot.EyeM - 0.35);
			if (FVector2d::Distance(FVector2d(Pose.LookAt.X, Pose.LookAt.Y), FVector2d(Pose.Position.X, Pose.Position.Y)) < 0.5)
			{
				// End of the track: keep looking the way we walked.
				const FVector3d Back = AlongTrack(Placement.Track, FMath::Max(D - 8.0, 0.0));
				Pose.LookAt = Pose.Position + (Pose.Position - Back - FVector3d(0, 0, Shot.EyeM));
			}
			break;
		}
		case EDemoShotKind::Static:
		{
			const FVector2d From(Placement.Camera.X, Placement.Camera.Y);
			const FVector2d To(Placement.Target.X, Placement.Target.Y);
			const FVector2d P = From + (To - From).GetSafeNormal() * (Shot.DollyM * Ease(Time));
			Pose.Position = OnGround(Field, P, Shot.EyeM);
			Pose.LookAt = Placement.Target;
			break;
		}
		case EDemoShotKind::Rise:
		{
			const FVector2d From(Placement.Camera.X, Placement.Camera.Y);
			const FVector2d Forward = (FVector2d(Placement.Target.X, Placement.Target.Y) - From).GetSafeNormal();
			const double Up = Ease(Time);
			const FVector2d P = From + Forward * (0.3 * Shot.RiseM * Up);
			Pose.Position = OnGround(Field, P, FMath::Lerp(static_cast<double>(Shot.EyeM), static_cast<double>(Shot.RiseM), Up));
			// Tilt down over the canopy while rising.
			const FVector2d LookP = P + Forward * 40.0;
			Pose.LookAt = OnGround(Field, LookP, FMath::Lerp(static_cast<double>(Shot.EyeM) + 2.0, -5.0, Up));
			break;
		}
		case EDemoShotKind::Orbit:
		{
			const FVector2d Center(Placement.Target.X, Placement.Target.Y);
			const FVector2d Offset = FVector2d(Placement.Camera.X, Placement.Camera.Y) - Center;
			// Constant speed with soft ends.
			const double Angle = FMath::Atan2(Offset.Y, Offset.X) + FMath::DegreesToRadians(static_cast<double>(Shot.OrbitDeg)) * (0.5 * Time + 0.5 * Ease(Time));
			const FVector2d P = Center + Dir(Angle) * Offset.Size();
			Pose.Position = FVector3d(P.X, P.Y, MushroomCameraZ(Field, P, Center, Shot.EyeM));
			Pose.LookAt = Placement.Target;
			break;
		}
		}
		return Pose;
	}

	FDemoMushroomScene FindMushrooms(const FDemoShot& Shot, int32 Year, const FMushroomSpeciesRow& Species,
		const TArray<FMyceliumPatch>& Patches, const FSpawnContext& Context, TFunctionRef<bool(const FVector2f& PositionM)> IsCovered)
	{
		using namespace DemoReelDetail;
		FDemoMushroomScene Out;
		const int64 SeasonValue = StaticEnum<ESeason>()->GetValueByNameString(Shot.Season);
		const ESeason Season = SeasonValue == INDEX_NONE ? ESeason::Summer : static_cast<ESeason>(SeasonValue);
		const int32 SeasonDays = FMath::Max(1, Context.Calendar.SeasonLengthDays[static_cast<int32>(Season)]);

		struct FGroup
		{
			double Seconds = 0.0;
			int32 Year = 0;
			int32 Day = 0;
			uint32 PatchId = 0;
			FVector2d Center = FVector2d::ZeroVector;
			int32 Count = 0;
			double Score = 0.0;
		};
		TArray<FGroup> Groups;
		TArray<FFruitBody> Fruits;
		TArray<double> Weights;
		TSet<uint32> PatchesFound;
		// First only mushrooms in the open; when the species grows only under cover, any.
		for (int32 Pass = 0; Pass < 2 && Groups.Num() == 0; ++Pass)
		for (int32 Y = Year; Y < Year + 6 && PatchesFound.Num() <= Shot.Pick; ++Y)
		{
			for (int32 Day = 1; Day <= SeasonDays; ++Day)
			{
				const double Seconds = CalendarMath::EncodeDate(Context.Calendar, Y, Season, Day, Shot.Hour);
				for (const FMyceliumPatch& Patch : Patches)
				{
					if (Patch.SpeciesId != Shot.Species)
					{
						continue;
					}
					Fruits.Reset();
					SpawnModel::FruitBodiesAt(Species, Patch, Context, Seconds, Fruits);
					// Young to mature mushrooms look best; buttons a little, rotting ones and those hidden
					// under bracken, brambles or shrubs not at all.
					Weights.Reset();
					for (const FFruitBody& Fruit : Fruits)
					{
						const float Age = Fruit.AgeFraction(Seconds);
						// Not near the forest's boundary either: the void beyond would show in the background.
						const bool bHidden = (Pass == 0 && IsCovered(Fruit.PositionM)) || (Context.Field && !Inside(*Context.Field, FVector2d(Fruit.PositionM), MushroomMarginM));
						Weights.Add(bHidden ? 0.0 : (Age < 0.08f ? 0.3 : (Age < 0.75f ? 1.0 : (Age < 0.88f ? 0.3 : 0.0))));
					}
					// Densest group: the mushroom with the most good neighbours within a close-up frame.
					FGroup Best;
					for (int32 I = 0; I < Fruits.Num(); ++I)
					{
						if (Weights[I] <= 0.0)
						{
							continue;
						}
						double Local = 0.0;
						FVector2d Sum = FVector2d::ZeroVector;
						int32 Count = 0;
						for (int32 J = 0; J < Fruits.Num(); ++J)
						{
							if (Weights[J] > 0.0 && FVector2f::Distance(Fruits[I].PositionM, Fruits[J].PositionM) < 1.2f)
							{
								Local += Weights[J];
								Sum += FVector2d(Fruits[J].PositionM) * Weights[J];
								++Count;
							}
						}
						const double Score = Local - 0.05 * FMath::Abs(Day - Shot.Day) - 0.3 * (Y - Year);
						if (Count >= Shot.MinFruits && (Best.Count == 0 || Score > Best.Score))
						{
							Best = { Seconds, Y, Day, Patch.Id, Sum / Local, Count, Score };
						}
					}
					if (Best.Count > 0)
					{
						Groups.Add(Best);
						PatchesFound.Add(Patch.Id);
					}
				}
			}
		}
		// The best day of each patch, then the Pick-th best patch (the last one when there are fewer).
		Groups.Sort([](const FGroup& A, const FGroup& B) { return A.Score > B.Score; });
		TArray<uint32> Seen;
		const FGroup* Chosen = nullptr;
		for (const FGroup& Group : Groups)
		{
			if (Seen.Contains(Group.PatchId))
			{
				continue;
			}
			Seen.Add(Group.PatchId);
			Chosen = &Group;
			if (Seen.Num() > Shot.Pick)
			{
				break;
			}
		}
		if (Chosen)
		{
			Out.bFound = true;
			Out.GameSeconds = Chosen->Seconds;
			Out.CenterM = Chosen->Center;
			Out.Count = Chosen->Count;
			Out.Description = FString::Printf(TEXT("%d %s, year %d %s day %d, at (%.0f, %.0f) m%s"), Chosen->Count, *Shot.Species.ToString(),
				Chosen->Year, *Shot.Season, Chosen->Day, Chosen->Center.X, Chosen->Center.Y, IsCovered(FVector2f(Chosen->Center)) ? TEXT(", under cover") : TEXT(""));
		}
		return Out;
	}

	TSharedRef<FVegetationObstacleGrid> MakeGroundCover(const FForestField& Field, const FVegetationDefinition& Vegetation, const FVector2d& Center, double RadiusM)
	{
		TSharedRef<FVegetationObstacleGrid> Grid = MakeShared<FVegetationObstacleGrid>();
		TArray<FVegetationInstance> Instances;
		VegetationScatter::Scatter(Field, Vegetation, EVegetationLayer::Ground, FBox2d(Center - FVector2d(RadiusM), Center + FVector2d(RadiusM)), Instances);
		Grid->AddCover(Vegetation, Instances);
		return Grid;
	}

	FDemoPlacement PlaceMushroomShot(const FForestField& Field, const FDemoShot& Shot, const FDemoMushroomScene& Scene,
		TFunctionRef<bool(const FVector2f& PositionM, float RadiusM)> IsBlocked, TFunctionRef<bool(const FVector2f& PositionM, float RadiusM)> IsCovered)
	{
		using namespace DemoReelDetail;
		FDemoPlacement Out;
		if (!Scene.bFound)
		{
			return Out;
		}
		const FVector2d Center = Scene.CenterM;
		const double Arc = Shot.Kind == EDemoShotKind::Orbit ? FMath::DegreesToRadians(static_cast<double>(Shot.OrbitDeg)) : 0.0;
		const double Dolly = Shot.Kind == EDemoShotKind::Static ? FMath::Min(static_cast<double>(Shot.DollyM), 0.6 * Shot.DistanceM) : 0.0;
		// Camera positions along the shot must be free, and so must the view to the mushrooms: no trunk,
		// and first try without any bracken, bramble or shrub in front (they hid mushrooms in the demo).
		bool bAvoidCover = true;
		auto Clear = [&](const FVector2d& P)
		{
			if (!Inside(Field, P, 2.0) || Field.Sample(static_cast<float>(P.X), static_cast<float>(P.Y)).bWater || IsBlocked(FVector2f(P), 0.35f)
				|| (bAvoidCover && IsCovered(FVector2f(P), 0.4f)))
			{
				return false;
			}
			for (const double U : { 0.25, 0.5, 0.7 })
			{
				if (IsBlocked(FVector2f(FMath::Lerp(P, Center, U)), 0.1f))
				{
					return false;
				}
			}
			for (int32 Step = 1; bAvoidCover && Step < 8; ++Step)
			{
				if (IsCovered(FVector2f(FMath::Lerp(P, Center, Step / 8.0)), 0.05f))
				{
					return false;
				}
			}
			return true;
		};
		const double Phase = ShroomHash::ToUnitFloat(ShroomHash::Hash(0xDE30u, static_cast<uint32>(Center.X * 10.0), static_cast<uint32>(Center.Y * 10.0))) * UE_DOUBLE_TWO_PI;
		for (const double Distance : { static_cast<double>(Shot.DistanceM), 0.8 * Shot.DistanceM, 0.6 * Shot.DistanceM, -1.0,
			static_cast<double>(Shot.DistanceM), 0.8 * Shot.DistanceM, 0.6 * Shot.DistanceM })
		{
			if (Distance < 0.0)
			{
				// No view free of cover: accept plants in front rather than no shot.
				bAvoidCover = false;
				continue;
			}
			for (int32 K = 0; K < 24; ++K)
			{
				const double Start = Phase + UE_DOUBLE_TWO_PI * K / 24;
				bool bFree = true;
				for (int32 Step = 0; Step <= 6 && bFree; ++Step)
				{
					const double S = Step / 6.0;
					bFree = Clear(Center + Dir(Start + Arc * S) * (Distance - Dolly * S));
				}
				if (!bFree)
				{
					continue;
				}
				const FVector2d P = Center + Dir(Start) * Distance;
				Out.bFound = true;
				Out.Camera = FVector3d(P.X, P.Y, MushroomCameraZ(Field, P, Center, Shot.EyeM));
				Out.Target = OnGround(Field, Center, 0.06);
				Out.GameSeconds = Scene.GameSeconds;
				Out.Description = TEXT("Mushrooms: ") + Scene.Description;
				return Out;
			}
		}
		return Out;
	}
}
