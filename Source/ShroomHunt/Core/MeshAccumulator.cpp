#include "Core/MeshAccumulator.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTangents.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "StaticMeshAttributes.h"

using namespace UE::Geometry;

int32 FMeshAccumulator::AddVertex(const FVector3d& Position, const FLinearColor& Color)
{
	Positions.Add(Position);
	Colors.Add(Color);
	return Positions.Num() - 1;
}

void FMeshAccumulator::AddTriangle(int32 A, int32 B, int32 C, const FVector2f& UvA, const FVector2f& UvB, const FVector2f& UvC, uint8 Surface)
{
	if (A == B || B == C || A == C)
	{
		return;
	}
	// Right-handed description, left-handed storage (see the header).
	FTriangle& Tri = Triangles.AddDefaulted_GetRef();
	Tri.Vertices = FIntVector(A, C, B);
	Tri.UVs[0] = UvA;
	Tri.UVs[1] = UvC;
	Tri.UVs[2] = UvB;
	Tri.Surface = static_cast<uint8>(FMath::Min<int32>(Surface, MaxSurfaces - 1));
}

void FMeshAccumulator::AddQuad(int32 A, int32 B, int32 C, int32 D, const FVector2f& UvA, const FVector2f& UvB, const FVector2f& UvC, const FVector2f& UvD, uint8 Surface)
{
	AddTriangle(A, B, C, UvA, UvB, UvC, Surface);
	AddTriangle(A, C, D, UvA, UvC, UvD, Surface);
}

void FMeshAccumulator::Emit(FDynamicMesh3& Out) const
{
	Emit(Out, TConstArrayView<uint8>());
}

void FMeshAccumulator::Emit(FDynamicMesh3& Out, TConstArrayView<uint8> SlotPerSurface) const
{
	Out.Clear();
	Out.EnableAttributes();
	FDynamicMeshAttributeSet* Attributes = Out.Attributes();
	Attributes->SetNumUVLayers(2);
	Attributes->EnablePrimaryColors();
	FDynamicMeshUVOverlay* UV0 = Attributes->GetUVLayer(0);
	FDynamicMeshUVOverlay* UV1 = Attributes->GetUVLayer(1);
	FDynamicMeshColorOverlay* ColorOverlay = Attributes->PrimaryColors();
	FDynamicMeshMaterialAttribute* MaterialIds = nullptr;
	if (SlotPerSurface.Num() > 0)
	{
		Attributes->EnableMaterialID();
		MaterialIds = Attributes->GetMaterialID();
	}

	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		Out.AppendVertex(Positions[Index]);
		const FLinearColor& C = Colors[Index];
		ColorOverlay->AppendElement(FVector4f(C.R, C.G, C.B, C.A));
	}

	// UV1 elements per (vertex, surface): an overlay element belongs to one vertex only, so one shared
	// element per surface made SetTriangle fail silently and left UV1 at zero (the seasonal material
	// could not find the leaves).
	TMap<uint64, int32> SurfaceElements;
	auto GetSurfaceElement = [&](int32 Vertex, uint8 Surface)
	{
		const uint64 Key = (static_cast<uint64>(Vertex) << 8) | Surface;
		if (const int32* Found = SurfaceElements.Find(Key))
		{
			return *Found;
		}
		const int32 Element = UV1->AppendElement(FVector2f((Surface + 0.5f) / MaxSurfaces, 0.5f));
		SurfaceElements.Add(Key, Element);
		return Element;
	};

	// UV0 elements are shared between triangles when (vertex, uv) match, split at seams.
	TMap<FIntVector, int32> UvElements;
	auto GetUvElement = [&](int32 Vertex, const FVector2f& Uv)
	{
		const FIntVector Key(Vertex, FMath::RoundToInt(Uv.X * 8192.0f), FMath::RoundToInt(Uv.Y * 8192.0f));
		if (const int32* Found = UvElements.Find(Key))
		{
			return *Found;
		}
		const int32 Element = UV0->AppendElement(Uv);
		UvElements.Add(Key, Element);
		return Element;
	};

	for (const FTriangle& Tri : Triangles)
	{
		const int32 TriangleId = Out.AppendTriangle(Tri.Vertices.X, Tri.Vertices.Y, Tri.Vertices.Z);
		if (TriangleId < 0)
		{
			continue; // non-manifold edge (two triangles sharing an edge the same way): skipped
		}
		UV0->SetTriangle(TriangleId, FIndex3i(GetUvElement(Tri.Vertices.X, Tri.UVs[0]), GetUvElement(Tri.Vertices.Y, Tri.UVs[1]), GetUvElement(Tri.Vertices.Z, Tri.UVs[2])));
		UV1->SetTriangle(TriangleId, FIndex3i(GetSurfaceElement(Tri.Vertices.X, Tri.Surface), GetSurfaceElement(Tri.Vertices.Y, Tri.Surface),
			GetSurfaceElement(Tri.Vertices.Z, Tri.Surface)));
		ColorOverlay->SetTriangle(TriangleId, FIndex3i(Tri.Vertices.X, Tri.Vertices.Y, Tri.Vertices.Z));
		if (MaterialIds)
		{
			MaterialIds->SetValue(TriangleId, SlotPerSurface.IsValidIndex(Tri.Surface) ? SlotPerSurface[Tri.Surface] : 0);
		}
	}

	FMeshNormals::InitializeOverlayToPerVertexNormals(Attributes->PrimaryNormals(), false);
	FMeshTangentsf::ComputeDefaultOverlayTangents(Out);
}

