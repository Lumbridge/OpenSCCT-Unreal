using System;
using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge.Social;

/// <summary>What Unreal is asked to do (SocialRequest::Kind): a friend's Join from Discord or a website join, ready to go.</summary>
public enum SocialRequestKind
{
    None = 0,

    /// <summary>Join the online server at Value ("host:port").</summary>
    JoinAddress = 1,

    /// <summary>Join the hub at Value ("host:port").</summary>
    JoinHub = 2,

    /// <summary>Join the server the master lists as Value (through <c>SocialExports.JoinMaster</c>); Text is its name.</summary>
    JoinMaster = 3,

    /// <summary>Join the hub the master lists as Value (through <c>SocialExports.JoinHubListed</c>); Text is its name.</summary>
    JoinHubListed = 4,

    /// <summary>Show Text to the player for a few seconds.</summary>
    Notice = 5,

    /// <summary>Show Text and the page where the player signs in: a website join goes on once he is signed in.</summary>
    SignIn = 6,

    /// <summary>The master refused the saved sign-in: forget it here (the player signs in again).</summary>
    SignedOut = 7,

    /// <summary>This process only handed its website join to the game already running: quit.</summary>
    Quit = 8,
}

/// <summary>SocialRequest::Flags.</summary>
[Flags]
public enum SocialRequestFlags
{
    None = 0,

    /// <summary>The server asks for a password: ask the player before joining.</summary>
    NeedsPassword = 1,

    /// <summary>Join only with the master's ticket for the signed-in account (a website join).</summary>
    RequireAccount = 2,

    /// <summary>The request came from the website's Join button.</summary>
    FromWebsite = 4,

    /// <summary>The request came from a friend's Join on Discord.</summary>
    FromDiscord = 8,
}

/// <summary>One request for Unreal (Source/OpenSCCT/Runtime/ScctSocialApi.h, FScctSocialRequest). Value and Text go beside it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct SocialRequest
{
    public int Kind;  // SocialRequestKind
    public int Flags; // SocialRequestFlags
}

/// <summary>Update's readiness flags from Unreal.</summary>
[Flags]
public enum SocialReadiness
{
    None = 0,

    /// <summary>The front end is up (not in a match or the hub): a friend's Join may start now, leaving a visit.</summary>
    InMenus = 1,

    /// <summary>The front end is up and nothing is being visited or hosted: a website join may start now.</summary>
    Idle = 2,
}

/// <summary>The Discord connection as Unreal reads it (SocialStatus::Discord).</summary>
public enum DiscordState
{
    Off = 0,
    Connecting = 1,
    NotRunning = 2,
    Ready = 3,
}

/// <summary>What Unreal reads after each Update (FScctSocialStatus).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct SocialStatus
{
    public int Discord;          // DiscordState
    public int DiscordCanJoin;   // 1 when the game is registered with Discord and friends' Joins are taken
    public int WebJoinListening; // 1 while this game owns the website join pipe
    public int WebJoinPending;   // 1 while a website join waits for the menus or the sign-in
    public int PresenceLeases;   // presence leases the master accepted so far
    public int RequestsWaiting;  // requests waiting for TakeRequest
    public int LogWaiting;       // log lines waiting for TakeLog
}

/// <summary>The join secret of the online visit (GetVisitJoin, FScctSocialJoin). The value goes beside it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct SocialJoin
{
    public int Kind;     // DiscordJoinKind, -1 none
    public int Password; // 1 when the server asks for a password
}
