#include "Forest/ForestBake.h"
#include "Forest/ForestDefinition.h"
#include "Forest/ForestField.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ForestFieldTestsDetail
{
	const TCHAR* ForestName = TEXT("F01_LowlandOakBeech");

	uint32 HashRaster(const FFieldRaster& Raster)
	{
		uint32 H = 0;
		for (const float V : Raster.Values)
		{
			H = HashCombineFast(H, GetTypeHash(FMath::RoundToInt(V * 1000.0f)));
		}
		return H;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForestDefinitionTest, "ShroomHunt.Forest.Definition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FForestDefinitionTest::RunTest(const FString& Parameters)
{
	FForestDefinition Definition;
	TArray<FString> Errors;
	TestTrue(TEXT("F01 parses: ") + FString::Join(Errors, TEXT("; ")), ForestData::LoadForest(ForestFieldTestsDetail::ForestName, Definition, Errors));
	TestEqual(TEXT("Five tree genera"), Definition.Stands.Num(), 5);
	TestEqual(TEXT("1 km side"), Definition.GetSizeM(), 1008.0f);

	FForestDefinition Bad;
	TArray<FString> BadErrors;
	TestFalse(TEXT("Invalid heightmap size is rejected"),
		ForestData::ParseForest(TEXT("{ \"Name\": \"X\", \"HeightmapResolution\": 1000, \"Stands\": [ { \"Genus\": \"Fagus\", \"Share\": 1, \"Moisture\": [0, 0.5, 0.4, 1], \"PH\": [3, 4, 7, 8] } ] }"),
			TEXT("Test"), Bad, BadErrors));
	TestTrue(TEXT("Both errors reported (size and unordered trapezoid)"), BadErrors.Num() >= 2);

	TestEqual(TEXT("Height 0 encodes to the landscape mid value"), static_cast<int32>(ForestBake::EncodeHeight(0.0f)), 32768);
	TestEqual(TEXT("1 m encodes to +128 units at Z scale 100"), static_cast<int32>(ForestBake::EncodeHeight(1.0f)), 32768 + 128);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForestFieldTest, "ShroomHunt.Forest.Field",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FForestFieldTest::RunTest(const FString& Parameters)
{
	using namespace ForestFieldTestsDetail;

	FForestDefinition Definition;
	TArray<FString> Errors;
	if (!ForestData::LoadForest(ForestName, Definition, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}

	FForestField A;
	FForestField B;
	TestTrue(TEXT("Generates"), A.Generate(Definition) && B.Generate(Definition));
	TestTrue(TEXT("Deterministic heights"), HashRaster(A.GetHeights()) == HashRaster(B.GetHeights()));
	TestTrue(TEXT("Deterministic moisture"), HashRaster(A.GetMoisture()) == HashRaster(B.GetMoisture()));
	TestTrue(TEXT("Deterministic parcels"), A.GetParcelCells() == B.GetParcelCells());

	FForestDefinition Other = Definition;
	Other.Seed += 1;
	FForestField C;
	C.Generate(Other);
	TestTrue(TEXT("Another seed gives another forest"), HashRaster(A.GetHeights()) != HashRaster(C.GetHeights()));

	// Relief within the requested range, plausible water network.
	float MinH = 1e9f;
	float MaxH = -1e9f;
	for (const float H : A.GetHeights().Values)
	{
		MinH = FMath::Min(MinH, H);
		MaxH = FMath::Max(MaxH, H);
	}
	AddInfo(FString::Printf(TEXT("Relief %.1f m, %d parcels, %d stream cells, %d pond cells"), MaxH - MinH, A.GetParcels().Num(), A.GetStreamCellCount(), A.GetPondCellCount()));
	TestTrue(TEXT("Relief between 0.5x and 2x ReliefM"), MaxH - MinH > 0.5f * Definition.ReliefM && MaxH - MinH < 2.0f * Definition.ReliefM + Definition.ValleyDepthM);
	TestTrue(TEXT("Streams exist"), A.GetStreamCellCount() > 20);
	const int32 ExpectedParcels = FMath::RoundToInt(A.GetSizeM() * A.GetSizeM() / (Definition.ParcelSizeHa * 10000.0f));
	TestTrue(TEXT("Parcel count close to area / parcel size"), FMath::Abs(A.GetParcels().Num() - ExpectedParcels) <= FMath::Max(2, ExpectedParcels / 3));

	// Value ranges and stand normalization.
	const int32 Cells = A.GetMoisture().Values.Num();
	const int32 NumGenera = A.GetGenera().Num();
	const int32 Alder = A.FindGenus(TEXT("Alnus"));
	double AlderNearWater = 0.0;
	double AlderElsewhere = 0.0;
	int32 NearCount = 0;
	int32 FarCount = 0;
	double MoistureNear = 0.0;
	double MoistureAll = 0.0;
	for (int32 Index = 0; Index < Cells; ++Index)
	{
		const float M = A.GetMoisture().Values[Index];
		const float PH = A.GetPH().Values[Index];
		const float Canopy = A.GetCanopy().Values[Index];
		if (M < 0.0f || M > 1.0f || PH < 3.5f || PH > 8.5f || Canopy < 0.0f || Canopy > 1.0f)
		{
			AddError(FString::Printf(TEXT("Out of range at cell %d: moisture %.2f pH %.2f canopy %.2f"), Index, M, PH, Canopy));
			return false;
		}
		float Sum = 0.0f;
		for (int32 Genus = 0; Genus < NumGenera; ++Genus)
		{
			Sum += A.GetStandWeight(Index, Genus);
		}
		if (!FMath::IsNearlyEqual(Sum, 1.0f, 1e-3f))
		{
			AddError(FString::Printf(TEXT("Stand weights sum to %.3f at cell %d"), Sum, Index));
			return false;
		}
		MoistureAll += M;
		if (A.GetWaterDistance().Values[Index] < 20.0f && A.GetWaterCells()[Index] == 0)
		{
			AlderNearWater += A.GetStandWeight(Index, Alder);
			MoistureNear += M;
			++NearCount;
		}
		else if (A.GetWaterDistance().Values[Index] > 60.0f)
		{
			AlderElsewhere += A.GetStandWeight(Index, Alder);
			++FarCount;
		}
	}
	if (NearCount > 0 && FarCount > 0)
	{
		TestTrue(TEXT("Alders concentrate along the water"), AlderNearWater / NearCount > 3.0 * AlderElsewhere / FarCount);
		TestTrue(TEXT("Ground is wetter near the water"), MoistureNear / NearCount > MoistureAll / Cells);
	}

	// Sample() agrees with the rasters.
	const FForestSample Sample = A.Sample(500.0f, 500.0f);
	TestEqual(TEXT("Sample has one weight per genus"), Sample.StandWeights.Num(), NumGenera);
	TestTrue(TEXT("Altitude = base + height"), FMath::IsNearlyEqual(Sample.AltitudeM, Definition.BaseAltitudeM + Sample.HeightM, 1e-3f));
	TestTrue(TEXT("Parcel number is valid"), Sample.ParcelNumber >= 1 && Sample.ParcelNumber <= A.GetParcels().Num());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
