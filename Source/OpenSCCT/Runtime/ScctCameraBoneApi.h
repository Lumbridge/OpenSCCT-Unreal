#pragma once

#include "CoreMinimal.h"

// The check of the camera's bone service (Bridge/OpenScct.UnrealBridge/Pose/PoseCameraBones.cs: the mercenary's
// first-person view rides the head bone on a ladder and in the combat states). The service needs nothing from
// Unreal: the simulation asks it during its own tick and the view the controls hand over already carries the answer.
// This table only lets a run look at it. Mirrors Bridge/OpenScct.UnrealBridge/Pose/CameraBoneExports.cs; change both
// together. Every field is 4 bytes.

constexpr int32 ScctCameraBoneApiVersion = 1;

/** Where the local player's view of the last tick stands against the head bone and the cylinder's eye (units). */
struct FScctCameraBoneState
{
	int32 Attached; // 1 while the local camera is attached to a bone
	int32 HasHead;  // 1 when the local pawn's pose has 'B Head'
	float ViewX, ViewY, ViewZ;
	float HeadX, HeadY, HeadZ; // 'B Head' as last drawn
	float EyeX, EyeY, EyeZ;    // the pawn's location plus its eye height and walk bob
	int32 Answered;            // bone lookups the service has answered since the game began
};

static_assert(sizeof(FScctCameraBoneState) == 48, "FScctCameraBoneState must match BridgeCameraBoneState");

struct FScctCameraBoneApi
{
	int32 Version;

	/** Writes the local player's state. Returns 1, or 0 without a session, a local pawn or a view. */
	int32 (__cdecl* GetState)(FScctCameraBoneState* OutState);

	/**
	 * A check only (-ScctKnockCheck): knocks the local player down backwards as a hit does, which attaches the
	 * first-person camera to the head bone. Returns 1 when done. Nothing in normal play calls it.
	 */
	int32 (__cdecl* KnockCheck)();
};

/**
 * -ScctKnockCheck: once a frame of a match. Three seconds in the local player is knocked down, and from one second
 * before that the log gets a line every half second for eight seconds (ScctCameraBone: attached or not, the view, the
 * head bone, how far the view is from the head and from the cylinder's eye). Nothing without the switch.
 */
void ScctRunCameraBoneCheck(float MatchSeconds);
