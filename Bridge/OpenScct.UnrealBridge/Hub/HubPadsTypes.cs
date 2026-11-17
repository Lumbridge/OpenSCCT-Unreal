using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Hub;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctHubPadsApi.h mirrors them field for field;
// change both together and bump HubPadsExports.ApiVersion. Bridge/Probes/HubProbe (mode "social") prints their sizes.

public static class HubPadsFlags
{
    // HubPadsState.Flags
    /// <summary>The visit's session is drawn: the pads can be shown.</summary>
    public const int InHub = 1;

    /// <summary>A pad's laptop is open (GetText(LaptopPad) says which; the laptop's rows and maps can be read).</summary>
    public const int Laptop = 2;

    /// <summary>The player is away at a pad's match (GetText(MatchStatus)).</summary>
    public const int InMatch = 4;

    /// <summary>This PC hosts the hub.</summary>
    public const int Hosting = 8;

    // HubPadRow.Flags
    /// <summary>The player is queued on this pad.</summary>
    public const int Queued = 1;

    /// <summary>The pad's match was set at its laptop (else the hub's own).</summary>
    public const int Customised = 2;

    /// <summary>Picture names the map's picture, a PNG on this PC.</summary>
    public const int HasPicture = 4;

    /// <summary>The pad's sign is known: Sign* say where it stands.</summary>
    public const int HasSign = 8;

    /// <summary>The sign lies on a board's face (SignYaw, SignWidth, SignHeight); else it faces the camera.</summary>
    public const int SignMounted = 16;

    /// <summary>The player is queued here and ready.</summary>
    public const int Ready = 32;

    // HubLaptopMap.Flags
    /// <summary>The laptop's draft plays this map.</summary>
    public const int Selected = 1;
}

/// <summary>The texts HubPadsExports.GetText copies.</summary>
public enum HubPadsText
{
    /// <summary>The player's place in a pad's queue ("Queued on Pad 2 as a merc: 3/4, ..."), empty when on none.</summary>
    Queue = 0,

    /// <summary>The id of the pad the player is queued on, empty when none.</summary>
    QueuePad = 1,

    /// <summary>The note about the last match (going, back, why), empty once it is old.</summary>
    Note = 2,

    /// <summary>What the trip to a pad's match is doing, empty when there is none.</summary>
    MatchStatus = 3,

    /// <summary>The id of the pad whose laptop is open, empty when none.</summary>
    LaptopPad = 4,

    /// <summary>What the laptop says (applied, why not).</summary>
    LaptopStatus = 5,

    /// <summary>The laptop draft's map code.</summary>
    LaptopMap = 6,

    /// <summary>The laptop draft in one line (map, mode, rules, bots).</summary>
    LaptopSummary = 7,
}

/// <summary>What the pads' view reads every frame.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HubPadsState
{
    /// <summary>1 while in a hub (or away at one of its matches).</summary>
    public int Valid;

    public int PadCount;

    /// <summary>The hub bridge's revision (goes up when anything shown changed).</summary>
    public int Revision;

    /// <summary>Goes up each time the hub opened a pad's laptop for the player.</summary>
    public int LaptopSerial;

    /// <summary>The player's side in his queue (0 spies, 1 mercs), -1 when not queued.</summary>
    public int QueueSide;

    /// <summary>HubPadsFlags (the state's group).</summary>
    public int Flags;

    /// <summary>The bridge's clock, seconds (the overlay's pulse).</summary>
    public float Now;

    public int Reserved;
}

/// <summary>One match pad of the hub.</summary>
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public unsafe struct HubPadRow
{
    /// <summary>HubPadState: 0 off, 1 idle, 2 waiting, 3 countdown, 4 starting.</summary>
    public int State;

    /// <summary>Whole seconds left of the countdown (0 outside it).</summary>
    public int Seconds;

    /// <summary>The pad's number (Pad2 is 2).</summary>
    public int Number;

    /// <summary>EGameMode of its match.</summary>
    public int Mode;

    public int TeamSize;
    public int Spies;
    public int SpySlots;
    public int Mercs;
    public int MercSlots;

    /// <summary>HubPadsFlags (the pad's group).</summary>
    public int Flags;

    /// <summary>The state's colour, 0xRRGGBBAA.</summary>
    public uint Rgba;

    /// <summary>The pad's floor rectangle (UE units) and the height of its floor.</summary>
    public float LoX, LoY, HiX, HiY, FloorZ;

    /// <summary>The pad's sign: where it stands and, mounted, its board's facing (65536 to a turn) and size.</summary>
    public float SignX, SignY, SignZ, SignYaw, SignWidth, SignHeight;

    public fixed char Id[16];

    /// <summary>The map code (SteeE).</summary>
    public fixed char Map[32];

    /// <summary>The map's name as the menus show it.</summary>
    public fixed char Title[64];

    /// <summary>The theme kit the pad shows for its map (hub, polar, steel, aquarius, lab, museum).</summary>
    public fixed char Theme[16];

    public fixed char ModeName[32];

    /// <summary>The map's picture, a PNG path on this PC (HasPicture), else empty.</summary>
    public fixed char Picture[260];
}

/// <summary>A row of the open laptop: a setting of the pad's next match.</summary>
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public unsafe struct HubLaptopRow
{
    /// <summary>The picked value's index of Count.</summary>
    public int Index;

    public int Count;

    /// <summary>1 when it can be changed now (bots per side only with bots per side).</summary>
    public int Enabled;

    public fixed char Key[24];
    public fixed char Label[32];
    public fixed char Value[48];
}

/// <summary>A map the open laptop offers.</summary>
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public unsafe struct HubLaptopMap
{
    /// <summary>HubPadsFlags (the laptop map's group).</summary>
    public int Flags;

    public fixed char Id[32];
    public fixed char Name[64];

    /// <summary>The modes it plays, comma separated.</summary>
    public fixed char Modes[32];

    /// <summary>Its picture, a PNG path on this PC, or empty.</summary>
    public fixed char Picture[260];
}
