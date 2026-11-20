#pragma once

#include "CoreMinimal.h"

// The vision part of the C# bridge's flat interface: the values the goggles draw each actor by. The original's modes
// do not light everything alike: thermal draws an actor by its HeatIntensity and HeatOpacity, EMF by its EMFIntensity
// while bEMFIsOn and by its EMFOpacity (0 hides it, below 0.99 it is translucent), a spy shows in EMF only while EMF
// visible (weapon drawn, a vision mode on, binoculars, a spy mark, the camo suit, phospho), and motion vision redraws
// the scene clean inside the boxes of the motion tracker's locks. These mirror
// Bridge/OpenScct.UnrealBridge/Devices/VisionState.cs and VisionExports.cs field for field; change both together and
// bump the version.
//
// Fetch the table once, like the other feature tables: ScctFeature<FScctVisionApi>(). Every entry works on the session
// FScctBridgeApi started and returns 0 while there is none. Call them on the game thread, after FScctBridgeApi::Update.
// NameId and ClassId are ids into the world table's names (FScctWorldApi::GetName); a placed actor's NameId is its
// map name, which the imported level actor carries as the tag "Scct:<name>".

// Version 1: pawns, actors, the level and the motion tracker's locks.
// Version 2: the level's GlowThreshold.
constexpr int32 ScctVisionApiVersion = 2;

namespace EScctVisionPawnFlags
{
	constexpr int32 EmfOn = 1;                // Actor.bEMFIsOn
	constexpr int32 EmfShows = 2;             // EmfVision.Shows: not hidden, bEMFIsOn and EMFOpacity above 0 (a spy with his pistol away and no other cause is not)
	constexpr int32 Moving = 4;               // faster than the local tracker's MinDetectedVelocity (its class default 150 when there is no tracker)
	constexpr int32 Tracked = 8;              // the local merc's motion tracker holds a lock on it
	constexpr int32 ShockImmune = 16;         // a merc whose shocker protection is on (the post-shock timer or a protecting state)
	constexpr int32 ShockStateProtected = 32; // the protection comes from his state (a blast, a fall): no time left to show
	constexpr int32 StickySkinned = 64;       // SBasePawn.bStickySkinned: the Stickess skin while sticky shocked
	constexpr int32 Wet = 128;                // SBasePawn.bWet: in a rain volume (the wet skins in thermal and EMF)
	constexpr int32 Hidden = 256;             // Actor.bHidden
	constexpr int32 Merc = 512;               // a merc; clear for a spy
	constexpr int32 WeaponDrawn = 1024;       // WeaponLoaded != 0, one of the spy's EMF causes
}

/** One pawn's vision values, as of the last client tick. */
struct FScctVisionPawn
{
	int32 ActorId;       // FScctBridgePawn::ActorId
	int32 Flags;         // EScctVisionPawnFlags
	float HeatIntensity; // how hot thermal draws the body (1 for both sides by default)
	float HeatOpacity;   // 0 hides the body in thermal, 1 opaque
	float EMFIntensity;  // as replicated, whether or not bEMFIsOn
	float EMFOpacity;    // 0 hides the body in EMF, below 0.99 translucent
	float EmfGlow;       // EMFIntensity while bEMFIsOn, else 0
	float ShockFraction; // shock protection left, 0 to 1 of its whole time (0 while protected by state only)
	float ShockSeconds;  // seconds of shock protection left
	float Cloak;         // the camo suit, 0 none to 1 fully cloaked (GlassEffect / 255)
	float Speed;         // cm per second
};
static_assert(sizeof(FScctVisionPawn) == 44, "FScctVisionPawn must match BridgeVisionPawn");

namespace EScctVisionActorFlags
{
	constexpr int32 EmfOn = 1;    // Actor.bEMFIsOn
	constexpr int32 EmfShows = 2; // EmfVision.Shows
	constexpr int32 Hidden = 4;   // Actor.bHidden
	constexpr int32 MapActor = 8; // placed by the map: NameId is its map name
	constexpr int32 Tracked = 16; // the local merc's motion tracker holds a lock on it (a thrown grenade)
	constexpr int32 Mine = 32;    // a mine body, including map-placed and player-placed variants
}

/**
 * One actor other than a pawn whose values the vision modes draw by: electronics (mines, sticky cams, grenades,
 * objective computers, ammo crates, cameras, laser triggers, the spy's pistol), emitters, glass and volumes thermal
 * hides or thins: anything whose HeatIntensity, HeatOpacity, EMFIntensity or EMFOpacity is not the Actor default
 * (0, 1, 0, 1).
 */
