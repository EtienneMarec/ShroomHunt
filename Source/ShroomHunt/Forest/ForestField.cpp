#include "Forest/ForestField.h"

#include "Core/ShroomHash.h"
#include "Core/ShroomLog.h"
#include "Core/ShroomMath.h"
#include "Async/ParallelFor.h"

// -----------------------------------------------------------------------------
// FFieldRaster
// -----------------------------------------------------------------------------

void FFieldRaster::Init(int32 InWidth, int32 InHeight, float InCellSize, float Fill)
{
	Width = InWidth;
	Height = InHeight;
	CellSize = InCellSize;
	Values.Init(Fill, Width * Height);
}

float FFieldRaster::Sample(float XM, float YM) const
{
	if (!IsValid())
	{
		return 0.0f;
	}
	const float FX = FMath::Clamp(XM / CellSize, 0.0f, static_cast<float>(Width - 1));
	const float FY = FMath::Clamp(YM / CellSize, 0.0f, static_cast<float>(Height - 1));
	const int32 X0 = FMath::Min(FMath::FloorToInt(FX), Width - 2);
	const int32 Y0 = FMath::Min(FMath::FloorToInt(FY), Height - 2);
	const float TX = FX - X0;
	const float TY = FY - Y0;
	const float Bottom = FMath::Lerp(At(X0, Y0), At(X0 + 1, Y0), TX);
	const float Top = FMath::Lerp(At(X0, Y0 + 1), At(X0 + 1, Y0 + 1), TX);
	return FMath::Lerp(Bottom, Top, TY);
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

namespace ForestFieldDetail
{
	using ShroomMath::SmoothStep01;

	// Independent noise channels.
	namespace Channel
	{
		constexpr uint32 WarpX = 11;
		constexpr uint32 WarpY = 12;
		constexpr uint32 Hills = 13;
		constexpr uint32 Valley = 14;
		constexpr uint32 Limestone = 15;
		constexpr uint32 Sand = 16;
		constexpr uint32 PH = 17;
		constexpr uint32 Parcels = 18;
		constexpr uint32 Gaps = 19;
		constexpr uint32 StandNoise = 20;
	}

	/** fBm (~Gaussian, std ~0.3) to an approximately uniform [0, 1] value. */
	float NoiseToUniform(float Noise)
	{
		return 1.0f / (1.0f + FMath::Exp(-5.6f * Noise));
	}

	const int32 NeighborDX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	const int32 NeighborDY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	const float NeighborDistance[8] = { 1.0f, 1.0f, 1.0f, 1.0f, UE_SQRT_2, UE_SQRT_2, UE_SQRT_2, UE_SQRT_2 };

	/**
	 * Two-pass chamfer distance transform (approximate Euclidean, in meters) from seed cells.
	 * Optionally propagates a payload value from the nearest seed.
	 */
	FFieldRaster ChamferDistance(int32 Width, int32 Height, float CellSize, const TArray<bool>& Seeds, TArray<float>* Payload)
	{
		FFieldRaster Distance;
		Distance.Init(Width, Height, CellSize, 1e9f);
		for (int32 Index = 0; Index < Width * Height; ++Index)
		{
			if (Seeds[Index])
			{
				Distance.Values[Index] = 0.0f;
			}
		}
		auto Relax = [&](int32 X, int32 Y, int32 NX, int32 NY, float Step)
		{
			if (NX < 0 || NY < 0 || NX >= Width || NY >= Height)
			{
				return;
			}
			const float Candidate = Distance.At(NX, NY) + Step * CellSize;
			if (Candidate < Distance.At(X, Y))
			{
				Distance.At(X, Y) = Candidate;
				if (Payload)
				{
					(*Payload)[Distance.Index(X, Y)] = (*Payload)[Distance.Index(NX, NY)];
				}
			}
		};
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				Relax(X, Y, X - 1, Y, 1.0f);
				Relax(X, Y, X, Y - 1, 1.0f);
				Relax(X, Y, X - 1, Y - 1, UE_SQRT_2);
				Relax(X, Y, X + 1, Y - 1, UE_SQRT_2);
			}
		}
		for (int32 Y = Height - 1; Y >= 0; --Y)
		{
			for (int32 X = Width - 1; X >= 0; --X)
			{
				Relax(X, Y, X + 1, Y, 1.0f);
				Relax(X, Y, X, Y + 1, 1.0f);
				Relax(X, Y, X + 1, Y + 1, UE_SQRT_2);
				Relax(X, Y, X - 1, Y + 1, UE_SQRT_2);
			}
		}
		return Distance;
	}

	EStandAgeClass PickAgeClass(const FAgeClassShares& Shares, float U)
	{
		const float Values[5] = { Shares.ClearCut, Shares.Regeneration, Shares.Pole, Shares.Mature, Shares.Old };
		float Total = 0.0f;
		for (const float V : Values)
		{
			Total += FMath::Max(0.0f, V);
		}
		float Pick = U * FMath::Max(Total, KINDA_SMALL_NUMBER);
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Pick -= FMath::Max(0.0f, Values[Index]);
			if (Pick <= 0.0f)
			{
				return static_cast<EStandAgeClass>(Index);
			}
		}
		return EStandAgeClass::Mature;
	}

	float CanopyForAge(EStandAgeClass Age)
	{
		switch (Age)
		{
		case EStandAgeClass::ClearCut:     return 0.08f;
		case EStandAgeClass::Regeneration: return 0.4f;
		case EStandAgeClass::Pole:         return 0.9f;
		case EStandAgeClass::Mature:       return 0.83f;
		default:                           return 0.74f;
		}
	}

	float BasePH(ESoilType Soil)
	{
		switch (Soil)
		{
		case ESoilType::Sandy:     return 4.4f;
		case ESoilType::Clay:      return 6.0f;
		case ESoilType::Limestone: return 7.4f;
		case ESoilType::Peat:      return 4.1f;
		default:                   return 5.4f;
		}
	}
}

