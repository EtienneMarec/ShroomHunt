#include "Core/RuntimeMesh.h"

#include "Core/MeshAccumulator.h"
#include "Core/ShroomLog.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"

namespace ShroomMesh
{
	UStaticMesh* CreateTransientStaticMesh(const UE::Geometry::FDynamicMesh3& Mesh, UMaterialInterface* Material, FName MeshName, const TArray<FBox>* CollisionBoxes)
	{
		static const FName Slot(TEXT("Main"));
		FMeshDescription Description;
		ToMeshDescription(Mesh, Slot, Description);

		UStaticMesh* StaticMesh = NewObject<UStaticMesh>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), MeshName), RF_Transient);
		StaticMesh->GetStaticMaterials().Add(FStaticMaterial(Material, Slot, Slot));

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bBuildSimpleCollision = false;
		Params.bCommitMeshDescription = false;
		Params.bMarkPackageDirty = false;
		Params.bAllowCpuAccess = false;
		if (!StaticMesh->BuildFromMeshDescriptions({ &Description }, Params))
		{
			UE_LOG(LogShroomMesh, Error, TEXT("Failed to build runtime mesh %s"), *MeshName.ToString());
			return nullptr;
		}

		if (CollisionBoxes && CollisionBoxes->Num() > 0)
		{
			StaticMesh->CreateBodySetup();
			if (UBodySetup* Body = StaticMesh->GetBodySetup())
			{
				Body->AggGeom.BoxElems.Reset();
				for (const FBox& Box : *CollisionBoxes)
				{
					FKBoxElem Element(Box.GetSize().X, Box.GetSize().Y, Box.GetSize().Z);
					Element.Center = Box.GetCenter();
					Body->AggGeom.BoxElems.Add(Element);
				}
				Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
				Body->InvalidatePhysicsData();
				Body->CreatePhysicsMeshes();
			}
		}
		return StaticMesh;
	}
}
