#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The tutorials' interface of the C# bridge. It mirrors Bridge/OpenScct.UnrealBridge/Tutorials/TutorialExports.cs
// field for field; change both together and bump the version. Fetch it with ScctFeature<FScctTutorialApi>() like any
// other feature table. Every text is UTF-16, zero terminated, cut to fit; the entries that write one return its length.
//
// The tutorial maps and their order are the simulation's (SPersistentData.TutoMapsA / TutoMapsD: TSP20, TSP21, TSP22,
// TSP23 and the exam TSP29 for the spies; TME30, TME32 and TME33 for the mercs). The progress is the profile's tutorial
// part (Godot's TutorialProfile), kept in the file Unreal names with Load.

constexpr int32 ScctTutorialApiVersion = 1;

struct FScctTutorialProgress
{
	int32 SpyDone;           // bit i: the spies' tutorial i is done (the exam is the last)
	int32 MercDone;          // bit i: the mercs' tutorial i is done
	int32 ExamDone;
	int32 ExamJustCompleted; // the exam was passed for the first time this run
	int32 SpyCount;
	int32 MercCount;
	int32 ExamIndex;
};

struct FScctTutorialRound
{
	int32 Ended;       // 1 once the round has a winner
	int32 WinningTeam; // 0 spies, 1 mercs, 255 not decided
	int32 LocalTeam;   // 0 spy, 1 merc, -1 before the local controller exists
	int32 Tutorial;    // 1 when the session's map is a tutorial map
	int32 NumTut;      // its index in its side's list, -1 when not a tutorial
	int32 NumTeam;     // that side, -1 when not a tutorial
	int32 Remote;      // 1 for a round on a remote server
};

struct FScctTutorialGadget
{
	static constexpr int32 PathCapacity = 64;
	static constexpr int32 FileCapacity = 260;

	TCHAR Path[PathCapacity]; // the class path (TeamInfo.ClassInventory[i])
	TCHAR Icon[FileCapacity]; // the full path of its picture (the HUD's gadget atlas), or empty
	int32 U;                  // its tile in texels; Size 0 for the whole picture
	int32 V;
	int32 Size;
	int32 Retired;            // 1 for the spy's retired cloak gadget, which the pages leave out
};

struct FScctTutorialApi
{
	int32 Version;

	/** Reads the progress file into use: 1 read, 0 missing (no progress), -1 unreadable. */
	int32 (__cdecl* Load)(const TCHAR* Path);

	int32 (__cdecl* GetProgress)(FScctTutorialProgress* Progress);

	/** The map code of a side's tutorial (0 spies, 1 mercs); 0 past the end. */
	int32 (__cdecl* GetMap)(int32 Team, int32 Index, TCHAR* Buffer, int32 Capacity);

	/** The side's first tutorial not done; 0 when every one is. */
	int32 (__cdecl* NextTutorial)(int32 Team, TCHAR* Buffer, int32 Capacity);

	/** The running round as a tutorial play watches it; 1 while a session with a local controller runs. */
	int32 (__cdecl* GetRound)(FScctTutorialRound* Round);

	/** Marks a won tutorial map done and saves the file. 1 when it was marked. */
	int32 (__cdecl* FinishRound)(const TCHAR* Map, int32 bWon);

	/** The tutorial to play after this round ended (the next of that side not done); 0 for the Tutorials page. */
	int32 (__cdecl* NextAfter)(const TCHAR* Map, int32 bVisit, TCHAR* Buffer, int32 Capacity);

	/** Ends the local match's round with Team winning (negative: the local player's side) on the host's next tick. */
	int32 (__cdecl* ForceWin)(int32 Team);

	/** One gadget of the side's ClassInventory (Gadget may be null); returns how many the side has. */
	int32 (__cdecl* GetGadget)(int32 Team, int32 Index, FScctTutorialGadget* Gadget);

	/** Forgets all progress and saves. */
	void (__cdecl* Reset)();
};

static_assert(sizeof(FScctTutorialProgress) == 28, "FScctTutorialProgress must mirror BridgeTutorialProgress");
static_assert(sizeof(FScctTutorialRound) == 28, "FScctTutorialRound must mirror BridgeTutorialRound");
static_assert(sizeof(FScctTutorialGadget) == 664, "FScctTutorialGadget must mirror BridgeTutorialGadget");
static_assert(sizeof(FScctTutorialApi) == 8 + 10 * sizeof(void*), "FScctTutorialApi must mirror TutorialApi (64-bit)");

template <>
struct TScctFeatureInfo<FScctTutorialApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.TutorialExports"); }
	static constexpr int32 Version = ScctTutorialApiVersion;
};
