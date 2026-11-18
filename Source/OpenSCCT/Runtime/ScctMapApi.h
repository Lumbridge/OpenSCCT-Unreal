#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The strategic map, from the C# bridge (Bridge/OpenScct.UnrealBridge/Map/MapExports.cs, over the Godot client's
// StrategicMapModel linked from game/Scripts/UI): a map's floors and their pictures (svmap/<map>/ui/strategicmap/
// floor<n>.png), the legend and the numbered places, the floor shown with Up and Down, the half-second blink, and in a
// round the "you are here" marker and the team mates' markers from the live positions.
//
// This mirrors MapExports.cs field for field; change both together and bump the version. Strings are UTF-16 and zero
// terminated, cut to fit.

constexpr int32 ScctMapApiVersion = 2;

namespace EScctMapContext
{
	constexpr int32 Match = 0; // the round being played, with the live positions
	constexpr int32 Lobby = 1; // a lobby's map (the name given, else the lobby's own), without markers
}

/** The page as it is drawn. Markers are 0..1 across (U) and down (V) the picture; angles are radians clockwise from pointing right. */
struct FScctMapState
{
	static constexpr int32 PathCapacity = 260;
	static constexpr int32 MapCapacity = 64;
	static constexpr int32 MaxMates = 23;

	int32 FloorCount;
	/** The floor shown, from 0 (the lowest). */
	int32 Floor;
	/** The map has a snapshot camera, so markers and places can be put on its pictures. */
	int32 HasCamera;
	/** The markers' blink: 1 bright, 0 dim. */
	int32 Bright;
	/** Opened in a round (live positions), not the lobby's overview. */
	int32 Live;
	/** The shown floor's picture is converted: Picture is its file. */
	int32 HasPicture;
	int32 YouShown;
	float YouU;
	float YouV;
	float YouAngle;
	/** The local player has a pawn: its point in the simulation's units and its yaw (65536 a turn). */
	int32 HasYou;
	float YouX;
	float YouY;
	float YouZ;
	int32 YouYaw;
	int32 YouFloor;
	/** Team mates with a position; MateShown tells which are on the shown floor. */
	int32 MateCount;
	int32 MatesShown;
	int32 MateShown[MaxMates];
	float MateU[MaxMates];
	float MateV[MaxMates];
	float MateAngle[MaxMates];
	float MateX[MaxMates], MateY[MaxMates], MateZ[MaxMates];
	int32 MateFloor[MaxMates];
	float FloorMinZ, FloorMaxZ; // marker heights in [min, max)
	int32 PlaceCount;
	TCHAR Picture[PathCapacity];
	TCHAR Map[MapCapacity];
};

/** A numbered place of the map's identity on its floor's picture. */
struct FScctMapPlace
{
	int32 Number;
	int32 Floor;
	float U;
	float V;
	FVector3f World;
};

struct FScctMapTriangle
{
	FVector3f A, B, C;
};

struct FScctMapApi
{
	int32 Version;

	/** Opens the page (EScctMapContext; Map for the lobby, empty for the lobby's own). Returns 1, or 0 with the reason in LastError. */
	int32 (__cdecl* Open)(int32 Context, const TCHAR* Map);

	/** The half-second timer: the blink, and the markers placed again from fresh positions. */
	int32 (__cdecl* Tick)();

	/** The floor above / below. Returns 1 when the floor changed. */
	int32 (__cdecl* Up)();
	int32 (__cdecl* Down)();

	/** Returns 1 while the page is open. */
	int32 (__cdecl* GetState)(FScctMapState* State);

	/** The legend, lines broken with '\n'. Returns the full length. */
	int32 (__cdecl* GetLegend)(TCHAR* Buffer, int32 Capacity);

	/** The places on every floor. Returns the full count; past Capacity is not written. */
	int32 (__cdecl* ListPlaces)(FScctMapPlace* Places, int32 Capacity);

	/** The DisplayStrategicMap key may open the page now (a round, not a tutorial, not working an object). */
	int32 (__cdecl* CanDisplay)();

	void (__cdecl* Close)();

	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);

	/** Static map faces clipped to this floor in simulation coordinates. Only available in a match. */
	int32 (__cdecl* ListGeometry)(FScctMapTriangle* Triangles, int32 Capacity);
};

// The sizes the C# side has (Bridge/Probes/MapProbe prints them).
static_assert(sizeof(FScctMapState) == 1468, "FScctMapState must match BridgeMapState");
static_assert(sizeof(FScctMapPlace) == 28, "FScctMapPlace must match BridgeMapPlace");
static_assert(sizeof(FScctMapTriangle) == 36, "FScctMapTriangle must match BridgeMapTriangle");
static_assert(sizeof(FScctMapApi) == 96, "FScctMapApi must match MapApi (64-bit)");

// Fetched like every other feature table (Runtime/ScctFeatures.h), declared here so the shared list stays as it is.
template <> struct TScctFeatureInfo<FScctMapApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.MapExports"); }
	static constexpr int32 Version = ScctMapApiVersion;
};
