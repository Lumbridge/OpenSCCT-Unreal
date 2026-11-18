#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "ScctEchelonLightFilter.generated.h"

class ULightComponent;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class FSceneView;
class ISceneViewExtension;

/**
 * Routes the common LE_ESpotShadow exclusion through native lighting channels. Other receiver
 * lists share a primary-view depth capture for the remaining opaque/masked exclusions.
 * Ordinary lights reach both world channels. Actor-only lighting keeps its own channel.
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
	void OnLightPlanDressed() { bLightPlanReady = true; }
	void CaptureVisibleProjectors(const FSceneView& View);
	bool AddedToUnshapedLight(const ULightComponent* Light) const;

	// Components added to an existing actor after its initial registration.
	static void RouteNewWorldLight(ULightComponent& Light);
	void RouteReceiverComponent(UPrimitiveComponent& Mesh);
	// Keep level-state animation ownership independent of the installed depth filter.
	UMaterialInterface* HoldSteady(ULightComponent& Light, FName Parameter, float Value);

private:
	void RouteLight(ULightComponent& Light);
	void RestoreRouting();
	void Uninstall();
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
	TSet<FName> NativeReceivers;
	TSet<TWeakObjectPtr<ULightComponent>> NativeProjectors;
	TSet<TWeakObjectPtr<ULightComponent>> MirroredLights;
	TSet<TWeakObjectPtr<UPrimitiveComponent>> RoutedMeshes;
	bool bLightPlanReady = false;
	bool bNativeAllowed = false;
	TArray<TWeakObjectPtr<USceneCaptureComponent2D>> Captures;
	TMultiMap<FName, int32> CapturesByReceiver;
	TArray<TWeakObjectPtr<AActor>> Pending;
	FDelegateHandle SpawnHandle;
	TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
	bool bInstalled = false;
};
