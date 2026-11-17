#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The network readout's interface of the C# bridge. It mirrors Bridge/OpenScct.UnrealBridge/Hud/HudNetExports.cs; change
// both together and bump the version. Fetch it with ScctFeature<FScctHudNetApi>() like any other feature table.
//
// The bridge hands over the running totals of the local player's link (Godot's NetReadout, as PlayRoot.ReadNet fills it);
// UScctTelemetrySubsystem works the rates out over a one-second window, as Godot's HudNet does, and starts over when a
// total goes down (a new connection). The frame-rate readout is Unreal's own.

constexpr int32 ScctHudNetApiVersion = 1;

struct FScctHudNetState
{
	int32 Connected;      // 1 while the session has a connection to its server
	int32 HasRtt;         // 1 once a round trip was measured (a loopback to a local host never is)
	int32 IsHost;         // 1 on a listen host (the player's own connection is the loopback)
	int32 ServerNetRate;  // snapshots a second

	int64 RttMicros;
	int64 JitterMicros;
	int64 InBytes;
	int64 OutBytes;
	int64 InPackets;
	int64 InPacketsLost;
	int64 OutPackets;
	int64 OutPacketsLost;
	int64 Corrections;    // position corrections the prediction applied

	int32 Clients;           // on a host: the other players with a measured ping
	int32 BestClientPingMs;
	int32 WorstClientPingMs;
	int32 Unmeasured;        // on a host: the other players without one (bots)

	float BufferMs;           // the interpolation delay, ms
	float CorrectionDistance; // how far the last correction moved the pawn, Unreal units
	float SnapshotAgeMs;      // how old the newest pose snapshot is at the drawn moment
	int32 Reserved;

	int64 BufferUnderflows;
	int64 MovesReplayed;
	int64 AnimationGaps;
	int64 PoseResets;
	int64 PredictionResets;
	int64 StateGaps;
};

struct FScctHudNetApi
{
	int32 Version;
	// Fills the totals; 1 while a session with a connection runs, 0 otherwise (the state is then zeroed).
	int32 (__cdecl* GetState)(FScctHudNetState* State);
};

static_assert(sizeof(FScctHudNetState) == 168, "FScctHudNetState must mirror HudNetState");
static_assert(sizeof(FScctHudNetApi) == 16, "FScctHudNetApi must mirror HudNetApi");

template <>
struct TScctFeatureInfo<FScctHudNetApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudNetExports"); }
	static constexpr int32 Version = ScctHudNetApiVersion;
};