// -----------------------------------------------------------------------------
// Generation
// -----------------------------------------------------------------------------

bool FForestField::Generate(const FForestDefinition& InDefinition)
{
	const double Start = FPlatformTime::Seconds();
	Definition = InDefinition;
	Seed = ShroomHash::Hash(static_cast<uint32>(Definition.Seed), ShroomHash::HashString(Definition.Name));

	Genera.Reset();
	for (const FStandSpecies& Stand : Definition.Stands)
	{
		Genera.Add(Stand.Genus);
	}
	if (Genera.Num() == 0)
	{
		UE_LOG(LogShroomForest, Error, TEXT("Forest %s has no stand species"), *Definition.Name);
		return false;
	}

	GenerateHeights();
	GenerateHydrology();
	GenerateMoistureAndSoil();
	GenerateParcels();
	GenerateCanopyAndEdges();
	GenerateStands();

	UE_LOG(LogShroomForest, Log, TEXT("Forest field %s generated in %.2f s: %d x %d m, %d parcels, %d stream cells, %d pond cells"),
		*Definition.Name, FPlatformTime::Seconds() - Start, FMath::RoundToInt(GetSizeM()), FMath::RoundToInt(GetSizeM()),
		Parcels.Num(), StreamCellCount, PondCellCount);
	return true;
}

void FForestField::GenerateHeights()
{
	using namespace ForestFieldDetail;
	using namespace ShroomHash;

	const int32 Resolution = Definition.HeightmapResolution;
	Heights.Init(Resolution, Resolution, 1.0f);

	const double Scale = FMath::Max(10.0f, Definition.TerrainScaleM);
	const uint32 WarpXSeed = Hash(Seed, Channel::WarpX);
	const uint32 WarpYSeed = Hash(Seed, Channel::WarpY);
	const uint32 HillSeed = Hash(Seed, Channel::Hills);
	const uint32 ValleySeed = Hash(Seed, Channel::Valley);

	ParallelFor(Resolution, [&](int32 Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			// Domain warp gives organic, non-grid-aligned hills.
			const double WX = X + 0.25 * Scale * GradientFbm2D(WarpXSeed, X / Scale, Y / Scale, 3);
			const double WY = Y + 0.25 * Scale * GradientFbm2D(WarpYSeed, X / Scale, Y / Scale, 3);
			const float Hills = Definition.ReliefM * 0.75f * GradientFbm2D(HillSeed, WX / Scale, WY / Scale, 5);
			// A valley network where low-frequency noise crosses zero.
			const float ValleyNoise = FMath::Abs(GradientFbm2D(ValleySeed, WX / (1.6 * Scale), WY / (1.6 * Scale), 3));
			const float Valley = -Definition.ValleyDepthM * (1.0f - SmoothStep01(0.0f, 0.14f, ValleyNoise));
			Heights.At(X, Y) = Hills + Valley;
		}
	});
}

