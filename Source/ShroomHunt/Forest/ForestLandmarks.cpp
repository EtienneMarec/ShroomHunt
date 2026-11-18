#include "Forest/ForestLandmarks.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "Forest/ForestField.h"
#include "Forest/ForestPaths.h"

namespace ForestLandmarks
{
	const TCHAR* KindName(EForestLandmarkKind Kind)
	{
		switch (Kind)
		{
		case EForestLandmarkKind::ForesterHouse: return TEXT("ForesterHouse");
		case EForestLandmarkKind::HuntingHut: return TEXT("HuntingHut");
		case EForestLandmarkKind::HuntingTower: return TEXT("HuntingTower");
		case EForestLandmarkKind::Calvary: return TEXT("Calvary");
		case EForestLandmarkKind::Signpost: return TEXT("Signpost");
		case EForestLandmarkKind::ParcelStone: return TEXT("ParcelStone");
		case EForestLandmarkKind::Bridge: return TEXT("Bridge");
		case EForestLandmarkKind::LogPile: return TEXT("LogPile");
		}
		return TEXT("Unknown");
	}
}

namespace ForestLandmarksDetail
{
	constexpr uint32 LandmarkChannel = 40;

	float YawTowards(const FVector2f& From, const FVector2f& To)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(To.Y - From.Y, To.X - From.X));
	}

	bool IsOpenStand(EStandAgeClass Age)
	{
		return Age == EStandAgeClass::ClearCut || Age == EStandAgeClass::Regeneration;
	}
}

