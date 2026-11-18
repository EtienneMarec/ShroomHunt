#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestPaths.h"
#include "Misc/AutomationTest.h"
#include "Vegetation/VegetationDefinition.h"
#include "Vegetation/VegetationScatter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForestPathHelpersTest, "ShroomHunt.Forest.PathHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FForestPathHelpersTest::RunTest(const FString& Parameters)
{
	using namespace ForestPaths;

	// 20 x 20 grid with a wall at X = 10 and a single gap at Y = 15.
	constexpr int32 N = 20;
	auto Blocked = [](int32 Cell) { return Cell % N == 10 && Cell / N != 15; };
	auto Step = [&](int32, int32 To, float Length) { return Blocked(To) ? -1.0f : Length; };
	const int32 Start = 5 * N + 2;
	const int32 Goal = 5 * N + 17;
	TArray<int32> Cells;
	float Cost = 0.0f;
	TestTrue(TEXT("A* finds a way around the wall"), FindGridPath(N, N, Start, Step,
		[&](int32 C) { return C == Goal; },
		[&](int32 C) { return FVector2f::Distance(FVector2f(C % N, C / N), FVector2f(Goal % N, Goal / N)); },
		Cells, &Cost));
	TestTrue(TEXT("The path goes through the gap"), Cells.Contains(15 * N + 10));
	TestTrue(TEXT("Path ends are the start and the goal"), Cells.Num() > 0 && Cells[0] == Start && Cells.Last() == Goal);
	TestTrue(TEXT("Cost is at least the straight distance"), Cost >= 15.0f);

	auto Wall = [](int32, int32 To, float Length) { return To % N == 10 ? -1.0f : Length; };
	TestFalse(TEXT("No path through a closed wall"), FindGridPath(N, N, Start, Wall, [&](int32 C) { return C == Goal; }, [](int32) { return 0.0f; }, Cells));

	const TArray<FVector2f> Zigzag = { FVector2f(0, 0), FVector2f(10, 1), FVector2f(20, 0), FVector2f(30, 1), FVector2f(40, 0) };
	const TArray<FVector2f> Simple = Simplify(Zigzag, 2.0f);
	TestEqual(TEXT("Simplify drops points within the tolerance"), Simple.Num(), 2);
	const TArray<FVector2f> Smooth = Chaikin({ FVector2f(0, 0), FVector2f(10, 0), FVector2f(10, 10) }, 3);
	TestTrue(TEXT("Chaikin keeps the ends"), Smooth[0] == FVector2f(0, 0) && Smooth.Last() == FVector2f(10, 10));
	TestTrue(TEXT("Chaikin cuts the corner"), !Smooth.Contains(FVector2f(10, 0)));
	const TArray<FVector2f> Samples = Resample({ FVector2f(0, 0), FVector2f(10, 0) }, 2.0f);
	TestEqual(TEXT("Resample every 2 m over 10 m gives 6 points"), Samples.Num(), 6);

	// Three sites: three Voronoi edges meeting at the circumcenter.
	const TArray<FVector2f> Sites = { FVector2f(20, 20), FVector2f(80, 20), FVector2f(50, 80) };
	const TArray<FVoronoiEdge> Edges = VoronoiEdges(Sites, FBox2f(FVector2f(0, 0), FVector2f(100, 100)));
	TestEqual(TEXT("Three Voronoi edges"), Edges.Num(), 3);
	int32 Vertices = 0;
	for (const FVoronoiEdge& Edge : Edges)
	{
		Vertices += (Edge.bAIsVertex ? 1 : 0) + (Edge.bBIsVertex ? 1 : 0);
		// Each edge point is equidistant from its two sites.
		const FVector2f Mid = (Edge.A + Edge.B) * 0.5f;
		TestTrue(TEXT("Edge on the bisector"), FMath::IsNearlyEqual(FVector2f::Distance(Mid, Sites[Edge.SiteA]), FVector2f::Distance(Mid, Sites[Edge.SiteB]), 0.01f));
	}
	TestEqual(TEXT("Each edge has one Voronoi vertex end, the other on the box"), Vertices, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForestPathNetworkTest, "ShroomHunt.Forest.PathNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FForestPathNetworkTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(TEXT("F01_LowlandOakBeech"), Errors);
	const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors);
	if (!Field || !Vegetation)
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	const FForestDefinition& Definition = Field->GetDefinition();
	const FForestPathNetwork& Network = Field->GetPathNetwork();
	AddInfo(FString::Printf(TEXT("%d roads %.0f m, %d rides %.0f m, %d footpaths %.0f m, %d nodes"),
		Network.CountPaths(EForestPathRank::Road), Network.GetLengthM(EForestPathRank::Road),
		Network.CountPaths(EForestPathRank::Ride), Network.GetLengthM(EForestPathRank::Ride),
		Network.CountPaths(EForestPathRank::Footpath), Network.GetLengthM(EForestPathRank::Footpath), Network.Nodes.Num()));

	TestEqual(TEXT("Entrances"), Network.CountNodes(EForestPathNodeKind::Entrance), Definition.Entrances);
	TestEqual(TEXT("One base"), Network.CountNodes(EForestPathNodeKind::Base), 1);
	TestTrue(TEXT("At least two hubs"), Network.CountNodes(EForestPathNodeKind::Hub) >= 2);
	TestTrue(TEXT("Roads exist"), Network.CountPaths(EForestPathRank::Road) >= 2);
	TestTrue(TEXT("Rides exist"), Network.CountPaths(EForestPathRank::Ride) >= 3);
	TestTrue(TEXT("Footpaths exist"), Network.CountPaths(EForestPathRank::Footpath) >= 1);
	TestTrue(TEXT("Star crossroads exist"), Network.CountNodes(EForestPathNodeKind::Crossroads) >= 1);

	// Every path stays out of the ponds; rides are straight; roads keep a drivable grade.
	const int32 CoarseWidth = Field->GetCoarseWidth();
	auto CoarseIndex = [&](const FVector2f& P)
	{
		const int32 X = FMath::Clamp(FMath::RoundToInt(P.X / FForestField::CoarseCellM), 0, CoarseWidth - 1);
		const int32 Y = FMath::Clamp(FMath::RoundToInt(P.Y / FForestField::CoarseCellM), 0, CoarseWidth - 1);
		return Y * CoarseWidth + X;
	};
	int32 InPond = 0;
	float MaxRoadGrade = 0.0f;
	FVector2f WorstGradeAt = FVector2f::ZeroVector;
	float MaxRideBend = 0.0f;
	for (const FForestPath& Path : Network.Paths)
	{
		for (const FVector2f& P : Path.Points)
		{
			InPond += Field->GetWaterCells()[CoarseIndex(P)] == 2 ? 1 : 0;
		}
		if (Path.Rank == EForestPathRank::Ride && Path.Points.Num() > 2)
		{
			const FVector2f A = Path.Points[0];
			const FVector2f B = Path.Points.Last();
			const FVector2f Mid = Path.Points[Path.Points.Num() / 2];
			const FVector2f Dir = (B - A).GetSafeNormal();
			const FVector2f Offset = (Mid - A) - Dir * FVector2f::DotProduct(Mid - A, Dir);
			MaxRideBend = FMath::Max(MaxRideBend, Offset.Size());
		}
		if (Path.Rank == EForestPathRank::Road)
		{
			for (int32 Index = 5; Index < Path.Points.Num(); Index += 5)
			{
				const FVector2f A = Path.Points[Index - 5];
				const FVector2f B = Path.Points[Index];
				const float Run = FMath::Max(FVector2f::Distance(A, B), 1.0f);
				const float Rise = FMath::Abs(Field->GetHeights().Sample(B.X, B.Y) - Field->GetHeights().Sample(A.X, A.Y));
				if (Rise / Run > MaxRoadGrade)
				{
					MaxRoadGrade = Rise / Run;
					WorstGradeAt = B;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("Max road grade %.1f %% at (%.0f, %.0f), max ride bend %.2f m"), MaxRoadGrade * 100.0f, WorstGradeAt.X, WorstGradeAt.Y, MaxRideBend));
	TestEqual(TEXT("No path crosses a pond"), InPond, 0);
	TestTrue(TEXT("Rides are straight"), MaxRideBend < 0.5f);
	TestTrue(TEXT("Roads stay below a 15 % grade on the graded terrain"), MaxRoadGrade < 0.15f);

	// The road network connects the entrances, the base and the hubs (flood fill on the road surface).
	const FFieldRaster& Distance = Field->GetPathDistance();
	const TArray<uint8>& Ranks = Field->GetPathRankCells();
	const int32 R = Distance.Width;
	TBitArray<> Reached(false, R * R);
	auto IsRoad = [&](int32 Cell)
	{
		return Ranks[Cell] == static_cast<uint8>(EForestPathRank::Road) && Distance.Values[Cell] <= Definition.RoadHalfWidthM + 1.5f;
	};
	auto NearestRoadCell = [&](const FVector2f& P)
	{
		int32 Best = INDEX_NONE;
		float BestDistance = 8.0f;
		for (int32 Y = FMath::Max(0, FMath::FloorToInt(P.Y) - 8); Y <= FMath::Min(R - 1, FMath::CeilToInt(P.Y) + 8); ++Y)
		{
			for (int32 X = FMath::Max(0, FMath::FloorToInt(P.X) - 8); X <= FMath::Min(R - 1, FMath::CeilToInt(P.X) + 8); ++X)
			{
				const float D = FVector2f::Distance(P, FVector2f(X, Y));
				if (D < BestDistance && IsRoad(Y * R + X))
				{
					BestDistance = D;
					Best = Y * R + X;
				}
			}
		}
		return Best;
	};
	const FForestPathNode* FirstEntrance = Network.Nodes.FindByPredicate([](const FForestPathNode& N) { return N.Kind == EForestPathNodeKind::Entrance; });
	const int32 Seed = FirstEntrance ? NearestRoadCell(FirstEntrance->PositionM) : INDEX_NONE;
	if (!TestTrue(TEXT("The first entrance is on a road"), Seed != INDEX_NONE))
	{
		return false;
	}
	TArray<int32> Stack = { Seed };
	Reached[Seed] = true;
	while (Stack.Num() > 0)
	{
		const int32 Cell = Stack.Pop(EAllowShrinking::No);
		const int32 X = Cell % R;
		const int32 Y = Cell / R;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const int32 NX = X + DX;
				const int32 NY = Y + DY;
				if (NX >= 0 && NY >= 0 && NX < R && NY < R && !Reached[NY * R + NX] && IsRoad(NY * R + NX))
				{
					Reached[NY * R + NX] = true;
					Stack.Add(NY * R + NX);
				}
			}
		}
	}
	for (const FForestPathNode& Node : Network.Nodes)
	{
		if (Node.Kind == EForestPathNodeKind::Entrance || Node.Kind == EForestPathNodeKind::Base || Node.Kind == EForestPathNodeKind::Hub)
		{
			const int32 Cell = NearestRoadCell(Node.PositionM);
			TestTrue(FString::Printf(TEXT("Node at (%.0f, %.0f) is connected by road"), Node.PositionM.X, Node.PositionM.Y), Cell != INDEX_NONE && Reached[Cell]);
		}
	}

	// Trees keep off the paths.
	TArray<FVegetationInstance> Trees;
	VegetationScatter::Scatter(*Field, *Vegetation, EVegetationLayer::Trees, FBox2d(FVector2d::ZeroVector, FVector2d(Field->GetSizeM(), Field->GetSizeM())), Trees);
	int32 OnPath = 0;
	for (const FVegetationInstance& Tree : Trees)
	{
		const FForestSample S = Field->Sample(static_cast<float>(Tree.PositionM.X), static_cast<float>(Tree.PositionM.Y));
		OnPath += (S.PathRank >= 0 && S.PathDistanceM < ForestPaths::HalfWidthM(static_cast<EForestPathRank>(S.PathRank), Definition)) ? 1 : 0;
	}
	TestEqual(TEXT("No tree stands on a path surface"), OnPath, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
