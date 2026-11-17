#pragma once

#include "CoreMinimal.h"

class APostProcessVolume;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UScctSessionSubsystem;

/**
 * The X-ray view of a demo being watched (a port of the Godot client's game/Scripts/Play/Demo/DemoXRayView.cs and
 * game/Shaders/svm_demo_xray.gdshader): every living player's body is outlined in his team's colour (the HUD's spy
 * green, merc orange) and the outline is seen through walls, so a viewer can follow everyone at once. The body seen
 * from its own eyes (the first-person camera's) is left out, as Godot leaves out a body drawn only for its shadow.
 *
 * Godot draws the posed bodies alone into a mask and lays a two-pixel rim of it over the picture. Here each body is
 * drawn into the custom depth and stencil too (stencil 16 spy team, 17 merc team; the custom depth pass is not
 * hidden by the walls), and the post-process material /Game/OpenSCCT/Demo/M_ScctDemoXRay (built by
 * Tools/Materials/Build-DemoXRayMaterial.ps1) draws the rim round every marked silhouette. Turning it off gives the
 * bodies back unmarked. Owned by UScctDemoPlayback, which turns it on from the playback bar's X-RAY, the X key or
 * -ScctDemoXRay; the choice is kept across seeks and demos, as Godot's DemoPlayer.XRay is.
 */
class FScctDemoXRay
{
public:
	static constexpr int32 SpyStencil = 16;
	static constexpr int32 MercStencil = 17;

	~FScctDemoXRay() { Release(); }

	/** Once a frame while a demo plays: marks the bodies of Session's pawns (or gives them back when off). */
	void Update(UScctSessionSubsystem* Session, bool bOn);

	/** Gives every marked body back and takes the outline out of the picture. */
	void Release();

	/** Bodies marked by the last Update. */
	int32 GetMarked() const { return LastMarked; }

private:
	bool EnsureVolume(UWorld* World);

	TWeakObjectPtr<APostProcessVolume> Volume;
	TWeakObjectPtr<UMaterialInstanceDynamic> Material;
	TWeakObjectPtr<UWorld> VolumeWorld;
	bool bMaterialMissing = false;
	bool bShown = false;

	/** The bodies this marked, so only they are given back. */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> MarkedBodies;
	int32 LastMarked = -1;
	int32 LastSpies = -1;
};