void FForestField::GenerateHydrology()
{
	using namespace ForestFieldDetail;

	const int32 W = (Definition.HeightmapResolution - 1) / static_cast<int32>(CoarseCellM) + 1;
	const int32 Count = W * W;
	CoarseHeights.Init(W, W, CoarseCellM);
	for (int32 Y = 0; Y < W; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			CoarseHeights.At(X, Y) = Heights.At(X * 4, Y * 4);
		}
	}

	// 1. Priority-flood depression filling (Barnes et al. 2014) with a tiny gradient: every
	//    cell then drains to the map border, and depression depth = filled - original.
	struct FQueued
	{
		float H;
		int32 Index;
		bool operator<(const FQueued& Other) const { return H < Other.H || (H == Other.H && Index < Other.Index); }
	};
	TArray<float> Filled = CoarseHeights.Values;
	TArray<bool> Closed;
	Closed.Init(false, Count);
	TArray<FQueued> Queue;
	Queue.Reserve(W * 4);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 X = Index % W;
		const int32 Y = Index / W;
		if (X == 0 || Y == 0 || X == W - 1 || Y == W - 1)
		{
			Closed[Index] = true;
			Queue.HeapPush({ Filled[Index], Index });
		}
	}
	constexpr float Epsilon = 1e-3f;
	while (Queue.Num() > 0)
	{
		FQueued Current;
		Queue.HeapPop(Current, EAllowShrinking::No);
		const int32 CX = Current.Index % W;
		const int32 CY = Current.Index / W;
		for (int32 N = 0; N < 8; ++N)
		{
			const int32 NX = CX + NeighborDX[N];
			const int32 NY = CY + NeighborDY[N];
			if (NX < 0 || NY < 0 || NX >= W || NY >= W)
			{
				continue;
			}
			const int32 NIndex = NY * W + NX;
			if (Closed[NIndex])
			{
				continue;
			}
			Closed[NIndex] = true;
			Filled[NIndex] = FMath::Max(Filled[NIndex], Filled[Current.Index] + Epsilon);
			Queue.HeapPush({ Filled[NIndex], NIndex });
		}
	}

	// 2. D8 flow directions on the filled surface (border cells drain out of the map).
	TArray<int32> Downstream;
	Downstream.Init(INDEX_NONE, Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 X = Index % W;
		const int32 Y = Index / W;
		float BestDrop = 0.0f;
		for (int32 N = 0; N < 8; ++N)
		{
			const int32 NX = X + NeighborDX[N];
			const int32 NY = Y + NeighborDY[N];
			if (NX < 0 || NY < 0 || NX >= W || NY >= W)
			{
				continue;
			}
			const float Drop = (Filled[Index] - Filled[NY * W + NX]) / NeighborDistance[N];
			if (Drop > BestDrop)
			{
				BestDrop = Drop;
				Downstream[Index] = NY * W + NX;
			}
		}
	}

	// 3. Flow accumulation: visit cells from high to low, pass the upstream count down.
	TArray<int32> Order;
	Order.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Order.Add(Index);
	}
	Order.Sort([&Filled](int32 A, int32 B) { return Filled[A] > Filled[B] || (Filled[A] == Filled[B] && A < B); });
	FlowAccumulation.Init(W, W, CoarseCellM, 1.0f);
	for (const int32 Index : Order)
	{
		if (Downstream[Index] != INDEX_NONE)
		{
			FlowAccumulation.Values[Downstream[Index]] += FlowAccumulation.Values[Index];
		}
	}

	// 4. Streams (large upstream area) and ponds (deep enough depressions).
	const float CellArea = CoarseCellM * CoarseCellM;
	const float StreamCells = Definition.StreamThresholdHa * 10000.0f / CellArea;
	WaterCells.Init(0, Count);
	TArray<bool> StreamSeeds;
	StreamSeeds.Init(false, Count);
	TArray<float> CarveDepthPayload;
	CarveDepthPayload.Init(0.0f, Count);
	StreamCellCount = 0;
	PondCellCount = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FlowAccumulation.Values[Index] >= StreamCells)
		{
			WaterCells[Index] = 1;
			StreamSeeds[Index] = true;
			// Bigger streams cut deeper beds.
			CarveDepthPayload[Index] = FMath::Clamp(0.5f + 0.35f * FMath::Log2(FlowAccumulation.Values[Index] / StreamCells), 0.5f, 2.5f);
			++StreamCellCount;
		}
	}

	// Ponds: connected depression regions; only the deepest few of a sensible size hold water,
	// the others stay damp hollows (their wetness comes from the wetness index).
	{
		constexpr float RegionDepth = 0.05f;
		TArray<int32> Region;
		Region.Init(INDEX_NONE, Count);
		struct FDepression
		{
			TArray<int32> Cells;
			float MaxDepth = 0.0f;
		};
		TArray<FDepression> Depressions;
		TArray<int32> Stack;
		for (int32 Start = 0; Start < Count; ++Start)
		{
			if (Region[Start] != INDEX_NONE || Filled[Start] - CoarseHeights.Values[Start] < RegionDepth)
			{
				continue;
			}
			const int32 RegionId = Depressions.Num();
			Depressions.AddDefaulted();
			Stack.Reset();
			Stack.Add(Start);
			Region[Start] = RegionId;
			while (Stack.Num() > 0)
			{
				const int32 Cell = Stack.Pop(EAllowShrinking::No);
				Depressions[RegionId].Cells.Add(Cell);
				Depressions[RegionId].MaxDepth = FMath::Max(Depressions[RegionId].MaxDepth, Filled[Cell] - CoarseHeights.Values[Cell]);
				for (int32 N = 0; N < 4; ++N)
				{
					const int32 NX = Cell % W + NeighborDX[N];
					const int32 NY = Cell / W + NeighborDY[N];
					if (NX < 0 || NY < 0 || NX >= W || NY >= W)
					{
						continue;
					}
					const int32 Next = NY * W + NX;
					if (Region[Next] == INDEX_NONE && Filled[Next] - CoarseHeights.Values[Next] >= RegionDepth)
					{
						Region[Next] = RegionId;
						Stack.Add(Next);
					}
				}
			}
		}

		TArray<int32> Candidates;
		for (int32 Id = 0; Id < Depressions.Num(); ++Id)
		{
			const float AreaHa = Depressions[Id].Cells.Num() * CellArea / 10000.0f;
			if (Depressions[Id].MaxDepth >= Definition.PondMinDepthM && AreaHa >= 0.03f && AreaHa <= Definition.PondMaxAreaHa)
			{
				Candidates.Add(Id);
			}
		}
		Candidates.Sort([&Depressions](int32 A, int32 B)
		{
			return Depressions[A].MaxDepth > Depressions[B].MaxDepth || (Depressions[A].MaxDepth == Depressions[B].MaxDepth && A < B);
		});
		const int32 Kept = FMath::Min(Candidates.Num(), FMath::Max(0, Definition.MaxPonds));
		for (int32 Rank = 0; Rank < Kept; ++Rank)
		{
			for (const int32 Cell : Depressions[Candidates[Rank]].Cells)
			{
				if (WaterCells[Cell] == 0 && Filled[Cell] - CoarseHeights.Values[Cell] >= 0.15f)
				{
					WaterCells[Cell] = 2;
					++PondCellCount;
				}
			}
		}
	}

	const FFieldRaster StreamDistance = ChamferDistance(W, W, CoarseCellM, StreamSeeds, &CarveDepthPayload);
	FFieldRaster StreamDepth;
	StreamDepth.Init(W, W, CoarseCellM);
	StreamDepth.Values = CarveDepthPayload;
	if (StreamCellCount > 0)
	{
		CarveStreams(StreamDepth, StreamDistance);
	}

	// Distance to any water (streams and ponds), used by moisture, alders and mud.
	TArray<bool> WaterSeeds;
	WaterSeeds.Init(false, Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		WaterSeeds[Index] = WaterCells[Index] != 0;
	}
	WaterDistance = ChamferDistance(W, W, CoarseCellM, WaterSeeds, nullptr);
}

