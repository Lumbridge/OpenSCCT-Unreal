#pragma once

#include "CoreMinimal.h"

// The hub activities' half of the C# bridge (an OpenSCCT addition): the course run's and the range round's clocks,
// the notices, the nearest score board, the marks and targets the props draw, the plaza ball, the jukebox's record,
// the chime cues, and (installed when the table is fetched) the emote poses laid over every drawn pawn. These mirror
// Bridge/OpenScct.UnrealBridge/Hub/HubActivityTypes.cs and HubActivityExports.cs field for field; change both together
// and bump the version. Bridge/Probes/HubProbe (mode "social") prints the sizes asserted below.
//
// The table is fetched like the other feature tables: ScctFeature<FScctHubActivityApi>() (Runtime/ScctFeatures.h).
// Everything reads the visit of the hub table (ScctHubApi.h); while not in a hub the reads return nothing. Positions
// are in UE units. Text is UTF-16, zero terminated. Game thread only.

constexpr int32 ScctHubActivityApiVersion = 1;

namespace EScctHubActivityFlags
{
	// FScctHubActivityState::Flags
	constexpr int32 InHub = 1;    // the visit's session is drawn
	constexpr int32 Running = 2;  // the player runs a course
	constexpr int32 InRound = 4;  // the player shoots a range round
	constexpr int32 HasBall = 8;  // BallX, BallY, BallZ are where the plaza ball is
	constexpr int32 Emoting = 16; // the local player plays an emote

	// FScctHubActivityMark::Flags
	constexpr int32 Shown = 1; // drawn (a checkpoint the run passed is not)
	constexpr int32 Next = 2;  // the run's next checkpoint: it pulses

	// FScctHubActivityTarget::Flags
	constexpr int32 Mine = 1; // the player's own round's target (red, else grey)
	constexpr int32 Up = 2;   // up or still flipping: drawn
}

/** What a mark is. */
enum class EScctHubMarkKind : int32
{
	Other = 0,
	Start = 1,      // a course's start gate (green)
	Finish = 2,     // its finish (white)
	Checkpoint = 3, // amber
	Travel = 4,     // a travel point (blue disc)
	FiringLine = 5, // the aim range's line (Radius: its half length across the range)
	Jukebox = 6,
	Laptop = 7,     // a match pad's laptop on its desk
	Goal = 8,       // a goal of the plaza ball (Radius: the mouth's half width)
};

/** The texts GetText reads. */
enum class EScctHubActivityText : int32
{
	Activity = 0,   // the course run's or range round's clock (two lines), a finished run for 6 s, else empty
	Notices = 1,    // the notices still up, one per line, a big one in "> <"
	BoardTitle = 2, // the nearest score board within 2200 units of the eye that is not a world sign, else empty
	BoardLines = 3, // its rows, one per line
	TrackTitle = 4, // the jukebox's record
	LocalEmote = 5, // the local player's emote and how long it has played ("cheer 1.2 s"), else empty
};

struct FScctHubActivityState
{
	int32 Valid;        // 1 while in a hub
	int32 Flags;        // EScctHubActivityFlags (the state's group)
	int32 Track;        // the jukebox's track (0 to 3), -1 before the hub said
	int32 MarkCount;
	int32 MarkRevision; // changes when a mark was added, moved or replaced
	int32 TargetCount;
	int32 LocalPlayerId;
	int32 EmotePoses;   // poses that carried an emote since the bridge started (each pawn each frame)
	float Now;          // the play clock, seconds
	float BallX, BallY, BallZ;
};

struct FScctHubActivityMark
{
	int32 Kind;  // EScctHubMarkKind
	int32 Flags; // EScctHubActivityFlags (the mark's group)
	float X, Y, Z;
	float Radius;
	TCHAR Id[48];
};

struct FScctHubActivityTarget
{
	int32 Id;
	int32 Flags; // EScctHubActivityFlags (the target's group)
	float X, Y, Z; // its foot
	float Angle;   // how far it lies down, degrees (0 standing, 90 flat)
};

namespace EScctHubVoiceMixFlags
{
	constexpr int32 Occluded = 1; // the world's collision stands between his mouth and the ear
	constexpr int32 Audible = 2;  // the gain is above 0
}

/** How one other player's voice would be played to the listener, nearest first (the bridge's HubVoiceHook). */
struct FScctHubVoiceMix
{
	int32 PlayerId;
	int32 Flags;      // EScctHubVoiceMixFlags
	float Distance;   // from the ear to his mouth
	float Gain;       // smoothed, 0 to 1, before the listener's own volume and mute for him
	float LowPassHz;  // smoothed: 20000 in the open, 1200 through a wall
	float X, Y, Z;    // his mouth
	TCHAR Name[32];
};

struct FScctHubActivityApi
{
	int32 Version;

	/** The state. Returns Valid. */
	int32 (__cdecl* GetState)(FScctHubActivityState* State);

	/** Writes up to Capacity marks. Returns how many there are. */
	int32 (__cdecl* GetMarks)(FScctHubActivityMark* Marks, int32 Capacity);

	/** Writes up to Capacity targets of the aim range. Returns how many there are. */
	int32 (__cdecl* GetTargets)(FScctHubActivityTarget* Targets, int32 Capacity);

	/** Copies a text (EScctHubActivityText) as seen from the eye. Returns its full length. */
	int32 (__cdecl* GetText)(int32 Kind, float EyeX, float EyeY, float EyeZ, TCHAR* Buffer, int32 Capacity);

	/** Copies the next chime cue ("start", "cp", "fall", "finish", "best", "hit", "head", "end", "fanfare"). Returns its length, -1 when none waits. */
	int32 (__cdecl* TakeCue)(TCHAR* Buffer, int32 Capacity);

	/** Updates every other player's voice mix as heard from the ear over DeltaSeconds and writes them, nearest first. Returns how many there are. */
	int32 (__cdecl* GetVoiceMixes)(float EarX, float EarY, float EarZ, float DeltaSeconds, FScctHubVoiceMix* Mixes, int32 Capacity);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctHubActivityState) == 48, "FScctHubActivityState does not match HubActivityState");
static_assert(sizeof(FScctHubActivityMark) == 120, "FScctHubActivityMark does not match HubActivityMark");
static_assert(sizeof(FScctHubActivityTarget) == 24, "FScctHubActivityTarget does not match HubActivityTarget");
static_assert(sizeof(FScctHubVoiceMix) == 96, "FScctHubVoiceMix does not match HubVoiceMixRow");
static_assert(sizeof(FScctHubActivityApi) == 8 + 6 * sizeof(void*), "FScctHubActivityApi does not match HubActivityApi");
