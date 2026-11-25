#include "Vegetation/VegetationObstacles.h"

#include "Vegetation/VegetationMeshBuilder.h"

float FVegetationObstacle::DistanceTo(const FVector2f& P) const
{
	const FVector2f AB = B - A;
	const float LengthSq = AB.SizeSquared();
	const float T = LengthSq > 1e-6f ? FMath::Clamp(FVector2f::DotProduct(P - A, AB) / LengthSq, 0.0f, 1.0f) : 0.0f;
	return FVector2f::Distance(P, A + AB * T) - RadiusM;
}

bool FVegetationObstacleGrid::MakeObstacle(const FVegetationDefinition& Definition, const FVegetationInstance& Instance, FVegetationObstacle& Out)
{
	const FVector2f P(static_cast<float>(Instance.PositionM.X), static_cast<float>(Instance.PositionM.Y));
	Out.A = P;
	Out.B = P;
	switch (Instance.Kind)
	{
	case EVegetationKind::Sapling:
	case EVegetationKind::Young:
	case EVegetationKind::Pole:
	case EVegetationKind::Mature:
	case EVegetationKind::Old:
	{
		const FTreeArchetype* Tree = Definition.FindTree(Instance.Genus);
		const FTreeFormDefinition* Form = Definition.FindForm(Instance.Kind);
		if (!Tree || !Form)
		{
			return false;
		}
		// Breast-height radius x root flare and per-mesh jitter (see FTreeShape and BuildTree).
		Out.RadiusM = Tree->TrunkRadiusM * Form->TrunkRadius * Instance.Scale * 1.6f;
		return true;
	}
	case EVegetationKind::Log:
	{
		float Length = 0.0f;
		float Radius = 0.0f;
		VegetationMeshBuilder::GetLogSize(Instance.GetMeshSpec().GetSeed(), Length, Radius);
		const float Yaw = FMath::DegreesToRadians(Instance.YawDeg);
		const FVector2f Half = FVector2f(FMath::Cos(Yaw), FMath::Sin(Yaw)) * (0.5f * Length * Instance.Scale);
		Out.A = P - Half;
		Out.B = P + Half;
		Out.RadiusM = Radius * Instance.Scale;
		return true;
	}
	case EVegetationKind::Stump:
		Out.RadiusM = VegetationMeshBuilder::GetStumpRadius(Instance.GetMeshSpec().GetSeed()) * Instance.Scale;
		return true;
	default:
		return false;
	}
}

void FVegetationObstacleGrid::Add(const FVegetationObstacle& Obstacle)
{
	const int32 Index = Obstacles.Add(Obstacle);
	// Register in every cell its bounding box touches.
	const FVector2f Min(FMath::Min(Obstacle.A.X, Obstacle.B.X) - Obstacle.RadiusM, FMath::Min(Obstacle.A.Y, Obstacle.B.Y) - Obstacle.RadiusM);
	const FVector2f Max(FMath::Max(Obstacle.A.X, Obstacle.B.X) + Obstacle.RadiusM, FMath::Max(Obstacle.A.Y, Obstacle.B.Y) + Obstacle.RadiusM);
	for (int32 Y = FMath::FloorToInt(Min.Y / CellM); Y <= FMath::FloorToInt(Max.Y / CellM); ++Y)
	{
		for (int32 X = FMath::FloorToInt(Min.X / CellM); X <= FMath::FloorToInt(Max.X / CellM); ++X)
		{
			Cells.FindOrAdd(FIntPoint(X, Y)).Add(Index);
		}
	}
}

void FVegetationObstacleGrid::AddInstances(const FVegetationDefinition& Definition, const TArray<FVegetationInstance>& Instances)
{
	FVegetationObstacle Obstacle;
	for (const FVegetationInstance& Instance : Instances)
	{
		if (MakeObstacle(Definition, Instance, Obstacle))
		{
			Add(Obstacle);
		}
	}
}

bool FVegetationObstacleGrid::Overlaps(const FVector2f& P, float RadiusM) const
{
	const int32 MinX = FMath::FloorToInt((P.X - RadiusM) / CellM);
	const int32 MaxX = FMath::FloorToInt((P.X + RadiusM) / CellM);
	const int32 MinY = FMath::FloorToInt((P.Y - RadiusM) / CellM);
	const int32 MaxY = FMath::FloorToInt((P.Y + RadiusM) / CellM);
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			if (const TArray<int32>* Found = Cells.Find(FIntPoint(X, Y)))
			{
				for (const int32 Index : *Found)
				{
					if (Obstacles[Index].DistanceTo(P) < RadiusM)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

bool FVegetationObstacleGrid::OverlapsCapsule(const FVegetationObstacle& Capsule) const
{
	const float Length = FVector2f::Distance(Capsule.A, Capsule.B);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length / FMath::Max(Capsule.RadiusM * 0.5f, 0.1f)));
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		if (Overlaps(FMath::Lerp(Capsule.A, Capsule.B, static_cast<float>(Step) / Steps), Capsule.RadiusM))
		{
			return true;
		}
	}
	return false;
}
