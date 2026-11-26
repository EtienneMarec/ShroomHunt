#include "Demo/DemoReel.h"

#include "Forest/ForestField.h"
#include "Forest/ForestLandmarks.h"
#include "Forest/ForestPaths.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Time/TimeTypes.h"

namespace DemoReelDetail
{
	constexpr int32 Directions = 16;

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

	FDemoPlacement FindPlace(const FForestField& Field, const FDemoShot& Shot)
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
					if (S.bWater || S.Canopy > 0.12f || S.PathDistanceM < 6.0f)
					{
						return false;
					}
					// Prefer wide openings: the canopy stays low in a ring of 12 m.
					double Open = 0.0;
					for (int32 K = 0; K < 8; ++K)
					{
						const FVector2d R = Q + Dir(UE_DOUBLE_TWO_PI * K / 8) * 12.0;
						Open += 1.0 - Field.Sample(static_cast<float>(R.X), static_cast<float>(R.Y)).Canopy;
					}
					Score = Open;
					return true;
				}, P))
			{
				// Look toward the forest edge (where the canopy is densest 30 m away).
				const double Angle = BestDirection(Field, P, 30.0, [](const FForestSample& S) { return S.Canopy; });
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
				const FVector2d Front = Dir(FMath::DegreesToRadians(Mark.YawDeg));
				Stand(Where + Front * (6.0 + Mark.LengthM * 0.5), Where, -Shot.EyeM + 1.4);
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
		}
		return Pose;
	}
}
