#include "Forest/ForestPaths.h"

#include "Algo/Count.h"
#include "Algo/Reverse.h"
#include "Async/ParallelFor.h"
#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "Core/ShroomMath.h"
#include "Forest/ForestDefinition.h"
#include "Forest/ForestField.h"

// --- Network types -------------------------------------------------------------------

float FForestPath::GetLengthM() const
{
	float Length = 0.0f;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Length += FVector2f::Distance(Points[Index - 1], Points[Index]);
	}
	return Length;
}

int32 FForestPathNetwork::CountPaths(EForestPathRank Rank) const
{
	return Algo::CountIf(Paths, [Rank](const FForestPath& Path) { return Path.Rank == Rank; });
}

float FForestPathNetwork::GetLengthM(EForestPathRank Rank) const
{
	float Length = 0.0f;
	for (const FForestPath& Path : Paths)
	{
		Length += Path.Rank == Rank ? Path.GetLengthM() : 0.0f;
	}
	return Length;
}

int32 FForestPathNetwork::CountNodes(EForestPathNodeKind Kind) const
{
	return Algo::CountIf(Nodes, [Kind](const FForestPathNode& Node) { return Node.Kind == Kind; });
}

// --- Pure helpers ----------------------------------------------------------------------

namespace ForestPaths
{
	float HalfWidthM(EForestPathRank Rank, const FForestDefinition& Definition)
	{
		switch (Rank)
		{
		case EForestPathRank::Road: return Definition.RoadHalfWidthM;
		case EForestPathRank::Ride: return Definition.RideHalfWidthM;
		default: return Definition.FootpathHalfWidthM;
		}
	}

	float ShoulderM(EForestPathRank Rank)
	{
		switch (Rank)
		{
		case EForestPathRank::Road: return 3.5f;
		case EForestPathRank::Ride: return 2.0f;
		default: return 1.0f;
		}
	}

	bool FindGridPath(int32 Width, int32 Height, int32 Start,
		TFunctionRef<float(int32 From, int32 To, float StepLength)> StepCost,
		TFunctionRef<bool(int32 Cell)> IsGoal,
		TFunctionRef<float(int32 Cell)> Heuristic,
		TArray<int32>& OutCells, float* OutCost)
	{
		OutCells.Reset();
		const int32 Count = Width * Height;
		if (Start < 0 || Start >= Count)
		{
			return false;
		}
		struct FOpen
		{
			float F;
			int32 Cell;
		};
		auto Less = [](const FOpen& A, const FOpen& B) { return A.F < B.F; };
		TArray<float> G;
		G.Init(TNumericLimits<float>::Max(), Count);
		TArray<int32> Parent;
		Parent.Init(INDEX_NONE, Count);
		TBitArray<> Closed(false, Count);
		TArray<FOpen> Open;
		G[Start] = 0.0f;
		Open.HeapPush(FOpen{ Heuristic(Start), Start }, Less);

		static const int32 DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		static const int32 DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		while (Open.Num() > 0)
		{
			FOpen Top;
			Open.HeapPop(Top, Less, EAllowShrinking::No);
			if (Closed[Top.Cell])
			{
				continue;
			}
			Closed[Top.Cell] = true;
			if (IsGoal(Top.Cell))
			{
				for (int32 Cell = Top.Cell; Cell != INDEX_NONE; Cell = Parent[Cell])
				{
					OutCells.Add(Cell);
				}
				Algo::Reverse(OutCells);
				if (OutCost)
				{
					*OutCost = G[Top.Cell];
				}
				return true;
			}
			const int32 X = Top.Cell % Width;
			const int32 Y = Top.Cell / Width;
			for (int32 Dir = 0; Dir < 8; ++Dir)
			{
				const int32 NX = X + DX[Dir];
				const int32 NY = Y + DY[Dir];
				if (NX < 0 || NY < 0 || NX >= Width || NY >= Height)
				{
					continue;
				}
				const int32 Next = NY * Width + NX;
				if (Closed[Next])
				{
					continue;
				}
				const float Cost = StepCost(Top.Cell, Next, Dir < 4 ? 1.0f : UE_SQRT_2);
				if (Cost < 0.0f)
				{
					continue;
				}
				const float NewG = G[Top.Cell] + Cost;
				if (NewG < G[Next])
				{
					G[Next] = NewG;
					Parent[Next] = Top.Cell;
					Open.HeapPush(FOpen{ NewG + Heuristic(Next), Next }, Less);
				}
			}
		}
		return false;
	}

