#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Runtime/ScctArmouryPreviewApi.h"
#include "ScctArmouryPreviewStage.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UPoseableMeshComponent;
class USceneCaptureComponent2D;
class UTexture2D;
class UTextureRenderTarget2D;

/**
 * The Armoury's small studio (the Godot client's CosmeticPreview): one model on a turntable, lit by lamps of its own
 * and seen by a scene capture into a render target the Armoury page draws (Menu/SScctArmouryPreview.h). The model is
 * the one a match draws: the side's imported character for the suit and the light (the light framed on the head),
 * standing in the stock idle clip the bridge samples, or the imported gun of a gun slot in its rest pose; the camo or
 * tint is the content builder's texture put on each material slot as Play/ScctPawnLookSubsystem puts it on a pawn.
 *
 * The stage stands 4 km above the world's origin in whatever world the page is over (the front end, a lobby, a hub):
 * its meshes are drawn by scene captures only and listed for its own capture, its lamps reach 12 m and light only
 * lighting channel 2, so nothing of it shows in the world and nothing of the world shows in it.
 *
 * Without the imported models (Tools/Import/import_characters.py) HasModel is false and the page shows the item's flat
 * picture. The goggle lights' own mesh is the equipment feature's and is not on the stage: a light item lights the
 * head with a lamp of its colour instead.
 */
UCLASS(NotPlaceable, Transient)
class OPENSCCT_API AScctArmouryPreviewStage : public AActor
{
	GENERATED_BODY()

public:
	AScctArmouryPreviewStage();

	static constexpr int32 TargetWidth = 600;
	static constexpr int32 TargetHeight = 500;

	/** Spawns the stage in a world. */
	static AScctArmouryPreviewStage* Make(UWorld* World);

	/**
	 * Shows a slot of a side (0 spy, 1 merc): PatternItem is the item whose camo or tint the model wears (the body
	 * item for the suit and the light, the slot's own for a gun; empty for retail), LightItem the light item
	 * (empty for retail). Nothing is done when the same is already shown.
	 */
	void Show(int32 Side, int32 Slot, const FString& PatternItem, const FString& LightItem);

	/** Moves the idle clip on and turns the table (not while Held, a drag). */
	void Advance(float DeltaSeconds, bool bHeld);

	/** Turns the table by hand. */
	void Turn(float Degrees);

	bool HasModel() const { return bHasModel; }
	UTextureRenderTarget2D* GetTarget() const { return Target; }

	/** A loose PNG as a texture, read once per run and kept; null when it cannot be read. */
	static UTexture2D* LoadPicture(const FString& Path);

private:
	void LoadModel(const FString& Asset, int32 Kind, int32 View);
	void Frame(int32 View);
	void Dress(const FString& Pattern);

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Turntable;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> Body;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Lamps;

	/** The lamp of a light item's colour, at the head. */
	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	/** The materials the model came with, by slot. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Sources;

	FString ShownAsset;
	FString ShownKey;
	int32 ShownView = INDEX_NONE;
	/** 0 spy, 1 merc when the model is a character (it is posed), INDEX_NONE for a gun. */
	int32 PosedKind = INDEX_NONE;
	TArray<int32> BoneMap;
	TArray<FScctBridgeBone> Pose;
	float Time = 0.0f;
	float Yaw = -60.0f;
	bool bHasModel = false;
};
