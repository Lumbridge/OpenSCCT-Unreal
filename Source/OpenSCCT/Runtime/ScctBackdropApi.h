#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The menu's live backdrop, from the C# bridge (Bridge/OpenScct.UnrealBridge/Backdrop/BackdropExports.cs): a local
// Story match played by four bots that the machine's own player only watches. The match is made ready on a worker
// thread of the bridge (the map loads, the server starts), so the menu keeps drawing; the game thread then adopts
// it into the bridge's session slot and presents it like any other session (UScctSessionSubsystem::PresentBackdrop).
// The director is Menu/ScctMenuBackdrop.h.
//
// This mirrors BackdropExports.cs; change both together and bump the version.

constexpr int32 ScctBackdropApiVersion = 1;

/** How far the match being made ready is (FScctBackdropStatus::Prepare). */
namespace EScctBackdropPrepare
{
	enum Type : int32
	{
		Idle = 0,
		Loading = 1,
		Ready = 2,
		Failed = 3,
	};
}

struct FScctBackdropStatus
{
	/** EScctBackdropPrepare of the match being made ready. */
	int32 Prepare;

	/** The id of the backdrop session in the bridge's slot, 0 when the slot holds none (or holds a real match). */
	int32 SessionId;

	/** Client ticks that session has run. */
	int32 Ticks;

	/** Pawns alive on the client. */
	int32 PawnsAlive;

	/** 1 when there is a view to draw. */
	int32 ViewValid;

	/** 1 when the match is over. */
	int32 MatchEnded;

	/** The actor id of the watched pawn, 0 for none, and whether it is a mercenary. */
	int32 TargetActorId;
	int32 TargetMerc;
};

struct FScctBackdropApi
{
	int32 Version;

	/** 1 when a backdrop match can start on the map at once: its sim files are there and the bots' navigation is prepared. */
	int32 (__cdecl* CanPlay)(const TCHAR* Map);

	/** Starts making the map's match ready on a worker. Poll GetStatus().Prepare. */
	int32 (__cdecl* Begin)(const TCHAR* Map);

	/** Game thread: puts the ready match in the bridge's session slot. Returns its session id, 0 when none was ready. */
	int32 (__cdecl* Adopt)();

	/** Drops the match being made ready or waiting. The session in the slot is not touched. */
	void (__cdecl* Cancel)();

	/** Writes what the backdrop is doing. Returns 1 when a backdrop session plays in the slot. */
	int32 (__cdecl* GetStatus)(FScctBackdropStatus* Status);

	/** Cuts to the next player now (the camera also does so by itself every so often). */
	void (__cdecl* NextTarget)();

	/** Why the last start failed, cut to fit and zero terminated. Returns the full length. */
	int32 (__cdecl* GetError)(TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctBackdropStatus) == 32, "FScctBackdropStatus must match BackdropStatus");
static_assert(sizeof(FScctBackdropApi) == 64, "FScctBackdropApi must match BackdropApi (64-bit)");

template <> struct TScctFeatureInfo<FScctBackdropApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.BackdropExports"); }
	static constexpr int32 Version = ScctBackdropApiVersion;
};
