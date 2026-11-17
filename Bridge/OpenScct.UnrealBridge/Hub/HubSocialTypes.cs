using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Hub;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctHubSocialApi.h mirrors them field for
// field; change both together and bump HubSocialExports.ApiVersion.

/// <summary>The chat channels of the hub (HubHud's composer: Tab cycles the first three).</summary>
public enum HubChatChannel
{
    /// <summary>Everyone in the hub.</summary>
    Everyone = 0,

    /// <summary>The player's side only.</summary>
    Side = 1,

    /// <summary>The players within <see cref="OpenScct.Server.Hub.HubK.LocalChatRange"/> of the player only.</summary>
    Nearby = 2,

    /// <summary>The hub's own lines (welcome, side changes, notices); nobody can send on it.</summary>
    System = 3,
}

public static class HubSocialFlags
{
    // HubSocialState.Flags
    /// <summary>The visit's session is drawn (the HUD has something to show).</summary>
    public const int InHub = 1;

    /// <summary>The player runs a course.</summary>
    public const int Running = 2;

    /// <summary>The player shoots a range round.</summary>
    public const int InRound = 4;

    /// <summary>The player stands on a match pad (the queue line is GetText(Queue)).</summary>
    public const int OnPad = 8;

    /// <summary>This PC hosts the hub.</summary>
    public const int Hosting = 16;

    // HubSocialPlayer.Flags and HubSocialTag.Flags
    /// <summary>The local player.</summary>
    public const int Me = 1;

    /// <summary>Signed in on the master (else a guest).</summary>
    public const int SignedIn = 2;

    /// <summary>This player's chat lines are hidden for the visit.</summary>
    public const int ChatMuted = 4;

    /// <summary>Heard on voice right now (always off: the hub's voice is not carried yet).</summary>
    public const int Talking = 8;

    /// <summary>The player's pawn is in the drawn hub: X, Y and Z are where it stands.</summary>
    public const int Placed = 16;

    // HubSocialSign.Flags
    /// <summary>The sign lies on a board's face (Yaw, Width and Height are the board's); else it faces the camera.</summary>
    public const int Mounted = 1;

    /// <summary>Nothing of the hub's world stands between the eye and the sign.</summary>
    public const int Visible = 2;
}

/// <summary>What the hub HUD reads every frame (GetState).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HubSocialState
{
    /// <summary>1 while in a hub (the rest is valid), else 0.</summary>
    public int Valid;
    public int PlayerId;

    /// <summary>The side the hub says the player plays: 0 spies, 1 mercs, -1 not said yet.</summary>
    public int Team;

    /// <summary>The serial of the newest chat line (GetChat after an older serial reads the lines since).</summary>
    public int ChatSerial;

    /// <summary>Goes up when a sign was put up, changed or taken down.</summary>
    public int SignRevision;

    public int PlayerCount;
    public int DestinationCount;

    /// <summary>Goes up when the hub opened the travel menu for the player (he walked into a travel point).</summary>
    public int TravelMenuSerial;

    /// <summary><see cref="HubSocialFlags"/> (the state's group).</summary>
    public int Flags;

    /// <summary>The play clock (seconds), for the bubbles' and the notices' ages.</summary>
    public float Now;
}

/// <summary>One player of the hub as the player list shows him (GetPlayers).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubSocialPlayer
{
    public const int NameCapacity = 32;

    public int PlayerId;
    public int Team;
    public int PingMs;

    /// <summary><see cref="HubSocialFlags"/> (the player's group).</summary>
    public int Flags;
    public float X, Y, Z;
    public fixed char Name[NameCapacity];
}

/// <summary>A name over a pawn's head (GetNameTags), farthest first.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubSocialTag
{
    public const int NameCapacity = 32;
    public const int CaptionCapacity = 32;
    public const int BubbleCapacity = 96;

    /// <summary>Where the tag goes (UE units, over the head).</summary>
    public float X, Y, Z;

    /// <summary>From the eye, in UE units.</summary>
    public float Distance;
    public int PlayerId;
    public int Team;
    public int Flags;
    public fixed char Name[NameCapacity];

    /// <summary>What the player is doing ("waves"), empty when he is not emoting.</summary>
    public fixed char Caption[CaptionCapacity];

    /// <summary>What he said in the last few seconds, quoted and cut into two short lines at most, else empty.</summary>
    public fixed char Bubble[BubbleCapacity];
}

/// <summary>One line of the hub's chat (GetChat).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubSocialChatLine
{
    public const int NameCapacity = 32;
    public const int TextCapacity = 256;

    public int Serial;

    /// <summary>The speaker's id, 0 for the hub's own lines.</summary>
    public int PlayerId;

    /// <summary>The speaker's side (0 spies, 1 mercs), -1 for none.</summary>
    public int Team;

    /// <summary><see cref="HubChatChannel"/>.</summary>
    public int Channel;
    public fixed char Name[NameCapacity];
    public fixed char Text[TextCapacity];
}

/// <summary>A world text of the hub (GetSigns): a pad's sign, a leaderboard, a direction.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubSocialSign
{
    public const int IdCapacity = 48;
    public const int TextCapacity = 512;

    public float X, Y, Z;

    /// <summary>1 for a standard sign (a line about 36 units tall).</summary>
    public float Scale;

    /// <summary>The board's facing (65536 to a turn), width and height in units, when <see cref="HubSocialFlags.Mounted"/>.</summary>
    public float Yaw, Width, Height;

    /// <summary>From the eye, in UE units.</summary>
    public float Distance;

    /// <summary>0xRRGGBBAA.</summary>
    public uint Rgba;
    public int Flags;
    public fixed char Id[IdCapacity];

    /// <summary>Lines separated by '\n'.</summary>
    public fixed char Text[TextCapacity];
}

/// <summary>A district the travel menu offers (GetDestinations).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubSocialDestination
{
    public const int IdCapacity = 32;
    public const int TitleCapacity = 64;

    /// <summary>The player's best course time there in ms, -1 for none.</summary>
    public int BestMs;
    public fixed char Id[IdCapacity];
    public fixed char Title[TitleCapacity];
}

/// <summary>The texts GetText reads.</summary>
public enum HubSocialText
{
    /// <summary>The hub's keys in one line ("Hub   F4 travel   J spy   K merc ...").</summary>
    Help = 0,

    /// <summary>The emote menu: a line per emote with its key, then how to close it.</summary>
    EmoteMenu = 1,

    /// <summary>The player's place in a pad's queue ("Queued on Pad 2 as a merc: 3/4"), empty when on none.</summary>
    Queue = 2,

    /// <summary>The hub's name.</summary>
    HubName = 3,
}