	TArray<FVector2f> Simplify(const TArray<FVector2f>& Points, float Tolerance)
	{
		if (Points.Num() < 3)
		{
			return Points;
		}
		TBitArray<> Keep(false, Points.Num());
		Keep[0] = true;
		Keep[Points.Num() - 1] = true;
		TArray<TPair<int32, int32>> Stack;
		Stack.Add({ 0, Points.Num() - 1 });
		while (Stack.Num() > 0)
		{
			const TPair<int32, int32> Range = Stack.Pop(EAllowShrinking::No);
			const FVector2f A = Points[Range.Key];
			const FVector2f B = Points[Range.Value];
			const FVector2f AB = B - A;
			const float LengthSq = FMath::Max(AB.SizeSquared(), 1e-6f);
			float Farthest = -1.0f;
			int32 FarthestIndex = INDEX_NONE;
			for (int32 Index = Range.Key + 1; Index < Range.Value; ++Index)
			{
				const float T = FMath::Clamp(FVector2f::DotProduct(Points[Index] - A, AB) / LengthSq, 0.0f, 1.0f);
				const float Distance = FVector2f::Distance(Points[Index], A + AB * T);
				if (Distance > Farthest)
				{
					Farthest = Distance;
					FarthestIndex = Index;
				}
			}
			if (FarthestIndex != INDEX_NONE && Farthest > Tolerance)
			{
				Keep[FarthestIndex] = true;
				Stack.Add({ Range.Key, FarthestIndex });
				Stack.Add({ FarthestIndex, Range.Value });
			}
		}
		TArray<FVector2f> Out;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if (Keep[Index])
			{
				Out.Add(Points[Index]);
			}
		}
		return Out;
	}

	TArray<FVector2f> Chaikin(const TArray<FVector2f>& Points, int32 Iterations)
	{
		TArray<FVector2f> Current = Points;
		for (int32 Iteration = 0; Iteration < Iterations && Current.Num() >= 3; ++Iteration)
		{
			TArray<FVector2f> Next;
			Next.Reserve(Current.Num() * 2);
			Next.Add(Current[0]);
			for (int32 Index = 0; Index + 1 < Current.Num(); ++Index)
			{
				const FVector2f& A = Current[Index];
				const FVector2f& B = Current[Index + 1];
				if (Index > 0)
				{
					Next.Add(A * 0.75f + B * 0.25f);
				}
				if (Index + 2 < Current.Num())
				{
					Next.Add(A * 0.25f + B * 0.75f);
				}
			}
			Next.Add(Current.Last());
			Current = MoveTemp(Next);
		}
		return Current;
	}

	TArray<FVector2f> Resample(const TArray<FVector2f>& Points, float Spacing)
	{
		TArray<FVector2f> Out;
		if (Points.Num() == 0)
		{
			return Out;
		}
		Out.Add(Points[0]);
		float Carry = 0.0f;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector2f A = Points[Index - 1];
			const FVector2f B = Points[Index];
			const float Length = FVector2f::Distance(A, B);
			float S = Spacing - Carry;
			while (S < Length)
			{
				Out.Add(A + (B - A) * (S / Length));
				S += Spacing;
			}
			Carry = Length - (S - Spacing);
		}
		if (FVector2f::Distance(Out.Last(), Points.Last()) > Spacing * 0.25f)
		{
			Out.Add(Points.Last());
		}
		else
		{
			Out.Last() = Points.Last();
		}
		return Out;
	}

	TArray<float> SmoothProfile(const TArray<float>& Values, int32 Radius, int32 Passes)
	{
		TArray<float> Current = Values;
		const int32 Count = Current.Num();
		for (int32 Pass = 0; Pass < Passes && Count > 2; ++Pass)
		{
			TArray<float> Next;
			Next.SetNumUninitialized(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const int32 Lo = FMath::Max(0, Index - Radius);
				const int32 Hi = FMath::Min(Count - 1, Index + Radius);
				float Sum = 0.0f;
				for (int32 K = Lo; K <= Hi; ++K)
				{
					Sum += Current[K];
				}
				Next[Index] = Sum / (Hi - Lo + 1);
			}
			Current = MoveTemp(Next);
		}
		return Current;
	}

	TArray<FVoronoiEdge> VoronoiEdges(const TArray<FVector2f>& Sites, const FBox2f& Box)
	{
		TArray<FVoronoiEdge> Edges;
		constexpr float Big = 1e7f;
		for (int32 I = 0; I < Sites.Num(); ++I)
		{
			for (int32 J = I + 1; J < Sites.Num(); ++J)
			{
				// Bisector of I and J: P(t) = Mid + t * Dir.
				const FVector2f Mid = (Sites[I] + Sites[J]) * 0.5f;
				const FVector2f Normal = Sites[J] - Sites[I];
				if (Normal.IsNearlyZero())
				{
					continue;
				}
				const FVector2f Dir = FVector2f(-Normal.Y, Normal.X).GetSafeNormal();
				float Lo = -Big;
				float Hi = Big;
				bool bLoVertex = false;
				bool bHiVertex = false;
				bool bEmpty = false;
				// Closer to I (and J) than to any other site K: 2 P.(K - I) <= |K|^2 - |I|^2.
				for (int32 K = 0; K < Sites.Num() && !bEmpty; ++K)
				{
					if (K == I || K == J)
					{
						continue;
					}
					const FVector2f D = Sites[K] - Sites[I];
					const float Coef = 2.0f * FVector2f::DotProduct(Dir, D);
					const float Rhs = Sites[K].SizeSquared() - Sites[I].SizeSquared() - 2.0f * FVector2f::DotProduct(Mid, D);
					if (FMath::Abs(Coef) < 1e-6f)
					{
						bEmpty = Rhs < 0.0f;
					}
					else if (Coef > 0.0f)
					{
						if (Rhs / Coef < Hi) { Hi = Rhs / Coef; bHiVertex = true; }
					}
					else
					{
						if (Rhs / Coef > Lo) { Lo = Rhs / Coef; bLoVertex = true; }
					}
				}
				// Clip to the box (Liang-Barsky on the parameter range).
				for (int32 Axis = 0; Axis < 2 && !bEmpty; ++Axis)
				{
					const float M = Axis == 0 ? Mid.X : Mid.Y;
					const float Dv = Axis == 0 ? Dir.X : Dir.Y;
					const float Min = Axis == 0 ? Box.Min.X : Box.Min.Y;
					const float Max = Axis == 0 ? Box.Max.X : Box.Max.Y;
					if (FMath::Abs(Dv) < 1e-9f)
					{
						bEmpty = M < Min || M > Max;
						continue;
					}
					const float T1 = (Min - M) / Dv;
					const float T2 = (Max - M) / Dv;
					if (FMath::Min(T1, T2) > Lo) { Lo = FMath::Min(T1, T2); bLoVertex = false; }
					if (FMath::Max(T1, T2) < Hi) { Hi = FMath::Max(T1, T2); bHiVertex = false; }
				}
				if (bEmpty || Hi - Lo < 1.0f)
				{
					continue;
				}
				FVoronoiEdge& Edge = Edges.AddDefaulted_GetRef();
				Edge.A = Mid + Dir * Lo;
				Edge.B = Mid + Dir * Hi;
				Edge.SiteA = I;
				Edge.SiteB = J;
				Edge.bAIsVertex = bLoVertex;
				Edge.bBIsVertex = bHiVertex;
			}
		}
		return Edges;
	}
}

