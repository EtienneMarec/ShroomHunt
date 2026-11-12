#include "Mycology/SpeciesJsonParser.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using FJsonObjectPtr = TSharedPtr<FJsonObject>;
	using FJsonValuePtr = TSharedPtr<FJsonValue>;

	/** Parsing context: current source file, species id and the report to fill. */
	struct FContext
	{
		FString Source;
		FString SpeciesId;
		SpeciesJson::FParseReport& Report;

		void Error(const FString& Path, const FString& Message) const
		{
			Report.Errors.Add(FString::Printf(TEXT("%s: %s: %s"), *Source, *Path, *Message));
		}

		void Warning(const FString& Path, const FString& Message) const
		{
			Report.Warnings.Add(FString::Printf(TEXT("%s: %s: %s"), *Source, *Path, *Message));
		}

		/** Localizable text with a stable key, e.g. "BoletusEdulis.Description". */
		FText MakeText(const FString& Field, const FString& Value) const
		{
			return FText::AsLocalizable_Advanced(TEXT("ShroomHunt.Species"), SpeciesId + TEXT(".") + Field, FString(Value));
		}
	};

	FString Join(const FString& Path, const FString& Key)
	{
		return Path.IsEmpty() ? Key : Path + TEXT(".") + Key;
	}

	/** Returns the field or nullptr when it is missing or JSON null. */
	FJsonValuePtr GetField(const FJsonObject& Object, const FString& Key)
	{
		FJsonValuePtr Found = Object.TryGetField(Key);
		if (!Found.IsValid() || Found->IsNull())
		{
			return nullptr;
		}
		return Found;
	}

	void CheckKnownKeys(const FJsonObject& Object, std::initializer_list<const TCHAR*> Known, const FString& Path, const FContext& Ctx)
	{
		for (const TPair<FString, FJsonValuePtr>& Pair : Object.Values)
		{
			if (Pair.Key.StartsWith(TEXT("_")))
			{
				continue;
			}
			bool bKnown = false;
			for (const TCHAR* Name : Known)
			{
				if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase))
				{
					bKnown = true;
					break;
				}
			}
			if (!bKnown)
			{
				Ctx.Warning(Join(Path, Pair.Key), TEXT("unknown key (typo?) - ignored"));
			}
		}
	}

	bool ReadNumberValue(const FJsonValuePtr& Value, const FString& Path, const FContext& Ctx, float& Out)
	{
		double Number = 0.0;
		if (!Value.IsValid() || !Value->TryGetNumber(Number))
		{
			Ctx.Error(Path, TEXT("expected a number"));
			return false;
		}
		Out = static_cast<float>(Number);
		return true;
	}

	bool ReadNumber(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, float& Out, bool bRequired = true)
	{
		const FJsonValuePtr Value = GetField(Object, Key);
		if (!Value)
		{
			if (bRequired)
			{
				Ctx.Error(Join(Path, Key), TEXT("missing"));
			}
			return false;
		}
		return ReadNumberValue(Value, Join(Path, Key), Ctx, Out);
	}

	bool ReadString(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, FString& Out, bool bRequired = true)
	{
		const FJsonValuePtr Value = GetField(Object, Key);
		if (!Value)
		{
			if (bRequired)
			{
				Ctx.Error(Join(Path, Key), TEXT("missing"));
			}
			return false;
		}
		if (!Value->TryGetString(Out))
		{
			Ctx.Error(Join(Path, Key), TEXT("expected a string"));
			return false;
		}
		return true;
	}

	bool ReadBool(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, bool& Out)
	{
		const FJsonValuePtr Value = GetField(Object, Key);
		if (!Value)
		{
			return false;
		}
		if (!Value->TryGetBool(Out))
		{
			Ctx.Error(Join(Path, Key), TEXT("expected true or false"));
			return false;
		}
		return true;
	}

	const TArray<FJsonValuePtr>* ReadArray(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, bool bRequired = true)
	{
		const FJsonValuePtr Value = GetField(Object, Key);
		const TArray<FJsonValuePtr>* Array = nullptr;
		if (!Value)
		{
			if (bRequired)
			{
				Ctx.Error(Join(Path, Key), TEXT("missing"));
			}
			return nullptr;
		}
		if (!Value->TryGetArray(Array))
		{
			Ctx.Error(Join(Path, Key), TEXT("expected an array"));
			return nullptr;
		}
		return Array;
	}

	const FJsonObject* ReadObject(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, bool bRequired = true)
	{
		const FJsonValuePtr Value = GetField(Object, Key);
		const FJsonObjectPtr* Child = nullptr;
		if (!Value)
		{
			if (bRequired)
			{
				Ctx.Error(Join(Path, Key), TEXT("missing"));
			}
			return nullptr;
		}
		if (!Value->TryGetObject(Child) || !Child || !Child->IsValid())
		{
			Ctx.Error(Join(Path, Key), TEXT("expected an object"));
			return nullptr;
		}
		return Child->Get();
	}

	template <typename TEnum>
	bool ParseEnumName(const FString& Text, TEnum& Out)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index) // last entry is the generated _MAX
		{
			if (Enum->GetNameStringByIndex(Index).Equals(Text, ESearchCase::IgnoreCase))
			{
				Out = static_cast<TEnum>(Enum->GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	template <typename TEnum>
	FString EnumChoices()
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		TArray<FString> Names;
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			Names.Add(Enum->GetNameStringByIndex(Index));
		}
		return FString::Join(Names, TEXT(", "));
	}

	template <typename TEnum>
	bool ReadEnumValue(const FJsonValuePtr& Value, const FString& Path, const FContext& Ctx, TEnum& Out)
	{
		FString Text;
		if (!Value.IsValid() || !Value->TryGetString(Text))
		{
			Ctx.Error(Path, TEXT("expected a name string"));
			return false;
		}
		if (!ParseEnumName(Text, Out))
		{
			Ctx.Error(Path, FString::Printf(TEXT("unknown value '%s' (expected one of: %s)"), *Text, *EnumChoices<TEnum>()));
			return false;
		}
		return true;
	}

	template <typename TEnum>
	bool ReadEnum(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, TEnum& Out, bool bRequired = true)
	{
		const FJsonValuePtr Value = GetField(Object, Key);
		if (!Value)
		{
			if (bRequired)
			{
				Ctx.Error(Join(Path, Key), TEXT("missing"));
			}
			return false;
		}
		return ReadEnumValue(Value, Join(Path, Key), Ctx, Out);
	}

	template <typename TEnum>
	void ReadEnumArray(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, TArray<TEnum>& Out)
	{
		if (const TArray<FJsonValuePtr>* Array = ReadArray(Object, Key, Path, Ctx))
		{
			for (int32 Index = 0; Index < Array->Num(); ++Index)
			{
				TEnum Value{};
				if (ReadEnumValue((*Array)[Index], FString::Printf(TEXT("%s[%d]"), *Join(Path, Key), Index), Ctx, Value))
				{
					Out.AddUnique(Value);
				}
			}
		}
	}

	bool ParseColorText(const FString& Text, FColor& Out)
	{
		FString Hex = Text.TrimStartAndEnd();
		Hex.RemoveFromStart(TEXT("#"));
		if (Hex.Len() != 6 && Hex.Len() != 8)
		{
			return false;
		}
		for (const TCHAR C : Hex)
		{
			if (!FChar::IsHexDigit(C))
			{
				return false;
			}
		}
		Out = FColor::FromHex(Hex);
		return true;
	}

	bool ReadColorValue(const FJsonValuePtr& Value, const FString& Path, const FContext& Ctx, FColor& Out)
	{
		FString Text;
		if (!Value.IsValid() || !Value->TryGetString(Text) || !ParseColorText(Text, Out))
		{
			Ctx.Error(Path, TEXT("expected a color \"#RRGGBB\""));
			return false;
		}
		return true;
	}

	bool ReadColor(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, FColor& Out, bool bRequired = true)
	{
		const FJsonValuePtr Value = GetField(Object, Key);
		if (!Value)
		{
			if (bRequired)
			{
				Ctx.Error(Join(Path, Key), TEXT("missing"));
			}
			return false;
		}
		return ReadColorValue(Value, Join(Path, Key), Ctx, Out);
	}

	bool ReadNumberArray(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, int32 Count, TArray<float>& Out)
	{
		const TArray<FJsonValuePtr>* Array = ReadArray(Object, Key, Path, Ctx);
		if (!Array)
		{
			return false;
		}
		if (Array->Num() != Count)
		{
			Ctx.Error(Join(Path, Key), FString::Printf(TEXT("expected %d numbers, got %d"), Count, Array->Num()));
			return false;
		}
		Out.SetNum(Count);
		bool bOk = true;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			bOk &= ReadNumberValue((*Array)[Index], FString::Printf(TEXT("%s[%d]"), *Join(Path, Key), Index), Ctx, Out[Index]);
		}
		return bOk;
	}

	void ReadTrapezoid(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, FTrapezoid& Out)
	{
		TArray<float> V;
		if (ReadNumberArray(Object, Key, Path, Ctx, 4, V))
		{
			Out = FTrapezoid(V[0], V[1], V[2], V[3]);
			if (!Out.IsOrdered())
			{
				Ctx.Error(Join(Path, Key), TEXT("trapezoid must satisfy min <= optLow <= optHigh <= max"));
			}
		}
	}

	void ReadRange(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, FShroomRange& Out)
	{
		TArray<float> V;
		if (ReadNumberArray(Object, Key, Path, Ctx, 2, V))
		{
			Out = FShroomRange(V[0], V[1]);
			if (Out.Min > Out.Max)
			{
				Ctx.Error(Join(Path, Key), TEXT("range must satisfy min <= max"));
			}
		}
	}

	void ReadColorRange(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, FColorRange& Out)
	{
		const TArray<FJsonValuePtr>* Array = ReadArray(Object, Key, Path, Ctx);
		if (!Array)
		{
			return;
		}
		if (Array->Num() != 2)
		{
			Ctx.Error(Join(Path, Key), TEXT("expected two colors"));
			return;
		}
		ReadColorValue((*Array)[0], Join(Path, Key) + TEXT("[0]"), Ctx, Out.A);
		ReadColorValue((*Array)[1], Join(Path, Key) + TEXT("[1]"), Ctx, Out.B);
	}

	void CheckUnit(float Value, const FString& Path, const FContext& Ctx)
	{
		if (Value < 0.0f || Value > 1.0f)
		{
			Ctx.Error(Path, FString::Printf(TEXT("%.3f is outside [0, 1]"), Value));
		}
	}

	void CheckPositive(float Value, const FString& Path, const FContext& Ctx)
	{
		if (Value <= 0.0f)
		{
			Ctx.Error(Path, FString::Printf(TEXT("%.3f must be > 0"), Value));
		}
	}

	/** Iterates an array of objects, calling Fn(Object, ElementPath). */
	template <typename TFunc>
	void ForEachObject(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, bool bRequired, TFunc&& Fn)
	{
		const TArray<FJsonValuePtr>* Array = ReadArray(Object, Key, Path, Ctx, bRequired);
		if (!Array)
		{
			return;
		}
		for (int32 Index = 0; Index < Array->Num(); ++Index)
		{
			const FString ElementPath = FString::Printf(TEXT("%s[%d]"), *Join(Path, Key), Index);
			const FJsonObjectPtr* Element = nullptr;
			if (!(*Array)[Index].IsValid() || !(*Array)[Index]->TryGetObject(Element) || !Element || !Element->IsValid())
			{
				Ctx.Error(ElementPath, TEXT("expected an object"));
				continue;
			}
			Fn(*Element->Get(), ElementPath);
		}
	}

	void ReadShapes(const FJsonObject& Object, const FString& Key, const FString& Path, const FContext& Ctx, TArray<FCapShapeWeight>& Out)
	{
		ForEachObject(Object, Key, Path, Ctx, true, [&](const FJsonObject& Element, const FString& ElementPath)
		{
			CheckKnownKeys(Element, { TEXT("shape"), TEXT("weight") }, ElementPath, Ctx);
			FCapShapeWeight Shape;
			ReadEnum(Element, TEXT("shape"), ElementPath, Ctx, Shape.Shape);
			ReadNumber(Element, TEXT("weight"), ElementPath, Ctx, Shape.Weight);
			if (Shape.Weight <= 0.0f)
			{
				Ctx.Error(Join(ElementPath, TEXT("weight")), TEXT("must be > 0"));
			}
			Out.Add(Shape);
		});
		if (Out.Num() == 0)
		{
			Ctx.Error(Join(Path, Key), TEXT("needs at least one shape"));
		}
	}

	void ReadCap(const FJsonObject& Object, const FString& Path, const FContext& Ctx, FCapMorphology& Out)
	{
		CheckKnownKeys(Object, { TEXT("ShapesByStage"), TEXT("DiameterCm"), TEXT("MarginType"), TEXT("Surface"), TEXT("Colors"), TEXT("Warts") }, Path, Ctx);

		if (const FJsonObject* Shapes = ReadObject(Object, TEXT("ShapesByStage"), Path, Ctx))
		{
			const FString ShapesPath = Join(Path, TEXT("ShapesByStage"));
			CheckKnownKeys(*Shapes, { TEXT("young"), TEXT("mature"), TEXT("old") }, ShapesPath, Ctx);
			ReadShapes(*Shapes, TEXT("young"), ShapesPath, Ctx, Out.YoungShapes);
			ReadShapes(*Shapes, TEXT("mature"), ShapesPath, Ctx, Out.MatureShapes);
			ReadShapes(*Shapes, TEXT("old"), ShapesPath, Ctx, Out.OldShapes);
		}
		ReadRange(Object, TEXT("DiameterCm"), Path, Ctx, Out.DiameterCm);
		CheckPositive(Out.DiameterCm.Min, Join(Path, TEXT("DiameterCm[0]")), Ctx);
		ReadEnum(Object, TEXT("MarginType"), Path, Ctx, Out.Margin);
		ReadEnum(Object, TEXT("Surface"), Path, Ctx, Out.Surface);
		ReadColorRange(Object, TEXT("Colors"), Path, Ctx, Out.Colors);

		if (const FJsonObject* Warts = ReadObject(Object, TEXT("Warts"), Path, Ctx, false))
		{
			const FString WartsPath = Join(Path, TEXT("Warts"));
			CheckKnownKeys(*Warts, { TEXT("color"), TEXT("density"), TEXT("washOffByRain") }, WartsPath, Ctx);
			Out.Warts.bPresent = true;
			ReadColor(*Warts, TEXT("color"), WartsPath, Ctx, Out.Warts.Color);
			ReadNumber(*Warts, TEXT("density"), WartsPath, Ctx, Out.Warts.Density);
			CheckUnit(Out.Warts.Density, Join(WartsPath, TEXT("density")), Ctx);
			ReadBool(*Warts, TEXT("washOffByRain"), WartsPath, Ctx, Out.Warts.bWashOffByRain);
		}
	}

	void ReadUnderside(const FJsonObject& Object, const FString& Path, const FContext& Ctx, FUndersideMorphology& Out)
	{
		CheckKnownKeys(Object, { TEXT("Type"), TEXT("Attachment"), TEXT("ColorsByStage") }, Path, Ctx);
		ReadEnum(Object, TEXT("Type"), Path, Ctx, Out.Type);
		ReadEnum(Object, TEXT("Attachment"), Path, Ctx, Out.Attachment);
		if (const FJsonObject* Colors = ReadObject(Object, TEXT("ColorsByStage"), Path, Ctx))
		{
			const FString ColorsPath = Join(Path, TEXT("ColorsByStage"));
			CheckKnownKeys(*Colors, { TEXT("young"), TEXT("mature"), TEXT("old") }, ColorsPath, Ctx);
			ReadColor(*Colors, TEXT("young"), ColorsPath, Ctx, Out.YoungColor);
			ReadColor(*Colors, TEXT("mature"), ColorsPath, Ctx, Out.MatureColor);
			ReadColor(*Colors, TEXT("old"), ColorsPath, Ctx, Out.OldColor);
		}
	}

	void ReadStem(const FJsonObject& Object, const FString& Path, const FContext& Ctx, FStemMorphology& Out)
	{
		CheckKnownKeys(Object, { TEXT("LengthCm"), TEXT("ThicknessCm"), TEXT("Shape"), TEXT("Colors"), TEXT("Reticulation"), TEXT("Ring"), TEXT("Volva") }, Path, Ctx);
		ReadRange(Object, TEXT("LengthCm"), Path, Ctx, Out.LengthCm);
		ReadRange(Object, TEXT("ThicknessCm"), Path, Ctx, Out.ThicknessCm);
		CheckPositive(Out.LengthCm.Min, Join(Path, TEXT("LengthCm[0]")), Ctx);
		CheckPositive(Out.ThicknessCm.Min, Join(Path, TEXT("ThicknessCm[0]")), Ctx);
		ReadEnum(Object, TEXT("Shape"), Path, Ctx, Out.Shape);
		ReadColorRange(Object, TEXT("Colors"), Path, Ctx, Out.Colors);
		ReadEnum(Object, TEXT("Ring"), Path, Ctx, Out.Ring);
		ReadEnum(Object, TEXT("Volva"), Path, Ctx, Out.Volva);

		if (const FJsonObject* Reticulation = ReadObject(Object, TEXT("Reticulation"), Path, Ctx, false))
		{
			const FString RetPath = Join(Path, TEXT("Reticulation"));
			CheckKnownKeys(*Reticulation, { TEXT("present"), TEXT("color"), TEXT("extent") }, RetPath, Ctx);
			ReadBool(*Reticulation, TEXT("present"), RetPath, Ctx, Out.Reticulation.bPresent);
			if (Out.Reticulation.bPresent)
			{
				ReadColor(*Reticulation, TEXT("color"), RetPath, Ctx, Out.Reticulation.Color);
				ReadNumber(*Reticulation, TEXT("extent"), RetPath, Ctx, Out.Reticulation.Extent);
				CheckUnit(Out.Reticulation.Extent, Join(RetPath, TEXT("extent")), Ctx);
			}
		}
	}

	void ReadFlesh(const FJsonObject& Object, const FString& Path, const FContext& Ctx, FFleshTraits& Out)
	{
		CheckKnownKeys(Object, { TEXT("Color"), TEXT("ColorChange"), TEXT("Odor"), TEXT("Taste") }, Path, Ctx);
		ReadColor(Object, TEXT("Color"), Path, Ctx, Out.Color);
		ReadEnum(Object, TEXT("ColorChange"), Path, Ctx, Out.ColorChange);
		FString Odor;
		if (ReadString(Object, TEXT("Odor"), Path, Ctx, Odor))
		{
			Out.Odor = Ctx.MakeText(TEXT("Odor"), Odor);
		}
		ReadEnum(Object, TEXT("Taste"), Path, Ctx, Out.Taste);
	}

	void ParseRoot(const FJsonObject& Root, const FContext& Ctx, FMushroomSpeciesRow& Row)
	{
		CheckKnownKeys(Root, {
			TEXT("Name"), TEXT("ScientificName"), TEXT("CommonNames"), TEXT("Genus"), TEXT("Family"), TEXT("GuideGroup"),
			TEXT("Edibility"), TEXT("ToxicSyndrome"), TEXT("PricePerKg"), TEXT("Trophic"), TEXT("Substrates"), TEXT("Hosts"),
			TEXT("pH"), TEXT("AltitudeM"), TEXT("Moisture"), TEXT("Canopy"), TEXT("EdgeAffinity"), TEXT("Soils"),
			TEXT("FruitingMonths"), TEXT("RainWindowDays"), TEXT("RainMm"), TEXT("TempC"), TEXT("LagDays"), TEXT("FlushRate"),
			TEXT("FlushCooldownDays"), TEXT("PatchDensityPerHa"), TEXT("PatchRadiusM"), TEXT("Clustering"), TEXT("FruitsPerFlush"),
			TEXT("LifetimeDays"), TEXT("StageSplit"), TEXT("WormyChance"), TEXT("Cap"), TEXT("Underside"), TEXT("Stem"),
			TEXT("Flesh"), TEXT("SporePrint"), TEXT("Lookalikes"), TEXT("Description"), TEXT("HabitatText"), TEXT("Illustration") },
			FString(), Ctx);

		const FString Top; // path of top-level fields

		// Identity
		ReadString(Root, TEXT("ScientificName"), Top, Ctx, Row.ScientificName);
		if (const FJsonObject* Names = ReadObject(Root, TEXT("CommonNames"), Top, Ctx))
		{
			CheckKnownKeys(*Names, { TEXT("en"), TEXT("fr") }, TEXT("CommonNames"), Ctx);
			FString English;
			if (ReadString(*Names, TEXT("en"), TEXT("CommonNames"), Ctx, English))
			{
				Row.CommonName = Ctx.MakeText(TEXT("CommonName"), English);
			}
			ReadString(*Names, TEXT("fr"), TEXT("CommonNames"), Ctx, Row.FrenchName);
		}
		FString Text;
		if (ReadString(Root, TEXT("Genus"), Top, Ctx, Text))
		{
			Row.Genus = FName(*Text);
		}
		if (ReadString(Root, TEXT("Family"), Top, Ctx, Text))
		{
			Row.Family = FName(*Text);
		}
		ReadEnum(Root, TEXT("GuideGroup"), Top, Ctx, Row.GuideGroup);
		ReadEnum(Root, TEXT("Edibility"), Top, Ctx, Row.Edibility);
		ReadEnum(Root, TEXT("ToxicSyndrome"), Top, Ctx, Row.ToxicSyndrome);
		ReadNumber(Root, TEXT("PricePerKg"), Top, Ctx, Row.PricePerKg);
		if (Row.PricePerKg < 0.0f)
		{
			Ctx.Error(TEXT("PricePerKg"), TEXT("must be >= 0"));
		}

		// Ecology
		ReadEnum(Root, TEXT("Trophic"), Top, Ctx, Row.Trophic);
		ReadEnumArray(Root, TEXT("Substrates"), Top, Ctx, Row.Substrates);
		ForEachObject(Root, TEXT("Hosts"), Top, Ctx, false, [&](const FJsonObject& Element, const FString& ElementPath)
		{
			CheckKnownKeys(Element, { TEXT("genus"), TEXT("affinity") }, ElementPath, Ctx);
			FHostAffinity Host;
			FString Genus;
			if (ReadString(Element, TEXT("genus"), ElementPath, Ctx, Genus))
			{
				Host.Genus = FName(*Genus);
			}
			ReadNumber(Element, TEXT("affinity"), ElementPath, Ctx, Host.Affinity);
			CheckUnit(Host.Affinity, Join(ElementPath, TEXT("affinity")), Ctx);
			Row.Hosts.Add(Host);
		});
		if ((Row.Trophic == ETrophicMode::Mycorrhizal || Row.Trophic == ETrophicMode::Parasitic) && Row.Hosts.Num() == 0)
		{
			Ctx.Error(TEXT("Hosts"), TEXT("mycorrhizal and parasitic species need at least one host tree"));
		}
		ReadTrapezoid(Root, TEXT("pH"), Top, Ctx, Row.PH);
		ReadTrapezoid(Root, TEXT("AltitudeM"), Top, Ctx, Row.AltitudeM);
		ReadTrapezoid(Root, TEXT("Moisture"), Top, Ctx, Row.Moisture);
		ReadTrapezoid(Root, TEXT("Canopy"), Top, Ctx, Row.Canopy);
		ReadNumber(Root, TEXT("EdgeAffinity"), Top, Ctx, Row.EdgeAffinity);
		if (Row.EdgeAffinity < -1.0f || Row.EdgeAffinity > 1.0f)
		{
			Ctx.Error(TEXT("EdgeAffinity"), TEXT("must be in [-1, 1]"));
		}
		ForEachObject(Root, TEXT("Soils"), Top, Ctx, true, [&](const FJsonObject& Element, const FString& ElementPath)
		{
			CheckKnownKeys(Element, { TEXT("type"), TEXT("weight") }, ElementPath, Ctx);
			FSoilPreference Soil;
			ReadEnum(Element, TEXT("type"), ElementPath, Ctx, Soil.Soil);
			ReadNumber(Element, TEXT("weight"), ElementPath, Ctx, Soil.Weight);
			CheckUnit(Soil.Weight, Join(ElementPath, TEXT("weight")), Ctx);
			Row.Soils.Add(Soil);
		});

		// Phenology
		ReadTrapezoid(Root, TEXT("FruitingMonths"), Top, Ctx, Row.FruitingMonths);
		if (Row.FruitingMonths.Min < 1.0f || Row.FruitingMonths.Max > 25.0f)
		{
			Ctx.Error(TEXT("FruitingMonths"), TEXT("months must be in [1, 25) (values above 13 continue into the next year)"));
		}
		ReadNumber(Root, TEXT("RainWindowDays"), Top, Ctx, Row.RainWindowDays);
		CheckPositive(Row.RainWindowDays, TEXT("RainWindowDays"), Ctx);
		ReadRange(Root, TEXT("RainMm"), Top, Ctx, Row.RainMm);
		ReadTrapezoid(Root, TEXT("TempC"), Top, Ctx, Row.TempC);
		ReadRange(Root, TEXT("LagDays"), Top, Ctx, Row.LagDays);
		ReadNumber(Root, TEXT("FlushRate"), Top, Ctx, Row.FlushRate);
		CheckPositive(Row.FlushRate, TEXT("FlushRate"), Ctx);
		ReadNumber(Root, TEXT("FlushCooldownDays"), Top, Ctx, Row.FlushCooldownDays);

		// Spawning
		ReadNumber(Root, TEXT("PatchDensityPerHa"), Top, Ctx, Row.PatchDensityPerHa);
		CheckPositive(Row.PatchDensityPerHa, TEXT("PatchDensityPerHa"), Ctx);
		ReadRange(Root, TEXT("PatchRadiusM"), Top, Ctx, Row.PatchRadiusM);
		ReadEnum(Root, TEXT("Clustering"), Top, Ctx, Row.Clustering);
		ReadNumber(Root, TEXT("FruitsPerFlush"), Top, Ctx, Row.FruitsPerFlush);
		CheckPositive(Row.FruitsPerFlush, TEXT("FruitsPerFlush"), Ctx);
		ReadNumber(Root, TEXT("LifetimeDays"), Top, Ctx, Row.LifetimeDays);
		CheckPositive(Row.LifetimeDays, TEXT("LifetimeDays"), Ctx);
		TArray<float> Split;
		if (ReadNumberArray(Root, TEXT("StageSplit"), Top, Ctx, 2, Split))
		{
			Row.YoungUntil = Split[0];
			Row.MatureUntil = Split[1];
			if (!(0.0f < Row.YoungUntil && Row.YoungUntil < Row.MatureUntil && Row.MatureUntil <= 1.0f))
			{
				Ctx.Error(TEXT("StageSplit"), TEXT("must satisfy 0 < young < mature <= 1"));
			}
		}
		ReadNumber(Root, TEXT("WormyChance"), Top, Ctx, Row.WormyChance);
		CheckUnit(Row.WormyChance, TEXT("WormyChance"), Ctx);

		// Morphology
		if (const FJsonObject* Cap = ReadObject(Root, TEXT("Cap"), Top, Ctx))
		{
			ReadCap(*Cap, TEXT("Cap"), Ctx, Row.Cap);
		}
		if (const FJsonObject* Underside = ReadObject(Root, TEXT("Underside"), Top, Ctx))
		{
			ReadUnderside(*Underside, TEXT("Underside"), Ctx, Row.Underside);
		}
		if (const FJsonObject* Stem = ReadObject(Root, TEXT("Stem"), Top, Ctx))
		{
			ReadStem(*Stem, TEXT("Stem"), Ctx, Row.Stem);
		}
		if (const FJsonObject* Flesh = ReadObject(Root, TEXT("Flesh"), Top, Ctx))
		{
			ReadFlesh(*Flesh, TEXT("Flesh"), Ctx, Row.Flesh);
		}
		ReadColor(Root, TEXT("SporePrint"), Top, Ctx, Row.SporePrint);

		// Knowledge
		ForEachObject(Root, TEXT("Lookalikes"), Top, Ctx, false, [&](const FJsonObject& Element, const FString& ElementPath)
		{
			CheckKnownKeys(Element, { TEXT("species"), TEXT("danger"), TEXT("keyDifferences") }, ElementPath, Ctx);
			FLookalike Lookalike;
			FString Species;
			if (ReadString(Element, TEXT("species"), ElementPath, Ctx, Species))
			{
				Lookalike.Species = FName(*Species);
			}
			ReadEnum(Element, TEXT("danger"), ElementPath, Ctx, Lookalike.Danger);
			if (const TArray<FJsonValuePtr>* Differences = ReadArray(Element, TEXT("keyDifferences"), ElementPath, Ctx))
			{
				for (int32 Index = 0; Index < Differences->Num(); ++Index)
				{
					FString Difference;
					if ((*Differences)[Index]->TryGetString(Difference))
					{
						const FString Key = FString::Printf(TEXT("Lookalike.%s.%d"), *Species, Index);
						Lookalike.KeyDifferences.Add(Ctx.MakeText(Key, Difference));
					}
					else
					{
						Ctx.Error(FString::Printf(TEXT("%s.keyDifferences[%d]"), *ElementPath, Index), TEXT("expected a string"));
					}
				}
			}
			Row.Lookalikes.Add(Lookalike);
		});
		if (ReadString(Root, TEXT("Description"), Top, Ctx, Text))
		{
			Row.Description = Ctx.MakeText(TEXT("Description"), Text);
		}
		if (ReadString(Root, TEXT("HabitatText"), Top, Ctx, Text))
		{
			Row.HabitatText = Ctx.MakeText(TEXT("HabitatText"), Text);
		}
		if (ReadString(Root, TEXT("Illustration"), Top, Ctx, Text, false))
		{
			Row.Illustration = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Text));
		}
	}
}

