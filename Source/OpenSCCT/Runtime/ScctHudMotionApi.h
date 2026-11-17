#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The motion tracking's interface of the C# bridge, beside the HUD's own table (Runtime/ScctHudApi.h): every frame the
// HUD hands over the view it draws, and the bridge projects the locks of the local merc's goggles tracker (moving pawns
// and projectiles in sight while motion tracking vision is on) and of the HUD's own tracker (the laser sight's target)
// with it (SMotionTrackingVision.ProjectLocks), builds the tracker's draw list (BuildHud) and hands it back as rows to
// paint: lock corners, sweep bands, friend marks and texts with the canvas's colours. It mirrors
// Bridge/OpenScct.UnrealBridge/Hud/HudMotionExports.cs; change both together and bump the version. Fetch it with
// ScctFeature<FScctHudMotionApi>() and call it once a frame after FScctBridgeApi::Update, as the HUD's getters.

constexpr int32 ScctHudMotionApiVersion = 1;

/** The view drawn this frame (Build). */
struct FScctHudMotionView
{
	float X, Y, Z;             // the camera's location
	float Pitch, Yaw, Roll;    // the camera's rotation in degrees (FRotator)
	float FocalX, FocalY;      // a point at view-space right r, up u, depth d lands at
	float CentreX, CentreY;    //   (CentreX + r / d * FocalX, CentreY - u / d * FocalY)
	float Width, Height;       // the area the rows are placed on (the HUD's full-screen layer), in its units
	int32 Page;                // 0 none (nothing drawn), 1 the merc's own page, 2 the camera network page
	int32 Reserved;
};

/** What one Build drew. */
struct FScctHudMotionState
{
	int32 Tracking;            // 1 while the local merc's goggles track (motion tracking vision on)
	int32 Locks;               // the goggles' locks
	int32 LaserLocks;          // the HUD tracker's locks (drawn after a tick that ran the laser target name pass)
	int32 Drawn;               // locks drawn (in front of the camera)
	int32 Friends;             // of those, drawn as a friend
	int32 Rows;                // rows written
	int32 TotalRows;           // rows the draw list had
	int32 NameRow;             // 1 when a row is the laser target's name over its bracket (the reticle leaves its own out)
	float FrameScale;          // the view's units per unit of the 640 x 480 frame the tracker draws in
	float Time;                // the level time the draw ran at
};

namespace EScctHudMotionKind
{
	constexpr int32 Lock = 0;   // a lock corner, SHudTextures.Vision_lock
	constexpr int32 Sweep = 1;  // the sweep band, SHudTextures.balayage_H
	constexpr int32 Friend = 2; // a friend mark, SHudTextures.Vision_friend
	constexpr int32 Text = 3;
}

/** One primitive of the tracker's HUD, in draw order (Build). */
struct FScctHudMotionRow
{
	int32 Kind;                // EScctHudMotionKind
	int32 Font;                // a text's font: 0 the tiny canvas font (speed, distance), 1 the medium one (Hold Fire, the name)
	float X, Y, XL, YL;        // a tile's corner and size in the view's units (a negative size mirrors it); a text's top left and line height (YL)
	float U, V, UL, VL;        // a tile's texel region
	uint8 R, G, B, A;          // the canvas colour (0..255; the canvas draws each channel doubled, capped at 255)
	TCHAR Text[32];            // a text's words, zero ended
};

/** A lock as the last Build placed it (GetLocks), for the log and checks. */
struct FScctHudMotionLock
{
	int32 ActorId;             // the locked actor (FScctBridgePawn::ActorId for a pawn)
	int32 Side;                // 0 a spy, 1 a merc, -1 anything else (a projectile)
	int32 Friend;              // 1 when drawn as a friend
	int32 Laser;               // 1 for the HUD tracker's lock (the laser's target), 0 for the goggles'
	int32 InFront;             // 1 when its box is in front of the camera
	int32 Removing;            // 1 while held after the actor stopped or left sight (it fades out after its hold)
	float X1, Y1, X2, Y2;      // the box in the view's units
	float Speed;               // the actor's speed, world units a second
	float StartTime;           // level time the lock began
	float WorldX, WorldY, WorldZ; // the actor's location (checks against Unreal's own projection)
};

struct FScctHudMotionApi
{
	int32 Version;
	// Projects and builds the trackers' HUD for the view; fills State and up to Capacity rows; returns the rows written.
	int32 (__cdecl* Build)(const FScctHudMotionView* View, FScctHudMotionState* State, FScctHudMotionRow* Rows, int32 Capacity);
	// Up to Capacity of the locks the last Build placed; returns how many.
	int32 (__cdecl* GetLocks)(FScctHudMotionLock* Locks, int32 Capacity);
	// Unattended checks only: on the local match's server, puts the nearest pawn of the other side (0) or of the player's
	// own side (1) a few metres in front of the local pawn, where it walks on; 1 when queued.
	int32 (__cdecl* Summon)(int32 Mate);
};

// The sizes the C# side has (HudProbe prints them).
static_assert(sizeof(FScctHudMotionView) == 56, "FScctHudMotionView must mirror HudMotionView");
static_assert(sizeof(FScctHudMotionState) == 40, "FScctHudMotionState must mirror HudMotionState");
static_assert(sizeof(FScctHudMotionRow) == 108, "FScctHudMotionRow must mirror HudMotionRow");
static_assert(sizeof(FScctHudMotionLock) == 60, "FScctHudMotionLock must mirror HudMotionLock");
static_assert(sizeof(FScctHudMotionApi) == 32, "FScctHudMotionApi must mirror HudMotionApi");

// The table's bridge class and version, as Runtime/ScctFeatures.h lists the others (kept here so this feature's
// files stay its own).
template <>
struct TScctFeatureInfo<FScctHudMotionApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.HudMotionExports"); }
	static constexpr int32 Version = ScctHudMotionApiVersion;
};
