#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The HUD's pictures from the C# bridge, beside the HUD's own table (Runtime/ScctHudApi.h): the converted interface
// textures by package and name (SHudTextures.Icones_Weapons, Hud.Move_coop, Hud.heart ...) as BGRA8 texels, a Magma
// material's texture and region, and the coop icon clips evaluated at a time. It mirrors
// Bridge/OpenScct.UnrealBridge/Hud/HudTextureExports.cs; change both together and bump the version. Fetch it with
// ScctFeature<FScctHudTextureApi>() like any other feature table. Hud/ScctHudTextures.h turns the texels into
// textures and brushes; HUD painters ask that, not this table.

constexpr int32 ScctHudTextureApiVersion = 1;

/** A picture of the converted content placed on the 640 x 480 Magma page (GetMaterial, GetCoopLayers). */
struct FScctHudTextureLayer
{
	TCHAR Package[16];       // the package and texture name, zero ended, as GetTexture takes them
	TCHAR Texture[32];
	float L, T, R, B;        // where it is drawn, in page units (a mirrored image has R left of L)
	int32 U0, V0, U1, V1;    // the texel region shown
	float Red, Green, Blue, Alpha; // its colour, 0..1 as the content gives it (sRGB), alpha included
};

struct FScctHudTextureApi
{
	int32 Version;
	// 1 when the content has the texture, with its size; its texels (blue, green, red, alpha; top row first) are
	// copied when Bgra holds Width x Height x 4 bytes (pass null to ask for the size). 0 when missing.
	int32 (__cdecl* GetTexture)(const TCHAR* Package, const TCHAR* Name, int32* Width, int32* Height, uint8* Bgra, int32 Capacity);
	// A Magma material's texture and texel region (the layer's names and U/V); 1 when found.
	int32 (__cdecl* GetMaterial)(const TCHAR* Package, const TCHAR* Material, FScctHudTextureLayer* Layer);
	// Coop icon 1..4 (FScctHudState::CoopIcon) Seconds after it appeared, as layers in drawing order; returns how many.
	int32 (__cdecl* GetCoopLayers)(int32 Icon, float Seconds, FScctHudTextureLayer* Rows, int32 Capacity);
	// The page box (left, top, right, bottom) every pose of coop icon 1..4 stays in, or with 0 the box of all four
	// (where A_Coop sits); 1 when known.
	int32 (__cdecl* GetCoopBounds)(int32 Icon, float* Ltrb);
	// Unattended checks only: the local pawn dies by its own hand on the local match's server (lives left as they
	// are), so a check sees what a dead player sees. 1 when queued, 0 on a remote round or without a pawn.
	int32 (__cdecl* KillLocalPawn)();
};

// The sizes the C# side has (HudProbe prints them).
static_assert(sizeof(FScctHudTextureLayer) == 144, "FScctHudTextureLayer must mirror HudTextureLayer");
static_assert(sizeof(FScctHudTextureApi) == 48, "FScctHudTextureApi must mirror HudTextureApi");

// The table's bridge class and version, as Runtime/ScctFeatures.h lists the others (kept here so this feature's
// files stay its own).
template <>
struct TScctFeatureInfo<FScctHudTextureApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudTextureExports"); }
	static constexpr int32 Version = ScctHudTextureApiVersion;
};
