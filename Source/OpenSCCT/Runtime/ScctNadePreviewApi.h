#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The grenade preview interface of the C# bridge (the console command nadepreview): the path the local player's
// selected grenade gadget would throw its projectile along if the button were let go now, worked out by the
// simulation's own launch, falling, bounce and fuse rules against its own collision. These mirror
// Bridge/OpenScct.UnrealBridge/Equipment/NadePreviewTypes.cs and NadePreviewExports.cs field for field; change both
// together and bump the version. Bridge/Probes/NadeProbe prints the C# sizes and throws real grenades against the
// prediction.
//
// Call it on the game thread after the frame's view was read (the aim is that view's: the mouse turn not yet
// simulated is in it), so after UScctSessionSubsystem's tick. Every field is 4 bytes wide, so the structs have no
// padding. Simulation units are Unreal centimetres.

constexpr int32 ScctNadePreviewApiVersion = 1;

namespace EScctNadePreviewState
{
	constexpr int32 None = 0;  // nothing to draw: no grenade gadget ready in the local player's hands
	constexpr int32 Aimed = 1; // the aimed throw of the selected grenade gadget
}

namespace EScctNadePreviewFlags
{
	constexpr int32 Charging = 1;    // the throw button is held: the charge is the gauge's, not the full tap
	constexpr int32 Blocked = 2;     // the simulation would refuse the throw now (refire time, reload, muzzle in a wall)
	constexpr int32 Stuck = 4;       // the projectile catches on the first surface it hits (the flare)
	constexpr int32 EndedByFuse = 8; // the flight ends when the fuse runs out
	constexpr int32 Airborne = 16;   // it is in the air when it ends: no surface under the marker
	constexpr int32 PipValid = 32;   // PipX/Y/Z is a camera place with a clear view of the landing point
}

namespace EScctNadePreviewKind
{
	constexpr int32 Other = 0;
	constexpr int32 Frag = 1;
	constexpr int32 Flare = 2;
	constexpr int32 Smoke = 3;
	constexpr int32 Chaff = 4;
	constexpr int32 FlashBang = 5;
	constexpr int32 Phosphorus = 6;
}

struct FScctNadePreview
{
	int32 State;   // EScctNadePreviewState
	int32 Flags;   // EScctNadePreviewFlags
	int32 Kind;    // EScctNadePreviewKind
	int32 ActorId; // the local pawn, whose muzzle the line leaves
	float Charge;  // 0..1, the value the throw would be made with (1 for a tap)
	float FlightSeconds;
	int32 Bounces;
	int32 Revision;                  // changes when the path was worked out again
	float StartX, StartY, StartZ;    // where the simulation spawns the projectile
	float LandX, LandY, LandZ;       // where it goes off or catches
	float NormalX, NormalY, NormalZ; // the surface there (up when Airborne)
	float Radius;                    // the effect's reach in cm, 0 for none
	float PipX, PipY, PipZ;          // the picture's camera place
};
static_assert(sizeof(FScctNadePreview) == 84, "FScctNadePreview must match BridgeNadePreview");

struct FScctNadePoint
{
	float X, Y, Z;
	int32 Flags; // 1: the projectile bounces here
};
static_assert(sizeof(FScctNadePoint) == 16, "FScctNadePoint must match BridgeNadePoint");

struct FScctNadePreviewApi
{
	int32 Version;

	/** 1 when the running session may show the preview (a match on this machine, or a practice round), else 0. */
	int32 (__cdecl* IsAllowed)();

	/** Writes this frame's head and up to Capacity path points, the launch point first. Returns the points written. */
	int32 (__cdecl* GetPreview)(FScctNadePreview* Head, FScctNadePoint* Points, int32 Capacity);
};
static_assert(sizeof(FScctNadePreviewApi) == 24, "FScctNadePreviewApi must match NadePreviewApi");

// Fetched like every other feature table (Runtime/ScctFeatures.h), declared here so the shared list stays as it is.
template <> struct TScctFeatureInfo<FScctNadePreviewApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.NadePreviewExports"); }
	static constexpr int32 Version = ScctNadePreviewApiVersion;
};
