#pragma once

#include "CoreMinimal.h"

// The account half of the C# bridge's social services (an OpenSCCT addition): the signed-in account on the master,
// its rank and progress, its titles, the Account page's actions and the settings sync. These mirror
// Bridge/OpenScct.UnrealBridge/Social/AccountTypes.cs and AccountExports.cs field for field; change both together and
// bump the version. Bridge/Probes/SocialProbe ("account") prints the sizes asserted below.
//
// The table is fetched like the other feature tables: ScctFeature<FScctAccountApi>() (Runtime/ScctFeatures.h). The
// service runs inside the social table's Update, so it moves only while UScctSocialSubsystem ticks. Text is UTF-16,
// zero terminated. Game thread only.

constexpr int32 ScctAccountApiVersion = 1;

/** FScctAccountInfo::View: which part of the Account page applies. */
enum class EScctAccountView : int32
{
	NoMaster = 0,  // no master server is set: there are no accounts
	SignedOut = 1,
	SigningIn = 2, // a sign-in waits for the player to finish it in the browser
	PickName = 3,  // signed in, the account has no name yet
	SignedIn = 4,
};

namespace EScctAccountFlags
{
	constexpr int32 Admin = 1;
	constexpr int32 Preview = 2;        // a made-up account (-ScctAccountPreview): nothing is asked of the master
	constexpr int32 RefreshFailed = 4;  // the last refresh failed; what is shown is from before
	constexpr int32 HasProgress = 8;    // the master has levels: rank, XP and titles are filled
	constexpr int32 Claiming = 16;
	constexpr int32 SavingPronouns = 32;
	constexpr int32 Prestiging = 64;
	constexpr int32 SigningOut = 128;
	constexpr int32 CanPrestige = 256;
	constexpr int32 AtCap = 512;
	constexpr int32 Master = 1024;      // level 55 at the last prestige
	constexpr int32 Known = 2048;       // the master described the account this run (stats are filled)
	constexpr int32 NameLocked = 4096;  // the name cannot be changed yet
	constexpr int32 Refreshing = 8192;
}

/** FScctAccountInfo::SyncState. */
enum class EScctProfileSyncState : int32
{
	Off = 0,     // not signed in, or the sync is off for this run
	Idle = 1,
	Queued = 2,  // a change waits out its 3 s before it is sent
	Sending = 3,
	Pulling = 4, // the account's copy is asked for or being applied
};

struct FScctAccountInfo
{
	int32 View;      // EScctAccountView
	int32 Flags;     // EScctAccountFlags
	int32 Revision;  // goes up whenever anything here or in the texts changed
	int32 Pronouns;  // index of the account's pronouns in the presets, -1 for another set
	int64 AccountId;
	int64 NameChangeAllowed; // Unix seconds the name may next be changed (0: now)
	int32 Prestige;
	int32 Level;
	int32 XpIntoLevel;
	int32 XpForNext; // 0 at the cap
	int64 TotalXp;
	int32 BestLevel;
	int32 RankColour; // 0xRRGGBB, the rank emblem's colour
	int32 TitlesUnlocked;
	int32 TitlesTotal;
	int32 Matches;
	int32 SpyWins;
	int32 MercWins;
	int32 Losses;
	int32 Kills;
	int32 Deaths;
	int32 TitleChosen; // the chosen title's bit when the account has it, else -1
	int32 SyncState;   // EScctProfileSyncState
};
static_assert(sizeof(FScctAccountInfo) == 104, "FScctAccountInfo must match the bridge's AccountInfoBlock");

/** GetText's kinds. */
enum class EScctAccountText : int32
{
	Name = 0,
	Pronouns = 1,
	Status = 2,          // the page's last message
	NameCooldown = 3,    // "You can change your name again on 3 March 2027." while locked
	RankTag = 4,         // "42", "P3 · 42" or "Master · 55"
	RankDescription = 5, // "Prestige 3, level 42 - Field Agent"
	NextLevel = 6,       // "1,250 / 2,150 XP to level 43" or "Level 55 · Maximum level"
	TotalXp = 7,         // "120,400 lifetime XP"
	Title = 8,           // the chosen title, or "No title equipped"
	PronounsExample = 9, // "Name used their last life"
	PrestigeHelp = 10,
	XpRules = 11,
	CareerHelp = 12,
	SignInCode = 13,
};