void FForestField::CarveStreams(const FFieldRaster& StreamDepth, const FFieldRaster& StreamDistance)
{
	using ShroomMath::SmoothStep01;
	const int32 Resolution = Heights.Width;
	ParallelFor(Resolution, [&](int32 Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const float Distance = StreamDistance.Sample(static_cast<float>(X), static_cast<float>(Y));
			if (Distance > 12.0f)
			{
				continue;
			}
			const float Depth = StreamDepth.Sample(static_cast<float>(X), static_cast<float>(Y));
			const float HalfWidth = 1.0f + 0.6f * Depth;
			Heights.At(X, Y) -= Depth * (1.0f - SmoothStep01(HalfWidth, HalfWidth + 4.0f, Distance));
		}
	});
	for (int32 Y = 0; Y < CoarseHeights.Height; ++Y)
	{
		for (int32 X = 0; X < CoarseHeights.Width; ++X)
		{
			CoarseHeights.At(X, Y) = Heights.At(X * 4, Y * 4);
		}
	}
}

void FForestField::GenerateMoistureAndSoil()
{
	using namespace ForestFieldDetail;
	using namespace ShroomHash;

	const int32 W = CoarseHeights.Width;
	const int32 Count = W * W;
	Slope.Init(W, W, CoarseCellM);
	Southness.Init(W, W, CoarseCellM);
	Moisture.Init(W, W, CoarseCellM);
	PH.Init(W, W, CoarseCellM);
	SoilCells.Init(static_cast<uint8>(ESoilType::Loam), Count);

	// Topographic wetness index: ln(upslope area / tan(slope)).
	TArray<float> Wetness;
	Wetness.SetNumUninitialized(Count);
	for (int32 Y = 0; Y < W; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const float FX = X * CoarseCellM;
			const float FY = Y * CoarseCellM;
			const float GX = (Heights.Sample(FX + 2.0f, FY) - Heights.Sample(FX - 2.0f, FY)) / 4.0f;
			const float GY = (Heights.Sample(FX, FY + 2.0f) - Heights.Sample(FX, FY - 2.0f)) / 4.0f;
			const float S = FMath::Sqrt(GX * GX + GY * GY);
			Slope.At(X, Y) = S;
			// Height rising towards the north (+X) means the slope faces south.
			Southness.At(X, Y) = S > 1e-4f ? FMath::Clamp(GX / S, -1.0f, 1.0f) * SmoothStep01(0.02f, 0.15f, S) : 0.0f;
			const float SpecificArea = FlowAccumulation.At(X, Y) * CoarseCellM; // area / contour width
			Wetness[Slope.Index(X, Y)] = FMath::Loge(SpecificArea / FMath::Max(S, 0.01f));
		}
	}

	// Smooth the wetness index (two 3x3 box passes) to soften D8 flow-line artifacts.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		TArray<float> Smoothed = Wetness;
		for (int32 Y = 1; Y < W - 1; ++Y)
		{
			for (int32 X = 1; X < W - 1; ++X)
			{
				float Sum = 0.0f;
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						Sum += Wetness[(Y + DY) * W + X + DX];
					}
				}
				Smoothed[Y * W + X] = Sum / 9.0f;
			}
		}
		Wetness = MoveTemp(Smoothed);
	}

	// Normalize the wetness index by its percentiles so every forest spans dry -> wet.
	TArray<float> Sorted = Wetness;
	Sorted.Sort();
	const float Low = Sorted[FMath::FloorToInt(Count * 0.05f)];
	const float High = Sorted[FMath::FloorToInt(Count * 0.97f)];

	const float SoilScale = FMath::Max(10.0f, Definition.SoilScaleM);
	const uint32 LimestoneSeed = Hash(Seed, Channel::Limestone);
	const uint32 SandSeed = Hash(Seed, Channel::Sand);
	const uint32 PHSeed = Hash(Seed, Channel::PH);

	// Soil noises, thresholded by percentile so the areas match the requested shares exactly.
	TArray<float> LimestoneNoise;
	TArray<float> SandNoise;
	LimestoneNoise.SetNumUninitialized(Count);
	SandNoise.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float FX = (Index % W) * CoarseCellM;
		const float FY = (Index / W) * CoarseCellM;
		LimestoneNoise[Index] = GradientFbm2D(LimestoneSeed, FX / (2.0 * SoilScale), FY / (2.0 * SoilScale), 3);
		SandNoise[Index] = GradientFbm2D(SandSeed, FX / SoilScale, FY / SoilScale, 4);
	}
	auto Percentile = [Count](TArray<float> Values, float Fraction)
	{
		Values.Sort();
		return Values[FMath::Clamp(FMath::FloorToInt(Count * Fraction), 0, Count - 1)];
	};
	const float LimestoneThreshold = Definition.LimestoneShare > 0.0f ? Percentile(LimestoneNoise, 1.0f - Definition.LimestoneShare) : TNumericLimits<float>::Max();
	const float SandThreshold = Percentile(SandNoise, 0.7f);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 X = Index % W;
		const int32 Y = Index / W;
		const float FX = X * CoarseCellM;
		const float FY = Y * CoarseCellM;

		float M = FMath::Clamp((Wetness[Index] - Low) / FMath::Max(High - Low, 0.01f), 0.0f, 1.0f);
		M = FMath::Max(M, 0.95f * (1.0f - SmoothStep01(0.0f, 24.0f, WaterDistance.Values[Index])));
		M = FMath::Clamp(M - 0.08f * Southness.Values[Index], 0.0f, 1.0f);
		if (WaterCells[Index] != 0)
		{
			M = 1.0f;
		}
		Moisture.Values[Index] = M;

		ESoilType Soil = ESoilType::Loam;
		if (WaterCells[Index] == 2 || (M > 0.93f && WaterCells[Index] == 0 && WaterDistance.Values[Index] < 12.0f && Slope.Values[Index] < 0.03f))
		{
			Soil = ESoilType::Peat;
		}
		else if (LimestoneNoise[Index] >= LimestoneThreshold)
		{
			Soil = ESoilType::Limestone;
		}
		else if (M > 0.72f)
		{
			Soil = ESoilType::Clay;
		}
		else if (SandNoise[Index] >= SandThreshold && M < 0.6f)
		{
			Soil = ESoilType::Sandy;
		}
		SoilCells[Index] = static_cast<uint8>(Soil);
		PH.Values[Index] = FMath::Clamp(BasePH(Soil) + 0.45f * Fbm2D(PHSeed, FX / 120.0, FY / 120.0, 3), 3.5f, 8.5f);
	}
}

