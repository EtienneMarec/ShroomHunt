#include "Forest/ForestDefinition.h"

#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ForestData
{
	FTrapezoid ToTrapezoid(const TArray<float>& Values)
	{
		if (Values.Num() != 4)
		{
			return FTrapezoid(0.0f, 0.0f, 1.0f, 1.0f);
		}
		return FTrapezoid(Values[0], Values[1], Values[2], Values[3]);
	}

	bool ParseForest(const FString& JsonText, const FString& SourceName, FForestDefinition& Out, TArray<FString>& OutErrors)
	{
		const int32 ErrorsBefore = OutErrors.Num();
		auto Error = [&OutErrors, &SourceName](const FString& Field, const FString& Message)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s: %s"), *SourceName, *Field, *Message));
		};

		Out = FForestDefinition();
		FText FailReason;
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &Out, 0, 0, false, &FailReason))
		{
			Error(TEXT("(file)"), FString::Printf(TEXT("invalid JSON: %s"), *FailReason.ToString()));
			return false;
		}

		if (Out.Name.IsEmpty())
		{
			Error(TEXT("Name"), TEXT("missing"));
		}
		const FString ExpectedName = FPaths::GetBaseFilename(SourceName);
		if (SourceName.EndsWith(TEXT(".json")) && Out.Name != ExpectedName)
		{
			Error(TEXT("Name"), FString::Printf(TEXT("'%s' must match the file name '%s'"), *Out.Name, *ExpectedName));
		}
		if (Out.HeightmapResolution < 65 || Out.HeightmapResolution > 4033 || (Out.HeightmapResolution - 1) % 4 != 0)
		{
			Error(TEXT("HeightmapResolution"), TEXT("must be a landscape size such as 253, 505, 1009 or 2017"));
		}
		if (Out.ReliefM <= 0.0f || Out.TerrainScaleM <= 0.0f || Out.SoilScaleM <= 0.0f)
		{
			Error(TEXT("ReliefM/TerrainScaleM/SoilScaleM"), TEXT("must be > 0"));
		}
		if (Out.StreamThresholdHa <= 0.0f || Out.ParcelSizeHa <= 0.0f)
		{
			Error(TEXT("StreamThresholdHa/ParcelSizeHa"), TEXT("must be > 0"));
		}
		if (Out.LimestoneShare < 0.0f || Out.LimestoneShare > 1.0f)
		{
			Error(TEXT("LimestoneShare"), TEXT("must be in [0, 1]"));
		}
		if (Out.Stands.Num() == 0)
		{
			Error(TEXT("Stands"), TEXT("needs at least one tree genus"));
		}
		float ShareSum = 0.0f;
		for (int32 Index = 0; Index < Out.Stands.Num(); ++Index)
		{
			const FStandSpecies& Stand = Out.Stands[Index];
			const FString Field = FString::Printf(TEXT("Stands[%d]"), Index);
			if (Stand.Genus.IsNone())
			{
				Error(Field + TEXT(".Genus"), TEXT("missing"));
			}
			if (Stand.Share < 0.0f)
			{
				Error(Field + TEXT(".Share"), TEXT("must be >= 0"));
			}
			ShareSum += Stand.Share;
			for (const TPair<const TCHAR*, const TArray<float>*>& Range : { TPair<const TCHAR*, const TArray<float>*>(TEXT("Moisture"), &Stand.Moisture),
				TPair<const TCHAR*, const TArray<float>*>(TEXT("PH"), &Stand.PH) })
			{
				if (Range.Value->Num() != 4 || !ToTrapezoid(*Range.Value).IsOrdered())
				{
					Error(Field + TEXT(".") + Range.Key, TEXT("expected [min, optLow, optHigh, max] in increasing order"));
				}
			}
			if (Stand.Pioneer < 0.0f || Stand.Pioneer > 1.0f || Stand.Riparian < 0.0f || Stand.Riparian > 1.0f)
			{
				Error(Field, TEXT("Pioneer and Riparian must be in [0, 1]"));
			}
		}
		if (Out.Entrances < 1 || Out.Hubs < 0 || Out.ExtraRoadLoops < 0 || Out.Footpaths < 0)
		{
			Error(TEXT("Entrances/Hubs/ExtraRoadLoops/Footpaths"), TEXT("need at least one entrance and no negative count"));
		}
		if (Out.RoadHalfWidthM <= 0.0f || Out.RideHalfWidthM <= 0.0f || Out.FootpathHalfWidthM <= 0.0f || Out.RoadGradeCost < 0.0f)
		{
			Error(TEXT("RoadHalfWidthM/RideHalfWidthM/FootpathHalfWidthM/RoadGradeCost"), TEXT("widths must be > 0 and the grade cost >= 0"));
		}
		if (Out.Stands.Num() > 0 && ShareSum <= 0.0f)
		{
			Error(TEXT("Stands"), TEXT("shares must not all be 0"));
		}
		return OutErrors.Num() == ErrorsBefore;
	}

	bool LoadForest(const FString& ForestName, FForestDefinition& Out, TArray<FString>& OutErrors)
	{
		const FString Path = FPaths::Combine(GetDefaultFolder(), ForestName + TEXT(".json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cannot read file"), *Path));
			return false;
		}
		return ParseForest(Text, FPaths::GetCleanFilename(Path), Out, OutErrors);
	}

	FString GetDefaultFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Forests")));
	}
}