struct FScctAccountTitle
{
	int32 Bit;
	int32 Unlocked;
	int32 Chosen;
};
static_assert(sizeof(FScctAccountTitle) == 12, "FScctAccountTitle must match the bridge's AccountTitle");

/** A packed rank as the lobby carries it (FScctOnlinePlayer::Rank), unpacked by DescribeRank. */
struct FScctAccountRank
{
	int32 Level;    // 0: no rank (a guest, a bot)
	int32 Prestige;
	int32 TitleBit; // -1: no title shown
	int32 Colour;   // 0xRRGGBB
	int32 Master;
};
static_assert(sizeof(FScctAccountRank) == 20, "FScctAccountRank must match the bridge's AccountRank");

/** GetIncomingProfile's parts. */
enum class EScctProfilePart : int32
{
	Settings = 0, // the settings registry's values, a JSON object of key and text
	Controls = 1, // controls.json (empty: keep this PC's)
	Bindings = 2, // bindings.json, both sides' key layouts (empty: keep this PC's)
};

namespace EScctAccountStartFlags
{
	constexpr int32 SyncOff = 1; // never send or take settings this run
	constexpr int32 Preview = 2; // show a made-up account
}

struct FScctAccountApi
{
	int32 Version;

	/** Starts the service once with the file that keeps its state between runs, and EScctAccountStartFlags. */
	void (__cdecl* Start)(const TCHAR* StateFile, int32 Flags);

	int32 (__cdecl* GetInfo)(FScctAccountInfo* Info);

	/** A text (EScctAccountText). Returns its full length. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);

	/** The catalogue's title at Index with its id, text and how it is unlocked. Returns 0 past the end. */
	int32 (__cdecl* GetTitle)(int32 Index, FScctAccountTitle* Title, TCHAR* Id, int32 IdCapacity, TCHAR* Text, int32 TextCapacity, TCHAR* How, int32 HowCapacity);

	/** The pronoun set at Index of the presets. Returns its length, or -1 past the end. */
	int32 (__cdecl* GetPronounPreset)(int32 Index, TCHAR* Buffer, int32 Capacity);

	/** Asks the master for the account (at most once a minute unless bForce). */
	void (__cdecl* Refresh)(int32 bForce);

	/** Claims or changes the name. Returns 1 when the master is asked; otherwise the Status text says why. */
	int32 (__cdecl* ClaimName)(const TCHAR* Name);

	int32 (__cdecl* SetPronouns)(int32 Preset);

	/** Enters the next prestige (the page has the player confirm first). */
	int32 (__cdecl* Prestige)();

	/** Ends the sign-in on the master and forgets it here; the social table then asks Unreal to delete the token. */
	int32 (__cdecl* SignOut)();

	/** The title the settings hold (its id, empty for none). */
	void (__cdecl* SetChosenTitle)(const TCHAR* Id);

	/** This PC's synced settings: registry values (JSON), controls.json, bindings.json; bChanged queues a send 3 s later. */
	void (__cdecl* SetLocalProfile)(const TCHAR* Settings, const TCHAR* Controls, const TCHAR* Bindings, int32 bChanged);

	/** A part of the account's settings waiting to be applied (EScctProfilePart). Returns its full length, or -1 when none waits. */
	int32 (__cdecl* GetIncomingProfile)(int32 Part, TCHAR* Buffer, int32 Capacity);

	/** The waiting settings were applied (1) or could not be (0). */
	void (__cdecl* FinishIncomingProfile)(int32 bApplied);

	/** A packed rank unpacked, and its line ("P3 · 42 - Field Agent"). Returns the line's full length. */
	int32 (__cdecl* DescribeRank)(int32 Packed, FScctAccountRank* Rank, TCHAR* Text, int32 Capacity);

	/** The prestige emblem's colour, 0xRRGGBB (1 to 10; 11 the master emblem's). */
	int32 (__cdecl* EmblemColour)(int32 Prestige);
};
static_assert(sizeof(FScctAccountApi) == 8 + 16 * sizeof(void*), "FScctAccountApi does not match AccountApi");
