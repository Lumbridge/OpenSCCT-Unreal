#pragma once

#include "CoreMinimal.h"

// The TestRange drill interface of the C# bridge (vision-cancelling, spy-grab): arm a drill for the level's match,
// start it, read the coach state and call its actions. These mirror Bridge/OpenScct.UnrealBridge/Test/LessonFeature.cs
// and LessonExports.cs field for field; change both together and bump the version. Bridge/Probes/LessonProbe prints
// the C# sizes.
//
// Fetch the table through ScctFeature<FScctLessonApi>() (Runtime/ScctFeatures.h). Call it on the game thread. Times
// are milliseconds, and NaN where there is nothing to tell yet. Text is UTF-16, zero terminated.

constexpr int32 ScctLessonApiVersion = 1;

namespace EScctLessonPhase
{
	constexpr int32 Preparing = 0; // the lesson has not placed the players yet
	constexpr int32 Ready = 1;     // on the ledge or in the lane, nothing measured since
	constexpr int32 Falling = 2;   // falling from the tower
	constexpr int32 Landing = 3;   // feet on the floor, still in the landing recovery
	constexpr int32 Result = 4;    // this drop's recovery is measured
}

/** A drill's coach state as of the last simulation tick. */
struct FScctLessonState
{
	static constexpr int32 FeedbackCapacity = 512;
	static constexpr int32 StateCapacity = 64;

	int32 Active;       // 1 while a drill runs on the current session
	int32 Kind;         // 0 vision-cancelling, 1 spy-grab
	int32 Ready;        // 1 once the lesson has placed the players
	int32 Phase;        // EScctLessonPhase
	int32 Moving;       // 1 while the spy-grab target walks up and down the lane
	int32 Debug;        // the lesson's debug flag (kept with the grab area in spy-grab)
	int32 Attempts;     // drops measured, or grabs tried with the lesson's own grab
	int32 Grabs;        // successful grabs
	int32 Returns;      // times put back on the ledge
	int32 HistoryCount; // recent attempts GetAttempts has (at most six)
	int32 Vision;       // the local controller's vision mode
	float RecoveryMs;   // touchdown to the end of the landing recovery of this drop
	float BaselineMs;   // the first drop without a switch
	float BestMs;       // the fastest recovery with a switch while moving
	float LastSwitchMs; // the last attempt's switch input after touchdown (negative: before it)
	float LastSavedMs;  // the last attempt's time saved against the baseline
	float Distance;     // spy-grab: cm from the local spy to the mercenary
	float PortalX, PortalY, PortalZ, PortalRadius; // the walk-in return volume at the foot of the tower
	TCHAR Feedback[FeedbackCapacity]; // the coach's instruction and feedback line
	TCHAR State[StateCapacity];       // the local controller's state name
};
static_assert(sizeof(TCHAR) == 2, "the bridge's text is UTF-16");
static_assert(sizeof(FScctLessonState) == 1236, "FScctLessonState must match BridgeLessonState");

/** One recent vision-cancelling attempt. */
struct FScctLessonAttempt
{
	static constexpr int32 ResultCapacity = 24;

	int32 Number;
	int32 Moving;      // a movement direction was held through the landing
	float RecoveryMs;
	float SwitchMs;    // switch input after touchdown (negative: before it); NaN for none
	float VisionMs;    // when the vision mode really changed after touchdown; NaN when it did not
	float SavedMs;     // against the baseline; NaN with no baseline
	TCHAR Result[ResultCapacity]; // Baseline, Too early, Keep moving, Cancelled, Vision switched, Try earlier
};
static_assert(sizeof(FScctLessonAttempt) == 72, "FScctLessonAttempt must match BridgeLessonAttempt");

struct FScctLessonApi
{
	int32 Version;

	/** Arms a drill for the next local start on TestRange; null or "" disarms. 1, or 0 for an unknown kind. */
	int32 (__cdecl* Arm)(const TCHAR* Kind);

	/** Starts the drill on the current session. 1, or 0 with the reason in LastError. */
	int32 (__cdecl* StartLesson)(const TCHAR* Kind);

	/** Ends the drill of the current session (the match goes on). 1 when one ran. */
	int32 (__cdecl* StopLesson)();

	/** Writes the coach state. 1 while a drill runs, else 0 (the state zeroed). */
	int32 (__cdecl* GetState)(FScctLessonState* State);

	/** Writes the recent attempts, oldest first. Returns the count written. */
	int32 (__cdecl* GetAttempts)(FScctLessonAttempt* Attempts, int32 Capacity);

	/** "reset", "moving", "debug", "forward", "back", "stop", "night", "thermal", "grab". 1 when a drill took it. */
	int32 (__cdecl* Action)(const TCHAR* Action);

	/** Copies the last failure's text. Returns its length. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};
static_assert(sizeof(FScctLessonApi) == 64, "FScctLessonApi must match LessonApi");