void FForestField::GenerateParcels()
{
	using namespace ForestFieldDetail;
	using namespace ShroomHash;

	const float Size = GetSizeM();
	const float ParcelArea = FMath::Max(1.0f, Definition.ParcelSizeHa) * 10000.0f;
	const int32 Target = FMath::Max(1, FMath::RoundToInt(Size * Size / ParcelArea));
	const float MinDistance = 0.75f * FMath::Sqrt(ParcelArea);
	const uint32 ParcelSeed = Hash(Seed, Channel::Parcels);

	// Dart throwing with deterministic candidates.
	TArray<FVector2f> Sites;
	for (uint32 Attempt = 0; Attempt < 5000 && Sites.Num() < Target; ++Attempt)
	{
		const FVector2f Candidate(ToUnitFloat(Hash(ParcelSeed, Attempt, 1u)) * Size, ToUnitFloat(Hash(ParcelSeed, Attempt, 2u)) * Size);
		bool bFar = true;
		for (const FVector2f& Site : Sites)
		{
			if (FVector2f::Distance(Site, Candidate) < MinDistance)
			{
				bFar = false;
				break;
			}
		}
		if (bFar)
		{
			Sites.Add(Candidate);
		}
	}
	// Number parcels in reading order (south-west to north-east), like a forest map.
	Sites.Sort([](const FVector2f& A, const FVector2f& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });

	float ShareTotal = 0.0f;
	for (const FStandSpecies& Stand : Definition.Stands)
	{
		ShareTotal += FMath::Max(0.0f, Stand.Share);
	}

	Parcels.Reset();
	for (int32 Index = 0; Index < Sites.Num(); ++Index)
	{
		FForestParcel Parcel;
		Parcel.Number = Index + 1;
		Parcel.Site = Sites[Index];
		Parcel.AgeClass = PickAgeClass(Definition.AgeClasses, ToUnitFloat(Hash(ParcelSeed, 1000u + Index, 3u)));
		float Pick = ToUnitFloat(Hash(ParcelSeed, 1000u + Index, 4u)) * ShareTotal;
		Parcel.DominantGenus = 0;
		for (int32 Genus = 0; Genus < Definition.Stands.Num(); ++Genus)
		{
			Pick -= FMath::Max(0.0f, Definition.Stands[Genus].Share);
			if (Pick <= 0.0f)
			{
				Parcel.DominantGenus = Genus;
				break;
			}
		}
		Parcels.Add(Parcel);
	}

	// Voronoi assignment of coarse cells.
	const int32 W = CoarseHeights.Width;
	ParcelCells.Init(0, W * W);
	for (int32 Index = 0; Index < W * W; ++Index)
	{
		const FVector2f P((Index % W) * CoarseCellM, (Index / W) * CoarseCellM);
		float Best = TNumericLimits<float>::Max();
		for (int32 Parcel = 0; Parcel < Parcels.Num(); ++Parcel)
		{
			const float D = FVector2f::DistSquared(P, Parcels[Parcel].Site);
			if (D < Best)
			{
				Best = D;
				ParcelCells[Index] = Parcel;
			}
		}
	}
}

