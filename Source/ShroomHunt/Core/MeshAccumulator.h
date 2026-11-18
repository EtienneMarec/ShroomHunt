#pragma once

#include "CoreMinimal.h"

struct FMeshDescription;

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/**
 * Collects vertices and triangles of a procedural mesh, then emits an FDynamicMesh3 with
 * per-vertex colors (sRGB albedo), UV0 (surface coordinates, split at seams), UV1 (a surface
 * id, U = (Id + 0.5) / 8) and per-vertex normals and tangents.
 *
 * Shared by the mushroom and vegetation builders.
 */
class SHROOMHUNT_API FMeshAccumulator
{
public:
	static constexpr int32 MaxSurfaces = 8;

	int32 AddVertex(const FVector3d& Position, const FLinearColor& Color);

	/**
	 * Adds a triangle described with the right-handed rule: (B - A) x (C - A) points to the
	 * front side. Unreal is left-handed (GeometryCore normal = (C - A) x (B - A), and that side
	 * is rendered), so the triangle is stored as A, C, B. Degenerate triangles are skipped.
	 */
	void AddTriangle(int32 A, int32 B, int32 C, const FVector2f& UvA, const FVector2f& UvB, const FVector2f& UvC, uint8 Surface);

	template <typename TEnum>
	void AddTriangle(int32 A, int32 B, int32 C, const FVector2f& UvA, const FVector2f& UvB, const FVector2f& UvC, TEnum Surface)
	{
		AddTriangle(A, B, C, UvA, UvB, UvC, static_cast<uint8>(Surface));
	}

	/** Adds a quad A-B-C-D (counter-clockwise seen from the front) as two triangles. */
	void AddQuad(int32 A, int32 B, int32 C, int32 D, const FVector2f& UvA, const FVector2f& UvB, const FVector2f& UvC, const FVector2f& UvD, uint8 Surface);

	int32 GetVertexCount() const { return Positions.Num(); }
	int32 GetTriangleCount() const { return Triangles.Num(); }
	const FVector3d& GetPosition(int32 Vertex) const { return Positions[Vertex]; }
	void SetColor(int32 Vertex, const FLinearColor& Color) { Colors[Vertex] = Color; }
	const FLinearColor& GetColor(int32 Vertex) const { return Colors[Vertex]; }

	void Emit(UE::Geometry::FDynamicMesh3& Out) const;

private:
	struct FTriangle
	{
		FIntVector Vertices;
		FVector2f UVs[3];
		uint8 Surface = 0;
	};

	TArray<FVector3d> Positions;
	TArray<FLinearColor> Colors;
	TArray<FTriangle> Triangles;
};

namespace ShroomMesh
{
	/** Converts a generated mesh to a static mesh description; every polygon group gets SlotName. */
	SHROOMHUNT_API void ToMeshDescription(const UE::Geometry::FDynamicMesh3& Mesh, FName SlotName, FMeshDescription& OutDescription);

	/** Writes a generated mesh as OBJ with per-vertex sRGB colors ("v x y z r g b"), for offline previews. */
	SHROOMHUNT_API bool WriteObj(const UE::Geometry::FDynamicMesh3& Mesh, const FString& Path, const FString& Header);

	/** Interpolates two sRGB colors (the vertex colors of generated meshes are sRGB albedo). */
	FORCEINLINE FLinearColor LerpSRGB(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(FMath::Lerp(A.R, B.R, Alpha), FMath::Lerp(A.G, B.G, Alpha), FMath::Lerp(A.B, B.B, Alpha), FMath::Lerp(A.A, B.A, Alpha));
	}

	/** Multiplies the RGB of an sRGB color, clamped to [0, 1]. */
	FORCEINLINE FLinearColor ScaleSRGB(const FLinearColor& C, float Factor)
	{
		return FLinearColor(FMath::Clamp(C.R * Factor, 0.0f, 1.0f), FMath::Clamp(C.G * Factor, 0.0f, 1.0f), FMath::Clamp(C.B * Factor, 0.0f, 1.0f), C.A);
	}
}
