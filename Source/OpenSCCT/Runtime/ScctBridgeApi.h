#pragma once

#include "CoreMinimal.h"

// The flat interface of the C# bridge. These mirror Bridge/OpenScct.UnrealBridge/BridgeTypes.cs and Exports.cs
// field for field; change both together and bump the version. The static_asserts at the end hold the sizes the
// C# side has (Bridge/BridgeProbe prints and checks the same numbers).
//
// Getting the table (type OpenScct.UnrealBridge.Exports; all __stdcall, as every [UnmanagedCallersOnly] default):
//   int32 GetApiVersion()                              ask this first; it writes nothing. If it is not
//                                                      ScctBridgeApiVersion, stop with "rebuild the bridge" and do
//                                                      not ask for the table.
//   int32 GetApiSized(FScctBridgeApi* Api, int32 Size) pass a zeroed table and sizeof(FScctBridgeApi). The bridge
//                                                      copies at most Size bytes, so a longer table on its side
//                                                      cannot run over this one. Returns the version, 0 when it
//                                                      wrote nothing.
//   int32 GetApi(FScctBridgeApi* Api)                  the hand-over of versions 1 and 2, kept for callers built
//                                                      against them: it writes only the first 104 bytes (version
//                                                      and the twelve entries of version 2). Do not use it here.
// The table only grows at its end.
//
// One session plays at a time, of either kind: a local match (StartLocal / StartLocalEx) or a round on a remote
// server, which the online table (ScctOnlineApi.h) puts in the same slot when its visit enters a round. Every
// per-frame entry of every Scct*Api.h table reads that one session, so both kinds are drawn through the same calls.
// GetSession gives its id; a changed id is a new match.

constexpr int32 ScctBridgeApiVersion = 3;
constexpr int32 ScctButtonCount = 20;

/** Indices into FScctBridgeInput::Buttons, in the order of the simulation's LocalInput.ButtonNames. */
enum class EScctButton : uint8
{
	Up, Down, Left, Right, PrevItem, NextItem, Fire, AltFire, Crouch, Roll, Using, FireMode, BackToWall, Reload,
	RateOfFire, HoldBreath, SpecialsChat, ZoomIn, ZoomOut, Cloak,
};

/** One pawn as it should be drawn this frame. Simulation units are Unreal centimetres, same axes. */
struct FScctBridgePawn
{
	int32 ActorId;
	int32 Kind; // 0 spy, 1 merc
	int32 Team;
	int32 Health;
	int32 Flags; // EScctPawnFlags
	float X, Y, Z;
	float PrePivotX, PrePivotY, PrePivotZ;
	float Yaw; // degrees
	float CollisionRadius, CollisionHeight;
	float AimYaw, AimPitch; // upper-body aim in degrees, already in the pose
};

/** One bone of a character's skeleton, in the order of the character file's skin joints. */
struct FScctBridgeBoneInfo
{
	int32 Parent; // -1 for a root
	TCHAR Name[64];
};

/**
 * One bone of a drawn pose: its transform relative to its parent, in the character file's own space (metres,
 * Y up, right-handed). FScctPose::ToUnreal converts it.
 */
struct FScctBridgeBone
{
	float Px, Py, Pz;
	float Qx, Qy, Qz, Qw;
	float Sx, Sy, Sz;
};

namespace EScctPawnFlags
{
	constexpr int32 Visible = 1;
	constexpr int32 Local = 2;
	// Version 3: the pawn is seen from its own eyes this frame (FScctBridgeView::HideLocalBody, on the local pawn).
	// Take its body out of the main view but keep its shadow (SetHiddenInGame with bCastHiddenShadow).
	constexpr int32 FirstPerson = 4;
}

/**
 * The local player's camera this frame, between the last two ticks with no look-ahead. Angles in degrees, -180 to
 * 180. The camera to draw is the controls table's GetView (ScctControlsApi.h), which adds the mouse turn not yet
 * simulated; this one carries HideLocalBody.
 */
struct FScctBridgeView
{
	float X, Y, Z;
	float Pitch, Yaw, Roll;
	float Fov;
	int32 Valid;
	int32 SpyFirstPerson;
	// Version 3: 1 when the local pawn is seen from its own eyes (no free camera, and no behind view or the spy's
	// first-person view): hide its body in the main view, keep its shadow. A merc in a behind view keeps his body.
	int32 HideLocalBody;
};

struct FScctBridgeInput
{
	uint8 Buttons[ScctButtonCount];
	float MouseX, MouseY; // counts since the last call, Y positive up
	float Up;
};

enum class EScctBridgeState : int32
{
	Idle = 0,
	Playing = 1,
	Failed = 2, // the last start failed or the session stopped on an error (LastError); ends with a stop or the next start
};

namespace EScctSessionFlags
{
	constexpr int32 Remote = 1; // a round on a remote server; clear for a local match
	constexpr int32 Merc = 2;   // the local player is a merc (his pawn's side once he has one, else the side asked for)
}

/** What GetSession says about the session in the main slot. */
struct FScctBridgeSessionInfo
{
	int32 Id;    // a new number for every local match and every online round, never 0, never reused; 0: none plays
	int32 State; // EScctBridgeState
	int32 Flags; // EScctSessionFlags
	int32 Ticks; // client simulation ticks run so far
	TCHAR Map[32]; // the map's folder name ("AquaE")
};

namespace EScctStartFlags
{
	constexpr int32 Sidearms = 1;     // the mercs carry OpenSCCT's sidearms
	constexpr int32 Rifles = 2;       // the mercs may pick OpenSCCT's additional rifles
	constexpr int32 OfflineRules = 4; // practice rules: no time limit, unlimited lives, 5 s respawn
	constexpr int32 ResponsiveSmoke = 16; // smoke grenades fill the shared simulated cloud (SvmResponsiveSmoke)
	constexpr int32 UnlimitedGear = 8; // Visit a Map practice: unlimited gear, instant gear changes, Switch Team
}