// --- Generation (FForestField members) ------------------------------------------------------

namespace ForestPathsDetail
{
	constexpr uint32 PathChannel = 30;
	constexpr float NodeMergeM = 6.0f;
}

void FForestField::GeneratePaths()
{
	using namespace ForestPaths;
	using namespace ForestPathsDetail;

	PathNetwork = FForestPathNetwork();
	const int32 W = CoarseHeights.Width;
	const int32 Count = W * W;
	const float Cell = CoarseCellM;
	const float Size = GetSizeM();
	const uint32 PathSeed = ShroomHash::Hash(Seed, PathChannel);

	auto CellPos = [&](int32 C) { return FVector2f((C % W) * Cell, (C / W) * Cell); };
	auto IsPond = [&](int32 C) { return WaterCells[C] == 2; };
	// Ponds plus a one-cell (4 m) margin: smoothed centerlines must not clip the banks.
	TArray<uint8> PondMargin;
	PondMargin.Init(0, Count);
	for (int32 C = 0; C < Count; ++C)
	{
		if (!IsPond(C))
		{
			continue;
		}
		const int32 X = C % W;
		const int32 Y = C / W;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				if (X + DX >= 0 && Y + DY >= 0 && X + DX < W && Y + DY < W)
				{
					PondMargin[(Y + DY) * W + X + DX] = 1;
				}
			}
		}
	}
	auto NearPond = [&](int32 C) { return PondMargin[C] != 0; };
	auto Noise01 = [&](int32 C, float ScaleM)
	{
		return 0.5f + 0.5f * ShroomHash::GradientNoise2D(PathSeed, (C % W) * Cell / ScaleM, (C / W) * Cell / ScaleM);
	};
	auto HashOrder = [&](TArray<int32>& Cells, uint32 Salt)
	{
		Cells.Sort([&](int32 A, int32 B)
		{
			const uint32 HA = ShroomHash::Hash(PathSeed, Salt, static_cast<uint32>(A));
			const uint32 HB = ShroomHash::Hash(PathSeed, Salt, static_cast<uint32>(B));
			return HA < HB || (HA == HB && A < B);
		});
	};
	auto BorderDistance = [&](int32 C)
	{
		const FVector2f P = CellPos(C);
		return FMath::Min(FMath::Min(P.X, P.Y), FMath::Min(Size - P.X, Size - P.Y));
	};

	auto AddNode = [&](const FVector2f& Position, EForestPathNodeKind Kind)
	{
		for (int32 Index = 0; Index < PathNetwork.Nodes.Num(); ++Index)
		{
			if (FVector2f::Distance(PathNetwork.Nodes[Index].PositionM, Position) < NodeMergeM)
			{
				return Index;
			}
		}
		FForestPathNode& Node = PathNetwork.Nodes.AddDefaulted_GetRef();
		Node.PositionM = Position;
		Node.Kind = Kind;
		return PathNetwork.Nodes.Num() - 1;
	};

	auto AddPath = [&](EForestPathRank Rank, const TArray<FVector2f>& Raw, int32 StartNode, int32 EndNode)
	{
		// Grid routes zigzag (8 directions): simplify generously, then round the corners. Fall back
		// to a finer simplification when the straighter line would clip a pond margin.
		auto Smooth = [&](float Tolerance)
		{
			return Resample(Rank == EForestPathRank::Ride ? Raw : Chaikin(Simplify(Raw, Tolerance), 4), 2.0f);
		};
		auto ClipsPond = [&](const TArray<FVector2f>& Candidate)
		{
			return Candidate.ContainsByPredicate([&](const FVector2f& P) { return IsPond(CoarseIndexAt(P.X, P.Y)); });
		};
		const float Coarse = Rank == EForestPathRank::Road ? 8.0f : 5.0f;
		TArray<FVector2f> Points = Smooth(Coarse);
		if (Rank != EForestPathRank::Ride && ClipsPond(Points))
		{
			Points = Smooth(Cell * 0.8f);
		}
		if (Points.Num() < 2)
		{
			return;
		}
		FForestPath Path;
		Path.Rank = Rank;
		Path.Points = MoveTemp(Points);
		if (Path.GetLengthM() < 8.0f)
		{
			return;
		}
		TArray<float> Profile;
		for (const FVector2f& P : Path.Points)
		{
			Profile.Add(Heights.Sample(P.X, P.Y));
		}
		const int32 Radius = Rank == EForestPathRank::Road ? 8 : (Rank == EForestPathRank::Ride ? 6 : 2);
		Path.Heights = SmoothProfile(Profile, Radius, 2);
		Path.StartNode = StartNode;
		Path.EndNode = EndNode;
		PathNetwork.Paths.Add(MoveTemp(Path));
	};

	// 1. Terminals: entrances on the border, the base near the first one, hubs on flat ground.
	TArray<int32> Terminals;
	auto FarFromTerminals = [&](int32 C, float MinDistance)
	{
		for (const int32 T : Terminals)
		{
			if (FVector2f::Distance(CellPos(T), CellPos(C)) < MinDistance)
			{
				return false;
			}
		}
		return true;
	};
	{
		TArray<int32> Border;
		for (int32 I = 0; I < W; ++I)
		{
			Border.Add(I);
			Border.Add((W - 1) * W + I);
			if (I > 0 && I < W - 1)
			{
				Border.Add(I * W);
				Border.Add(I * W + W - 1);
			}
		}
		TArray<int32> Gentle = Border.FilterByPredicate([&](int32 C) { return WaterCells[C] == 0 && WaterDistance.Values[C] > 20.0f && Slope.Values[C] < 0.08f; });
		TArray<int32>& Candidates = Gentle.Num() >= Definition.Entrances ? Gentle : Border;
		HashOrder(Candidates, 1u);
		for (const int32 C : Candidates)
		{
			if (Terminals.Num() >= Definition.Entrances)
			{
				break;
			}
			if (WaterCells[C] == 0 && FarFromTerminals(C, Size * 0.45f))
			{
				Terminals.Add(C);
				AddNode(CellPos(C), EForestPathNodeKind::Entrance);
			}
		}
	}
	if (Terminals.Num() == 0)
	{
		UE_LOG(LogShroomForest, Warning, TEXT("No entrance found for %s: no paths"), *Definition.Name);
		RasterizePaths();
		return;
	}
	{
		const FVector2f Entrance = CellPos(Terminals[0]);
		int32 Best = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 C = 0; C < Count; ++C)
		{
			const float D = FVector2f::Distance(CellPos(C), Entrance);
			if (D < 50.0f || D > 100.0f || WaterCells[C] != 0 || WaterDistance.Values[C] < 20.0f || BorderDistance(C) < 30.0f)
			{
				continue;
			}
			const float Score = Slope.Values[C] * 10.0f + 0.2f * ShroomHash::ToUnitFloat(ShroomHash::Hash(PathSeed, 2u, static_cast<uint32>(C)));
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = C;
			}
		}
		if (Best != INDEX_NONE)
		{
			Terminals.Add(Best);
			AddNode(CellPos(Best), EForestPathNodeKind::Base);
		}
	}
	{
		TArray<int32> Candidates;
		for (int32 C = 0; C < Count; ++C)
		{
			if (WaterCells[C] == 0 && Slope.Values[C] < 0.06f && WaterDistance.Values[C] > 25.0f && BorderDistance(C) > 90.0f)
			{
				Candidates.Add(C);
			}
		}
		HashOrder(Candidates, 3u);
		int32 Added = 0;
		for (const int32 C : Candidates)
		{
			if (Added >= Definition.Hubs)
			{
				break;
			}
			if (FarFromTerminals(C, Definition.HubSpacingM))
			{
				Terminals.Add(C);
				AddNode(CellPos(C), EForestPathNodeKind::Hub);
				++Added;
			}
		}
	}
	// Terminal i is node i (AddNode only merges nodes closer than 6 m; terminals are far apart).

	// 2. Roads: minimum spanning tree of the routing costs + loops, routed with A*, merging
	// into roads already built (cheaper to follow an existing road).
	TArray<uint8> RoadMask;
	RoadMask.Init(0, Count);
	auto RoadStep = [&](int32 From, int32 To, float Length) -> float
	{
		if (NearPond(To))
		{
			return -1.0f;
		}
		const float LengthM = Length * Cell;
		const float Grade = FMath::Abs(CoarseHeights.Values[To] - CoarseHeights.Values[From]) / LengthM;
		float Cost = LengthM * (1.0f + Definition.RoadGradeCost * Grade * Grade + 0.3f * Noise01(To, 90.0f));
		if (WaterCells[To] == 1)
		{
			Cost += 40.0f; // stream crossing (culvert or ford)
		}
		return RoadMask[To] ? Cost * 0.35f : Cost;
	};
	const int32 NumTerminals = Terminals.Num();
	TArray<float> PairCost;
	PairCost.Init(TNumericLimits<float>::Max(), NumTerminals * NumTerminals);
	for (int32 I = 0; I < NumTerminals; ++I)
	{
		for (int32 J = I + 1; J < NumTerminals; ++J)
		{
			const FVector2f Goal = CellPos(Terminals[J]);
			TArray<int32> Cells;
			float Cost = 0.0f;
			if (FindGridPath(W, W, Terminals[I], RoadStep,
				[&](int32 C) { return C == Terminals[J]; },
				[&](int32 C) { return 0.35f * FVector2f::Distance(CellPos(C), Goal); },
				Cells, &Cost))
			{
				PairCost[I * NumTerminals + J] = PairCost[J * NumTerminals + I] = Cost;
			}
		}
	}
	TArray<TPair<int32, int32>> RoadEdges;
	{
		TArray<bool> InTree;
		InTree.Init(false, NumTerminals);
		InTree[0] = true;
		for (int32 Step = 1; Step < NumTerminals; ++Step)
		{
			float Best = TNumericLimits<float>::Max();
			TPair<int32, int32> BestEdge(INDEX_NONE, INDEX_NONE);
			for (int32 I = 0; I < NumTerminals; ++I)
			{
				for (int32 J = 0; J < NumTerminals && InTree[I]; ++J)
				{
					if (!InTree[J] && PairCost[I * NumTerminals + J] < Best)
					{
						Best = PairCost[I * NumTerminals + J];
						BestEdge = { I, J };
					}
				}
			}
			if (BestEdge.Key == INDEX_NONE)
			{
				break; // unreachable terminal (should not happen)
			}
			InTree[BestEdge.Value] = true;
			RoadEdges.Add(BestEdge);
		}
		// Loops: the non-tree connections that save the most compared with going round the tree.
		auto TreeDistance = [&](int32 From, int32 To)
		{
			TArray<float> Distance;
			Distance.Init(-1.0f, NumTerminals);
			Distance[From] = 0.0f;
			TArray<int32> Queue = { From };
			while (Queue.Num() > 0)
			{
				const int32 Node = Queue.Pop();
				for (const TPair<int32, int32>& Edge : RoadEdges)
				{
					const int32 Other = Edge.Key == Node ? Edge.Value : (Edge.Value == Node ? Edge.Key : INDEX_NONE);
					if (Other != INDEX_NONE && Distance[Other] < 0.0f)
					{
						Distance[Other] = Distance[Node] + PairCost[Node * NumTerminals + Other];
						Queue.Add(Other);
					}
				}
			}
			return Distance[To];
		};
		TArray<TPair<float, TPair<int32, int32>>> LoopCandidates;
		for (int32 I = 0; I < NumTerminals; ++I)
		{
			for (int32 J = I + 1; J < NumTerminals; ++J)
			{
				const bool bInTree = RoadEdges.ContainsByPredicate([I, J](const TPair<int32, int32>& E) { return (E.Key == I && E.Value == J) || (E.Key == J && E.Value == I); });
				const float Direct = PairCost[I * NumTerminals + J];
				if (!bInTree && Direct < TNumericLimits<float>::Max())
				{
					LoopCandidates.Add(TPair<float, TPair<int32, int32>>(TreeDistance(I, J) / Direct, TPair<int32, int32>(I, J)));
				}
			}
		}
		LoopCandidates.Sort([](const auto& A, const auto& B) { return A.Key > B.Key; });
		for (int32 Index = 0; Index < FMath::Min(Definition.ExtraRoadLoops, LoopCandidates.Num()); ++Index)
		{
			if (LoopCandidates[Index].Key > 1.3f)
			{
				RoadEdges.Add(LoopCandidates[Index].Value);
			}
		}
		RoadEdges.Sort([&](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
		{
			return PairCost[A.Key * NumTerminals + A.Value] < PairCost[B.Key * NumTerminals + B.Value];
		});
	}
	for (const TPair<int32, int32>& Edge : RoadEdges)
	{
		const FVector2f Goal = CellPos(Terminals[Edge.Value]);
		TArray<int32> Cells;
		if (!FindGridPath(W, W, Terminals[Edge.Key], RoadStep,
			[&](int32 C) { return C == Terminals[Edge.Value]; },
			[&](int32 C) { return 0.35f * FVector2f::Distance(CellPos(C), Goal); },
			Cells))
		{
			continue;
		}
		// Keep only the stretches that are not on an existing road; their ends become junctions.
		TArray<FVector2f> Run;
		int32 RunStartNode = INDEX_NONE;
		auto Flush = [&](int32 EndNode)
		{
			if (Run.Num() >= 2)
			{
				AddPath(EForestPathRank::Road, Run, RunStartNode, EndNode);
			}
			Run.Reset();
		};
		for (int32 K = 0; K < Cells.Num(); ++K)
		{
			const int32 C = Cells[K];
			if (!RoadMask[C])
			{
				if (Run.Num() == 0)
				{
					RunStartNode = K == 0 ? Edge.Key : AddNode(CellPos(Cells[K - 1]), EForestPathNodeKind::Junction);
					if (K > 0)
					{
						Run.Add(CellPos(Cells[K - 1]));
					}
				}
				Run.Add(CellPos(C));
			}
			else if (Run.Num() > 0)
			{
				Run.Add(CellPos(C));
				Flush(AddNode(CellPos(C), EForestPathNodeKind::Junction));
			}
		}
		if (Run.Num() > 0)
		{
			Flush(Edge.Value);
		}
		for (const int32 C : Cells)
		{
			RoadMask[C] = 1;
		}
	}

	// 3. Rides: the straight parcel boundaries (Voronoi edges), cut where they would cross a pond.
	TArray<uint8> NetworkMask = RoadMask;
	{
		TArray<FVector2f> Sites;
		for (const FForestParcel& Parcel : Parcels)
		{
			Sites.Add(Parcel.Site);
		}
		const FBox2f Box(FVector2f::ZeroVector, FVector2f(Size, Size));
		for (const FVoronoiEdge& Edge : VoronoiEdges(Sites, Box))
		{
			const float Length = FVector2f::Distance(Edge.A, Edge.B);
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length / 2.0f));
			TArray<FVector2f> Piece;
			bool bPieceStartsAtA = true;
			auto FlushPiece = [&](bool bEndsAtB)
			{
				if (Piece.Num() >= 2 && FVector2f::Distance(Piece[0], Piece.Last()) >= Definition.RideMinLengthM)
				{
					const int32 StartNode = AddNode(Piece[0], bPieceStartsAtA && Edge.bAIsVertex ? EForestPathNodeKind::Crossroads : EForestPathNodeKind::End);
					const int32 EndNode = AddNode(Piece.Last(), bEndsAtB && Edge.bBIsVertex ? EForestPathNodeKind::Crossroads : EForestPathNodeKind::End);
					AddPath(EForestPathRank::Ride, { Piece[0], Piece.Last() }, StartNode, EndNode);
					for (const FVector2f& P : Piece)
					{
						NetworkMask[CoarseIndexAt(P.X, P.Y)] = 1;
					}
				}
				Piece.Reset();
			};
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const FVector2f P = Edge.A + (Edge.B - Edge.A) * (static_cast<float>(Step) / Steps);
				if (NearPond(CoarseIndexAt(P.X, P.Y)))
				{
					FlushPiece(false);
					bPieceStartsAtA = false;
					continue;
				}
				Piece.Add(P);
			}
			FlushPiece(true);
		}
	}

	// 4. Footpaths: winding paths from the network to pond shores, then to old stands.
	{
		TArray<FVector2f> Targets;
		// Ponds, largest first.
		TArray<int32> Component;
		Component.Init(INDEX_NONE, Count);
		TArray<TPair<int32, FVector2f>> Ponds; // size, centroid
		for (int32 C = 0; C < Count; ++C)
		{
			if (!IsPond(C) || Component[C] != INDEX_NONE)
			{
				continue;
			}
			const int32 Id = Ponds.Num();
			TArray<int32> Stack = { C };
			Component[C] = Id;
			FVector2f Sum = FVector2f::ZeroVector;
			int32 Cells = 0;
			while (Stack.Num() > 0)
			{
				const int32 Cur = Stack.Pop(EAllowShrinking::No);
				Sum += CellPos(Cur);
				++Cells;
				const int32 X = Cur % W;
				const int32 Y = Cur / W;
				const int32 Neighbors[4] = { X > 0 ? Cur - 1 : -1, X < W - 1 ? Cur + 1 : -1, Y > 0 ? Cur - W : -1, Y < W - 1 ? Cur + W : -1 };
				for (const int32 N : Neighbors)
				{
					if (N >= 0 && IsPond(N) && Component[N] == INDEX_NONE)
					{
						Component[N] = Id;
						Stack.Add(N);
					}
				}
			}
			Ponds.Add({ Cells, Sum / static_cast<float>(Cells) });
		}
		Ponds.Sort([](const auto& A, const auto& B) { return A.Key > B.Key; });
		for (const auto& Pond : Ponds)
		{
			Targets.Add(Pond.Value);
		}
		for (const FForestParcel& Parcel : Parcels)
		{
			if (Parcel.AgeClass == EStandAgeClass::Old)
			{
				Targets.Add(Parcel.Site);
			}
		}

		int32 Built = 0;
		for (const FVector2f& Target : Targets)
		{
			if (Built >= Definition.Footpaths)
			{
				break;
			}
			// Dry start cell near the target: on a pond shore (4-10 m from the water) or in the stand.
			const bool bPond = IsPond(CoarseIndexAt(Target.X, Target.Y));
			int32 Start = INDEX_NONE;
			float BestDistance = 120.0f;
			for (int32 C = 0; C < Count; ++C)
			{
				const float WaterGap = WaterDistance.Values[C];
				const bool bValid = WaterCells[C] == 0 && WaterGap >= 4.0f && (!bPond || WaterGap <= 10.0f);
				const float D = FVector2f::Distance(CellPos(C), Target);
				if (bValid && D < BestDistance)
				{
					BestDistance = D;
					Start = C;
				}
			}
			if (Start == INDEX_NONE || NetworkMask[Start])
			{
				continue;
			}
			TArray<int32> Cells;
			const bool bFound = FindGridPath(W, W, Start,
				[&](int32 From, int32 To, float Length) -> float
				{
					if (WaterCells[To] != 0 || NearPond(To))
					{
						return -1.0f;
					}
					const float LengthM = Length * Cell;
					const float Grade = FMath::Abs(CoarseHeights.Values[To] - CoarseHeights.Values[From]) / LengthM;
					return LengthM * (1.0f + 60.0f * Grade * Grade + 2.0f * Noise01(To, 25.0f));
				},
				[&](int32 C) { return NetworkMask[C] != 0; },
				[](int32) { return 0.0f; },
				Cells);
			if (!bFound || Cells.Num() < 5)
			{
				continue;
			}
			Algo::Reverse(Cells); // from the network to the destination
			TArray<FVector2f> Points;
			for (const int32 C : Cells)
			{
				Points.Add(CellPos(C));
				NetworkMask[C] = 1;
			}
			const int32 StartNode = AddNode(Points[0], EForestPathNodeKind::Junction);
			const int32 EndNode = AddNode(Points.Last(), EForestPathNodeKind::Destination);
			AddPath(EForestPathRank::Footpath, Points, StartNode, EndNode);
			++Built;
		}
	}

	RasterizePaths();
}