void FForestField::GenerateCanopyAndEdges()
{
	using namespace ForestFieldDetail;
	using namespace ShroomHash;

	const int32 W = CoarseHeights.Width;
	const int32 Count = W * W;
	Canopy.Init(W, W, CoarseCellM);
	const uint32 GapSeed = Hash(Seed, Channel::Gaps);
	TArray<bool> Open;
	Open.Init(false, Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float FX = (Index % W) * CoarseCellM;
		const float FY = (Index / W) * CoarseCellM;
		float C = CanopyForAge(Parcels[ParcelCells[Index]].AgeClass);
		// Natural gaps (windthrow, old tree falls).
		C -= 0.35f * SmoothStep01(0.45f, 0.8f, NoiseToUniform(Fbm2D(GapSeed, FX / 60.0, FY / 60.0, 3)));
		if (WaterCells[Index] != 0)
		{
			C = 0.0f;
		}
		C = FMath::Clamp(C, 0.0f, 1.0f);
		Canopy.Values[Index] = C;
		Open[Index] = C < 0.35f;
	}
	const FFieldRaster OpenDistance = ChamferDistance(W, W, CoarseCellM, Open, nullptr);
	Edgeness.Init(W, W, CoarseCellM);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Edgeness.Values[Index] = 1.0f - SmoothStep01(0.0f, 40.0f, OpenDistance.Values[Index]);
	}
}

