#include "Environment/GroundTextures.h"
#include "Misc/AutomationTest.h"
#include "Vegetation/VegetationDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GroundTextureTestsDetail
{
	float ColorDistance(const FColor& A, const FColor& B)
	{
		return FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B) + FMath::Abs(A.A - B.A);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundTextureTest, "ShroomHunt.Environment.GroundTextures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGroundTextureTest::RunTest(const FString& Parameters)
{
	using namespace GroundTextureTestsDetail;
	FGroundTexturesDefinition Ground;
	FVegetationDefinition Vegetation;
	TArray<FString> Errors;
	if (!GroundTextures::Load(Ground, Errors) || !VegetationData::LoadVegetation(Vegetation, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	for (const TPair<FName, float>& Entry : Ground.LeafGenera)
	{
		TestNotNull(FString::Printf(TEXT("Leaf genus %s exists in Vegetation.json"), *Entry.Key.ToString()), Vegetation.FindTree(Entry.Key));
	}
	for (const FName Expected : { FName(TEXT("Litter")), FName(TEXT("Moss")), FName(TEXT("Grass")), FName(TEXT("Mud")), FName(TEXT("Gravel")), FName(TEXT("Dirt")) })
	{
		TestTrue(FString::Printf(TEXT("Landscape layer %s has a ground style"), *Expected.ToString()),
			Ground.Layers.ContainsByPredicate([Expected](const FGroundLayerStyle& S) { return S.Layer == Expected; }));
	}

	// Smaller textures keep the test fast; the drawing scales with the resolution.
	Ground.Resolution = 256;
	for (const FGroundLayerStyle& Style : Ground.Layers)
	{
		const FString Key = Style.Layer.ToString();
		const FGroundTextureImage Full = GroundTextures::Generate(Ground, Style, Vegetation);
		double AlphaSum = 0.0;
		for (const FColor& C : Full.Albedo)
		{
			AlphaSum += C.A;
		}
		TestTrue(FString::Printf(TEXT("%s mean surface at ground level (alpha %.1f, expected ~102)"), *Key, AlphaSum / Full.Albedo.Num()),
			FMath::Abs(AlphaSum / Full.Albedo.Num() - 0.4 * 255.0) < 4.0);
		const FGroundTextureImage Image = GroundTextures::Generate(Ground, Style, Vegetation);
		const FGroundTextureImage Again = GroundTextures::Generate(Ground, Style, Vegetation);
		const int32 Size = Image.Size;
		if (!TestEqual(Key + TEXT(" size"), Image.Albedo.Num(), Size * Size) || !TestEqual(Key + TEXT(" normal size"), Image.Normal.Num(), Size * Size))
		{
			continue;
		}
		TestTrue(Key + TEXT(" is deterministic"), Image.Albedo == Again.Albedo && Image.Normal == Again.Normal);

		// Seamless: across the wrap, neighbors differ no more than inside the tile.
		double Inside = 0.0;
		double Across = 0.0;
		for (int32 I = 0; I < Size; ++I)
		{
			Inside += ColorDistance(Image.Albedo[I * Size + Size / 2], Image.Albedo[I * Size + Size / 2 + 1]);
			Inside += ColorDistance(Image.Albedo[(Size / 2) * Size + I], Image.Albedo[(Size / 2 + 1) * Size + I]);
			Across += ColorDistance(Image.Albedo[I * Size + Size - 1], Image.Albedo[I * Size]);
			Across += ColorDistance(Image.Albedo[(Size - 1) * Size + I], Image.Albedo[I]);
		}
		TestTrue(FString::Printf(TEXT("%s tiles seamlessly (edge %.1f vs inside %.1f)"), *Key, Across / Size, Inside / Size), Across < Inside * 1.6 + Size);

		bool bNormalsUp = true;
		for (const FColor& N : Image.Normal)
		{
			bNormalsUp &= N.B >= 128;
		}
		TestTrue(Key + TEXT(" normals face up"), bNormalsUp);
	}

	// The leaf litter is a closed carpet of leaves, and its relief stays physical (D-042, D-043): a few
	// centimeters around ground level, no plateaus that the displacement would turn into slabs.
	if (const FGroundLayerStyle* Litter = Ground.Layers.FindByPredicate([](const FGroundLayerStyle& S) { return S.Layer == TEXT("Litter"); }))
	{
		const FGroundTextureImage Image = GroundTextures::Generate(Ground, *Litter, Vegetation);
		TestTrue(FString::Printf(TEXT("Litter covers the soil (%.0f %% covered)"), Image.CoveredShare * 100.0f), Image.CoveredShare > 0.85f);
		int32 MinAlpha = 255;
		int32 MaxAlpha = 0;
		int32 MaxStep = 0;
		const int32 Size = Image.Size;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 A = Image.Albedo[Y * Size + X].A;
				MinAlpha = FMath::Min(MinAlpha, A);
				MaxAlpha = FMath::Max(MaxAlpha, A);
				MaxStep = FMath::Max(MaxStep, FMath::Abs(A - Image.Albedo[Y * Size + (X + 1) % Size].A));
			}
		}
		const float RangeCm = (MaxAlpha - MinAlpha) / 255.0f * Ground.ReliefRangeCm;
		const float StepCm = MaxStep / 255.0f * Ground.ReliefRangeCm;
		// Physical sizes times the layer's stylized exaggeration.
		const float Scale = FMath::Max(Litter->ReliefScale, 1.0f);
		TestTrue(FString::Printf(TEXT("Litter relief within a few cm (%.1f cm), steps under 1 cm (%.2f cm), scale %.1f"), RangeCm, StepCm, Scale),
			RangeCm < 6.0f * Scale && StepCm < 1.0f * Scale);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
