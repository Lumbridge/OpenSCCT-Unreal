#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The tutorial message's interface of the C# bridge. It mirrors Bridge/OpenScct.UnrealBridge/Hud/HudTutorialExports.cs;
// change both together and bump the version. Fetch it with ScctFeature<FScctHudTutorialApi>() like any other feature
// table.
//
// The message's text crosses as FScctHudState::TutorialId (SHud.sTutoMap: a tutorial map's hint, or an offline
// session's zone tip). While it is up the bridge holds the game message queue, and the HUD holds its briefing lines.
// This table says whether the map is a tutorial (the end banner then says "Finished") and lets an unattended run raise
// a message through the game's own SHud.AddTutoMapMessage.

constexpr int32 ScctHudTutorialApiVersion = 1;

struct FScctHudTutorialState
{
	int32 TutorialMap; // 1 on a tutorial map (TME or TSP)
	int32 Offline;     // 1 when the HUD takes tutorial messages (they are stored only offline)
	int32 Shown;       // 1 while a tutorial message is up
};

struct FScctHudTutorialApi
{
	int32 Version;
	// Fills the state; returns 1 while a session with a local HUD runs.
	int32 (__cdecl* GetState)(FScctHudTutorialState* State);
	// SHud.AddTutoMapMessage(Text) on the local HUD; an empty text clears it. 1 when the HUD now holds that text.
	int32 (__cdecl* ShowMessage)(const TCHAR* Text);
};

static_assert(sizeof(FScctHudTutorialState) == 12, "FScctHudTutorialState must mirror HudTutorialState");
static_assert(sizeof(FScctHudTutorialApi) == 24, "FScctHudTutorialApi must mirror HudTutorialApi");

template <>
struct TScctFeatureInfo<FScctHudTutorialApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudTutorialExports"); }
	static constexpr int32 Version = ScctHudTutorialApiVersion;
};
