#include "Forest/ForestBake.h"

#include "Core/ShroomLog.h"
#include "Core/ShroomMath.h"
#include "Forest/ForestField.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace ForestBakeDetail
{
	using ShroomMath::SmoothStep01;

	bool WritePng(const FString& Path, const void* Data, int64 DataSize, int32 Width, int32 Height, ERGBFormat Format, int32 BitDepth)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetRaw(Data, DataSize, Width, Height, Format, BitDepth))
		{
			return false;
		}
		const TArray64<uint8> Compressed = Wrapper->GetCompressed();
		return FFileHelper::SaveArrayToFile(Compressed, *Path);
	}

	FColor Lerp(const FColor& A, const FColor& B, float T)
	{
		T = FMath::Clamp(T, 0.0f, 1.0f);
		return FColor(
			static_cast<uint8>(FMath::Lerp<float>(A.R, B.R, T)),
			static_cast<uint8>(FMath::Lerp<float>(A.G, B.G, T)),
			static_cast<uint8>(FMath::Lerp<float>(A.B, B.B, T)), 255);
	}

	FColor Scale(const FColor& C, float F)
	{
		return FColor(
			static_cast<uint8>(FMath::Clamp(C.R * F, 0.0f, 255.0f)),
			static_cast<uint8>(FMath::Clamp(C.G * F, 0.0f, 255.0f)),
			static_cast<uint8>(FMath::Clamp(C.B * F, 0.0f, 255.0f)), 255);
	}

	const FColor Water(70, 120, 200);

	FColor SoilColor(uint8 Soil)
	{
		switch (static_cast<ESoilType>(Soil))
		{
		case ESoilType::Sandy:     return FColor(232, 212, 150);
		case ESoilType::Clay:      return FColor(176, 112, 86);
		case ESoilType::Limestone: return FColor(225, 225, 214);
		case ESoilType::Peat:      return FColor(70, 58, 46);
		default:                   return FColor(150, 112, 72);
		}
	}

	FColor GenusColor(FName Genus, int32 Index)
	{
		static const TMap<FName, FColor> Known = {
			{ TEXT("Fagus"), FColor(86, 168, 72) },    // beech: bright green
			{ TEXT("Quercus"), FColor(150, 124, 42) }, // oak: olive brown
			{ TEXT("Carpinus"), FColor(66, 142, 140) },// hornbeam: teal
			{ TEXT("Betula"), FColor(232, 232, 205) }, // birch: pale
			{ TEXT("Alnus"), FColor(60, 88, 168) },    // alder: blue
		};
		if (const FColor* Found = Known.Find(Genus))
		{
			return *Found;
		}
		static const FColor Fallback[4] = { FColor(200, 90, 160), FColor(220, 140, 60), FColor(120, 200, 220), FColor(160, 160, 160) };
		return Fallback[Index % 4];
	}

	/** Lambert hillshade of the fine heightmap (light from the north-west, 45 degrees). */
	float Hillshade(const FFieldRaster& Heights, float XM, float YM)
	{
		const float GX = (Heights.Sample(XM + 2.0f, YM) - Heights.Sample(XM - 2.0f, YM)) / 4.0f;
		const float GY = (Heights.Sample(XM, YM + 2.0f) - Heights.Sample(XM, YM - 2.0f)) / 4.0f;
		const FVector3f Normal = FVector3f(-GX * 3.0f, -GY * 3.0f, 1.0f).GetSafeNormal(); // exaggerated relief
		const FVector3f Light = FVector3f(0.6f, -0.6f, 0.6f).GetSafeNormal();
		return FMath::Clamp(0.35f + 0.75f * FVector3f::DotProduct(Normal, Light), 0.2f, 1.25f);
	}
}

namespace ForestBake
{
	using namespace ForestBakeDetail;

	const TArray<FName>& GetLayerNames()
	{
		static const TArray<FName> Names = { TEXT("Litter"), TEXT("Moss"), TEXT("Grass"), TEXT("Mud"), TEXT("Gravel"), TEXT("Dirt") };
		return Names;
	}

