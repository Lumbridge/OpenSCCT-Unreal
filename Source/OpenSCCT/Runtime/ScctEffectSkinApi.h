#pragma once

#include "CoreMinimal.h"

// The run-time skins of the effect meshes (the proximity mine's status light: SMineProx swaps LightOn and LightOff
// into Skins[1] as it arms, is chaffed and comes back), as the Godot client's EffectViews.ApplySkins draws them.
// Mirrors Bridge/OpenScct.UnrealBridge/Events/EffectSkinExports.cs; change both together. Every field is 4 bytes.

constexpr int32 ScctEffectSkinApiVersion = 1;

/** How many material slots GetActorSkins reports at most. */
constexpr int32 ScctEffectSkinSlots = 8;

/** One material slot of an effect actor's mesh as it is worn this frame. */
struct FScctEffectSkinSlot
{
	int32 SkinId; // name id of the skin's object path; 0: the mesh's own material (no skin, or not one the effects draw)
	int32 Frame;  // the cell of the skin's flipbook shown now, 0 for a still skin
};

namespace EScctEffectSkinFlags
{
	constexpr int32 Additive = 1;
	constexpr int32 Translucent = 2; // drawn added too, as the Godot client does: black is its see-through
	constexpr int32 Animated = 4;    // the picture is a sheet of Columns by Rows cells
}

/** How one skin is drawn (the textures table of shared/fx/effects.json). */
struct FScctEffectSkin
{
	int32 Flags;   // EScctEffectSkinFlags
	int32 FileId;  // name id of the picture's file relative to the content root: the flipbook sheet when animated
	int32 Columns; // the sheet's cells, read left to right, top to bottom (1, 1, 1 for a still picture)
	int32 Rows;
	int32 Frames;
	float Fps;     // cells a second, 0 for a still picture
};

static_assert(sizeof(FScctEffectSkinSlot) == 8, "FScctEffectSkinSlot must match BridgeEffectSkinSlot");
static_assert(sizeof(FScctEffectSkin) == 24, "FScctEffectSkin must match BridgeEffectSkin");

struct FScctEffectSkinApi
{
	int32 Version;

	/**
	 * Writes the skins the effect actor wears now, one record per material slot from slot 0. Returns how many slots
	 * its Skins has (at most ScctEffectSkinSlots; only Capacity are written), 0 when it has none or is gone. A slot
	 * beyond the count keeps the mesh's own material.
	 */
	int32 (__cdecl* GetActorSkins)(int32 ActorId, FScctEffectSkinSlot* Slots, int32 Capacity);

	/** Writes how the skin with this name id is drawn. Returns 1, or 0 when it is not a skin the effects draw. */
	int32 (__cdecl* GetSkin)(int32 SkinId, FScctEffectSkin* OutSkin);

	/**
	 * A check only (-ScctMineLight): two proximity mines on the local match's server, owned by its first mercenary,
	 * side by side in the air a little ahead of the point along the direction and facing back at it. 1 when queued,
	 * 0 without a local server. Nothing in normal play calls it.
	 */
	int32 (__cdecl* PlaceCheckMines)(float X, float Y, float Z, float DirX, float DirY, float DirZ);

	/** A check only: locks the oldest proximity mine of the local match's server as a chaff grenade does, for Seconds. 1 when queued. */
	int32 (__cdecl* ChaffCheckMine)(float Seconds);
};
