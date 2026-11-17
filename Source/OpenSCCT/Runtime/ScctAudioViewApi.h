#pragma once

#include "CoreMinimal.h"

// The C# bridge's audio view table: what the listening player's view does to the match's sound this frame, the
// scope's or binoculars' audio zoom with the FOV it is measured against and the actors whose sounds are bound to
// the listener. Mirrors Bridge/OpenScct.UnrealBridge/Events/AudioViewExports.cs; change both together and bump the
// version. Fetched with ScctFeature<FScctAudioViewApi>() (Runtime/ScctFeatures.h).

constexpr int32 ScctAudioViewApiVersion = 1;

struct FScctAudioView
{
	/** The local controller's AudioZoomFactor: 0 outside a scope or binoculars, else 0.33, 0.66 or 1 by zoom step. */
	float Zoom;
	/** The local controller's FovAngle in degrees (horizontal over a 4:3 picture, the zoom in it). */
	float Fov;
	/** The actor the player views (its ViewTarget, else its pawn), 0 for none: its ambient loop moves with the listener. */
	int32 ViewActorId;
	/** The pawn the player controls, 0 for none. */
	int32 LocalPawnId;
	/** 1 when a local controller exists this frame (else Zoom 0 and Fov 90). */
	int32 Valid;
};

static_assert(sizeof(FScctAudioView) == 20, "FScctAudioView must match BridgeAudioView");

struct FScctAudioViewApi
{
	int32 Version;

	/** Fills the audio view of this frame. Returns 1, or 0 when no session runs (the view is then cleared). */
	int32 (__cdecl* GetView)(FScctAudioView* OutView);
};
