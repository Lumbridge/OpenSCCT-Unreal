#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctWindowLighting.generated.h"

class UScctSessionSubsystem;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
namespace GLTF { struct FAsset; }

struct FScctWindowEdit
{
	int32 Name, Mesh, Primitive, Element;
	float R, G, B;
	int32 HasPoint;
	float X, Y, Z, NX, NY, NZ;
};

USTRUCT()
struct FScctWindowMaterial
{
	GENERATED_BODY()
	TWeakObjectPtr<UStaticMeshComponent> Component;
	int32 Slot = 0;
	UPROPERTY() TObjectPtr<UMaterialInterface> Original;
	UPROPERTY() TObjectPtr<UMaterialInterface> Replacement;
};

USTRUCT()
struct FScctWindowMesh
{
	GENERATED_BODY()
	TWeakObjectPtr<UStaticMeshComponent> Component;
	UPROPERTY() TObjectPtr<UStaticMesh> Original;
	UPROPERTY() TObjectPtr<UStaticMesh> Replacement;
};

/** Applies the editor's sparse window-light response to this session's displayed lightmaps and placed meshes. */
UCLASS()
class UScctWindowLighting : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	void Update(float Delta);
	void Restore();
	void EndSession();
	void Apply(const TArray<FScctWindowEdit>& Rows);
	UPROPERTY() TArray<FScctWindowMaterial> Materials;
	UPROPERTY() TArray<FScctWindowMesh> Meshes;
	TSharedPtr<GLTF::FAsset> Geometry;
};
