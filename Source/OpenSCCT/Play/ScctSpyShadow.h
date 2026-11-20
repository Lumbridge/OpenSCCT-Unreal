#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctSpyShadow.generated.h"

class AScctPawnProxy;
class AStaticMeshActor;
class UMaterialInstanceDynamic;
class USkinnedAsset;

USTRUCT()
struct FScctPawnProjection
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<AStaticMeshActor> Projection;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
	TWeakObjectPtr<USkinnedAsset> ResolvedSkeleton;
	TArray<TPair<FName, FName>> PartBones;
	float Darkness = 0.0f;
	FVector ToLight = FVector::UpVector;
};

/** Projects the local spy's pose onto baked surfaces using the map's shadow lighting. */
UCLASS()
class UScctSpyShadow : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void Present(float DeltaSeconds);
	void EndSession();
	bool CreateProjection(FScctPawnProjection& State);
	void Draw(FScctPawnProjection& State, const AScctPawnProxy& Pawn, const FVector& Origin);

	UPROPERTY(Transient) FScctPawnProjection LocalProjection;
	bool bAssetMissing = false;
};
