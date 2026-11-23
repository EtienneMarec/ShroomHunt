#include "DynamicMesh/DynamicMesh3.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Forest/LandmarkMeshBuilder.h"
#include "Knowledge/KnowledgeText.h"
#include "Knowledge/KnowledgeTypes.h"
#include "Misc/AutomationTest.h"
#include "Mushroom/MushroomMorphology.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnowledgeTest, "ShroomHunt.Knowledge.Levels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FKnowledgeTest::RunTest(const FString& Parameters)
{
	// The real sources file is valid against the species data.
	FKnowledgeSourceData Sources;
	TArray<FString> Errors;
	TMap<FName, FMushroomSpeciesRow> Species;
	FString SpeciesSource;
	if (!TestTrue(TEXT("Sources.json loads"), KnowledgeText::LoadSources(Sources, Errors))
		|| !TestTrue(TEXT("Species load"), USpeciesDatabaseSubsystem::LoadSpeciesForTools(Species, SpeciesSource)))
	{
		AddError(FString::Join(Errors, TEXT("; ")));
		return false;
	}
	const bool bValid = KnowledgeText::ValidateSources(Sources, Species, Errors);
	TestTrue(TEXT("Sources.json is valid: ") + FString::Join(Errors, TEXT("; ")), bValid);
	const FKnowledgeSource* Poster = Sources.Find(TEXT("PosterOurWoods"));
	const FKnowledgeSource* Page = Sources.Find(TEXT("TornPageCep"));
	if (!TestNotNull(TEXT("Poster"), Poster) || !TestNotNull(TEXT("Torn page"), Page))
	{
		return false;
	}
	TestTrue(TEXT("Starting knowledge (D-015): the poster teaches chanterelle and fly agaric"),
		Poster->Species.Contains(TEXT("CantharellusCibarius")) && Poster->Species.Contains(TEXT("AmanitaMuscaria")));
	TestTrue(TEXT("The torn page teaches the cep"), Page->Species.Contains(TEXT("BoletusEdulis")) && Page->Kind == EKnowledgeSourceKind::Page);

	// Validation catches broken data.
	FKnowledgeSourceData Broken = Sources;
	Broken.Sources[0].Species.Add(TEXT("NotASpecies"));
	Broken.Sources[1].Landmark = TEXT("Castle");
	Broken.Sources[1].Id = Broken.Sources[0].Id;
	TArray<FString> BrokenErrors;
	TestFalse(TEXT("Unknown species, landmark and duplicate id are rejected"), KnowledgeText::ValidateSources(Broken, Species, BrokenErrors));
	TestEqual(TEXT("Three errors"), BrokenErrors.Num(), 3);

	// Levels: identified needs the source AND an examined specimen, in either order.
	const FName Chanterelle = TEXT("CantharellusCibarius");
	const FName Cep = TEXT("BoletusEdulis");
	FKnowledgeState State(&Sources);
	TestTrue(TEXT("Unknown at start"), State.GetLevel(Chanterelle) == EKnowledgeLevel::Unknown);
	TestFalse(TEXT("Examining without the poster does not identify"), State.RecordObservation(Chanterelle, 100.0));
	TestTrue(TEXT("Observed after examining"), State.GetLevel(Chanterelle) == EKnowledgeLevel::Observed);
	TArray<FName> Identified;
	TestTrue(TEXT("Poster acquired"), State.AcquireSource(Poster->Id, Identified));
	TestTrue(TEXT("Reading the poster identifies the chanterelle examined before"), Identified.Num() == 1 && Identified[0] == Chanterelle);
	TestTrue(TEXT("The fly agaric is taught but still unknown"), State.IsTaught(TEXT("AmanitaMuscaria")) && State.GetLevel(TEXT("AmanitaMuscaria")) == EKnowledgeLevel::Unknown);
	TestTrue(TEXT("Examining a fly agaric now identifies it"), State.RecordObservation(TEXT("AmanitaMuscaria"), 200.0));
	TestFalse(TEXT("A second examination is not a new identification"), State.RecordObservation(TEXT("AmanitaMuscaria"), 300.0));
	TestFalse(TEXT("A source is acquired once"), State.AcquireSource(Poster->Id, Identified));
	TestFalse(TEXT("The poster does not teach the cep"), State.IsTaught(Cep));
	State.RecordObservation(Cep, 400.0);
	TestTrue(TEXT("Cep observed only"), State.GetLevel(Cep) == EKnowledgeLevel::Observed);
	State.AcquireSource(Page->Id, Identified);
	TestTrue(TEXT("The torn page identifies the cep"), State.GetLevel(Cep) == EKnowledgeLevel::Identified);
	const FSpeciesKnowledge* Seen = State.Find(TEXT("AmanitaMuscaria"));
	TestTrue(TEXT("Find counts and dates"), Seen && Seen->Observations == 2 && Seen->FirstSeenGameSeconds == 200.0 && Seen->LastSeenGameSeconds == 300.0);
	State.Reset();
	TestTrue(TEXT("Reset forgets everything"), State.GetLevel(Cep) == EKnowledgeLevel::Unknown && !State.HasSource(Page->Id));

	// Texts: the real name only once identified; encyclopedia pages.
	const FMushroomSpeciesRow& ChanterelleRow = Species[Chanterelle];
	const FMushroomMorphology M = MushroomMorphology::Sample(ChanterelleRow, Chanterelle, 3u, EMushroomStage::Mature);
	TestTrue(TEXT("Unknown specimen: no name"), !KnowledgeText::SpecimenLabel(EKnowledgeLevel::Observed, ChanterelleRow, M).EqualTo(ChanterelleRow.CommonName));
	TestTrue(TEXT("Identified specimen: common name"), KnowledgeText::SpecimenLabel(EKnowledgeLevel::Identified, ChanterelleRow, M).EqualTo(ChanterelleRow.CommonName));
	TestEqual(TEXT("Scientific name from an id"), KnowledgeText::SpeciesIdToScientific(TEXT("OmphalotusOlearius")), FString(TEXT("Omphalotus olearius")));
	State.RecordObservation(Cep, 0.0);
	const KnowledgeText::FEntry Observed = KnowledgeText::EncyclopediaEntry(Cep, Species[Cep], State, 1);
	TestFalse(TEXT("An observed page does not name the species"), Observed.Title.ToString().Contains(Species[Cep].CommonName.ToString()));
	State.AcquireSource(Page->Id, Identified);
	const KnowledgeText::FEntry Named = KnowledgeText::EncyclopediaEntry(Cep, Species[Cep], State, 0);
	TestTrue(TEXT("An identified page names it and lists lookalikes"), Named.Title.ToString().Contains(Species[Cep].CommonName.ToString())
		&& Named.Lines.ContainsByPredicate([](const FText& Line) { return Line.ToString().StartsWith(TEXT("Do not confuse")); }));
	const TArray<FText> PosterLines = KnowledgeText::SourceLines(*Poster, [&Species](FName Id) { return Species.Find(Id); });
	TestTrue(TEXT("The poster warns that the fly agaric is toxic"), PosterLines.ContainsByPredicate([](const FText& Line) { return Line.ToString().Contains(TEXT("TOXIC")); }));

	// Every source hangs on a landmark of the F01 forest that can carry a sheet.
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(TEXT("F01_LowlandOakBeech"), Errors);
	if (!TestTrue(TEXT("Forest field"), Field.IsValid()))
	{
		return false;
	}
	// The buyer's counter (M3) hangs on the forester's house.
	{
		const FForestLandmark* House = Field->GetLandmarks().FindByPredicate([](const FForestLandmark& L) { return L.Kind == EForestLandmarkKind::ForesterHouse; });
		FTransform Mount;
		FVector2D Size;
		TestTrue(TEXT("The buyer's counter hangs on the forester's house"), House && LandmarkMeshBuilder::GetCounterMount(House->Kind, Mount, Size));
	}
	for (const FKnowledgeSource& Source : Sources.Sources)
	{
		if (Source.Kind == EKnowledgeSourceKind::Book)
		{
			continue; // sold at the counter
		}
		const FForestLandmark* Host = Field->GetLandmarks().FindByPredicate([&Source](const FForestLandmark& L) { return Source.Landmark == ForestLandmarks::KindName(L.Kind); });
		FTransform Mount;
		FVector2D Size;
		TestTrue(Source.Id.ToString() + TEXT(": hangs on a landmark of F01"), Host && LandmarkMeshBuilder::GetPaperMount(Host->Kind, Mount, Size));
		UE::Geometry::FDynamicMesh3 Mesh;
		FLandmarkCollision Collision;
		LandmarkMeshBuilder::BuildPaper(FVector2D(50.0, 70.0), { FLinearColor::Red, FLinearColor::Yellow }, Source.Kind == EKnowledgeSourceKind::Page, Mesh, &Collision);
		TestTrue(Source.Id.ToString() + TEXT(": sheet mesh with collision"), Mesh.TriangleCount() > 0 && Collision.Boxes.Num() == 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
