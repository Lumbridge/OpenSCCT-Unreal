using System;
using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Social;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctAccountApi.h mirrors them field for field;
// change both together and bump AccountExports.ApiVersion. Bridge/Probes/SocialProbe prints their sizes.

/// <summary>Which part of the Account page applies (the Godot client's AccountScreen.View).</summary>
public enum AccountView
{
    /// <summary>No master server is set: there are no accounts.</summary>
    NoMaster = 0,

    SignedOut = 1,

    /// <summary>A sign-in waits for the player to finish it in the browser (the online subsystem started it).</summary>
    SigningIn = 2,

    /// <summary>Signed in, but the account has no name yet: the player must claim one.</summary>
    PickName = 3,

    SignedIn = 4,
}

[Flags]
public enum AccountFlags
{
    None = 0,
    Admin = 1,

    /// <summary>The page shows a made-up account (-ScctAccountPreview): nothing is asked of the master.</summary>
    Preview = 2,

    /// <summary>The last refresh failed; what is shown is from before.</summary>
    RefreshFailed = 4,

    /// <summary>The master has levels: rank, XP and titles are filled.</summary>
    HasProgress = 8,

    Claiming = 16,

    // 32 is unused.

    Prestiging = 64,
    SigningOut = 128,
    CanPrestige = 256,
    AtCap = 512,

    /// <summary>Level 55 at the last prestige: the master prestige emblem.</summary>
    Master = 1024,

    /// <summary>The master described the account this run (stats are filled).</summary>
    Known = 2048,

    /// <summary>The name cannot be changed yet (NameChangeAllowed says when).</summary>
    NameLocked = 4096,

    Refreshing = 8192,
}

/// <summary>Where the settings sync stands (AccountInfoBlock.SyncState).</summary>
public enum ProfileSyncState
{
    /// <summary>Not signed in, or the sync is turned off for this run.</summary>
    Off = 0,

    Idle = 1,

    /// <summary>A change waits out its 3 s before it is sent.</summary>
    Queued = 2,

    Sending = 3,

    /// <summary>The account's copy is being asked for.</summary>
    Pulling = 4,
}

/// <summary>The account as the Account page shows it (FScctAccountInfo).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct AccountInfoBlock
{
    public int View;
    public int Flags;
    public int Revision;

    /// <summary>The prestige emblems reached: one per prestige, and the master emblem (0 to 11).</summary>
    public int EmblemsReached;

    public long AccountId;

    /// <summary>When the name may next be changed, Unix seconds (0: now).</summary>
    public long NameChangeAllowed;

    public int Prestige;
    public int Level;
    public int XpIntoLevel;

    /// <summary>What the next level costs (0 at the cap).</summary>
    public int XpForNext;

    public long TotalXp;
    public int BestLevel;

    /// <summary>The colour of the rank's emblem, 0xRRGGBB (RankIcons.ColourOf).</summary>
    public int RankColour;

    public int TitlesUnlocked;
    public int TitlesTotal;
    public int Matches;
    public int SpyWins;
    public int MercWins;
    public int Losses;
    public int Kills;
    public int Deaths;

    /// <summary>The chosen title's index in the catalogue when the account has it, else -1.</summary>
    public int TitleChosen;

    public int SyncState;
}

/// <summary>A text of the account (AccountExports.GetText).</summary>
public enum AccountText
{
    /// <summary>The claimed name (empty until claimed).</summary>
    Name = 0,

    /// <summary>The page's last message ("Name claimed: ...", the master's refusal).</summary>
    Status = 1,

    /// <summary>"You can change your name again on 3 March 2027." while the name is locked.</summary>
    NameCooldown = 2,

    /// <summary>"42", "P3 · 42" or "Master · 55".</summary>
    RankTag = 3,

    /// <summary>"Prestige 3, level 42 - Field Agent".</summary>
    RankDescription = 4,

    /// <summary>"1,250 / 2,150 XP to level 43", or "Level 55 · Maximum level".</summary>
    NextLevel = 5,

    /// <summary>"120,400 lifetime XP".</summary>
    TotalXp = 6,

    /// <summary>The chosen title's text, or "No title equipped".</summary>
    Title = 7,

    /// <summary>The prestige button's line: what prestige does, or "Master prestige · The highest rank.".</summary>
    PrestigeHelp = 8,

    /// <summary>The XP rules as one line (the Godot page's tooltip).</summary>
    XpRules = 9,

    /// <summary>The career snapshot's footnote.</summary>
    CareerHelp = 10,

    /// <summary>The sign-in code while a sign-in waits.</summary>
    SignInCode = 11,
}

/// <summary>One title of the catalogue (FScctAccountTitle); its id, text and how it is unlocked go beside it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct AccountTitle
{
    public int Bit;
    public int Unlocked;
    public int Chosen;
}

/// <summary>A packed rank as the lobby carries it (FScctOnlinePlayer.Rank), unpacked (FScctAccountRank).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct AccountRank
{
    /// <summary>0 for no rank (a guest, a bot).</summary>
    public int Level;

    public int Prestige;

    /// <summary>The shown title's bit, -1 for none.</summary>
    public int TitleBit;

    public int Colour;
    public int Master;
}

/// <summary>A part of the account's settings record for Unreal to apply (AccountExports.GetIncomingProfile).</summary>
public enum ProfilePart
{
    /// <summary>The settings registry's values as a JSON object of key and text.</summary>
    Settings = 0,

    /// <summary>controls.json as text (empty: keep this PC's).</summary>
    Controls = 1,

    /// <summary>bindings.json as text, both sides' key layouts (empty: keep this PC's).</summary>
    Bindings = 2,
}
