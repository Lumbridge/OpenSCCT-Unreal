#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "ScctEchelonLightFilter.generated.h"

class ULightComponent;
class UMaterialInterface;
class USceneCaptureComponent2D;
class FSceneView;
class ISceneViewExtension;

/**
 * Two placements of the same mesh can reject different LE_ESpotShadow lights. For each distinct list of
 * rejected receivers the primary view can draw their depth once. A projector compares that depth with the
 * opaque/masked surface it is lighting. This keeps the normal shadow pass and the vision stencil independent.
 * Forward translucency and auxiliary scene captures do not consume the primary view's receiver depth.
 */
UCLASS()
class UScctEchelonLightFilter : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void Apply();
	void CaptureVisibleProjectors(const FSceneView& View);
	bool AddedToUnshapedLight(const ULightComponent* Light) const;

private:
	void RegisterMap(const FString& Folder);
	void QueueActor(AActor* Actor);
	void AddReceiver(AActor& Actor);
	void Clear();

	struct FLightState
	{
		TWeakObjectPtr<ULightComponent> Light;
		TStrongObjectPtr<UMaterialInterface> Original;
		bool bMegaLights;
		int32 Capture;
	};
	TArray<FLightState> Lights;
	TArray<TWeakObjectPtr<USceneCaptureComponent2D>> Captures;
	TMultiMap<FName, int32> CapturesByReceiver;
	TArray<TWeakObjectPtr<AActor>> Pending;
	FDelegateHandle SpawnHandle;
	TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
	bool bInstalled = false;
};
