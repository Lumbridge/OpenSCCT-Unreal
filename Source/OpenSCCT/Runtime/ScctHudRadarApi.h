#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The radar's interface of the C# bridge, beside the HUD's own table (Runtime/ScctHudApi.h, whose GetRadar gives the
// blips): the heart sensor's cone with the frame of its clip, and an unattended check's way to select the sensor. The
// bridge also plays the cone's scan ping and heartbeat (as the original's HUD does) through the events feature, so
// they come out of the audio subsystem like every other sound. It mirrors
// Bridge/OpenScct.UnrealBridge/Hud/HudRadarExports.cs; change both together and bump the version. Fetch it with
// ScctFeature<FScctHudRadarApi>() and call it once a frame after FScctBridgeApi::Update, as the HUD's getters.

constexpr int32 ScctHudRadarApiVersion = 1;

/** The heart beat sensor's cone (GetHeartSensor). */
struct FScctHudHeartSensor
{
	int32 Cone;          // as FScctHudState::HeartCone: 0 hidden, 1 idle, 2 scanning, 3 detected
	float Frame;         // the shown cone's clip frame, 10 a second, from 0 up to LoopFrames (the wave's place)
	int32 LoopFrames;    // 20 for the idle cone (A_Heart), 18 for the detected one (A_Heart1); 0 when none is shown
	int32 CheckSounds;   // scan pings asked for this session (one each time the shown cone wraps)
	int32 DetectedSounds; // heartbeats asked for (at most every 1.5 s while something is detected)
	int32 SoundsRaised;  // of those, the calls raised as sound events
};

struct FScctHudRadarApi
{
	int32 Version;
	// The heart sensor after the last tick; 1 when a cone is shown.
	int32 (__cdecl* GetHeartSensor)(FScctHudHeartSensor* State);
	// Unattended checks only: selects the local pawn's heart beat sensor with the gadget keys; 1 when it is selected.
	int32 (__cdecl* SelectHeartSensor)();
};

// The sizes the C# side has (HudProbe prints them).
static_assert(sizeof(FScctHudHeartSensor) == 24, "FScctHudHeartSensor must mirror HudHeartSensorState");
static_assert(sizeof(FScctHudRadarApi) == 24, "FScctHudRadarApi must mirror HudRadarApi");

// The table's bridge class and version, as Runtime/ScctFeatures.h lists the others (kept here so this feature's
// files stay its own).
template <>
struct TScctFeatureInfo<FScctHudRadarApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudRadarExports"); }
	static constexpr int32 Version = ScctHudRadarApiVersion;
};