namespace ShroomMesh
{
	void ToMeshDescription(const FDynamicMesh3& Mesh, TConstArrayView<FName> SlotNames, FMeshDescription& OutDescription)
	{
		FStaticMeshAttributes Attributes(OutDescription);
		Attributes.Register();
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(&Mesh, OutDescription, true);

		// The converter makes polygon group i for material id i; runtime builds match sections by slot name.
		TPolygonGroupAttributesRef<FName> Names = Attributes.GetPolygonGroupMaterialSlotNames();
		for (const FPolygonGroupID GroupId : OutDescription.PolygonGroups().GetElementIDs())
		{
			Names[GroupId] = SlotNames.Num() > 0 ? SlotNames[FMath::Min(GroupId.GetValue(), SlotNames.Num() - 1)] : NAME_None;
		}
	}

	void ToMeshDescription(const FDynamicMesh3& Mesh, FName SlotName, FMeshDescription& OutDescription)
	{
		FStaticMeshAttributes Attributes(OutDescription);
		Attributes.Register();
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(&Mesh, OutDescription, true);

		// Runtime builds match sections to materials by slot name.
		TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		for (const FPolygonGroupID GroupId : OutDescription.PolygonGroups().GetElementIDs())
		{
			SlotNames[GroupId] = SlotName;
		}
	}

	bool WriteObj(const FDynamicMesh3& Mesh, const FString& Path, const FString& Header)
	{
		const FDynamicMeshColorOverlay* Colors = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryColors() : nullptr;
		TStringBuilder<1 << 20> Text;
		Text.Appendf(TEXT("# %s\n"), *Header);
		TMap<int32, int32> ObjIndex;
		int32 Next = 1;
		for (const int32 VertexId : Mesh.VertexIndicesItr())
		{
			const FVector3d P = Mesh.GetVertex(VertexId);
			// FMeshAccumulator writes one color element per vertex.
			const FVector4f C = Colors ? Colors->GetElement(VertexId) : FVector4f(0.8f, 0.8f, 0.8f, 1.0f);
			Text.Appendf(TEXT("v %.4f %.4f %.4f %.3f %.3f %.3f\n"), P.X, P.Y, P.Z, C.X, C.Y, C.Z);
			ObjIndex.Add(VertexId, Next++);
		}
		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			const FIndex3i T = Mesh.GetTriangle(TriangleId);
			Text.Appendf(TEXT("f %d %d %d\n"), ObjIndex[T.A], ObjIndex[T.B], ObjIndex[T.C]);
		}
		return FFileHelper::SaveStringToFile(Text.ToView(), *Path);
	}
}