	FString GetDefaultOutputFolder(const FString& ForestName)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ForestBake") / ForestName);
	}

	uint16 EncodeHeight(float HeightM)
	{
		// Landscape: 32768 = Z 0; one unit = ZScale / 128 cm.
		const float Units = 32768.0f + HeightM * 100.0f * 128.0f / LandscapeZScale;
		return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Units), 0, 65535));
	}

	bool BakeAll(const FForestField& Field, const FString& OutputFolder, FString& OutReport)
	{
		if (!Field.IsGenerated())
		{
			OutReport = TEXT("FAILED: field not generated");
			return false;
		}
		IFileManager::Get().MakeDirectory(*OutputFolder, true);
		const FFieldRaster& Heights = Field.GetHeights();
		const int32 Resolution = Heights.Width;
		bool bOk = true;

		// --- Heightmap -------------------------------------------------------------
		float MinH = TNumericLimits<float>::Max();
		float MaxH = TNumericLimits<float>::Lowest();
		TArray<uint16> HeightData;
		HeightData.SetNumUninitialized(Resolution * Resolution);
		for (int32 Index = 0; Index < HeightData.Num(); ++Index)
		{
			const float H = Heights.Values[Index];
			MinH = FMath::Min(MinH, H);
			MaxH = FMath::Max(MaxH, H);
			HeightData[Index] = EncodeHeight(H);
		}
		bOk &= WritePng(OutputFolder / TEXT("heightmap.png"), HeightData.GetData(), HeightData.Num() * sizeof(uint16), Resolution, Resolution, ERGBFormat::Gray, 16);

		// --- Weightmaps --------------------------------------------------------------
		const FFieldRaster& Moisture = Field.GetMoisture();
		const FFieldRaster& Canopy = Field.GetCanopy();
		const FFieldRaster& WaterDistance = Field.GetWaterDistance();
		const FFieldRaster& PathDistance = Field.GetPathDistance();
		const TArray<uint8>& PathRanks = Field.GetPathRankCells();
		const FForestDefinition& Definition = Field.GetDefinition();
		const int32 LayerCount = GetLayerNames().Num();
		TArray<TArray<uint8>> Layers;
		Layers.SetNum(LayerCount);
		for (TArray<uint8>& Layer : Layers)
		{
			Layer.SetNumUninitialized(Resolution * Resolution);
		}
		TArray<double> Coverage;
		Coverage.Init(0.0, LayerCount);
		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				const float M = Moisture.Sample(X, Y);
				const float C = Canopy.Sample(X, Y);
				const float Wd = WaterDistance.Sample(X, Y);
				const float Mud = FMath::Max(SmoothStep01(0.86f, 0.99f, M), 1.0f - SmoothStep01(1.5f, 4.0f, Wd));
				const float Grass = (1.0f - Mud) * SmoothStep01(0.55f, 0.9f, 1.0f - C);
				const float Moss = (1.0f - Mud) * (1.0f - Grass) * SmoothStep01(0.45f, 0.85f, M) * C;
				const float Litter = FMath::Max(0.0f, 1.0f - Mud - Grass - Moss);
				float Values[6] = { Litter, Moss, Grass, Mud, 0.0f, 0.0f };

				// Paths: gravel roads with dirt shoulders, grassy rides with dirt wheel ruts, dirt footpaths.
				const int32 Cell = Y * Resolution + X;
				if (PathRanks.IsValidIndex(Cell) && PathRanks[Cell] != 255)
				{
					const EForestPathRank Rank = static_cast<EForestPathRank>(PathRanks[Cell]);
					const float D = PathDistance.Values[Cell];
					const float HalfWidth = ForestPaths::HalfWidthM(Rank, Definition);
					float Gravel = 0.0f, Dirt = 0.0f, PathGrass = 0.0f;
					const float Surface = 1.0f - SmoothStep01(HalfWidth - 0.4f, HalfWidth + 0.5f, D);
					if (Rank == EForestPathRank::Road)
					{
						Gravel = Surface;
						Dirt = (1.0f - Surface) * (1.0f - SmoothStep01(HalfWidth + 0.5f, HalfWidth + 2.0f, D)) * 0.7f;
					}
					else if (Rank == EForestPathRank::Ride)
					{
						const float Rut = 1.0f - SmoothStep01(0.15f, 0.4f, FMath::Abs(D - 0.75f));
						Dirt = Surface * Rut;
						PathGrass = Surface * (1.0f - Rut);
					}
					else
					{
						Dirt = Surface;
					}
					const float PathTotal = FMath::Min(Gravel + Dirt + PathGrass, 1.0f);
					for (float& V : Values)
					{
						V *= 1.0f - PathTotal;
					}
					Values[2] += PathGrass;
					Values[4] += Gravel;
					Values[5] += Dirt;
				}
				float Sum = 0.0f;
				for (const float V : Values)
				{
					Sum += V;
				}
				Sum = FMath::Max(Sum, KINDA_SMALL_NUMBER);
				int32 Assigned = 0;
				for (int32 L = 0; L < LayerCount; ++L)
				{
					const int32 V = L == LayerCount - 1 ? 255 - Assigned : FMath::RoundToInt(255.0f * Values[L] / Sum);
					const uint8 Byte = static_cast<uint8>(FMath::Clamp(V, 0, 255));
					Layers[L][Y * Resolution + X] = Byte;
					Assigned += Byte;
					Coverage[L] += Byte / 255.0;
				}
			}
		}
		for (int32 L = 0; L < LayerCount; ++L)
		{
			bOk &= WritePng(OutputFolder / FString::Printf(TEXT("weight_%s.png"), *GetLayerNames()[L].ToString()),
				Layers[L].GetData(), Layers[L].Num(), Resolution, Resolution, ERGBFormat::Gray, 8);
		}

		// --- Debug contact sheet (north = +X at the top, east = +Y to the right) -------------
		const int32 W = Field.GetCoarseWidth();
		const int32 Tile = W * 2;
		const int32 SheetW = Tile * 3;
		const int32 SheetH = Tile * 2;
		TArray<FColor> Sheet;
		Sheet.Init(FColor(30, 30, 30), SheetW * SheetH);
		const TArray<uint8>& Soils = Field.GetSoilCells();
		const TArray<uint8>& WaterCells = Field.GetWaterCells();
		const TArray<int32>& ParcelCells = Field.GetParcelCells();
		const TArray<FName>& Genera = Field.GetGenera();
		const float HeightRange = FMath::Max(MaxH - MinH, 1.0f);

		for (int32 PY = 0; PY < Tile; ++PY)
		{
			for (int32 PX = 0; PX < Tile; ++PX)
			{
				const int32 CX = W - 1 - PY / 2; // field X (north) decreases downwards
				const int32 CY = PX / 2;         // field Y (east) increases to the right
				const int32 Cell = CY * W + CX;
				const float XM = CX * FForestField::CoarseCellM;
				const float YM = CY * FForestField::CoarseCellM;
				const bool bWater = WaterCells[Cell] != 0;
				const float Shade = Hillshade(Heights, XM, YM);
				const bool bParcelBorder = (CX + 1 < W && ParcelCells[Cell] != ParcelCells[Cell + 1]) || (CY + 1 < W && ParcelCells[Cell] != ParcelCells[Cell + W]);

				FColor Tiles[6];
				const float HeightT = (Heights.Sample(XM, YM) - MinH) / HeightRange;
				Tiles[0] = bWater ? Water : Scale(Lerp(FColor(110, 150, 80), FColor(185, 160, 110), HeightT), Shade);
				{
					const int32 Fine = Heights.Index(FMath::Min(FMath::RoundToInt(XM), Heights.Width - 1), FMath::Min(FMath::RoundToInt(YM), Heights.Height - 1));
					const uint8 Rank = PathRanks[Fine];
					if (Rank != 255 && PathDistance.Values[Fine] < ForestPaths::HalfWidthM(static_cast<EForestPathRank>(Rank), Definition) + 1.5f)
					{
						static const FColor PathColors[3] = { FColor(250, 245, 230), FColor(245, 215, 70), FColor(200, 70, 40) };
						Tiles[0] = PathColors[FMath::Min<int32>(Rank, 2)];
					}
				}
				Tiles[1] = Lerp(FColor(214, 196, 150), FColor(28, 70, 150), Moisture.Values[Cell]);
				Tiles[2] = bWater ? Water : Scale(SoilColor(Soils[Cell]), 0.85f + 0.15f * Shade);
				const float PH = Field.GetPH().Values[Cell];
				Tiles[3] = PH < 5.5f ? Lerp(FColor(200, 60, 40), FColor(235, 205, 80), (PH - 3.5f) / 2.0f) : Lerp(FColor(235, 205, 80), FColor(50, 150, 175), (PH - 5.5f) / 2.0f);
				int32 Dominant = 0;
				for (int32 G = 1; G < Genera.Num(); ++G)
				{
					if (Field.GetStandWeight(Cell, G) > Field.GetStandWeight(Cell, Dominant))
					{
						Dominant = G;
					}
				}
				const float C = Canopy.Values[Cell];
				Tiles[4] = bWater ? Water : (bParcelBorder ? FColor(20, 20, 20) : Scale(GenusColor(Genera[Dominant], Dominant), 0.45f + 0.6f * C));
				Tiles[5] = bWater ? Water : Lerp(FColor(235, 225, 150), FColor(20, 90, 30), C);
				if (bParcelBorder)
				{
					Tiles[5] = FColor(20, 20, 20);
				}

				for (int32 T = 0; T < 6; ++T)
				{
					const int32 OX = (T % 3) * Tile;
					const int32 OY = (T / 3) * Tile;
					Sheet[(OY + PY) * SheetW + OX + PX] = Tiles[T];
				}
			}
		}
		TArray<uint8> SheetBytes;
		SheetBytes.Reserve(Sheet.Num() * 4);
		for (const FColor& Pixel : Sheet)
		{
			SheetBytes.Add(Pixel.R);
			SheetBytes.Add(Pixel.G);
			SheetBytes.Add(Pixel.B);
			SheetBytes.Add(255);
		}
		bOk &= WritePng(OutputFolder / TEXT("debug_maps.png"), SheetBytes.GetData(), SheetBytes.Num(), SheetW, SheetH, ERGBFormat::RGBA, 8);

		// --- Report ----------------------------------------------------------------------
		const FForestDefinition& Def = Field.GetDefinition();
		const double Cells = static_cast<double>(Resolution) * Resolution;
		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("Forest %s (%s), seed %d"), *Def.Name, *Def.DisplayName, Def.Seed));
		Lines.Add(FString::Printf(TEXT("Size %.0f x %.0f m, heightmap %d x %d (1 m)"), Field.GetSizeM(), Field.GetSizeM(), Resolution, Resolution));
		Lines.Add(FString::Printf(TEXT("Height %.1f .. %.1f m (altitude %.0f .. %.0f m)"), MinH, MaxH, Def.BaseAltitudeM + MinH, Def.BaseAltitudeM + MaxH));
		Lines.Add(FString::Printf(TEXT("Landscape import: Location (0, 0, 0), Scale (100, 100, %.0f), Section Size 63x63, Sections Per Component 2x2, Components 8x8 (for 1009)"), LandscapeZScale));
		Lines.Add(FString::Printf(TEXT("Parcels %d, stream cells %d, pond cells %d (4 m cells)"), Field.GetParcels().Num(), Field.GetStreamCellCount(), Field.GetPondCellCount()));
		{
			const FForestPathNetwork& Network = Field.GetPathNetwork();
			Lines.Add(FString::Printf(TEXT("Paths: %d roads %.0f m, %d rides %.0f m, %d footpaths %.0f m; nodes: %d entrances, %d base, %d hubs, %d crossroads"),
				Network.CountPaths(EForestPathRank::Road), Network.GetLengthM(EForestPathRank::Road),
				Network.CountPaths(EForestPathRank::Ride), Network.GetLengthM(EForestPathRank::Ride),
				Network.CountPaths(EForestPathRank::Footpath), Network.GetLengthM(EForestPathRank::Footpath),
				Network.CountNodes(EForestPathNodeKind::Entrance), Network.CountNodes(EForestPathNodeKind::Base),
				Network.CountNodes(EForestPathNodeKind::Hub), Network.CountNodes(EForestPathNodeKind::Crossroads)));
		}
		for (int32 L = 0; L < LayerCount; ++L)
		{
			Lines.Add(FString::Printf(TEXT("Layer %-6s %5.1f %%"), *GetLayerNames()[L].ToString(), 100.0 * Coverage[L] / Cells));
		}
		Lines.Add(TEXT("debug_maps.png tiles (north up): terrain+water+paths (road white, ride yellow, footpath red) | moisture | soil (sand, loam, clay, limestone, peat) / pH | dominant tree genus + parcels | canopy + parcels"));
		OutReport = FString::Join(Lines, TEXT("\n"));
		bOk &= FFileHelper::SaveStringToFile(OutReport, *(OutputFolder / TEXT("bake_report.txt")));

		OutReport = FString::Printf(TEXT("%s: baked to %s\n%s"), bOk ? TEXT("OK") : TEXT("FAILED"), *OutputFolder, *OutReport);
		UE_LOG(LogShroomForest, Log, TEXT("%s"), *OutReport);
		return bOk;
	}
}