namespace SpeciesJson
{
	FString FParseReport::ToString() const
	{
		TArray<FString> Lines;
		for (const FString& Error : Errors)
		{
			Lines.Add(TEXT("ERROR   ") + Error);
		}
		for (const FString& Warning : Warnings)
		{
			Lines.Add(TEXT("WARNING ") + Warning);
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	bool ParseSpecies(const FString& JsonText, const FString& SourceName, FName& OutId, FMushroomSpeciesRow& OutRow, FParseReport& Report)
	{
		const int32 ErrorsBefore = Report.Errors.Num();
		FContext Ctx{ SourceName, FString(), Report };

		FJsonObjectPtr Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			Ctx.Error(TEXT("(file)"), FString::Printf(TEXT("invalid JSON: %s"), *Reader->GetErrorMessage()));
			return false;
		}

		FString Id;
		if (!ReadString(*Root, TEXT("Name"), FString(), Ctx, Id) || Id.IsEmpty())
		{
			return false;
		}
		const FString ExpectedId = FPaths::GetBaseFilename(SourceName);
		if (!ExpectedId.IsEmpty() && SourceName.EndsWith(TEXT(".json")) && !Id.Equals(ExpectedId, ESearchCase::CaseSensitive))
		{
			Ctx.Error(TEXT("Name"), FString::Printf(TEXT("'%s' must match the file name '%s'"), *Id, *ExpectedId));
		}

		Ctx.SpeciesId = Id;
		OutId = FName(*Id);
		OutRow = FMushroomSpeciesRow();
		ParseRoot(*Root, Ctx, OutRow);
		return Report.Errors.Num() == ErrorsBefore;
	}

	bool ParseSpeciesFile(const FString& FilePath, FName& OutId, FMushroomSpeciesRow& OutRow, FParseReport& Report)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FilePath))
		{
			Report.Errors.Add(FString::Printf(TEXT("%s: cannot read file"), *FilePath));
			return false;
		}
		return ParseSpecies(Text, FPaths::GetCleanFilename(FilePath), OutId, OutRow, Report);
	}

	int32 LoadFolder(const FString& Folder, TMap<FName, FMushroomSpeciesRow>& OutSpecies, FParseReport& Report)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(Folder, TEXT("*.json")), true, false);
		Files.Sort();
		if (Files.Num() == 0)
		{
			Report.Errors.Add(FString::Printf(TEXT("%s: no species files found"), *Folder));
			return 0;
		}

		int32 Loaded = 0;
		for (const FString& File : Files)
		{
			FName Id;
			FMushroomSpeciesRow Row;
			if (!ParseSpeciesFile(FPaths::Combine(Folder, File), Id, Row, Report))
			{
				continue;
			}
			if (OutSpecies.Contains(Id))
			{
				Report.Errors.Add(FString::Printf(TEXT("%s: duplicate species id '%s'"), *File, *Id.ToString()));
				continue;
			}
			OutSpecies.Add(Id, MoveTemp(Row));
			++Loaded;
		}
		return Loaded;
	}

	FString GetDefaultFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Species")));
	}
}