void FForestField::RasterizePaths()
{
	using namespace ForestPaths;
	using ShroomMath::SmoothStep01;

	const int32 R = Heights.Width;
	PathDistance.Init(R, R, 1.0f, 1000.0f);
	PathRankCells.Init(255, R * R);
	TArray<float> Metric;
	Metric.Init(TNumericLimits<float>::Max(), R * R);
	TArray<float> Target;
	Target.Init(0.0f, R * R);

	// Nearest path per fine cell: the one whose surface is closest (distance minus half width).
	for (const FForestPath& Path : PathNetwork.Paths)
	{
		const float HalfWidth = HalfWidthM(Path.Rank, Definition);
		const float Reach = FMath::Max(HalfWidth + ShoulderM(Path.Rank) + 2.0f, 12.0f);
		for (int32 Index = 1; Index < Path.Points.Num(); ++Index)
		{
			const FVector2f A = Path.Points[Index - 1];
			const FVector2f B = Path.Points[Index];
			const FVector2f AB = B - A;
			const float LengthSq = FMath::Max(AB.SizeSquared(), 1e-6f);
			const int32 X0 = FMath::Max(0, FMath::FloorToInt(FMath::Min(A.X, B.X) - Reach));
			const int32 X1 = FMath::Min(R - 1, FMath::CeilToInt(FMath::Max(A.X, B.X) + Reach));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt(FMath::Min(A.Y, B.Y) - Reach));
			const int32 Y1 = FMath::Min(R - 1, FMath::CeilToInt(FMath::Max(A.Y, B.Y) + Reach));
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector2f P(static_cast<float>(X), static_cast<float>(Y));
					const float T = FMath::Clamp(FVector2f::DotProduct(P - A, AB) / LengthSq, 0.0f, 1.0f);
					const float Distance = FVector2f::Distance(P, A + AB * T);
					if (Distance > Reach)
					{
						continue;
					}
					const int32 Cell = Y * R + X;
					const float M = Distance - HalfWidth;
					if (M < Metric[Cell])
					{
						Metric[Cell] = M;
						PathDistance.Values[Cell] = Distance;
						PathRankCells[Cell] = static_cast<uint8>(Path.Rank);
						Target[Cell] = FMath::Lerp(Path.Heights[Index - 1], Path.Heights[Index], T);
					}
				}
			}
		}
	}

	// Grade the terrain: flat surface, blended shoulders; roads get a camber and side ditches.
	ParallelFor(R, [&](int32 Y)
	{
		for (int32 X = 0; X < R; ++X)
		{
			const int32 Cell = Y * R + X;
			if (PathRankCells[Cell] == 255)
			{
				continue;
			}
			const EForestPathRank Rank = static_cast<EForestPathRank>(PathRankCells[Cell]);
			const float HalfWidth = HalfWidthM(Rank, Definition);
			const float D = PathDistance.Values[Cell];
			float Weight = 1.0f - SmoothStep01(HalfWidth, HalfWidth + ShoulderM(Rank), D);
			if (Rank == EForestPathRank::Footpath)
			{
				Weight *= 0.7f;
			}
			// Keep the stream beds open where a path crosses them: bridges span the gap
			// (grading used to fill the bed with an embankment, leaving the bridge useless).
			Weight *= SmoothStep01(2.5f, 6.0f, WaterDistance.Sample(static_cast<float>(X), static_cast<float>(Y)));
			float H = FMath::Lerp(Heights.Values[Cell], Target[Cell], Weight);
			if (Rank == EForestPathRank::Road)
			{
				if (D < HalfWidth)
				{
					H += 0.06f * (1.0f - FMath::Square(D / HalfWidth));
				}
				const float Ditch = FMath::Clamp(1.0f - FMath::Abs(D - (HalfWidth + 0.9f)) / 0.7f, 0.0f, 1.0f);
				H -= 0.3f * Ditch * Ditch * (3.0f - 2.0f * Ditch);
			}
			Heights.Values[Cell] = H;
		}
	});
	for (int32 Y = 0; Y < CoarseHeights.Height; ++Y)
	{
		for (int32 X = 0; X < CoarseHeights.Width; ++X)
		{
			CoarseHeights.At(X, Y) = Heights.At(X * 4, Y * 4);
		}
	}
}
