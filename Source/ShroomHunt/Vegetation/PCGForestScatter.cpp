#include "Vegetation/PCGForestScatter.h"

#include "Core/ShroomLog.h"
#include "Data/PCGBasePointData.h"
#include "Forest/ForestField.h"
#include "Forest/ForestFieldCache.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Vegetation/VegetationScatter.h"
#include "Vegetation/VegetationSettings.h"

#define LOCTEXT_NAMESPACE "PCGForestScatterElement"

#if WITH_EDITOR
FText UPCGForestScatterSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Forest Scatter");
}

FText UPCGForestScatterSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Trees or understory of a ShroomHunt forest, placed from its forest field. Points carry Mesh, Kind and Genus attributes.");
}
#endif

FPCGElementPtr UPCGForestScatterSettings::CreateElement() const
{
	return MakeShared<FPCGForestScatterElement>();
}

bool FPCGForestScatterElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGForestScatterElement::Execute);
	check(Context);
	if (!Context->ExecutionSource.IsValid())
	{
		return true;
	}
	const UPCGForestScatterSettings* Settings = Context->GetInputSettings<UPCGForestScatterSettings>();
	check(Settings);

	TArray<FString> Errors;
	const TSharedPtr<const FForestField> Field = ForestFieldCache::GetField(Settings->ForestName, Errors);
	const TSharedPtr<const FVegetationDefinition> Vegetation = ForestFieldCache::GetVegetation(Errors);
	if (!Field || !Vegetation)
	{
		for (const FString& Error : Errors)
		{
			PCGE_LOG(Error, GraphAndLog, FText::FromString(Error));
		}
		return true;
	}

	// Field meters <-> world centimeters: 1 m per landscape vertex, height 0 at the origin's Z.
	constexpr double CmPerM = 100.0;
	const FVector Origin = Settings->ForestOrigin;
	const FBox Bounds = Context->ExecutionSource->GetExecutionState().GetBounds();
	const FBox2d BoundsM(
		FVector2d((Bounds.Min.X - Origin.X) / CmPerM, (Bounds.Min.Y - Origin.Y) / CmPerM),
		FVector2d((Bounds.Max.X - Origin.X) / CmPerM, (Bounds.Max.Y - Origin.Y) / CmPerM));

	TArray<FVegetationInstance> Instances;
	VegetationScatter::Scatter(*Field, *Vegetation, Settings->Layer, BoundsM, Instances);

	UPCGBasePointData* Points = FPCGContext::NewPointData_AnyThread(Context);
	check(Points);
	Points->SetNumPoints(Instances.Num());
	Points->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Seed | EPCGPointNativeProperties::MetadataEntry);
	Points->SetDensity(1.0f);
	Points->SetSteepness(1.0f);

	FPCGMetadataDomain* Domain = Points->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
	check(Domain);
	FPCGMetadataAttribute<FSoftObjectPath>* MeshAttribute = Domain->FindOrCreateAttribute<FSoftObjectPath>(Settings->MeshAttribute, FSoftObjectPath(), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/true);
	FPCGMetadataAttribute<FString>* KindAttribute = Domain->FindOrCreateAttribute<FString>(TEXT("Kind"), FString(), false, true);
	FPCGMetadataAttribute<FString>* GenusAttribute = Domain->FindOrCreateAttribute<FString>(TEXT("Genus"), FString(), false, true);
	if (!MeshAttribute || !KindAttribute || !GenusAttribute)
	{
		PCGE_LOG(Error, GraphAndLog, LOCTEXT("AttributeFailed", "Could not create the Mesh/Kind/Genus attributes."));
		return true;
	}

	const UVegetationSettings* VegetationSettings = GetDefault<UVegetationSettings>();
	TMap<FString, FSoftObjectPath> MeshPaths;
	TPCGValueRange<FTransform> Transforms = Points->GetTransformValueRange(/*bAllocate=*/false);
	TPCGValueRange<int32> Seeds = Points->GetSeedValueRange(/*bAllocate=*/false);
	TPCGValueRange<PCGMetadataEntryKey> Entries = Points->GetMetadataEntryValueRange(/*bAllocate=*/false);
	for (int32 Index = 0; Index < Instances.Num(); ++Index)
	{
		const FVegetationInstance& Instance = Instances[Index];
		const FVector Location = Origin + FVector(Instance.PositionM.X, Instance.PositionM.Y, Instance.PositionM.Z) * CmPerM;
		Transforms[Index] = FTransform(FRotator(Instance.PitchDeg, Instance.YawDeg, Instance.RollDeg), Location, FVector(Instance.Scale));
		Seeds[Index] = PCGHelpers::ComputeSeedFromPosition(Location);

		Entries[Index] = PCGInvalidEntryKey;
		Domain->InitializeOnSet(Entries[Index]);
		const FVegetationMeshSpec Spec = Instance.GetMeshSpec();
		const FString Key = Spec.GetKey();
		const FSoftObjectPath* Path = MeshPaths.Find(Key);
		if (!Path)
		{
			Path = &MeshPaths.Add(Key, VegetationSettings->GetMeshPath(Spec));
		}
		MeshAttribute->SetValue(Entries[Index], *Path);
		KindAttribute->SetValue(Entries[Index], FString(VegetationData::KindName(Instance.Kind)));
		GenusAttribute->SetValue(Entries[Index], Instance.Genus.IsNone() ? FString() : Instance.Genus.ToString());
	}

	UE_LOG(LogShroomVegetation, Verbose, TEXT("Forest Scatter %s %s: %d points in [%.0f, %.0f] - [%.0f, %.0f] m"), *Settings->ForestName,
		Settings->Layer == EVegetationLayer::Trees ? TEXT("trees") : TEXT("understory"), Instances.Num(), BoundsM.Min.X, BoundsM.Min.Y, BoundsM.Max.X, BoundsM.Max.Y);

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = Points;
	return true;
}

#undef LOCTEXT_NAMESPACE
