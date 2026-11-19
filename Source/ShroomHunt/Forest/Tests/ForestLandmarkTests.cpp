#include "DynamicMesh/DynamicMesh3.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/ForestPlaceNames.h"
#include "Forest/ForestWater.h"
#include "Forest/LandmarkMeshBuilder.h"
#include "Misc/AutomationTest.h"
#include "Vegetation/VegetationDefinition.h"
#include "Vegetation/VegetationScatter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForestLandmarkTest, "ShroomHunt.Forest.Landmarks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FForestLandmarkTest::RunTest(const FString& Parameters)
{
	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(TEXT("F01_LowlandOakBeech"), Errors);
	const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors);
	if (!Field || !Vegetation)
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	const TArray<FForestLandmark>& Landmarks = Field->GetLandmarks();
	TMap<EForestLandmarkKind, int32> Counts;
	for (const FForestLandmark& L : Landmarks)
	{
		Counts.FindOrAdd(L.Kind)++;
	}
	AddInfo(FString::Printf(TEXT("%d landmarks: %d signposts, %d stones, %d towers, %d bridges, %d log piles"), Landmarks.Num(),
		Counts.FindRef(EForestLandmarkKind::Signpost), Counts.FindRef(EForestLandmarkKind::ParcelStone), Counts.FindRef(EForestLandmarkKind::HuntingTower),
		Counts.FindRef(EForestLandmarkKind::Bridge), Counts.FindRef(EForestLandmarkKind::LogPile)));
	TestEqual(TEXT("One forester's house"), Counts.FindRef(EForestLandmarkKind::ForesterHouse), 1);
	TestEqual(TEXT("One hunting hut"), Counts.FindRef(EForestLandmarkKind::HuntingHut), 1);
	TestEqual(TEXT("One calvary"), Counts.FindRef(EForestLandmarkKind::Calvary), 1);
	TestTrue(TEXT("Hunting towers"), Counts.FindRef(EForestLandmarkKind::HuntingTower) >= 1);
	TestTrue(TEXT("Signposts at the named places"), Counts.FindRef(EForestLandmarkKind::Signpost) >= 5);
	TestEqual(TEXT("One parcel stone per star crossroads"), Counts.FindRef(EForestLandmarkKind::ParcelStone),
		Field->GetPathNetwork().CountNodes(EForestPathNodeKind::Crossroads));

	// Placement: on dry land, off the path surfaces (bridges excepted), inside the map.
	const FForestDefinition& Definition = Field->GetDefinition();
	for (const FForestLandmark& L : Landmarks)
	{
		const FString What = FString::Printf(TEXT("%s at (%.0f, %.0f)"), ForestLandmarks::KindName(L.Kind), L.PositionM.X, L.PositionM.Y);
		TestTrue(What + TEXT(" is inside the map"), L.PositionM.X > 0.0f && L.PositionM.Y > 0.0f && L.PositionM.X < Field->GetSizeM() && L.PositionM.Y < Field->GetSizeM());
		if (L.Kind == EForestLandmarkKind::Bridge)
		{
			TestTrue(What + TEXT(" spans a stream"), L.LengthM > 2.0f && L.WidthM > 1.0f);
			continue;
		}
		const FForestSample S = Field->Sample(L.PositionM.X, L.PositionM.Y);
		TestFalse(What + TEXT(" is not in the water"), S.bWater);
		TestTrue(What + TEXT(" is off the path surface"), S.PathRank < 0 || S.PathDistanceM >= ForestPaths::HalfWidthM(static_cast<EForestPathRank>(S.PathRank), Definition));
		TestTrue(What + TEXT(" keeps its clearance free of trees"), S.bNearLandmark);
	}

	// No two landmarks overlap: footprints apart (bridges as capsules).
	int32 Overlaps = 0;
	for (int32 I = 0; I < Landmarks.Num(); ++I)
	{
		for (int32 J = I + 1; J < Landmarks.Num(); ++J)
		{
			const FForestLandmark& A = Landmarks[I];
			const FForestLandmark& B = Landmarks[J];
			const bool bBridges = A.Kind == EForestLandmarkKind::Bridge && B.Kind == EForestLandmarkKind::Bridge;
			const FForestLandmark& Shape = A.Kind == EForestLandmarkKind::Bridge ? A : B;
			const FForestLandmark& Other = A.Kind == EForestLandmarkKind::Bridge ? B : A;
			const float Gap = ForestLandmarks::FootprintDistance(Shape, Other.PositionM)
				- (Other.Kind == EForestLandmarkKind::Bridge ? 0.5f * Other.WidthM : ForestLandmarks::FootprintRadius(Other.Kind));
			if (!bBridges && Gap < 0.0f)
			{
				++Overlaps;
				AddError(FString::Printf(TEXT("%s at (%.0f, %.0f) overlaps %s at (%.0f, %.0f)"), ForestLandmarks::KindName(A.Kind), A.PositionM.X, A.PositionM.Y,
					ForestLandmarks::KindName(B.Kind), B.PositionM.X, B.PositionM.Y));
			}
		}
	}
	TestEqual(TEXT("No two landmarks overlap"), Overlaps, 0);

	// Names: every signpost has one, all different; the base is the forester's house.
	FPlaceNameData NameData;
	if (!TestTrue(TEXT("PlaceNames.json loads"), ForestPlaceNames::Load(NameData, Errors)))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	const TArray<FString> Names = ForestPlaceNames::NameNodes(*Field, NameData);
	TSet<FString> Unique;
	int32 Named = 0;
	for (int32 N = 0; N < Names.Num(); ++N)
	{
		if (Names[N].IsEmpty())
		{
			continue;
		}
		++Named;
		Unique.Add(Names[N]);
		if (Field->GetPathNetwork().Nodes[N].Kind == EForestPathNodeKind::Base)
		{
			TestEqual(TEXT("The base is the forester's house"), Names[N], NameData.Prefixes.Base);
		}
	}
	AddInfo(FString::Printf(TEXT("%d named places, e.g. %s"), Named, Unique.Num() > 0 ? *Unique.Array()[0] : TEXT("-")));
	TestEqual(TEXT("Place names are unique"), Unique.Num(), Named);
	for (const FForestLandmark& L : Landmarks)
	{
		if (L.Kind == EForestLandmarkKind::Signpost)
		{
			TestTrue(TEXT("Every signpost has a name"), Names.IsValidIndex(L.Node) && !Names[L.Node].IsEmpty());
		}
	}
	TArray<FString> DeterminismNames = ForestPlaceNames::NameNodes(*Field, NameData);
	TestTrue(TEXT("Names are deterministic"), DeterminismNames == Names);

	// Meshes of every kind build as closed, outward-facing solids with collision where needed.
	for (const FForestLandmark& L : Landmarks)
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		FLandmarkCollision Collision;
		TestTrue(FString::Printf(TEXT("%s mesh builds"), ForestLandmarks::KindName(L.Kind)), LandmarkMeshBuilder::Build(L, Mesh, &Collision));
		TestTrue(FString::Printf(TEXT("%s has collision"), ForestLandmarks::KindName(L.Kind)), Collision.Boxes.Num() > 0);
		double Volume = 0.0;
		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			FVector3d Normal, Centroid;
			double Area = 0.0;
			Mesh.GetTriInfo(Tri, Normal, Area, Centroid);
			Volume += Area * FVector3d::DotProduct(Normal, Centroid) / 3.0;
		}
		TestTrue(FString::Printf(TEXT("%s faces outward"), ForestLandmarks::KindName(L.Kind)), Volume > 0.0);
	}

	// Water surface: ponds and streams, levels within the terrain range.
	UE::Geometry::FDynamicMesh3 Water;
	ForestWaterMesh::Build(*Field, Water);
	TestTrue(TEXT("Water surface exists"), Water.TriangleCount() > 200);
	const UE::Geometry::FAxisAlignedBox3d WaterBounds = Water.GetBounds();
	float MinH = TNumericLimits<float>::Max();
	float MaxH = TNumericLimits<float>::Lowest();
	for (const float H : Field->GetHeights().Values)
	{
		MinH = FMath::Min(MinH, H);
		MaxH = FMath::Max(MaxH, H);
	}
	TestTrue(TEXT("Water levels within the terrain heights"), WaterBounds.Min.Z > MinH - 1.0 && WaterBounds.Max.Z < MaxH);

	// The open edges of the water surface must be hidden under the ground, otherwise the player
	// sees where the mesh stops. Sample every boundary edge.
	int32 EdgeSamples = 0;
	int32 VisibleSamples = 0;
	int32 VisiblePond = 0;
	const FFieldRaster& PondLevel = Field->GetWaterLevel();
	auto IsPondVertex = [&](const FVector3d& P)
	{
		const int32 X = FMath::Clamp(FMath::RoundToInt(P.X / FForestField::CoarseCellM), 0, PondLevel.Width - 1);
		const int32 Y = FMath::Clamp(FMath::RoundToInt(P.Y / FForestField::CoarseCellM), 0, PondLevel.Height - 1);
		return FMath::Abs(PondLevel.At(X, Y) - P.Z) < 1e-3;
	};
	for (const int32 Edge : Water.EdgeIndicesItr())
	{
		if (!Water.IsBoundaryEdge(Edge))
		{
			continue;
		}
		const UE::Geometry::FIndex2i Ends = Water.GetEdgeV(Edge);
		const FVector3d A = Water.GetVertex(Ends.A);
		const FVector3d B = Water.GetVertex(Ends.B);
		for (const double T : { 0.0, 0.5, 1.0 })
		{
			const FVector3d P = FMath::Lerp(A, B, T);
			++EdgeSamples;
			const bool bVisible = Field->GetHeights().Sample(static_cast<float>(P.X), static_cast<float>(P.Y)) < P.Z - 0.05;
			VisibleSamples += bVisible ? 1 : 0;
			VisiblePond += bVisible && IsPondVertex(A) ? 1 : 0;
		}
	}
	const float VisibleShare = EdgeSamples > 0 ? static_cast<float>(VisibleSamples) / EdgeSamples : 0.0f;
	AddInfo(FString::Printf(TEXT("Water: %d triangles, %.1f %% of the edge samples above the ground (%d of %d on ponds)"),
		Water.TriangleCount(), VisibleShare * 100.0f, VisiblePond, VisibleSamples));
	TestTrue(TEXT("Water edges are hidden under the banks (at most 5 % visible)"), VisibleShare <= 0.05f);

	// Trees keep out of the landmark clearances.
	TArray<FVegetationInstance> Trees;
	VegetationScatter::Scatter(*Field, *Vegetation, EVegetationLayer::Trees, FBox2d(FVector2d::ZeroVector, FVector2d(Field->GetSizeM(), Field->GetSizeM())), Trees);
	int32 InClearance = 0;
	for (const FVegetationInstance& Tree : Trees)
	{
		InClearance += Field->Sample(static_cast<float>(Tree.PositionM.X), static_cast<float>(Tree.PositionM.Y)).bNearLandmark ? 1 : 0;
	}
	TestEqual(TEXT("No tree inside a landmark clearance"), InClearance, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
