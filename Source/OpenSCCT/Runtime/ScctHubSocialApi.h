#pragma once

#include "CoreMinimal.h"

// The hub HUD's half of the C# bridge (an OpenSCCT addition): name tags, chat with its channels, the player list,
// world signs, the travel menu, the U prompt, the hub's keys, and the bot visitors of a hub this PC hosts. These
// mirror Bridge/OpenScct.UnrealBridge/Hub/HubSocialTypes.cs and HubSocialExports.cs field for field; change both
// together and bump the version. Bridge/Probes/HubProbe (mode "social") prints the sizes asserted below.
//
// The table is fetched like the other feature tables: ScctFeature<FScctHubSocialApi>() (Runtime/ScctFeatures.h).
// Everything reads the visit of the hub table (ScctHubApi.h); while not in a hub the reads return nothing. Positions
// are in UE units, the space Unreal's level is drawn in. Text is UTF-16, zero terminated. Game thread only.

constexpr int32 ScctHubSocialApiVersion = 1;

/** The hub's chat channels. */
enum class EScctHubChatChannel : int32
{
	Everyone = 0, // the whole hub
	Side = 1,     // the player's side only
	Nearby = 2,   // the players within 3000 units only
	System = 3,   // the hub's own lines; nobody sends on it
};

namespace EScctHubSocialFlags
{
	// FScctHubSocialState::Flags
	constexpr int32 InHub = 1;    // the visit's session is drawn
	constexpr int32 Running = 2;  // the player runs a course
	constexpr int32 InRound = 4;  // the player shoots a range round
	constexpr int32 OnPad = 8;    // the player stands on a match pad (GetText(Queue))
	constexpr int32 Hosting = 16; // this PC hosts the hub

	// FScctHubSocialPlayer::Flags and FScctHubSocialTag::Flags
	constexpr int32 Me = 1;
	constexpr int32 SignedIn = 2;  // signed in on the master, else a guest
	constexpr int32 ChatMuted = 4; // this player's chat lines are hidden for the visit
	constexpr int32 TagRankShift = 16; // FScctHubSocialTag::Flags only: the packed rank without its title (level, then prestige) sits above this bit
	constexpr int32 Talking = 8;   // heard on voice now (never set yet: the hub's voice is not carried)
	constexpr int32 Placed = 16;   // his pawn is in the drawn hub: X, Y, Z are where it stands

	// FScctHubSocialSign::Flags
	constexpr int32 Mounted = 1; // lies on a board's face (Yaw, Width, Height); else faces the camera
	constexpr int32 Visible = 2; // nothing of the hub's world stands between the eye and the sign
}

/** What the hub HUD reads every frame. */
struct FScctHubSocialState
{
	int32 Valid;            // 1 while in a hub
	int32 PlayerId;
	int32 Team;             // 0 spies, 1 mercs, -1 not said yet
	int32 ChatSerial;       // the newest chat line's serial (GetChat after an older one reads the lines since)
	int32 SignRevision;     // goes up when a sign changed
	int32 PlayerCount;
	int32 DestinationCount;
	int32 TravelMenuSerial; // goes up when the hub opened the travel menu for the player (he walked into a travel point)
	int32 Flags;            // EScctHubSocialFlags (the state's group)
	float Now;              // the play clock, seconds
};

/** One player of the hub, as the player list shows him: by side, then by name. */
struct FScctHubSocialPlayer
{
	int32 PlayerId;
	int32 Team;
	int32 PingMs;
	int32 Flags; // EScctHubSocialFlags (the player's group)
	float X, Y, Z;
	TCHAR Name[32];
};

/** A name over a pawn's head, farthest first (the order to draw them in). */
struct FScctHubSocialTag
{
	float X, Y, Z;     // where the tag goes, over the head
	float Distance;    // from the eye
	int32 PlayerId;
	int32 Team;
	int32 Flags;
	TCHAR Name[32];
	TCHAR Caption[32]; // what he is doing ("waves"), empty when not emoting
	TCHAR Bubble[96];  // what he said in the last few seconds, quoted, two short lines at most, else empty
};

/** One line of the hub's chat. */
struct FScctHubSocialChatLine
{
	int32 Serial;
	int32 PlayerId; // 0 for the hub's own lines
	int32 Team;     // the speaker's side, -1 for none
	int32 Channel;  // EScctHubChatChannel
	TCHAR Name[32];
	TCHAR Text[256];
};

