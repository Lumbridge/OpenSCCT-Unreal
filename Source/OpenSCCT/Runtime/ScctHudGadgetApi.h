#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The gadget views' interface of the C# bridge, beside the HUD's own table (Runtime/ScctHudApi.h): what the sticky cam
// and camera network pages show that FScctHudState does not carry, and an unattended check's way into the gadget
// menus. It mirrors Bridge/OpenScct.UnrealBridge/Hud/HudGadgetExports.cs; change both together and bump the version.
// Fetch it with ScctFeature<FScctHudGadgetApi>() like any other feature table, and call it once a frame after
// FScctBridgeApi::Update, as the HUD's getters.

constexpr int32 ScctHudGadgetApiVersion = 1;

/** The camera a gadget view looks through (GetCameraView). */
struct FScctHudCameraView
{
	int32 View;            // as FScctHudState::GadgetView: 0 none, 1 sticky cam (spy), 2 camera network (merc)
	float Fov;             // the camera's field of view in degrees; the zoom runs from MaxFov (wide) to MinFov
	float MinFov, MaxFov;
	int32 GasDoses;        // sticky cam gas doses left; -1 for a camera of the network (no gas)
	float GasReadySeconds; // seconds until the next dose can be released, 0 when it can go now
};

/** FScctHudGadgetApi::ForceMenu's menus. */
namespace EScctHudGadgetMenu
{
	constexpr int32 StickyCam = 1; // the spy's sticky cam menu: New Cam, Previous Cam, Exit
	constexpr int32 Mine = 2;      // the merc's mine menu: the mine types, Remove 1st, Exit
}

/** FScctHudGadgetApi::SelectGadget's gadgets. */
namespace EScctHudGadgetKind
{
	constexpr int32 StickyCam = 1;     // SGadStickyCam
	constexpr int32 MineLauncher = 2;  // SGadMultiMinesLauncher
	constexpr int32 CamNetwork = 3;    // SGadCamNetwork
}

struct FScctHudGadgetApi
{
	int32 Version;
	int32 (__cdecl* GetCameraView)(FScctHudCameraView* View); // 1 while the local player looks through a gadget's camera
	// Unattended checks only: selects a gadget (EScctHudGadgetKind) through the gadget keys' own selection; 1 when it
	// is the selected one.
	int32 (__cdecl* SelectGadget)(int32 Kind);
	// Unattended checks only: selects the menu's gadget and puts the local controller in the menu, as holding the
	// gadget's Alt-fire does in the original. The menu acts as soon as Alt-fire is up: the controller has it held for
	// the next tick, and the caller presses the Alt-fire key right away and keeps it down. Returns 1 when the
	// controller is in the menu, 0 when the pawn has no such gadget.
	int32 (__cdecl* ForceMenu)(int32 Menu);
};

// The sizes the C# side has (HudProbe prints them).
static_assert(sizeof(FScctHudCameraView) == 24, "FScctHudCameraView must mirror HudCameraView");
static_assert(sizeof(FScctHudGadgetApi) == 32, "FScctHudGadgetApi must mirror HudGadgetApi");

// The table's bridge class and version, as Runtime/ScctFeatures.h lists the others (kept here so this feature's
// files stay its own).
template <>
struct TScctFeatureInfo<FScctHudGadgetApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudGadgetExports"); }
	static constexpr int32 Version = ScctHudGadgetApiVersion;
};
