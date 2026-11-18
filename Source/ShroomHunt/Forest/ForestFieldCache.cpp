#include "Forest/ForestFieldCache.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "Forest/ForestDefinition.h"
#include "Forest/ForestField.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Vegetation/VegetationDefinition.h"

namespace ForestFieldCacheDetail
{
	struct FFieldEntry
	{
		uint32 TextHash = 0;
		TSharedPtr<const FForestField> Field;
	};

	FCriticalSection& GetLock()
	{
		static FCriticalSection Lock;
		return Lock;
	}

	TMap<FString, FFieldEntry>& GetFields()
	{
		static TMap<FString, FFieldEntry> Fields;
		return Fields;
	}

	uint32& GetVegetationHash()
	{
		static uint32 Hash = 0;
		return Hash;
	}

	TSharedPtr<const FVegetationDefinition>& GetVegetationEntry()
	{
		static TSharedPtr<const FVegetationDefinition> Vegetation;
		return Vegetation;
	}
}

namespace ForestFieldCache
{
	TSharedPtr<const FForestField> GetField(const FString& ForestName, TArray<FString>& OutErrors)
	{
		using namespace ForestFieldCacheDetail;
		const FString Path = FPaths::Combine(ForestData::GetDefaultFolder(), ForestName + TEXT(".json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cannot read file"), *Path));
			return nullptr;
		}
		const uint32 TextHash = ShroomHash::HashString(Text);

		FScopeLock Lock(&GetLock());
		if (const FFieldEntry* Found = GetFields().Find(ForestName))
		{
			if (Found->TextHash == TextHash)
			{
				return Found->Field;
			}
		}

		FForestDefinition Definition;
		if (!ForestData::ParseForest(Text, FPaths::GetCleanFilename(Path), Definition, OutErrors))
		{
			return nullptr;
		}
		const double Start = FPlatformTime::Seconds();
		TSharedPtr<FForestField> Field = MakeShared<FForestField>();
		if (!Field->Generate(Definition))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: generation failed"), *ForestName));
			return nullptr;
		}
		UE_LOG(LogShroomForest, Log, TEXT("Generated forest field %s in %.2f s (cached)"), *ForestName, FPlatformTime::Seconds() - Start);
		FFieldEntry& Entry = GetFields().FindOrAdd(ForestName);
		Entry.TextHash = TextHash;
		Entry.Field = Field;
		return Field;
	}

	TSharedPtr<const FVegetationDefinition> GetVegetation(TArray<FString>& OutErrors)
	{
		using namespace ForestFieldCacheDetail;
		const FString Path = VegetationData::GetDefaultPath();
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cannot read file"), *Path));
			return nullptr;
		}
		const uint32 TextHash = ShroomHash::HashString(Text);

		FScopeLock Lock(&GetLock());
		TSharedPtr<const FVegetationDefinition>& Entry = GetVegetationEntry();
		if (Entry.IsValid() && GetVegetationHash() == TextHash)
		{
			return Entry;
		}
		TSharedPtr<FVegetationDefinition> Definition = MakeShared<FVegetationDefinition>();
		if (!VegetationData::ParseVegetation(Text, FPaths::GetCleanFilename(Path), *Definition, OutErrors))
		{
			return nullptr;
		}
		Entry = Definition;
		GetVegetationHash() = TextHash;
		return Entry;
	}

	void Clear()
	{
		using namespace ForestFieldCacheDetail;
		FScopeLock Lock(&GetLock());
		GetFields().Reset();
		GetVegetationEntry().Reset();
		GetVegetationHash() = 0;
	}
}
