#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The team ping's interface of the C# bridge: whether the local player can ping now, sending a ping aimed through the
// camera Unreal draws, and the team's pings to draw. It mirrors Bridge/OpenScct.UnrealBridge/Hud/TeamPingExports.cs;
// change both together and bump the version. Fetch it with ScctFeature<FScctTeamPingApi>() like any other feature table.
//
// A ping goes out on the running session's own connection (in a local match the in-process server, in an online round
// the visit's server). The server checks it against the sender's view and the world, at most one a second, and sends it
// to the whole team, the sender included: what was sent comes back as a row of GetPings, like a teammate's.

constexpr int32 ScctTeamPingApiVersion = 1;

/** The camera a ping is aimed through: its location and its rotation in degrees, as Unreal has them. */
struct FScctTeamPingCamera
{
	float X, Y, Z;
	float Pitch, Yaw, Roll;
};

/** Whether a ping can be placed, and the counts of what was sent and received (GetState). */
struct FScctTeamPingState
{
	int32 Context;   // a number for the pawn, the view's source and the team, never 0; 0 while no ping can be placed
	int32 Team;      // 0 spies, 1 mercs; -1 while no ping can be placed
	int32 Supported; // 1 when the server takes team pings (it offers them in every mode but Merc Deathmatch and the hub)
	int32 Sent;      // pings handed to the connection this session
	int32 Received;  // pings of the team received (the player's own echo included)
	int32 Refused;   // sends refused here: a kind the team does not have, or no spot found
	float TargetX, TargetY, TargetZ; // where the last ping sent was aimed
	int32 LastKind;
};

/** The kinds of ping (the shared protocol's TeamPingKind). Co-op boost and Mine are the spies' only. */
namespace EScctTeamPingKind
{
	constexpr int32 Generic = 0;   // a tap: "here"
	constexpr int32 Watch = 1;     // the wheel's top: watch here
	constexpr int32 Check = 2;     // the wheel's right: check here
	constexpr int32 CoopBoost = 3; // the wheel's bottom (spies): co-op boost
	constexpr int32 Mine = 4;      // the wheel's left (spies): a mine
	constexpr int32 Count = 5;
}

/** One teammate's ping (GetPings). */
struct FScctTeamPingRow
{
	int32 Kind;          // EScctTeamPingKind
	int32 Team;          // the team it was placed for
	int32 SenderId;      // the sender's player slot on the server (not a replicated player id)
	int32 SenderNameId;  // the player's own name as a HUD text id (FScctHudApi::GetText) on his own pings; 0 on a teammate's (the client knows no other slot's name)
	float X, Y, Z;       // the marked spot in the world
	float Age;           // seconds since the server accepted it; it lives 4 s
	float Opacity;       // 1, fading to 0 over its last 0.75 s
	int32 Own;           // 1 for the local player's own ping
};

struct FScctTeamPingApi
{
	int32 Version;
	// Fills the state; returns its Context (0 when no ping can be placed now).
	int32 (__cdecl* GetState)(FScctTeamPingState* State);
	// Aims a ping of Kind through the camera (the trace from its centre, 40 m, else 12 m ahead) and sends it; the spot
	// goes to Target (3 floats) when given. 1 sent, 0 no ping can be placed now, -1 a kind the team does not have, -2 no spot.
	int32 (__cdecl* Send)(int32 Kind, const FScctTeamPingCamera* Camera, float* Target);
	// The team's live pings, oldest first; returns the count written.
	int32 (__cdecl* GetPings)(FScctTeamPingRow* Rows, int32 Capacity);
};

static_assert(sizeof(FScctTeamPingCamera) == 24, "FScctTeamPingCamera must mirror TeamPingCamera");
static_assert(sizeof(FScctTeamPingState) == 40, "FScctTeamPingState must mirror TeamPingState");
static_assert(sizeof(FScctTeamPingRow) == 40, "FScctTeamPingRow must mirror TeamPingRow");
static_assert(sizeof(FScctTeamPingApi) == 32, "FScctTeamPingApi must mirror TeamPingApi");

// The table's bridge class and version, as Runtime/ScctFeatures.h lists the others (kept here so this feature's
// files stay its own).
template <>
struct TScctFeatureInfo<FScctTeamPingApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.TeamPingExports"); }
	static constexpr int32 Version = ScctTeamPingApiVersion;
};
