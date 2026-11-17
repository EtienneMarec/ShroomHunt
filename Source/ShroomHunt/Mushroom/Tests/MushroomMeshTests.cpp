#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "VectorUtil.h"
#include "Misc/AutomationTest.h"
#include "Mushroom/MushroomMeshBuilder.h"
#include "Mushroom/MushroomMorphology.h"
#include "Mycology/SpeciesJsonParser.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MushroomMeshTestsDetail
{
	bool LoadSpecies(FAutomationTestBase& Test, TMap<FName, FMushroomSpeciesRow>& Out)
	{
		SpeciesJson::FParseReport Report;
		SpeciesJson::LoadFolder(SpeciesJson::GetDefaultFolder(), Out, Report);
		if (Report.HasErrors() || Out.Num() == 0)
		{
			Test.AddError(TEXT("Cannot load Data/Species: ") + Report.ToString());
			return false;
		}
		return true;
	}

	/**
	 * Signed volume using Unreal's own triangle normal (UE::Geometry::VectorUtil::Normal,
	 * left-handed: (C - A) x (B - A)). Positive when the faces UE renders as front faces
	 * point outward; negative for an inside-out mesh.
	 */
	double SignedVolume(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		double Volume = 0.0;
		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleId, A, B, C);
			const FVector3d AreaNormal = FVector3d::CrossProduct(C - A, B - A); // = 2 * area * VectorUtil::Normal(A, B, C)
			Volume += FVector3d::DotProduct(A, AreaNormal) / 6.0;
		}
		return Volume;
	}

	/** Unreal's triangle normal must agree with our left-handed formula (guards the convention). */
	bool MatchesEngineNormal(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleId, A, B, C);
			const FVector3d Engine = UE::Geometry::VectorUtil::Normal(A, B, C);
			const FVector3d Ours = FVector3d::CrossProduct(C - A, B - A).GetSafeNormal();
			if (!Ours.IsNearlyZero() && FVector3d::DotProduct(Engine, Ours) < 0.99)
			{
				return false;
			}
		}
		return true;
	}

	/** The rendered normal at the highest vertex (top of the cap) must point up. */
	bool TopNormalPointsUp(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		int32 TopVertex = INDEX_NONE;
		double TopZ = -1e30;
		for (const int32 VertexId : Mesh.VertexIndicesItr())
		{
			if (Mesh.GetVertex(VertexId).Z > TopZ)
			{
				TopZ = Mesh.GetVertex(VertexId).Z;
				TopVertex = VertexId;
			}
		}
		const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		TArray<int32> Elements;
		Normals->GetVertexElements(TopVertex, Elements);
		for (const int32 Element : Elements)
		{
			if (Normals->GetElement(Element).Z < 0.3f)
			{
				return false;
			}
		}
		return Elements.Num() > 0;
	}

	uint32 HashMesh(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		uint32 H = static_cast<uint32>(Mesh.TriangleCount());
		for (const int32 VertexId : Mesh.VertexIndicesItr())
		{
			const FVector3d P = Mesh.GetVertex(VertexId);
			H = HashCombineFast(H, GetTypeHash(FMath::RoundToInt(P.X * 1000.0)));
			H = HashCombineFast(H, GetTypeHash(FMath::RoundToInt(P.Y * 1000.0)));
			H = HashCombineFast(H, GetTypeHash(FMath::RoundToInt(P.Z * 1000.0)));
		}
		return H;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMushroomMorphologyTest, "ShroomHunt.Mushroom.Morphology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMushroomMorphologyTest::RunTest(const FString& Parameters)
{
	using namespace MushroomMeshTestsDetail;
	TMap<FName, FMushroomSpeciesRow> Species;
	if (!LoadSpecies(*this, Species))
	{
		return false;
	}

	for (const TPair<FName, FMushroomSpeciesRow>& Pair : Species)
	{
		const FMushroomSpeciesRow& Row = Pair.Value;
		const FString Id = Pair.Key.ToString();

		const FMushroomMorphology A = MushroomMorphology::Sample(Row, Pair.Key, 1234u, EMushroomStage::Mature);
		const FMushroomMorphology B = MushroomMorphology::Sample(Row, Pair.Key, 1234u, EMushroomStage::Mature);
		TestTrue(Id + TEXT(": same seed, same individual"), A.CapDiameter == B.CapDiameter && A.StemLength == B.StemLength
			&& A.CapShape == B.CapShape && A.CapColorCenter == B.CapColorCenter && A.ShapeSeed == B.ShapeSeed);

		double YoungSum = 0.0;
		double OldSum = 0.0;
		int32 Different = 0;
		for (uint32 Seed = 0; Seed < 200; ++Seed)
		{
			const FMushroomMorphology Young = MushroomMorphology::Sample(Row, Pair.Key, Seed, EMushroomStage::Young);
			const FMushroomMorphology Old = MushroomMorphology::Sample(Row, Pair.Key, Seed, EMushroomStage::Old);
			YoungSum += Young.CapDiameter;
			OldSum += Old.CapDiameter;
			if (!FMath::IsNearlyEqual(Young.CapDiameter, MushroomMorphology::Sample(Row, Pair.Key, Seed + 1000, EMushroomStage::Young).CapDiameter))
			{
				++Different;
			}
			if (Old.CapDiameter < Row.Cap.DiameterCm.Min - 1e-3f || Old.CapDiameter > Row.Cap.DiameterCm.Max + 1e-3f)
			{
				AddError(FString::Printf(TEXT("%s: cap diameter %.2f outside species range"), *Id, Old.CapDiameter));
			}
		}
		TestTrue(Id + TEXT(": old caps are larger than young caps on average"), OldSum > YoungSum * 1.5);
		TestTrue(Id + TEXT(": seeds produce varied individuals"), Different > 190);
	}

	const FMushroomMorphology FlyAgaric = MushroomMorphology::Sample(Species[TEXT("AmanitaMuscaria")], TEXT("AmanitaMuscaria"), 7u, EMushroomStage::Mature);
	TestTrue(TEXT("Fly agaric has warts, ring and volva"), FlyAgaric.bWarts && FlyAgaric.WartCount > 10
		&& FlyAgaric.Ring == ERingType::Skirt && FlyAgaric.Volva == EVolvaType::Friable);
	const FMushroomMorphology Cep = MushroomMorphology::Sample(Species[TEXT("BoletusEdulis")], TEXT("BoletusEdulis"), 7u, EMushroomStage::Mature);
	TestTrue(TEXT("Cep has pores (no gill fins)"), Cep.UndersideType == EUndersideType::Pores && Cep.GillCount == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMushroomMeshBuilderTest, "ShroomHunt.Mushroom.MeshBuilder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FMushroomMeshBuilderTest::RunTest(const FString& Parameters)
{
	using namespace MushroomMeshTestsDetail;
	TMap<FName, FMushroomSpeciesRow> Species;
	if (!LoadSpecies(*this, Species))
	{
		return false;
	}

	// Guard the winding convention itself: our formula must equal the engine's triangle normal.
	{
		const FMushroomMorphology Probe = MushroomMorphology::Sample(Species.CreateConstIterator()->Value, Species.CreateConstIterator()->Key, 1u, EMushroomStage::Mature);
		UE::Geometry::FDynamicMesh3 ProbeMesh;
		MushroomMeshBuilder::Build(Probe, FMushroomMeshDetail::Far(), ProbeMesh);
		TestTrue(TEXT("Test normal formula matches UE::Geometry::VectorUtil::Normal"), MatchesEngineNormal(ProbeMesh));
	}

	static const EMushroomStage Stages[3] = { EMushroomStage::Young, EMushroomStage::Mature, EMushroomStage::Old };
	for (const TPair<FName, FMushroomSpeciesRow>& Pair : Species)
	{
		for (const EMushroomStage Stage : Stages)
		{
			for (uint32 Seed = 0; Seed < 5; ++Seed)
			{
				const FMushroomMorphology M = MushroomMorphology::Sample(Pair.Value, Pair.Key, Seed, Stage);
				const FString Label = FString::Printf(TEXT("%s stage %d seed %u"), *Pair.Key.ToString(), static_cast<int32>(Stage), Seed);

				UE::Geometry::FDynamicMesh3 Near;
				UE::Geometry::FDynamicMesh3 Far;
				MushroomMeshBuilder::Build(M, FMushroomMeshDetail::Near(), Near);
				MushroomMeshBuilder::Build(M, FMushroomMeshDetail::Far(), Far);

				if (Near.TriangleCount() < 200)
				{
					AddError(Label + FString::Printf(TEXT(": only %d triangles"), Near.TriangleCount()));
					continue;
				}
				TestTrue(Label + TEXT(": far LOD is lighter"), Far.TriangleCount() * 2 < Near.TriangleCount());
				TestTrue(Label + TEXT(": attributes present"), Near.HasAttributes() && Near.Attributes()->HasPrimaryColors() && Near.Attributes()->NumUVLayers() == 2);

				// Bounds: stands on the ground, plausible size.
				const UE::Geometry::FAxisAlignedBox3d Bounds = Near.GetBounds();
				const double Height = Bounds.Max.Z;
				TestTrue(Label + TEXT(": starts at the ground"), FMath::Abs(Bounds.Min.Z) < 0.1 * M.StemLength + 0.5);
				TestTrue(Label + FString::Printf(TEXT(": height %.1f close to stem + cap"), Height),
					Height > M.StemLength * 0.9 && Height < M.StemLength + M.CapDiameter * 0.9 + 1.0);
				TestTrue(Label + TEXT(": width close to the cap diameter"), Bounds.Width() < M.CapDiameter * 1.35 + 1.0 && Bounds.Width() > M.CapDiameter * 0.7);

				// Winding: outward normals give a clearly positive signed volume (stem alone ~ pi r^2 L).
				const double StemVolume = UE_DOUBLE_PI * FMath::Square(0.5 * M.StemDiameter) * M.StemLength;
				TestTrue(Label + TEXT(": front faces point outward (positive volume)"), SignedVolume(Near) > 0.3 * StemVolume);
				TestTrue(Label + TEXT(": far LOD front faces point outward"), SignedVolume(Far) > 0.3 * StemVolume);
				TestTrue(Label + TEXT(": rendered normal on top of the cap points up"), TopNormalPointsUp(Near));

				// Determinism.
				UE::Geometry::FDynamicMesh3 Again;
				MushroomMeshBuilder::Build(M, FMushroomMeshDetail::Near(), Again);
				TestTrue(Label + TEXT(": deterministic"), HashMesh(Again) == HashMesh(Near));
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