/** A world text of the hub (a pad's sign, a leaderboard), nearest first. */
struct FScctHubSocialSign
{
	float X, Y, Z;
	float Scale;              // 1 for a standard sign (a line about 36 units tall)
	float Yaw, Width, Height; // the board's facing (65536 to a turn) and size, when Mounted
	float Distance;           // from the eye
	uint32 Rgba;              // 0xRRGGBBAA
	int32 Flags;              // EScctHubSocialFlags (the sign's group)
	TCHAR Id[48];
	TCHAR Text[512];          // lines separated by '\n'
};

/** A district the travel menu offers. */
struct FScctHubSocialDestination
{
	int32 BestMs; // the player's best course time there, -1 for none
	TCHAR Id[32];
	TCHAR Title[64];
};

/** The texts GetText reads. */
enum class EScctHubSocialText : int32
{
	Help = 0,      // the hub's keys in one line, from the player's bindings
	EmoteMenu = 1, // a line per emote with its key, then how to close the menu
	Queue = 2,     // the player's place in a pad's queue, empty when on none
	HubName = 3,
};

struct FScctHubSocialApi
{
	int32 Version;

	/** The state. Returns Valid. */
	int32 (__cdecl* GetState)(FScctHubSocialState* State);

	/** Writes up to Capacity players. Returns how many there are. */
	int32 (__cdecl* GetPlayers)(FScctHubSocialPlayer* Players, int32 Capacity);

	/** The name tags round the eye (within 4500 units). Returns how many there are. */
	int32 (__cdecl* GetNameTags)(float EyeX, float EyeY, float EyeZ, FScctHubSocialTag* Tags, int32 Capacity);

	/** The chat lines newer than serial After, oldest first (64 are kept). Returns how many there are. */
	int32 (__cdecl* GetChat)(int32 After, FScctHubSocialChatLine* Lines, int32 Capacity);

	/** The world signs within 7000 units of the eye, nearest first, with whether the world hides each. Returns how many there are. */
	int32 (__cdecl* GetSigns)(float EyeX, float EyeY, float EyeZ, FScctHubSocialSign* Signs, int32 Capacity);

	/** The travel menu's districts. Returns how many there are. */
	int32 (__cdecl* GetDestinations)(FScctHubSocialDestination* Destinations, int32 Capacity);

	/** Copies a text (EScctHubSocialText). Returns its full length. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** Copies what U does where the eye is ("U  next record"), empty for nothing. Returns its full length. */
	int32 (__cdecl* GetPrompt)(float EyeX, float EyeY, float EyeZ, TCHAR* Buffer, int32 Capacity);

	/** Copies the key the player's bindings put a command on ("P"), or "Unbound". Returns its full length. */
	int32 (__cdecl* GetKeyHint)(const TCHAR* Command, TCHAR* Buffer, int32 Capacity);

	/** Says a line on a channel ("/wave" plays an emote, "/l text" speaks to nearby players). Returns 1 when sent. */
	int32 (__cdecl* SendChat)(int32 Channel, const TCHAR* Text);

	/** Runs a hub command that talks to the hub: HubSpy, HubMerc, HubPlaza (HubRespawn), HubUse, HubCancel, HubWatch, HubEmote1..8. Returns 1 when it ran. */
	int32 (__cdecl* RunCommand)(const TCHAR* Command);

	/** Travels to the district with this id. Returns 1 when asked. */
	int32 (__cdecl* Travel)(const TCHAR* Id);

	/** Hides (1) or shows again (0) a player's chat lines for the visit, by name. */
	void (__cdecl* MuteChat)(const TCHAR* Name, int32 bMuted);

	/** Sends Count bot visitors into the hub this PC hosts (0 sends them away). Returns 1 when they are on their way. */
	int32 (__cdecl* StartBots)(int32 Count);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctHubSocialState) == 40, "FScctHubSocialState does not match HubSocialState");
static_assert(sizeof(FScctHubSocialPlayer) == 92, "FScctHubSocialPlayer does not match HubSocialPlayer");
static_assert(sizeof(FScctHubSocialTag) == 348, "FScctHubSocialTag does not match HubSocialTag");
static_assert(sizeof(FScctHubSocialChatLine) == 592, "FScctHubSocialChatLine does not match HubSocialChatLine");
static_assert(sizeof(FScctHubSocialSign) == 1160, "FScctHubSocialSign does not match HubSocialSign");
static_assert(sizeof(FScctHubSocialDestination) == 196, "FScctHubSocialDestination does not match HubSocialDestination");
static_assert(sizeof(FScctHubSocialApi) == 8 + 14 * sizeof(void*), "FScctHubSocialApi does not match HubSocialApi");