void FForestField::GenerateStands()
{
	using namespace ForestFieldDetail;
	using namespace ShroomHash;

	const int32 W = CoarseHeights.Width;
	const int32 Count = W * W;
	const int32 NumGenera = Genera.Num();
	StandWeights.Init(0.0f, Count * NumGenera);

	float MaxShare = KINDA_SMALL_NUMBER;
	for (const FStandSpecies& Stand : Definition.Stands)
	{
		MaxShare = FMath::Max(MaxShare, Stand.Share);
	}
	TArray<FTrapezoid> MoistureRanges;
	TArray<FTrapezoid> PHRanges;
	for (const FStandSpecies& Stand : Definition.Stands)
	{
		MoistureRanges.Add(ForestData::ToTrapezoid(Stand.Moisture));
		PHRanges.Add(ForestData::ToTrapezoid(Stand.PH));
	}
	const uint32 NoiseSeed = Hash(Seed, Channel::StandNoise);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float FX = (Index % W) * CoarseCellM;
		const float FY = (Index / W) * CoarseCellM;
		const FForestParcel& Parcel = Parcels[ParcelCells[Index]];
		const bool bYoung = Parcel.AgeClass == EStandAgeClass::ClearCut || Parcel.AgeClass == EStandAgeClass::Regeneration;
		const float WaterProximity = 1.0f - SmoothStep01(0.0f, 30.0f, WaterDistance.Values[Index]);

		float Sum = 0.0f;
		for (int32 Genus = 0; Genus < NumGenera; ++Genus)
		{
			const FStandSpecies& Stand = Definition.Stands[Genus];
			float Weight = MoistureRanges[Genus].Evaluate(Moisture.Values[Index]) * PHRanges[Genus].Evaluate(PH.Values[Index]);
			// Foresters favour the parcel's main species; others mix in by share.
			Weight *= Genus == Parcel.DominantGenus ? 3.0f : 0.15f + 0.6f * Stand.Share / MaxShare;
			// Pioneers take open and young stands, recede under closed canopy.
			Weight *= bYoung ? 1.0f + 2.0f * Stand.Pioneer : 1.0f - 0.6f * Stand.Pioneer;
			// Riparian trees line the water.
			Weight *= Stand.Riparian > 0.0f ? 0.1f + 2.5f * Stand.Riparian * WaterProximity : 1.0f;
			// Patchy mixing at the 50 m scale.
			Weight *= 0.7f + 0.6f * NoiseToUniform(Fbm2D(Hash(NoiseSeed, static_cast<uint32>(Genus)), FX / 50.0, FY / 50.0, 2));
			StandWeights[Index * NumGenera + Genus] = Weight;
			Sum += Weight;
		}
		if (Sum <= KINDA_SMALL_NUMBER)
		{
			StandWeights[Index * NumGenera + Parcel.DominantGenus] = 1.0f;
			continue;
		}
		for (int32 Genus = 0; Genus < NumGenera; ++Genus)
		{
			StandWeights[Index * NumGenera + Genus] /= Sum;
		}
	}
}

