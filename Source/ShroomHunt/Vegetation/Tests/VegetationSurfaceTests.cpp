#include "Core/MeshAccumulator.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "StaticMeshAttributes.h"
#include "Vegetation/VegetationDefinition.h"
#include "Vegetation/VegetationMeshBuilder.h"
#include "Vegetation/VegetationSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace VegetationSurfaceTestsDetail
{
	/** Number of vertex instances whose UV1.U encodes the surface id (U = (Id + 0.5) / 8); -1 without UV1. */
	int32 CountSurface(const FMeshDescription& Description, EVegetationSurface Surface)
	{
		const FStaticMeshConstAttributes Attributes(Description);
		const TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		if (UVs.GetNumChannels() < 2)
		{
			return -1;
		}
		const float Expected = (static_cast<float>(Surface) + 0.5f) / 8.0f;
		int32 Count = 0;
		for (const FVertexInstanceID Id : Description.VertexInstances().GetElementIDs())
		{
			Count += FMath::IsNearlyEqual(UVs.Get(Id, 1).X, Expected, 1e-3f) ? 1 : 0;
		}
		return Count;
	}
}

/**
 * The seasonal material finds the leaves by the surface id in UV channel 1 (TexCoord[1]). It once
 * read 0 everywhere: overlay elements shared between vertices were rejected without an error.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVegetationSurfaceTest, "ShroomHunt.Vegetation.SurfaceIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FVegetationSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace VegetationSurfaceTestsDetail;
	FVegetationDefinition Definition;
	TArray<FString> Errors;
	if (!VegetationData::LoadVegetation(Definition, Errors))
	{
		AddError(FString::Join(Errors, TEXT("\n")));
		return false;
	}
	FVegetationMeshSpec Spec;
	Spec.Kind = EVegetationKind::Mature;
	Spec.Genus = TEXT("Fagus");

	// 1. The mesh description handed to the static mesh carries the surface ids.
	UE::Geometry::FDynamicMesh3 Mesh;
	VegetationMeshBuilder::Build(Definition, Spec, Mesh);
	FMeshDescription Description;
	ShroomMesh::ToMeshDescription(Mesh, TEXT("Vegetation"), Description);
	const int32 Leaves = CountSurface(Description, EVegetationSurface::Foliage);
	const int32 Bark = CountSurface(Description, EVegetationSurface::Bark);
	TestTrue(FString::Printf(TEXT("Built mesh: foliage vertices in UV1 (%d)"), Leaves), Leaves > 1000);
	TestTrue(FString::Printf(TEXT("Built mesh: bark vertices in UV1 (%d)"), Bark), Bark > 100);
	TestEqual(TEXT("Built mesh: every vertex has a surface id"), Leaves + Bark, Description.VertexInstances().Num());

	// 2. The baked asset kept them.
	const FString Package = GetDefault<UVegetationSettings>()->GetGeneratedPackageName(Spec);
	const FString Name = FPackageName::GetShortName(Package);
	if (UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		const FMeshDescription* Baked = Asset->GetMeshDescription(0);
		const int32 BakedLeaves = Baked ? CountSurface(*Baked, EVegetationSurface::Foliage) : -1;
		TestTrue(FString::Printf(TEXT("Baked %s: foliage vertices in UV1 (%d)"), *Name, BakedLeaves), BakedLeaves > 1000);
	}
	else
	{
		AddWarning(Package + TEXT(" is not baked yet"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
