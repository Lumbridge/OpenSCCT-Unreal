#pragma once

#include "CoreMinimal.h"

// The pose's tie to the frame's camera: the local spy's third-person body aims at the view drawn this frame (the
// look-ahead, the mouse turn the simulation has not taken yet), the spy's eye view says which of its sockets a wall
// hides from the eye, and the merc's view model says the frame rate each clip ran at. Mirrors
// Bridge/OpenScct.UnrealBridge/Pose/PoseAimExports.cs; change both together.

constexpr int32 ScctPoseAimApiVersion = 1;

/** What the last pose of a pawn aimed at. */
struct FScctAimProbe
{
	float BodyYaw;      // the body's yaw as drawn, degrees
	float PosedViewYaw; // the view yaw the drawn upper body stands for, degrees
	float TickViewYaw;  // the same for the aim interpolated between the last two ticks alone
	float PosedPitch, TickPitch; // the drawn and the tick upper-body pitch, degrees
	int32 FromView;     // 1 when the pose aimed at the drawn view handed over this frame
};
static_assert(sizeof(FScctAimProbe) == 24, "FScctAimProbe must match BridgeAimProbe");

/** One view model channel's clip and the frame rate it ran at in the last draw. */
struct FScctClipRate
{
	int32 Channel;
	float Fps;
	int32 FromProject; // 1 when an animation project's gameplay clip set the rate, 0 for the stock 15 fps
	TCHAR Name[32];    // the clip, zero-terminated
};
static_assert(sizeof(FScctClipRate) == 76, "FScctClipRate must match BridgeClipRate");

namespace EScctHiddenSocket
{
	constexpr int32 Muzzle = 1; // "FX"
	constexpr int32 Laser = 2;  // "Laser"
}

struct FScctPoseAimApi
{
	int32 Version;

	/**
	 * Before this frame's poses (GetPawnPose): the view drawn this frame, degrees (the controls' AimPitch and AimYaw),
	 * for the local spy ActorId; 0 hands none over. Only the local player's own spy takes it. 1 when taken.
	 */
	int32 (__cdecl* SetDrawnView)(int32 ActorId, float Pitch, float Yaw, float Roll);

	/** What the last pose of ActorId aimed at. 1, or 0 without a pose. */
	int32 (__cdecl* GetAimProbe)(int32 ActorId, FScctAimProbe* OutProbe);

	/**
	 * After GetFirstPerson: the sockets of this frame's spy eye view that a wall hides from the eye
	 * (EScctHiddenSocket bits), and the muzzle socket's distance from the eye in units (may be null).
	 */
	int32 (__cdecl* GetFirstPersonSockets)(float* OutMuzzleDistance);

	/** After GetFirstPerson: the clips the merc's view model drew this frame and their frame rates. The count written. */
	int32 (__cdecl* GetViewModelClipRates)(FScctClipRate* OutRates, int32 Capacity);
};
