#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctVisionApi.h"

/**
 * The simulation's per-actor vision values for this frame, read from the bridge's vision table (Runtime/ScctVisionApi.h):
 * each pawn's heat, EMF, motion and shock protection state, the other actors the modes draw by their own values
 * (electronics, heat-hidden emitters, glass), and the level's motion vision tweak. The vision subsystem refreshes it
 * once a frame; whatever draws a mode reads it from there (UScctVisionSubsystem::GetData).
 *
 * Unattended switch: -ScctVisionDump[=seconds] logs every row once that many seconds into the match (default 6), with
 * the world names of the actors, so the values can be checked from a run's log.
 */
class OPENSCCT_API FScctVisionData
{
public:
	/** Reads this frame's rows. False while there is no table or no session (the rows are then empty). */
	bool Refresh(float DeltaSeconds);

	/** Drops every row (the match is over). */
	void Reset();

	/** The table, or null when the bridge does not have it (said once in the log). */
	const FScctVisionApi* GetApi() const { return Api; }

	/** The pawn's row this frame, or null. */
	const FScctVisionPawn* FindPawn(int32 ActorId) const;

	/** The other actor's row this frame by its simulation ActorId, or null. */
	const FScctVisionActor* FindActor(int32 ActorId) const;

	/** The placed actor's row this frame by its map name's world name id, or null. */
	const FScctVisionActor* FindMapActor(int32 NameId) const;

	TConstArrayView<FScctVisionPawn> GetPawns() const { return Pawns; }
	TConstArrayView<FScctVisionActor> GetActors() const { return Actors; }

	/** The level's values; valid once a frame was read with a map loaded. */
	const FScctVisionLevel& GetLevel() const { return Level; }
	bool HasLevel() const { return bHasLevel; }

	/**
	 * The local merc's motion tracker locks boxed from Camera in the 640 x 480 HUD frame (the bridge works the boxes
	 * out as the original's HUD tick does). Empty without a tracker or locks.
	 */
	TConstArrayView<FScctVisionLock> ReadMotionLocks(const FScctVisionCamera& Camera);

private:
	void Bind();
	void Dump() const;

	const FScctVisionApi* Api = nullptr;
	bool bTriedBind = false;

	TArray<FScctVisionPawn> Pawns;
	TArray<FScctVisionActor> Actors;
	TArray<FScctVisionLock> Locks;
	TMap<int32, int32> PawnIndex;
	TMap<int32, int32> ActorIndex;
	TMap<int32, int32> MapActorIndex;
	FScctVisionLevel Level = {};
	bool bHasLevel = false;

	/** -ScctVisionDump: seconds into the match to log every row, negative when not asked. */
	float DumpAt = -1.0f;
	float MatchSeconds = 0.0f;
	bool bDumped = false;
};
