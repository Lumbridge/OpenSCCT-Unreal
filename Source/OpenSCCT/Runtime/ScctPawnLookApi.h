#pragma once

#include "CoreMinimal.h"

// How each pawn looks: the players' cosmetics (a camo or tint on the suit and the gun, the hue of the goggle light)
// after the "hide my / others' cosmetics" options and the map outfit's policy, and the map outfits themselves
// (render/outfits.json: a model in place of the stock mesh, body and head slot textures, the goggle bone). The bridge
// works out the look (a port of the Godot client's PawnCosmetics and PawnOutfits) and poses an outfit model's
// skeleton with the team's stock clips; Unreal puts the textures on (Play/ScctPawnLookSubsystem). The heat and EMF
// masks of an outfit model belong to the vision feature, which reads the model's name from GetOutfit. Mirrors
// Bridge/OpenScct.UnrealBridge/Pose/PawnLookExports.cs and PawnLooks.cs; change both together.

constexpr int32 ScctPawnLookApiVersion = 1;

namespace EScctPawnLookFlags
{
	/** The pawn of the player at this machine. */
	constexpr int32 Mine = 1;
	/** The light item turns the goggle glow (HueTurn is set). */
	constexpr int32 Hue = 2;
	/** The team wears a map outfit (any part of it). */
	constexpr int32 Outfit = 4;
	/** The outfit's own model is drawn in place of the stock mesh. */
	constexpr int32 OutfitModel = 8;
	/** No item in any slot. */
	constexpr int32 Retail = 16;
}

/** One pawn's look as this machine draws it. Names are ids of the session's name table (GetName; 0 none). */
struct FScctPawnLook
{
	int32 ActorId;
	/** EScctPawnLookFlags. */
	int32 Flags;
	/** 0 spy, 1 merc. */
	int32 Side;
	/** The body item ("spy.body.woodland") and the pattern its texture is made with ("woodland", "tint_5a6ea8"). */
	int32 BodyItem;
	int32 BodyPattern;
	/** 0 a camo texture, 1 a tint (also a made texture), -1 none. */
	int32 BodyKind;
	/** A tint's colour, 0xRRGGBB (0xFFFFFF otherwise). */
	uint32 BodyRgb;
	/** The light item and its hue in degrees. */
	int32 LightItem;
	float LightHue;
	/** Radians from the side's retail glow colour to the item's; 0 without EScctPawnLookFlags::Hue. */
	float HueTurn;
	/** The item of the gun in the pawn's hands and its pattern. */
	int32 GunItem;
	int32 GunPattern;
};

static_assert(sizeof(FScctPawnLook) == 48, "FScctPawnLook must match BridgePawnLook");

/** A team's map outfit. */
struct FScctKindOutfit
{
	/** 1 when the team wears one (any part); nothing else is set otherwise. */
	int32 Dressed;
	/** 1 when the model is the map's own glb (imported by Tools/Import/import_outfits.py), 0 a stock mesh. */
	int32 MapGlb;
	/** 0 map, 1 player, 2 none: whose look wins on the team. */
	int32 Policy;
	/** The model's name: "SPerso.DEF_01" for a stock mesh, the glb's path under render/ for the map's own; 0 none. */
	int32 Model;
	/** The map's folder name. */
	int32 Map;
	/** Full paths of the body and head slot textures (0 when none or missing). */
	int32 BodyTexture;
	int32 HeadTexture;
	/** The goggle lights' bone on the model and their offset in its axes (uu). */
	int32 GoggleBone;
	float GoggleX;
	float GoggleY;
	float GoggleZ;
	/** Bones of the model's skeleton (the session's GetSkeleton for the kind lists them); 0 without a model. */
	int32 ModelBones;
};

static_assert(sizeof(FScctKindOutfit) == 48, "FScctKindOutfit must match BridgeKindOutfit");

struct FScctPawnLookApi
{
	int32 Version;

	/** Writes the pawns' looks. Returns how many pawns there are (only the first Capacity are written). */
	int32 (__cdecl* GetLooks)(FScctPawnLook* OutLooks, int32 Capacity);
	/** Fills a team's (0 spies, 1 mercs) outfit. Returns 1 when the team wears one. */
	int32 (__cdecl* GetOutfit)(int32 Kind, FScctKindOutfit* OutOutfit);
	/** Writes a name of the table (UTF-16, no terminator). Returns its length. */
	int32 (__cdecl* GetName)(int32 Id, TCHAR* OutName, int32 Capacity);
	/**
	 * Writes the full path of the camo texture of a pattern (name id) for a retail texture of a package folder
	 * ("SPerso", "SWeaponFirst"; "outfit:0" or "outfit:1" for a team's outfit model). Returns its length, 0 when
	 * the content builder made none.
	 */
	int32 (__cdecl* CosmeticTexture)(int32 Pattern, const TCHAR* Package, int32 PackageLength, const TCHAR* Retail, int32 RetailLength,
		TCHAR* OutPath, int32 Capacity);
	/** The cosmetics options. Returns 1. */
	int32 (__cdecl* SetOptions)(int32 HideMine, int32 HideOthers);
	/** The player's own look shown on his pawn while "hide mine" is on ("woodland+blue", "hex:..."). Returns the names used. */
	int32 (__cdecl* SetOwnLook)(const TCHAR* Text, int32 Length);
	/** Unattended checks: puts a look on the player's info on the local server. Returns 1 when queued. */
	int32 (__cdecl* ForceLocalLook)(const TCHAR* Text, int32 Length);
	/** Unattended checks: every other player on the local server gets a look of its own. Returns 1 when queued. */
	int32 (__cdecl* GiveOthersLooks)();
};