int32 FForestField::CoarseIndexAt(float XM, float YM) const
{
	const int32 W = Moisture.Width;
	const int32 X = FMath::Clamp(FMath::RoundToInt(XM / CoarseCellM), 0, W - 1);
	const int32 Y = FMath::Clamp(FMath::RoundToInt(YM / CoarseCellM), 0, W - 1);
	return Y * W + X;
}

FForestSample FForestField::Sample(float XM, float YM) const
{
	FForestSample Out;
	if (!IsGenerated())
	{
		return Out;
	}
	const int32 Cell = CoarseIndexAt(XM, YM);
	Out.HeightM = Heights.Sample(XM, YM);
	Out.AltitudeM = Definition.BaseAltitudeM + Out.HeightM;
	Out.Slope = Slope.Sample(XM, YM);
	Out.Southness = Southness.Sample(XM, YM);
	Out.Moisture = Moisture.Sample(XM, YM);
	Out.PH = PH.Sample(XM, YM);
	Out.Soil = static_cast<ESoilType>(SoilCells[Cell]);
	Out.Canopy = Canopy.Sample(XM, YM);
	Out.Edgeness = Edgeness.Sample(XM, YM);
	Out.bWater = WaterCells[Cell] != 0;
	Out.WaterDistanceM = Out.bWater ? 0.0f : WaterDistance.Sample(XM, YM);
	const FForestParcel& Parcel = Parcels[ParcelCells[Cell]];
	Out.ParcelNumber = Parcel.Number;
	Out.AgeClass = Parcel.AgeClass;
	for (int32 Genus = 0; Genus < Genera.Num(); ++Genus)
	{
		Out.StandWeights.Add(GetStandWeight(Cell, Genus));
	}
	return Out;
}
