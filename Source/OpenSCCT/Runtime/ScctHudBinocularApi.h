#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The binocular pages' interface of the C# bridge, beside the HUD's own table (Runtime/ScctHudApi.h, whose snapshot
// carries the binocular mode, the zoom step, the lock texts and GetBinocularTargets): the spy's P_BinocularsATT page of
// the content evaluated at a moment (goggle frame, sound meter, zoom scale, key help, speak box) as layers to paint, and
// an unattended check's way to turn the view onto a target. It mirrors
// Bridge/OpenScct.UnrealBridge/Hud/HudBinocularExports.cs; change both together and bump the version. Fetch it with
// ScctFeature<FScctHudBinocularApi>() like any other feature table, and call it once a frame after
// FScctBridgeApi::Update, as the HUD's getters.

constexpr int32 ScctHudBinocularApiVersion = 1;

/** A picture or a text of a Magma page, in units of the 640 x 480 page (GetSpyPage). */
struct FScctHudPageLayer
{
	int32 Kind;              // 0 an image, 1 a text
	int32 Align;             // a text's alignment in its box: 0 left, 1 centre, 2 right
	TCHAR Package[16];       // an image's package and texture, zero ended, as UScctHudTextures::Find takes them
	TCHAR Texture[32];
	TCHAR Text[96];          // a text's words, zero ended
	float L, T, R, B;        // the box before its rotation
	float Rotation;          // degrees about the box's centre, clockwise on screen
	float U0, V0, U1, V1;    // an image's texel region (U1 left of U0 shows it mirrored)
	float Red, Green, Blue, Alpha; // the colour, 0..1 as the content gives it (sRGB), the parents' tint applied
	float Height;            // a text's height in page units
	float ShadowRed, ShadowGreen, ShadowBlue, ShadowAlpha;
	float ShadowX, ShadowY;  // where a text's shadow sits from it, in page units (y down)
	float Tracking;          // a text's letter spacing in thousandths of its height
};

/** What the spy page shows this frame beyond its own keyframes (GetSpyPage). */
struct FScctHudBinocularPageInput
{
	float Seconds;           // since the binoculars came up: the frame, sound meter and zoom areas start then
	float SpeakSeconds;      // since the speak box (the Hack prompt) appeared; below 0 while it is not shown
	int32 ZoomStep;          // trunc(FScctHudState::BinocularFov x 0.1): 1 is x6, 2 x4, anything else x2
	int32 Reserved;
	TCHAR ZoomKeys[64];      // the key help's "zoom in / zoom out" keys ("Mouse Wheel Up / Mouse Wheel Down")
	TCHAR ExitKey[64];       // the key that lowers the binoculars
	TCHAR SpeakText[64];     // the speak box's text (FScctHudState::HackPromptId)
};

struct FScctHudBinocularApi
{
	int32 Version;
	// Up to Capacity layers of P_BinocularsATT in drawing order; returns how many there are (0 without the page).
	int32 (__cdecl* GetSpyPage)(const FScctHudBinocularPageInput* Input, FScctHudPageLayer* Rows, int32 Capacity);
	// Unattended checks only: turns the local player's view and aim to look from his eye at the world point, as a mouse
	// turn would leave them. 1 when there is a pawn to turn.
	int32 (__cdecl* AimAt)(float X, float Y, float Z);
};

// The sizes the C# side has (HudProbe prints them).
static_assert(sizeof(FScctHudPageLayer) == 380, "FScctHudPageLayer must mirror HudPageLayer");
static_assert(sizeof(FScctHudBinocularPageInput) == 400, "FScctHudBinocularPageInput must mirror HudBinocularPageInput");
static_assert(sizeof(FScctHudBinocularApi) == 24, "FScctHudBinocularApi must mirror HudBinocularApi");

// The table's bridge class and version, as Runtime/ScctFeatures.h lists the others (kept here so this feature's
// files stay its own).
template <>
struct TScctFeatureInfo<FScctHudBinocularApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudBinocularExports"); }
	static constexpr int32 Version = ScctHudBinocularApiVersion;
};