void FForestField::GenerateLandmarks()
{
	using namespace ForestLandmarksDetail;
	using namespace ShroomHash;

	Landmarks.Reset();
	const int32 W = CoarseHeights.Width;
	const int32 Count = W * W;
	const float Size = GetSizeM();
	const uint32 LandmarkSeed = Hash(Seed, LandmarkChannel);
	const TArray<FForestPathNode>& Nodes = PathNetwork.Nodes;
	const TArray<FForestPath>& Paths = PathNetwork.Paths;

	auto CellPos = [&](int32 C) { return FVector2f((C % W) * CoarseCellM, (C / W) * CoarseCellM); };
	auto Inside = [&](const FVector2f& P, float Margin) { return P.X >= Margin && P.Y >= Margin && P.X <= Size - Margin && P.Y <= Size - Margin; };
	auto IsWet = [&](const FVector2f& P) { return WaterCells[CoarseIndexAt(P.X, P.Y)] != 0 || WaterDistance.Sample(P.X, P.Y) < 3.0f; };
	// Clear of every path surface by Margin meters.
	auto OffPath = [&](const FVector2f& P, float Margin)
	{
		const int32 Fine = PathDistance.Index(FMath::Clamp(FMath::RoundToInt(P.X), 0, PathDistance.Width - 1), FMath::Clamp(FMath::RoundToInt(P.Y), 0, PathDistance.Height - 1));
		const uint8 Rank = PathRankCells[Fine];
		return Rank == 255 || PathDistance.Values[Fine] >= ForestPaths::HalfWidthM(static_cast<EForestPathRank>(Rank), Definition) + Margin;
	};
	auto FarFromLandmarks = [&](const FVector2f& P, float Distance)
	{
		for (const FForestLandmark& L : Landmarks)
		{
			if (L.Kind != EForestLandmarkKind::Bridge && FVector2f::Distance(L.PositionM, P) < Distance + L.ClearanceM)
			{
				return false;
			}
		}
		return true;
	};
	// A dry spot Distance meters around Center, off the paths and away from other landmarks.
	auto FindSpot = [&](const FVector2f& Center, float Distance, float PathMargin, float OwnClearance, uint32 Salt, FVector2f& Out)
	{
		const float Start = ToUnitFloat(Hash(LandmarkSeed, Salt)) * UE_TWO_PI;
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const float Radius = Distance * (1.0f + 0.5f * Ring);
			for (int32 Step = 0; Step < 16; ++Step)
			{
				const float Angle = Start + Step * UE_TWO_PI / 16.0f;
				const FVector2f P = Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
				if (Inside(P, 4.0f) && !IsWet(P) && OffPath(P, PathMargin) && FarFromLandmarks(P, OwnClearance))
				{
					Out = P;
					return true;
				}
			}
		}
		return false;
	};
	auto Add = [&](EForestLandmarkKind Kind, const FVector2f& Position, float YawDeg, int32 Node, float Clearance) -> FForestLandmark&
	{
		FForestLandmark& L = Landmarks.AddDefaulted_GetRef();
		L.Kind = Kind;
		L.PositionM = Position;
		L.GroundM = Heights.Sample(Position.X, Position.Y);
		L.YawDeg = YawDeg;
		L.Node = Node;
		L.ClearanceM = Clearance;
		return L;
	};

	TArray<TArray<int32>> Incident;
	Incident.SetNum(Nodes.Num());
	for (int32 P = 0; P < Paths.Num(); ++P)
	{
		if (Paths[P].StartNode != INDEX_NONE) { Incident[Paths[P].StartNode].AddUnique(P); }
		if (Paths[P].EndNode != INDEX_NONE) { Incident[Paths[P].EndNode].AddUnique(P); }
	}
	auto HasRoad = [&](int32 Node) { return Incident[Node].ContainsByPredicate([&](int32 P) { return Paths[P].Rank == EForestPathRank::Road; }); };

	// 1. Forester's house beyond the end of the base road, door toward the road.
	for (int32 N = 0; N < Nodes.Num(); ++N)
	{
		if (Nodes[N].Kind != EForestPathNodeKind::Base)
		{
			continue;
		}
		FVector2f Away(1.0f, 0.0f);
		if (Incident[N].Num() > 0)
		{
			const FForestPath& Road = Paths[Incident[N][0]];
			const FVector2f Next = Road.StartNode == N ? Road.Points[FMath::Min(5, Road.Points.Num() - 1)] : Road.Points[FMath::Max(0, Road.Points.Num() - 6)];
			Away = (Nodes[N].PositionM - Next).GetSafeNormal();
		}
		FVector2f Spot = Nodes[N].PositionM + Away * 16.0f;
		if (!(Inside(Spot, 10.0f) && !IsWet(Spot) && OffPath(Spot, 7.0f)) && !FindSpot(Nodes[N].PositionM, 16.0f, 7.0f, 14.0f, 1u, Spot))
		{
			continue;
		}
		Add(EForestLandmarkKind::ForesterHouse, Spot, YawTowards(Spot, Nodes[N].PositionM), N, 14.0f);
		break;
	}

	// 2. Hunting hut at the hub closest to a young or clear-cut parcel.
	{
		int32 BestHub = INDEX_NONE;
		float BestDistance = TNumericLimits<float>::Max();
		for (int32 N = 0; N < Nodes.Num(); ++N)
		{
			if (Nodes[N].Kind != EForestPathNodeKind::Hub)
			{
				continue;
			}
			for (int32 C = 0; C < Count; ++C)
			{
				if (IsOpenStand(Parcels[ParcelCells[C]].AgeClass))
				{
					const float D = FVector2f::Distance(CellPos(C), Nodes[N].PositionM);
					if (D < BestDistance)
					{
						BestDistance = D;
						BestHub = N;
					}
				}
			}
		}
		FVector2f Spot;
		if (BestHub != INDEX_NONE && FindSpot(Nodes[BestHub].PositionM, 14.0f, 4.0f, 7.0f, 2u, Spot))
		{
			Add(EForestLandmarkKind::HuntingHut, Spot, YawTowards(Spot, Nodes[BestHub].PositionM), BestHub, 7.0f);
		}
	}

	// 3. Calvary at a road junction (else at the hub nearest to the first entrance).
	{
		int32 Chosen = INDEX_NONE;
		uint32 BestHash = MAX_uint32;
		for (int32 N = 0; N < Nodes.Num(); ++N)
		{
			if (Nodes[N].Kind == EForestPathNodeKind::Junction && HasRoad(N) && Hash(LandmarkSeed, 3u, static_cast<uint32>(N)) < BestHash)
			{
				BestHash = Hash(LandmarkSeed, 3u, static_cast<uint32>(N));
				Chosen = N;
			}
		}
		if (Chosen == INDEX_NONE)
		{
			Chosen = Nodes.IndexOfByPredicate([](const FForestPathNode& Node) { return Node.Kind == EForestPathNodeKind::Hub; });
		}
		FVector2f Spot;
		if (Chosen != INDEX_NONE && FindSpot(Nodes[Chosen].PositionM, 5.5f, 2.0f, 3.0f, 4u, Spot))
		{
			Add(EForestLandmarkKind::Calvary, Spot, YawTowards(Spot, Nodes[Chosen].PositionM), Chosen, 3.0f);
		}
	}

	// 4. Signposts at every named place, 5. parcel stones at the star crossroads.
	for (int32 N = 0; N < Nodes.Num(); ++N)
	{
		const EForestPathNodeKind Kind = Nodes[N].Kind;
		const bool bNamed = Kind == EForestPathNodeKind::Crossroads || Kind == EForestPathNodeKind::Hub || Kind == EForestPathNodeKind::Entrance
			|| (Kind == EForestPathNodeKind::Junction && HasRoad(N));
		FVector2f Spot;
		if (bNamed && FindSpot(Nodes[N].PositionM, 3.6f, 0.6f, 1.0f, 100u + N, Spot))
		{
			Add(EForestLandmarkKind::Signpost, Spot, YawTowards(Spot, Nodes[N].PositionM), N, 1.5f);
		}
		if (Kind == EForestPathNodeKind::Crossroads && FindSpot(Nodes[N].PositionM, 2.6f, 0.3f, 0.5f, 200u + N, Spot))
		{
			FForestLandmark& Stone = Add(EForestLandmarkKind::ParcelStone, Spot, YawTowards(Spot, Nodes[N].PositionM), N, 1.0f);
			const int32 CX = FMath::RoundToInt(Nodes[N].PositionM.X / CoarseCellM);
			const int32 CY = FMath::RoundToInt(Nodes[N].PositionM.Y / CoarseCellM);
			for (int32 DY = -4; DY <= 4; ++DY)
			{
				for (int32 DX = -4; DX <= 4; ++DX)
				{
					if (CX + DX >= 0 && CY + DY >= 0 && CX + DX < W && CY + DY < W)
					{
						Stone.ParcelNumbers.AddUnique(Parcels[ParcelCells[(CY + DY) * W + CX + DX]].Number);
					}
				}
			}
			Stone.ParcelNumbers.Sort();
		}
	}

	// 6. Hunting towers at stand edges facing an open parcel.
	{
		TArray<int32> Candidates;
		for (int32 C = 0; C < Count; ++C)
		{
			const FVector2f P = CellPos(C);
			if (IsOpenStand(Parcels[ParcelCells[C]].AgeClass) || WaterCells[C] != 0 || !Inside(P, 20.0f) || !OffPath(P, 3.0f))
			{
				continue;
			}
			const int32 X = C % W;
			const int32 Y = C / W;
			bool bEdge = false;
			for (int32 DY = -2; DY <= 2 && !bEdge; ++DY)
			{
				for (int32 DX = -2; DX <= 2; ++DX)
				{
					if (X + DX >= 0 && Y + DY >= 0 && X + DX < W && Y + DY < W && IsOpenStand(Parcels[ParcelCells[(Y + DY) * W + X + DX]].AgeClass))
					{
						bEdge = true;
						break;
					}
				}
			}
			if (bEdge)
			{
				Candidates.Add(C);
			}
		}
		Candidates.Sort([&](int32 A, int32 B) { return Hash(LandmarkSeed, 6u, static_cast<uint32>(A)) < Hash(LandmarkSeed, 6u, static_cast<uint32>(B)); });
		int32 Towers = 0;
		for (const int32 C : Candidates)
		{
			if (Towers >= 2)
			{
				break;
			}
			const FVector2f P = CellPos(C);
			const bool bSpaced = !Landmarks.ContainsByPredicate([&](const FForestLandmark& L) { return L.Kind == EForestLandmarkKind::HuntingTower && FVector2f::Distance(L.PositionM, P) < 150.0f; });
			if (!bSpaced || !FarFromLandmarks(P, 40.0f))
			{
				continue;
			}
			// Face the open parcel (centroid of the open cells around).
			FVector2f Open = FVector2f::ZeroVector;
			int32 OpenCount = 0;
			for (int32 DY = -4; DY <= 4; ++DY)
			{
				for (int32 DX = -4; DX <= 4; ++DX)
				{
					const int32 X = C % W + DX;
					const int32 Y = C / W + DY;
					if (X >= 0 && Y >= 0 && X < W && Y < W && IsOpenStand(Parcels[ParcelCells[Y * W + X]].AgeClass))
					{
						Open += CellPos(Y * W + X);
						++OpenCount;
					}
				}
			}
			Add(EForestLandmarkKind::HuntingTower, P, YawTowards(P, Open / FMath::Max(OpenCount, 1)), INDEX_NONE, 3.0f);
			++Towers;
		}
	}

	// 7. Bridges where a path crosses a stream.
	for (const FForestPath& Path : Paths)
	{
		const float Width = Path.Rank == EForestPathRank::Road ? 4.6f : (Path.Rank == EForestPathRank::Ride ? 3.0f : 1.4f);
		for (int32 Index = 1; Index < Path.Points.Num(); ++Index)
		{
			auto InStream = [&](int32 I) { return WaterCells[CoarseIndexAt(Path.Points[I].X, Path.Points[I].Y)] == 1; };
			if (!InStream(Index) || InStream(Index - 1))
			{
				continue;
			}
			int32 Last = Index;
			while (Last + 1 < Path.Points.Num() && InStream(Last + 1))
			{
				++Last;
			}
			// Abutments 6 m beyond the water, where the path is fully graded (the grading stops
			// near streams to keep the bed open): the deck rests on both banks.
			const int32 IA = FMath::Max(Index - 3, 0);
			const int32 IB = FMath::Min(Last + 3, Path.Points.Num() - 1);
			const FVector2f A = Path.Points[IA];
			const FVector2f B = Path.Points[IB];
			const float Span = FVector2f::Distance(A, B) + 1.0f;
			const FVector2f Center = (A + B) * 0.5f;
			const bool bDryEnds = WaterCells[CoarseIndexAt(A.X, A.Y)] == 0 && WaterCells[CoarseIndexAt(B.X, B.Y)] == 0;
			const bool bDuplicate = Landmarks.ContainsByPredicate([&](const FForestLandmark& L) { return L.Kind == EForestLandmarkKind::Bridge && FVector2f::Distance(L.PositionM, Center) < 8.0f; });
			if (Span <= 18.0f && bDryEnds && !bDuplicate)
			{
				FForestLandmark& Bridge = Add(EForestLandmarkKind::Bridge, Center, YawTowards(A, B), INDEX_NONE, 0.0f);
				const float Banks = FMath::Max(Path.Heights[IA], Path.Heights[IB]);
				const float Bed = Heights.Sample(Center.X, Center.Y);
				Bridge.GroundM = FMath::Max(Banks, Bed + 0.8f) + 0.05f;
				Bridge.LengthM = Span;
				Bridge.WidthM = Width;
			}
			Index = Last;
		}
	}

	// 8. Log piles along the roads next to mature and old stands.
	{
		struct FCandidate
		{
			FVector2f Position;
			float Yaw;
			uint32 Order;
		};
		TArray<FCandidate> Candidates;
		for (int32 P = 0; P < Paths.Num(); ++P)
		{
			const FForestPath& Road = Paths[P];
			if (Road.Rank != EForestPathRank::Road)
			{
				continue;
			}
			for (int32 Index = 10; Index + 10 < Road.Points.Num(); Index += 20)
			{
				const FVector2f Dir = (Road.Points[Index + 1] - Road.Points[Index - 1]).GetSafeNormal();
				const FVector2f Side(-Dir.Y, Dir.X);
				for (const float Sign : { 1.0f, -1.0f })
				{
					const FVector2f Spot = Road.Points[Index] + Side * Sign * (Definition.RoadHalfWidthM + 2.8f);
					const EStandAgeClass Age = Parcels[ParcelCells[CoarseIndexAt(Spot.X, Spot.Y)]].AgeClass;
					if ((Age == EStandAgeClass::Mature || Age == EStandAgeClass::Old) && Inside(Spot, 5.0f) && !IsWet(Spot) && OffPath(Spot, 0.8f))
					{
						Candidates.Add({ Spot, YawTowards(Road.Points[Index - 1], Road.Points[Index + 1]), Hash(LandmarkSeed, 8u, static_cast<uint32>(P * 4096 + Index), Sign > 0.0f ? 1u : 2u) });
						break;
					}
				}
			}
		}
		Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Order < B.Order; });
		int32 Piles = 0;
		for (const FCandidate& Candidate : Candidates)
		{
			if (Piles >= 4)
			{
				break;
			}
			const bool bSpaced = !Landmarks.ContainsByPredicate([&](const FForestLandmark& L) { return L.Kind == EForestLandmarkKind::LogPile && FVector2f::Distance(L.PositionM, Candidate.Position) < 120.0f; });
			if (bSpaced && FarFromLandmarks(Candidate.Position, 6.0f))
			{
				FForestLandmark& Pile = Add(EForestLandmarkKind::LogPile, Candidate.Position, Candidate.Yaw, INDEX_NONE, 3.0f);
				Pile.LengthM = 4.0f;
				++Piles;
			}
		}
	}

	// Clearance mask for the vegetation.
	LandmarkMask.Init(0, Count);
	for (const FForestLandmark& L : Landmarks)
	{
		const float Radius = L.Kind == EForestLandmarkKind::Bridge ? L.LengthM * 0.5f : L.ClearanceM;
		const int32 Reach = FMath::CeilToInt(Radius / CoarseCellM) + 1;
		const int32 CX = FMath::RoundToInt(L.PositionM.X / CoarseCellM);
		const int32 CY = FMath::RoundToInt(L.PositionM.Y / CoarseCellM);
		for (int32 DY = -Reach; DY <= Reach; ++DY)
		{
			for (int32 DX = -Reach; DX <= Reach; ++DX)
			{
				const int32 X = CX + DX;
				const int32 Y = CY + DY;
				if (X >= 0 && Y >= 0 && X < W && Y < W && FVector2f::Distance(CellPos(Y * W + X), L.PositionM) <= Radius + CoarseCellM * 0.5f)
				{
					LandmarkMask[Y * W + X] = 1;
				}
			}
		}
	}

	UE_LOG(LogShroomForest, Verbose, TEXT("%d landmarks placed in %s"), Landmarks.Num(), *Definition.Name);
}
