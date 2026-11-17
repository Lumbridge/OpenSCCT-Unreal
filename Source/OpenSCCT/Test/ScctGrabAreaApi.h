#pragma once

#include "CoreMinimal.h"

// The practice grab-area interface of the C# bridge: where a spy can stand to grab each living mercenary and what
// Use would do now. These mirror Bridge/OpenScct.UnrealBridge/Test/GrabAreaReader.cs and GrabAreaExports.cs field
// for field; change both together and bump the version. Bridge/Probes/GrabAreaProbe prints the C# sizes.
//
// Fetch the table through ScctFeature<FScctGrabAreaApi>() (Runtime/ScctFeatures.h). Call it on the game thread,
// after the frame's UpdateSession (UScctSessionSubsystem::OnFrame). Every field is 4 bytes wide, so the structs
// have no padding.

constexpr int32 ScctGrabAreaApiVersion = 1;

namespace EScctGrabAreaState
{
	constexpr int32 GrabNow = 0;       // the local spy's grab test passes: Use grabs him now
	constexpr int32 OutOfPosition = 1; // he can be grabbed, but not from where the local player stands
	constexpr int32 Refused = 2;       // he cannot be grabbed now: just got up, shocked, or in a state that refuses it
}

/** One living mercenary's grab window as of the last simulation tick. Simulation units are Unreal centimetres. */
struct FScctGrabArea
{
	int32 ActorId;
	int32 State;           // EScctGrabAreaState
	float X, Y, Z;         // the floor under him, lifted 2 cm
	float FacingYaw;       // degrees: where his body faces
	float SectorYaw;       // degrees: the middle of the window, the side a spy stands on
	float Radius;          // cm: how far from him a grab reaches (fGrabDistance)
	float HalfAngle;       // degrees either side of SectorYaw for a spy facing straight at him
	float TurnedHalfAngle; // degrees either side with the spy's body turned by up to the front cone (at most 180)
};
static_assert(sizeof(FScctGrabArea) == 40, "FScctGrabArea must match BridgeGrabArea");

struct FScctGrabAreaApi
{
	int32 Version;

	/** 1 when the running session is a practice round (the level's PracticeRules on), else 0. */
	int32 (__cdecl* IsPracticeRound)();

	/** Writes every living mercenary's grab window. Returns the count written; 0 outside a practice round. */
	int32 (__cdecl* GetGrabAreas)(FScctGrabArea* Areas, int32 Capacity);
};
static_assert(sizeof(FScctGrabAreaApi) == 24, "FScctGrabAreaApi must match GrabAreaApi");
