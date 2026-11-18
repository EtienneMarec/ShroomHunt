#include "Forest/ForestPlaceNames.h"

#include "Core/ShroomHash.h"
#include "Forest/ForestField.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

const TArray<FString>* FPlaceNameData::Find(const FString& Context) const
{
	const FPlaceNameList* List = Lists.FindByPredicate([&Context](const FPlaceNameList& L) { return L.Context == Context; });
	return List ? &List->Names : nullptr;
}

namespace ForestPlaceNames
{
	bool Parse(const FString& JsonText, FPlaceNameData& Out, TArray<FString>& OutErrors)
	{
		const int32 ErrorsBefore = OutErrors.Num();
		Out = FPlaceNameData();
		FText FailReason;
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &Out, 0, 0, false, &FailReason))
		{
			OutErrors.Add(FString::Printf(TEXT("PlaceNames.json: invalid JSON: %s"), *FailReason.ToString()));
			return false;
		}
		const FPlaceNamePrefixes& P = Out.Prefixes;
		if (P.Crossroads.IsEmpty() || P.Star.IsEmpty() || P.Hub.IsEmpty() || P.Junction.IsEmpty() || P.Entrance.IsEmpty() || P.Base.IsEmpty())
		{
			OutErrors.Add(TEXT("PlaceNames.json: Prefixes: Crossroads, Star, Hub, Junction, Entrance and Base are required"));
		}
		const TArray<FString>* Generic = Out.Find(TEXT("Generic"));
		if (!Generic || Generic->Num() < 10)
		{
			OutErrors.Add(TEXT("PlaceNames.json: Lists: a Generic list with at least 10 names is required"));
		}
		return OutErrors.Num() == ErrorsBefore;
	}

	bool Load(FPlaceNameData& Out, TArray<FString>& OutErrors)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Names"), TEXT("PlaceNames.json")));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: cannot read file"), *Path));
			return false;
		}
		return Parse(Text, Out, OutErrors);
	}

	TArray<FString> NameNodes(const FForestField& Field, const FPlaceNameData& Data)
	{
		using namespace ShroomHash;
		const FForestPathNetwork& Network = Field.GetPathNetwork();
		TArray<FString> Names;
		Names.SetNum(Network.Nodes.Num());

		TArray<int32> Degree;
		Degree.Init(0, Network.Nodes.Num());
		TArray<bool> OnRoad;
		OnRoad.Init(false, Network.Nodes.Num());
		for (const FForestPath& Path : Network.Paths)
		{
			for (const int32 Node : { Path.StartNode, Path.EndNode })
			{
				if (Network.Nodes.IsValidIndex(Node))
				{
					++Degree[Node];
					OnRoad[Node] |= Path.Rank == EForestPathRank::Road;
				}
			}
		}

		const int32 W = Field.GetCoarseWidth();
		const TArray<uint8>& WaterCells = Field.GetWaterCells();
		auto NearPond = [&](const FVector2f& P)
		{
			const int32 Reach = FMath::CeilToInt(60.0f / FForestField::CoarseCellM);
			const int32 CX = FMath::RoundToInt(P.X / FForestField::CoarseCellM);
			const int32 CY = FMath::RoundToInt(P.Y / FForestField::CoarseCellM);
			for (int32 Y = FMath::Max(0, CY - Reach); Y <= FMath::Min(W - 1, CY + Reach); ++Y)
			{
				for (int32 X = FMath::Max(0, CX - Reach); X <= FMath::Min(W - 1, CX + Reach); ++X)
				{
					if (WaterCells[Y * W + X] == 2)
					{
						return true;
					}
				}
			}
			return false;
		};

		const uint32 Seed = Hash(static_cast<uint32>(Field.GetDefinition().Seed), 0x4E4Du);
		const TArray<FString> NoNames;
		const TArray<FString>& Generic = Data.Find(TEXT("Generic")) ? *Data.Find(TEXT("Generic")) : NoNames;
		TSet<FString> Used;
		for (int32 N = 0; N < Network.Nodes.Num(); ++N)
		{
			const FForestPathNode& Node = Network.Nodes[N];
			FString Prefix;
			switch (Node.Kind)
			{
			case EForestPathNodeKind::Base: Names[N] = Data.Prefixes.Base; continue;
			case EForestPathNodeKind::Crossroads: Prefix = Degree[N] >= 5 ? Data.Prefixes.Star : Data.Prefixes.Crossroads; break;
			case EForestPathNodeKind::Hub: Prefix = Data.Prefixes.Hub; break;
			case EForestPathNodeKind::Entrance: Prefix = Data.Prefixes.Entrance; break;
			case EForestPathNodeKind::Junction: Prefix = OnRoad[N] ? Data.Prefixes.Junction : FString(); break;
			default: break;
			}
			if (Prefix.IsEmpty())
			{
				continue;
			}

			// Context lists first (pond, open parcel, dominant tree), then the generic names.
			const FForestSample Sample = Field.Sample(Node.PositionM.X, Node.PositionM.Y);
			TArray<FString> Contexts;
			if (NearPond(Node.PositionM))
			{
				Contexts.Add(TEXT("Pond"));
			}
			if (Sample.AgeClass == EStandAgeClass::ClearCut || Sample.AgeClass == EStandAgeClass::Regeneration)
			{
				Contexts.Add(TEXT("Open"));
			}
			int32 Dominant = 0;
			for (int32 G = 1; G < Sample.StandWeights.Num(); ++G)
			{
				Dominant = Sample.StandWeights[G] > Sample.StandWeights[Dominant] ? G : Dominant;
			}
			if (Field.GetGenera().IsValidIndex(Dominant))
			{
				Contexts.Add(Field.GetGenera()[Dominant].ToString());
			}

			auto TryList = [&](const TArray<FString>& List, uint32 Salt)
			{
				if (List.Num() == 0)
				{
					return false;
				}
				const int32 Start = static_cast<int32>(Hash(Seed, static_cast<uint32>(N), Salt) % static_cast<uint32>(List.Num()));
				for (int32 K = 0; K < List.Num(); ++K)
				{
					const FString Candidate = Prefix + TEXT(" ") + List[(Start + K) % List.Num()];
					if (!Used.Contains(Candidate))
					{
						Used.Add(Candidate);
						Names[N] = Candidate;
						return true;
					}
				}
				return false;
			};
			bool bNamed = false;
			for (int32 C = 0; C < Contexts.Num() && !bNamed; ++C)
			{
				// Context names are kept for a minority of places so that generic names stay common.
				const bool bUseContext = ToUnitFloat(Hash(Seed, static_cast<uint32>(N), 77u + C)) < 0.6f;
				if (const TArray<FString>* List = Data.Find(Contexts[C]); List && bUseContext)
				{
					bNamed = TryList(*List, 10u + C);
				}
			}
			if (!bNamed && !TryList(Generic, 99u))
			{
				Names[N] = FString::Printf(TEXT("%s %d"), *Prefix, N);
			}
		}
		return Names;
	}
}