/** What StartLocalEx starts. Zero it, set Size = sizeof(FScctBridgeStartOptions), then the fields. */
struct FScctBridgeStartOptions
{
	int32 Size;
	int32 bMerc;
	int32 Bots;
	int32 Flags;      // EScctStartFlags
	int32 Difficulty; // the bots' skill: 0 default (normal), 1 easy, 2 normal, 3 hard, 4 expert
	TCHAR Map[32];
	TCHAR Mode[32];       // Story, DiskHunt, Deathmatch, MercDeathMatch; empty: Story
	TCHAR PlayerName[48]; // empty: "Player"

	// The player's own rules over the map's, each one higher than its value so that zero keeps the map's:
	// 0 the map's own, 1 none or unlimited, n the value n - 1.
	int32 TimeLimitPlusOne; // minutes
	int32 SpyLivesPlusOne;  // not used in Merc Deathmatch
	int32 MercLivesPlusOne;
	int32 RespawnPlusOne;   // seconds

	// The player count the map's match-size rules lay the match out for; 0 for the bots and the local player.
	int32 MatchPlayers;
};

struct FScctBridgeApi
{
	int32 Version;
	int32 (__cdecl* Configure)(const TCHAR* ContentRoot);
	int32 (__cdecl* StartLocal)(const TCHAR* Map, const TCHAR* Mode, int32 bMerc, int32 Bots);
	void (__cdecl* Stop)();
	int32 (__cdecl* GetState)();
	int32 (__cdecl* Update)();
	void (__cdecl* SetInput)(const FScctBridgeInput* Input);
	void (__cdecl* Command)(const TCHAR* Name, int32 Arg);
	int32 (__cdecl* GetPawns)(FScctBridgePawn* Pawns, int32 Capacity);
	int32 (__cdecl* GetView)(FScctBridgeView* View);
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);

	// Version 2
	int32 (__cdecl* GetSkeleton)(int32 Kind, FScctBridgeBoneInfo* Bones, int32 Capacity);
	int32 (__cdecl* GetPawnPose)(int32 ActorId, FScctBridgeBone* Bones, int32 Capacity);

	// Version 3
	// A local match with every option. Returns the new session's id, 0 on failure (LastError).
	int32 (__cdecl* StartLocalEx)(const FScctBridgeStartOptions* Options);
	// The session in the slot, local or online. Returns its id, 0 when none plays. Info may be null.
	int32 (__cdecl* GetSession)(FScctBridgeSessionInfo* Info);
	// Stop and Update for the session with that id only. A session that is gone or was replaced is left alone:
	// StopSession returns 0, UpdateSession -1. Use these from a world's clean-up and tick, so one world cannot end
	// or drive another's match. Plain Stop ends whatever plays; plain Update drives whatever plays.
	int32 (__cdecl* StopSession)(int32 SessionId);
	int32 (__cdecl* UpdateSession)(int32 SessionId);
	// For ShutdownModule: leaves the online server and ends the session without ever throwing, and gives up after
	// TimeoutMs (0: 3000) if the match's server thread does not stop. Returns 1 when all stopped in time.
	int32 (__cdecl* Shutdown)(int32 TimeoutMs);
};

// The entries above as of version 3:
// - Update returns the client ticks run since the last Update, whoever ran them: during an online round the online
//   table's Update runs the ticks earlier in the frame and this still reports them. Call Online.Update, then this.
// - Configure returning 0 does not leave the bridge Failed; only a failed start or a session error does.
// - A session that fails is stopped at once (its server thread ends); GetState is Failed until Stop, StopSession
//   or the next start, and GetSession's id is 0.
// - StartLocal still returns 1 or 0; ask GetSession for the id, or use StartLocalEx.
// - FScctBridgePawn::Team is the replicated team of every pawn (0 spies, 1 mercs), other players' included.
// - Pawn yaw and the view's angles are within -180 to 180.

static_assert(sizeof(FScctBridgePawn) == 64, "FScctBridgePawn must match BridgePawn");
static_assert(sizeof(FScctBridgeBoneInfo) == 132, "FScctBridgeBoneInfo must match BridgeBoneInfo");
static_assert(sizeof(FScctBridgeBone) == 40, "FScctBridgeBone must match BridgeBone");
static_assert(sizeof(FScctBridgeView) == 40, "FScctBridgeView must match BridgeView");
static_assert(sizeof(FScctBridgeInput) == 32, "FScctBridgeInput must match BridgeInput");
static_assert(sizeof(FScctBridgeSessionInfo) == 80, "FScctBridgeSessionInfo must match BridgeSessionInfo");
static_assert(sizeof(FScctBridgeStartOptions) == 264, "FScctBridgeStartOptions must match BridgeStartOptions");
static_assert(sizeof(FScctBridgeApi) == 8 + 17 * sizeof(void*), "FScctBridgeApi must match BridgeApi (64-bit)");
static_assert(sizeof(TCHAR) == 2, "the bridge's strings are UTF-16");

namespace FScctPose
{
	/**
	 * A bone transform of the character file as Unreal's importer stores the same skeleton: the importer swaps Y
	 * and Z and scales metres to centimetres, which mirrors the space, so rotations flip with it.
	 */
	inline FTransform ToUnreal(const FScctBridgeBone& Bone)
	{
		return FTransform(
			FQuat(-Bone.Qx, -Bone.Qz, -Bone.Qy, Bone.Qw),
			FVector(Bone.Px, Bone.Pz, Bone.Py) * 100.0,
			FVector(Bone.Sx, Bone.Sz, Bone.Sy));
	}
}
