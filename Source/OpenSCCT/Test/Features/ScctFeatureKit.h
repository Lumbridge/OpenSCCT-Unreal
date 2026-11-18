#pragma once

#include "CoreMinimal.h"
#include "Test/ScctTest.h"

class AScctPawnProxy;
class FScctHudModel;
class UScctEffectsSubsystem;
struct FScctBridgePawn;
struct FScctWorldDebugApi;

// ---------------------------------------------------------------------------------------------------------------
// PER-FEATURE CHECKS (Test/Features/*.cpp)
//
// The staging switches of the client (-ScctAutoFire, -ScctLevelDo, -ScctAutoplay, ...) only log; these named tests
// stage the same situations and assert what Unreal then draws or the HUD then shows, the Godot client's GadgetsSmoke,
// PlayModeSmokes, PlayDoorTimerSmoke and TestRangeSmokes:
//
//   combat-autofire  gadgets  doors  lights  emitters  bots  traversal  modes
//
// Each reads its result from Unreal (actors, light and particle components, the effects subsystem's counts, the HUD
// model) or from a bridge table, prints one "SCCT_FEATURE <test>: ..." line per stage and fails with the reason.
//
// -ScctFeatureBreak leaves out the step each test stages (no shot, no throw, no trigger, no bots, no climb, no mode),
// so every test can be seen to fail with its reason (Run-ScctTests -ExpectedFailures <test> -Extra -ScctFeatureBreak).
// -ScctFeatureShots saves a picture at each stage to Saved/ScctShots/<test>/.
// ---------------------------------------------------------------------------------------------------------------

namespace ScctFeatureKit
{
	/** -ScctFeatureBreak: the test leaves out the step it stages. */
	bool IsBroken();

	/** One "SCCT_FEATURE <test>: <text>" line. */
	void Note(const FString& Test, const FString& Text);

	/** With -ScctFeatureShots, a picture (interface included) to Saved/ScctShots/<test>/<index>-<label>.png. */
	void Shot(const FString& Test, const FString& Label);

	/** The bridge's world debug table (Play/ScctLevelStateDebug.h), or null with the reason. */
	const FScctWorldDebugApi* GetWorldDebug(FString* OutError);

	/** The HUD model of the test's world, or null before the HUD has its state. */
	const FScctHudModel* GetHud(const FScctTestContext& Context);

	/** The effects subsystem of the test's world, or null. */
	const UScctEffectsSubsystem* GetEffects(const FScctTestContext& Context);

	/** The local player's pawn as the bridge has it this frame, or null. */
	const FScctBridgePawn* GetLocalPawn(const FScctTestContext& Context);

	/** "/Game/Imported/Maps/<Map>/<Map>". */
	FString MapLevel(const FString& Map);

	/** The local session's simulation ticks so far (TestExports.GetCounters, 60 a second), or -1 without the test table. */
	int64 GetClientTicks(const FScctTestContext& Context);
}

/**
 * One level opened for a check and waited on until its match plays with the local player's body drawn. A new match
 * is told by its session id, so the same map can be opened again. Step returns Running while it waits, Fail with the
 * reason after Timeout, and Pass once the match is ready (it keeps returning Pass after that while the match lasts).
 */
class FScctFeatureStage
{
public:
	/** Travels to Map's imported level with Options ("?Side=merc?Bots=0"); a Level path overrides the map's. */
	bool Open(FScctTestContext& Context, const FString& Map, const FString& Options, FString& OutError, const FString& Level = FString());

	FScctTestResult Step(FScctTestContext& Context, double TimeoutSeconds = 150.0);

	/** Seconds since the match became ready (0 before). */
	double GetPlayedSeconds(const FScctTestContext& Context) const;

	/**
	 * Seconds of the simulation since the match became ready (its ticks over 60), or GetPlayedSeconds without the test
	 * table. A starved run (shaders still compiling, a few frames a second) advances the simulation slower than the
	 * wall clock, so a check that waits for the simulation to act waits on this.
	 */
	double GetSimSeconds(const FScctTestContext& Context) const;

	/** Engine frames since the match became ready. */
	int64 GetFrames() const { return ReadyAt >= 0.0 ? static_cast<int64>(GFrameCounter) - ReadyFrame : 0; }

	/** The match that was ready ended or was replaced. */
	bool HasEnded(const FScctTestContext& Context) const;

	bool IsReady() const { return ReadyAt >= 0.0; }

	const FString& GetMap() const { return Map; }

private:
	FString Map;
	FString LevelName;
	double OpenedAt = 0.0;
	double ReadyAt = -1.0;
	int64 ReadyTicks = -1;
	int64 ReadyFrame = 0;
	int32 PreviousSessionId = 0;
	int32 SessionId = 0;
};

/**
 * Holds and lets go the player's named actions ("forward", "fire", ...) for a check, and lets go of all of them at
 * the end.
 */
class FScctFeatureKeys
{
public:
	void Hold(FScctTestContext& Context, const FString& Action, bool bHeld);
	void ReleaseAll(FScctTestContext& Context);
	bool IsHeld(const FString& Action) const { return Held.Contains(Action); }

private:
	TSet<FString> Held;
};