struct FScctVisionActor
{
	int32 ActorId;     // the simulation actor's id, stable while it lives
	int32 NameId;      // world name id of its map name, 0 for an actor spawned in play
	int32 ClassId;     // world name id of its class without the package (SMines, SStickyCam, SComputerObjectiveTrigger...)
	int32 OwnerPawnId; // the pawn that owns or carries it (a weapon's holder), 0 when none
	int32 Flags;       // EScctVisionActorFlags
	float HeatIntensity;
	float HeatOpacity;
	float EMFIntensity;
	float EMFOpacity;
	float EmfGlow;     // EMFIntensity while bEMFIsOn, else 0
	float X, Y, Z;     // where it is, cm
};
static_assert(sizeof(FScctVisionActor) == 52, "FScctVisionActor must match BridgeVisionActor");

namespace EScctVisionLevelFlags
{
	constexpr int32 HasTracker = 1;    // the local player is a merc with a motion tracker
	constexpr int32 TrackerActive = 2; // the tracker scans now (motion vision on)
}

/** The level's vision values. */
struct FScctVisionLevel
{
	int32 MtvSlope;    // LevelInfo.MTVSlope 0..255 (default 85): motion vision's contrast, s = 1 + 3 MTVSlope / 255
	int32 MtvOffset;   // LevelInfo.MTVOffset 0..255 (default 52): motion vision's brightness offset, MTVOffset / 255
	int32 Flags;       // EScctVisionLevelFlags
	int32 LockCount;   // locks the local tracker holds now
	float FocusTime;   // the tracker's FocusTime and OutTime, seconds (0 without a tracker)
	float OutTime;
	float TimeSeconds; // the client's LevelInfo.TimeSeconds of the last tick: the clock the lock times run on
	int32 GlowThreshold; // LevelInfo.GlowThreshold 0..255 (default 224): the glow's bright pass keeps what is brighter than this / 255; 255 no glow
};
static_assert(sizeof(FScctVisionLevel) == 32, "FScctVisionLevel must match BridgeVisionLevel");

/**
 * The view the motion tracker's locks are boxed from, and how its 640 x 480 HUD frame sits on the screen:
 * screen x = FrameOffsetX + frame x * FrameScaleX, screen y = frame y * FrameScaleY.
 */
struct FScctVisionCamera
{
	float X, Y, Z;          // cm
	float Pitch, Yaw, Roll; // degrees
	float FovX;             // horizontal field of view across the whole screen, degrees
	float ScreenWidth, ScreenHeight;               // pixels
	float FrameScaleX, FrameScaleY, FrameOffsetX;  // the frame on the screen
};
static_assert(sizeof(FScctVisionCamera) == 48, "FScctVisionCamera must match BridgeVisionCamera");

namespace EScctVisionLockFlags
{
	constexpr int32 Clean = 1;    // a clean view rectangle: in front, focused and not faded out (motion vision redraws the scene clean in it)
	constexpr int32 InFront = 2;  // in front of the camera
	constexpr int32 Removing = 4; // the target is no longer seen: held until EndTime, then fading over OutTime
	constexpr int32 Friend = 8;   // a teammate
	constexpr int32 Pawn = 16;    // the target is a pawn
}

/** One lock of the local merc's motion tracker, boxed in the 640 x 480 HUD frame. */
struct FScctVisionLock
{
	int32 ActorId; // the locked pawn (FScctBridgePawn::ActorId) or projectile
	int32 Flags;   // EScctVisionLockFlags
	float X1, X2, Y1, Y2;     // left, right, top, bottom in the frame
	float StartTime, EndTime; // when the lock began and, once let go, when its hold ends (FScctVisionLevel::TimeSeconds clock)
};
static_assert(sizeof(FScctVisionLock) == 32, "FScctVisionLock must match BridgeVisionLock");

/**
 * The table. The Get* entries that fill an array return how many items there are, which can be more than Capacity
 * (only Capacity are written; a null buffer asks for the count).
 */
struct FScctVisionApi
{
	int32 Version;

	/** Every pawn's vision values. */
	int32 (__cdecl* GetPawns)(FScctVisionPawn* Pawns, int32 Capacity);
	/** The other actors the vision modes draw by their own values. */
	int32 (__cdecl* GetActors)(FScctVisionActor* Actors, int32 Capacity);
	/** The level's vision values. Returns 1, or 0 while no map is loaded. */
	int32 (__cdecl* GetLevel)(FScctVisionLevel* Level);
	/** Boxes the local tracker's locks from Camera (as the original's HUD tick does) and writes them. 0 without a tracker or locks. */
	int32 (__cdecl* GetMotionLocks)(const FScctVisionCamera* Camera, FScctVisionLock* Locks, int32 Capacity);
};
static_assert(sizeof(FScctVisionApi) == 40, "FScctVisionApi must match VisionApi");
