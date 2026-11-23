#include "Vegetation/VegetationDefinition.h"

#include "Core/ShroomHash.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

float FAgeClassValues::Get(EStandAgeClass AgeClass) const
{
	switch (AgeClass)
	{
	case EStandAgeClass::ClearCut: return ClearCut;
	case EStandAgeClass::Regeneration: return Regeneration;
	case EStandAgeClass::Pole: return Pole;
	case EStandAgeClass::Mature: return Mature;
	case EStandAgeClass::Old: return Old;
	}
	return 0.0f;
}

const FTreeFormMix& FAgeClassFormMix::Get(EStandAgeClass AgeClass) const
{
	switch (AgeClass)
	{
	case EStandAgeClass::ClearCut: return ClearCut;
	case EStandAgeClass::Regeneration: return Regeneration;
	case EStandAgeClass::Pole: return Pole;
	case EStandAgeClass::Old: return Old;
	default: return Mature;
	}
}

int32 FVegetationDefinition::GetVariantCount(EVegetationKind Kind) const
{
	if (VegetationData::IsTreeKind(Kind))
	{
		const FTreeFormDefinition* Form = FindForm(Kind);
		return Form ? FMath::Max(1, Form->Variants) : 0;
	}
	const FUnderstoryRule* Rule = FindRule(Kind);
	return Rule ? FMath::Max(1, Rule->Variants) : 0;
}

const FUnderstoryRule* FVegetationDefinition::FindRule(EVegetationKind Kind) const
{
	const FUnderstoryRule* Rule = Understory.FindByPredicate([Kind](const FUnderstoryRule& R) { return R.Kind == Kind; });
	return Rule ? Rule : Ground.FindByPredicate([Kind](const FUnderstoryRule& R) { return R.Kind == Kind; });
}

TArray<const FUnderstoryRule*> FVegetationDefinition::GetRules(EVegetationLayer Layer) const
{
	TArray<const FUnderstoryRule*> Rules;
	for (const TArray<FUnderstoryRule>* List : { &Understory, &Ground })
	{
		for (const FUnderstoryRule& Rule : *List)
		{
			if (VegetationData::GetLayer(Rule.Kind) == Layer)
			{
				Rules.Add(&Rule);
			}
		}
	}
	return Rules;
}

FString FVegetationMeshSpec::GetKey() const
{
	if (Genus.IsNone())
	{
		return FString::Printf(TEXT("%s_%d"), VegetationData::KindName(Kind), Variant);
	}
	return FString::Printf(TEXT("%s_%s_%d"), *Genus.ToString(), VegetationData::KindName(Kind), Variant);
}

uint32 FVegetationMeshSpec::GetSeed() const
{
	return ShroomHash::Hash(ShroomHash::HashString(GetKey()), 0x7EE5u);
}

namespace VegetationData
{
	bool IsTreeKind(EVegetationKind Kind)
	{
		return Kind <= EVegetationKind::Old;
	}

	bool HasGenus(EVegetationKind Kind)
	{
		return IsTreeKind(Kind) || Kind == EVegetationKind::Leaf;
	}

	EVegetationLayer GetLayer(EVegetationKind Kind)
	{
		if (Kind == EVegetationKind::Pole || Kind == EVegetationKind::Mature || Kind == EVegetationKind::Old)
		{
			return EVegetationLayer::Trees;
		}
		return Kind >= EVegetationKind::Leaf ? EVegetationLayer::Ground : EVegetationLayer::Understory;
	}

	const TCHAR* KindName(EVegetationKind Kind)
	{
		switch (Kind)
		{
		case EVegetationKind::Sapling: return TEXT("Sapling");
		case EVegetationKind::Pole: return TEXT("Pole");
		case EVegetationKind::Mature: return TEXT("Mature");
		case EVegetationKind::Old: return TEXT("Old");
		case EVegetationKind::Bracken: return TEXT("Bracken");
		case EVegetationKind::Bramble: return TEXT("Bramble");
		case EVegetationKind::Log: return TEXT("Log");
		case EVegetationKind::Stump: return TEXT("Stump");
		case EVegetationKind::Leaf: return TEXT("Leaf");
		case EVegetationKind::Twigs: return TEXT("Twigs");
		case EVegetationKind::Stones: return TEXT("Stones");
		case EVegetationKind::Moss: return TEXT("Moss");
		}
		return TEXT("Unknown");
	}

	FLinearColor ToColor(const TArray<float>& Values, const FLinearColor& Fallback)
	{
		if (Values.Num() != 3)
		{
			return Fallback;
		}
		return FLinearColor(FMath::Clamp(Values[0], 0.0f, 1.0f), FMath::Clamp(Values[1], 0.0f, 1.0f), FMath::Clamp(Values[2], 0.0f, 1.0f), 1.0f);
	}

