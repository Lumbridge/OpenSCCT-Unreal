#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The in-match chat's interface of the C# bridge: sending a line typed in the round. It mirrors
// Bridge/OpenScct.UnrealBridge/Hud/HudChatExports.cs; change both together and bump the version. Fetch it with
// ScctFeature<FScctHudChatApi>() like any other feature table.
//
// The line goes out on the running session's own connection (in a local match the in-process server, in an online
// round the visit's server), which relays it back to its recipients, the sender included: what was said arrives as
// a received chat line (FScctHudApi::DrainMessages' Chat and TeamChat messages, and UScctOnlineSubsystem::GetChat in
// a visit). Nothing is shown when Send returns; the transcript fills from what comes back.

constexpr int32 ScctHudChatApiVersion = 1;

struct FScctHudChatApi
{
	int32 Version;
	int32 (__cdecl* Send)(const TCHAR* Text, int32 bTeamOnly); // 1 when handed to the connection; at most 100 characters go out
	int32 (__cdecl* CanSend)();                                  // 1 while a session's connection has joined its server
};

static_assert(sizeof(FScctHudChatApi) == 24, "FScctHudChatApi must mirror HudChatApi");

// The table's bridge class and version, as Runtime/ScctFeatures.h lists the others (kept here so this feature's
// files stay its own).
template <>
struct TScctFeatureInfo<FScctHudChatApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudChatExports"); }
	static constexpr int32 Version = ScctHudChatApiVersion;
};
