using System;
using System.IO;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Demo;

/// <summary>
/// The player's demo-saving choice (an OpenSCCT feature, docs/DEMOS.md): whether this game saves its offline matches
/// and its online rounds as demos, and where. Ported from the Godot client's personal recording preference
/// (game/Scripts/Online/OnlineService.cs RecordDemos, DemoRecordingStatus and SetRecordDemos; OnlineSettings.RecordDemos;
/// PlayRoot's --svm-record giving PlaySettings.DemoFolder), with the offline and online halves as two choices.
/// <para>Offline: a local match started while <see cref="RecordOffline"/> is on gets the folder as its
/// <c>PlaySettings.DemoFolder</c>, so the play session records the host's packets from its first tick with game rules
/// and finishes the file when the session is stopped.</para>
/// <para>Online: every visit is made with the folder and <see cref="RecordOnline"/> (OnlineClientSettings.RecordDemos
/// and DemoFolder); a server with personal demos then records each round for this player and sends the demo, and its
/// training data when it has some, down after the round. A change while connected is told to the server at once
/// (<see cref="OnlineClient.SetRecordDemos"/>) and applies from the next round.</para>
/// <para>Unreal holds the choice (two options of its settings registry and -ScctRecord) and hands it over with
/// <see cref="Configure"/>, before any match starts and whenever it changes. Game thread only.</para>
/// </summary>
public static class DemoSaving
{
    /// <summary>The Godot client's wording for the personal recording preference (OnlineService, OnlineClient).</summary>
    public const string OnlineOnText = "Your upcoming matches will be saved to Demos on this computer.";

    public const string OnlineOffText = "Match recording is off for you.";

    private static int s_revision = 1;
    private static int s_seenHash;

    /// <summary>Where demos are saved (Unreal's Saved/Demos), or null before Unreal said.</summary>
    public static string? Folder { get; private set; }

    public static bool RecordOffline { get; private set; }

    public static bool RecordOnline { get; private set; }

    /// <summary>The demo the last local match recorded into (it stays after the match ended), or null.</summary>
    public static string? LastOfflineDemo { get; private set; }

    /// <summary>Takes Unreal's choice. A visit in progress is told about a changed online choice at once.</summary>
    public static void Configure(string? folder, bool offline, bool online)
    {
        string? before = Folder;
        bool onlineBefore = RecordOnline;
        Folder = string.IsNullOrWhiteSpace(folder) ? null : Path.GetFullPath(folder);
        RecordOffline = offline;
        RecordOnline = online;
        if (OnlineBridge.Existing?.Client is { } client && (onlineBefore != online || before != Folder) && client.RecordDemos != OnlineEnabled)
        {
            // The visit's settings already carry the folder (every visit is made with it), so only the choice changes.
            client.SetRecordDemos(OnlineEnabled);
        }

        s_revision++;
    }

    /// <summary>The folder a local match records into, made when missing, or null when offline matches are not saved.</summary>
    internal static string? OfflineFolder()
    {
        if (!RecordOffline || Folder is null)
        {
            return null;
        }

        Directory.CreateDirectory(Folder);
        return Folder;
    }

    /// <summary>Online rounds are saved (there is a folder and the choice is on).</summary>
    internal static bool OnlineEnabled => RecordOnline && Folder is not null;

    /// <summary>The folder for a visit's settings (made when missing), whether or not the choice is on: it can be turned on mid-visit.</summary>
    internal static string? VisitFolder()
    {
        if (Folder is null)
        {
            return null;
        }

        try
        {
            Directory.CreateDirectory(Folder);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return null;
        }

        return Folder;
    }

    /// <summary>A local match was started: remembers its demo.</summary>
    internal static void Started(BridgeSession session)
    {
        if (!session.IsRemote && session.Session.DemoPath is { } path)
        {
            LastOfflineDemo = path;
            s_revision++;
        }
    }

    /// <summary>The status line of online saving, as the lobby and the results show it (OnlineService.DemoRecordingStatus).</summary>
    public static string OnlineStatus =>
        OnlineBridge.Existing?.Client is { } client ? client.DemoRecordingStatus : OnlineEnabled ? OnlineOnText : OnlineOffText;

    /// <summary>The status line of offline saving.</summary>
    public static string OfflineStatus =>
        !RecordOffline ? "Offline matches are not saved." :
        Folder is null ? "Offline matches cannot be saved: there is no demo folder." :
        "Your offline matches will be saved to Demos on this computer.";

    /// <summary>The demo the local match being played records into, or null.</summary>
    public static string? CurrentOfflineDemo =>
        Exports.Current is { IsRemote: false, IsDisposed: false } session ? session.Session.DemoPath : null;

    /// <summary>The flags of <see cref="DemoInfoBlock"/>.</summary>
    public static int Flags()
    {
        int flags = 0;
        if (RecordOffline) flags |= DemoFlags.RecordOffline;
        if (RecordOnline) flags |= DemoFlags.RecordOnline;
        if (CurrentOfflineDemo is not null) flags |= DemoFlags.RecordingOffline;
        if (OnlineBridge.Existing?.Client is { } client)
        {
            if (client.RecordDemos) flags |= DemoFlags.VisitRecords;
            if (client.PendingDemoSave) flags |= DemoFlags.PendingSave;
            if (client.PendingTrainingSave) flags |= DemoFlags.PendingTraining;
            if (client.LastDemoPath is not null) flags |= DemoFlags.HasOnlineDemo;
        }

        if (LastOfflineDemo is not null) flags |= DemoFlags.HasOfflineDemo;
        return flags;
    }

    /// <summary>Goes up whenever anything the table describes changed (looked at when asked).</summary>
    public static int Revision()
    {
        var client = OnlineBridge.Existing?.Client;
        int hash = HashCode.Combine(Flags(), OnlineStatus, client?.LastDemoPath, CurrentOfflineDemo);
        if (hash != s_seenHash)
        {
            s_seenHash = hash;
            s_revision++;
        }

        return s_revision;
    }

    /// <summary>A text of the table (<see cref="DemoText"/>).</summary>
    public static string Text(DemoText kind) => kind switch
    {
        DemoText.Folder => Folder ?? string.Empty,
        DemoText.OnlineStatus => OnlineStatus,
        DemoText.OfflineStatus => OfflineStatus,
        DemoText.CurrentOfflineDemo => CurrentOfflineDemo ?? string.Empty,
        DemoText.LastOfflineDemo => LastOfflineDemo ?? string.Empty,
        DemoText.LastOnlineDemo => OnlineBridge.Existing?.Client?.LastDemoPath ?? string.Empty,
        _ => string.Empty,
    };
}

/// <summary>DemoInfoBlock.Flags (Source/OpenSCCT/Runtime/ScctDemoApi.h EScctDemoFlags).</summary>
public static class DemoFlags
{
    public const int RecordOffline = 1;
    public const int RecordOnline = 2;
    public const int RecordingOffline = 4;  // the local match being played records a demo
    public const int VisitRecords = 8;      // the online visit asked its server for personal demos
    public const int PendingSave = 16;      // a round of the visit is recording or its demo is still downloading
    public const int PendingTraining = 32;  // the demo is saved, its training data is still downloading
    public const int HasOnlineDemo = 64;
    public const int HasOfflineDemo = 128;
}

/// <summary>DemoApi.GetText's kinds (EScctDemoText).</summary>
public enum DemoText
{
    Folder = 0,
    OnlineStatus = 1,
    OfflineStatus = 2,
    CurrentOfflineDemo = 3,
    LastOfflineDemo = 4,
    LastOnlineDemo = 5,
}