	bool ParseVegetation(const FString& JsonText, const FString& SourceName, FVegetationDefinition& Out, TArray<FString>& OutErrors)
	{
		const int32 ErrorsBefore = OutErrors.Num();
		auto Error = [&OutErrors, &SourceName](const FString& Field, const FString& Message)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s: %s"), *SourceName, *Field, *Message));
		};
		auto CheckTrapezoid = [&Error](const FString& Field, const TArray<float>& Values)
		{
			if (Values.Num() != 4 || !ForestData::ToTrapezoid(Values).IsOrdered())
			{
				Error(Field, TEXT("expected [min, optLow, optHigh, max] in increasing order"));
			}
		};
		auto CheckColor = [&Error](const FString& Field, const TArray<float>& Values, bool bOptional)
		{
			if ((Values.Num() == 0 && bOptional) || Values.Num() == 3)
			{
				return;
			}
			Error(Field, TEXT("expected an sRGB color [r, g, b]"));
		};

		Out = FVegetationDefinition();
		FText FailReason;
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &Out, 0, 0, false, &FailReason))
		{
			Error(TEXT("(file)"), FString::Printf(TEXT("invalid JSON: %s"), *FailReason.ToString()));
			return false;
		}

		if (Out.Trees.Num() == 0)
		{
			Error(TEXT("Trees"), TEXT("needs at least one genus"));
		}
		for (int32 Index = 0; Index < Out.Trees.Num(); ++Index)
		{
			const FTreeArchetype& Tree = Out.Trees[Index];
			const FString Field = FString::Printf(TEXT("Trees[%d]"), Index);
			if (Tree.Genus.IsNone())
			{
				Error(Field + TEXT(".Genus"), TEXT("missing"));
			}
			if (Tree.HeightM <= 1.0f || Tree.TrunkRadiusM <= 0.0f || Tree.CrownRadiusM <= 0.0f)
			{
				Error(Field, TEXT("HeightM > 1, TrunkRadiusM > 0 and CrownRadiusM > 0 required"));
			}
			if (Tree.ClearBole < 0.0f || Tree.ClearBole > 0.9f || Tree.CrownPeak < 0.0f || Tree.CrownPeak > 1.0f)
			{
				Error(Field, TEXT("ClearBole must be in [0, 0.9] and CrownPeak in [0, 1]"));
			}
			if (Tree.CrownExponent < 0.5f)
			{
				Error(Field + TEXT(".CrownExponent"), TEXT("must be >= 0.5"));
			}
			CheckColor(Field + TEXT(".Bark"), Tree.Bark, false);
			CheckColor(Field + TEXT(".BarkPatch"), Tree.BarkPatch, true);
			CheckColor(Field + TEXT(".Foliage"), Tree.Foliage, false);
			CheckColor(Field + TEXT(".Litter"), Tree.Litter, true);
			if (Tree.LeafSizeCm <= 0.0f || Tree.LeafLobes < 0)
			{
				Error(Field, TEXT("LeafSizeCm > 0 and LeafLobes >= 0 required"));
			}
			CheckColor(Field + TEXT(".AutumnFoliage"), Tree.AutumnFoliage, true);
			auto CheckMonths = [&Error](const FString& MonthsField, const TArray<float>& Months)
			{
				if (Months.Num() != 0 && (Months.Num() != 2 || Months[0] < 1.0f || Months[1] < Months[0] || Months[1] > 14.0f))
				{
					Error(MonthsField, TEXT("expected [start, end] months in [1, 14] in increasing order"));
				}
			};
			CheckMonths(Field + TEXT(".ColoringMonths"), Tree.ColoringMonths);
			CheckMonths(Field + TEXT(".LeafFallMonths"), Tree.LeafFallMonths);
			if (Tree.LeafOutMonth < 1.0f || Tree.LeafOutMonth > 7.0f || Tree.WinterLeaves < 0.0f || Tree.WinterLeaves > 1.0f)
			{
				Error(Field, TEXT("LeafOutMonth in [1, 7] and WinterLeaves in [0, 1] required"));
			}
		}

		for (const EVegetationKind Kind : { EVegetationKind::Sapling, EVegetationKind::Pole, EVegetationKind::Mature, EVegetationKind::Old })
		{
			const FTreeFormDefinition* Form = Out.FindForm(Kind);
			if (!Form)
			{
				Error(TEXT("Forms"), FString::Printf(TEXT("missing form %s"), KindName(Kind)));
			}
			else if (Form->Height <= 0.0f || Form->TrunkRadius <= 0.0f || Form->Crown <= 0.0f || Form->Clumps < 1 || Form->Variants < 1)
			{
				Error(FString::Printf(TEXT("Forms.%s"), KindName(Kind)), TEXT("Height, TrunkRadius, Crown > 0 and Clumps, Variants >= 1 required"));
			}
		}
		for (const FTreeFormDefinition& Form : Out.Forms)
		{
			if (!IsTreeKind(Form.Kind))
			{
				Error(TEXT("Forms"), FString::Printf(TEXT("%s is not a tree form"), KindName(Form.Kind)));
			}
		}

		const FTreeDensityDefinition& Density = Out.TreeDensity;
		if (Density.PerHa.GetMax() <= 0.0f)
		{
			Error(TEXT("TreeDensity.PerHa"), TEXT("needs a density > 0"));
		}
		if (Density.SpacingFactor < 0.0f || Density.SpacingFactor > 1.0f)
		{
			Error(TEXT("TreeDensity.SpacingFactor"), TEXT("must be in [0, 1]"));
		}
		if (Density.GapCanopy.Num() != 2 || Density.GapCanopy[0] > Density.GapCanopy[1])
		{
			Error(TEXT("TreeDensity.GapCanopy"), TEXT("expected [low, high]"));
		}

		auto CheckRules = [&](const TArray<FUnderstoryRule>& Rules, const TCHAR* ListName, EVegetationLayer Layer, const TCHAR* Allowed)
		{
			for (int32 Index = 0; Index < Rules.Num(); ++Index)
			{
				const FUnderstoryRule& Rule = Rules[Index];
				const FString Field = FString::Printf(TEXT("%s[%d] (%s)"), ListName, Index, KindName(Rule.Kind));
				if (GetLayer(Rule.Kind) != Layer)
				{
					Error(Field + TEXT(".Kind"), FString::Printf(TEXT("must be %s"), Allowed));
				}
				CheckTrapezoid(Field + TEXT(".Moisture"), Rule.Moisture);
				CheckTrapezoid(Field + TEXT(".PH"), Rule.PH);
				CheckTrapezoid(Field + TEXT(".Light"), Rule.Light);
				if (Rule.SpacingFactor < 0.0f || Rule.SpacingFactor > 1.0f || Rule.Variants < 1 || Rule.SlopeBoost < 0.0f || Rule.BankBoost < 0.0f)
				{
					Error(Field, TEXT("SpacingFactor in [0, 1], Variants >= 1 and SlopeBoost, BankBoost >= 0 required"));
				}
				if (Rule.PathFactor.Num() != 0 && (Rule.PathFactor.Num() != 3 || Rule.PathFactor.ContainsByPredicate([](float F) { return F < 0.0f || F > 1.0f; })))
				{
					Error(Field + TEXT(".PathFactor"), TEXT("expected [road, ride, footpath] in [0, 1]"));
				}
				for (int32 Other = 0; Other < Index; ++Other)
				{
					if (Rules[Other].Kind == Rule.Kind)
					{
						Error(Field + TEXT(".Kind"), TEXT("duplicate"));
					}
				}
			}
		};
		CheckRules(Out.Understory, TEXT("Understory"), EVegetationLayer::Understory, TEXT("Sapling, Bracken, Bramble, Log or Stump"));
		CheckRules(Out.Ground, TEXT("Ground"), EVegetationLayer::Ground, TEXT("Leaf, Twigs, Stones or Moss"));
		return OutErrors.Num() == ErrorsBefore;
	}

	FString GetDefaultPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Vegetation"), TEXT("Vegetation.json")));
	}

	bool LoadVegetation(FVegetationDefinition& Out, TArray<FString>& OutErrors)
	{
		const FString Path = GetDefaultPath();
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cannot read file"), *Path));
			return false;
		}
		return ParseVegetation(Text, FPaths::GetCleanFilename(Path), Out, OutErrors);
	}

	TArray<FVegetationMeshSpec> ListMeshes(const FVegetationDefinition& Definition)
	{
		TArray<FVegetationMeshSpec> Specs;
		for (const FTreeArchetype& Tree : Definition.Trees)
		{
			for (const FTreeFormDefinition& Form : Definition.Forms)
			{
				for (int32 Variant = 0; Variant < Form.Variants; ++Variant)
				{
					FVegetationMeshSpec& Spec = Specs.AddDefaulted_GetRef();
					Spec.Kind = Form.Kind;
					Spec.Genus = Tree.Genus;
					Spec.Variant = Variant;
				}
			}
		}
		for (const TArray<FUnderstoryRule>* Rules : { &Definition.Understory, &Definition.Ground })
		{
			for (const FUnderstoryRule& Rule : *Rules)
			{
				if (Rule.Kind == EVegetationKind::Sapling)
				{
					continue; // tree form, listed above
				}
				// Leaf litter: one set per genus (the leaves of the trees above).
				TArray<FName> Genera;
				if (Rule.Kind == EVegetationKind::Leaf)
				{
					for (const FTreeArchetype& Tree : Definition.Trees)
					{
						Genera.Add(Tree.Genus);
					}
				}
				else
				{
					Genera.Add(NAME_None);
				}
				for (const FName Genus : Genera)
				{
					for (int32 Variant = 0; Variant < Rule.Variants; ++Variant)
					{
						FVegetationMeshSpec& Spec = Specs.AddDefaulted_GetRef();
						Spec.Kind = Rule.Kind;
						Spec.Genus = Genus;
						Spec.Variant = Variant;
					}
				}
			}
		}
		return Specs;
	}
}
