using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Hub;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The table of hub entry points Unreal calls (Source/OpenSCCT/Runtime/ScctHubApi.h mirrors it). Unreal asks for
/// <see cref="HubExports.GetApi"/> once and keeps the table. Every entry catches its exceptions (none may cross into
/// native code). Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HubApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, void> AddMapRoot;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int> Connect;
    public delegate* unmanaged[Cdecl]<int, char*, int, int, int> HostAndJoin;
    public delegate* unmanaged[Cdecl]<void> Leave;
    public delegate* unmanaged[Cdecl]<int> Update;
    public delegate* unmanaged[Cdecl]<HubStatus*, int> GetStatus;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<void> ReportLoaded;
    public delegate* unmanaged[Cdecl]<char*, int, int> TakeLog;

    // Version 2: the master's hub list, listed joins and the hosted hub's listing.
    public delegate* unmanaged[Cdecl]<char*, char*, char*, void> ConfigureMaster;
    public delegate* unmanaged[Cdecl]<void> RequestList;
    public delegate* unmanaged[Cdecl]<int> GetListState;
    public delegate* unmanaged[Cdecl]<HubListEntry*, int, int> GetList;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> JoinListed;
    public delegate* unmanaged[Cdecl]<int, void> SetHostListed;
}

public static unsafe class HubExports
{
    private static HubBridge Hub => HubBridge.Instance;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HubApi* api)
    {
        if (api == null)
        {
            return HubBridge.ApiVersion;
        }

        api->Version = HubBridge.ApiVersion;
        api->AddMapRoot = &AddMapRoot;
        api->Connect = &Connect;
        api->HostAndJoin = &HostAndJoin;
        api->Leave = &Leave;
        api->Update = &Update;
        api->GetStatus = &GetStatus;
        api->GetText = &GetText;
        api->ReportLoaded = &ReportLoaded;
        api->TakeLog = &TakeLog;
        api->ConfigureMaster = &ConfigureMaster;
        api->RequestList = &RequestList;
        api->GetListState = &GetListState;
        api->GetList = &GetList;
        api->JoinListed = &JoinListed;
        api->SetHostListed = &SetHostListed;
        return HubBridge.ApiVersion;
    }

    /// <summary>A folder that holds SVMap folders, searched first for the hub map (seen by the next visit).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void AddMapRoot(char* folder)
    {
        string? path = Text(folder);
        Guard(() => Hub.AddMapRoot(path ?? string.Empty));
    }

    /// <summary>
    /// Joins the hub at "host", "host:port" or "[v6]:port" (port 28886 when none) as <paramref name="name"/> on
    /// <paramref name="team"/> (0 spies, 1 mercs). Returns 1 when the visit started; follow it with GetStatus.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Connect(char* address, char* name, int team)
    {
        string? where = Text(address), who = Text(name);
        try
        {
            if (string.IsNullOrWhiteSpace(where))
            {
                Hub.Fail("Enter the hub's address.");
                return 0;
            }

            Hub.Join(where, who, team);
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>
    /// Opens a hub on this PC on UDP <paramref name="port"/> (0: the hub's default port; every interface unless
    /// <paramref name="loopbackOnly"/>) and joins it. Returns 1 when it is up and the join started, 0 with the reason
    /// in GetText(Error).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int HostAndJoin(int port, char* name, int team, int loopbackOnly)
    {
        string? who = Text(name);
        try
        {
            return Hub.HostAndJoin(port, who, team, loopbackOnly != 0) ? 1 : 0;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Leaves the hub, closes the hub this PC hosts, and clears a failure. The state is Idle afterwards.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Leave() => Guard(() => Hub.Leave());

    /// <summary>Once a frame, before the main table's Update: runs the visit's ticks that are due. Returns the ticks run.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Update()
    {
        try
        {
            return Hub.Update();
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Writes the visit's status. Returns its state.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetStatus(HubStatus* status)
    {
        if (status == null)
        {
            return 0;
        }

        try
        {
            *status = Status(Hub);
            return status->State;
        }
        catch (Exception e)
        {
            Failed(e);
            *status = default;
            status->State = (int)HubState.Failed;
            status->Team = -1;
            return status->State;
        }
    }

    /// <summary>Copies a text (<see cref="HubText"/>) into <paramref name="buffer"/>. Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, TextOf(Hub, (HubText)kind));
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>Unreal's hub level is up and presents the visit's session: the hub may place the player.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ReportLoaded() => Guard(() => Hub.ReportLoaded());

    /// <summary>
    /// Copies the next waiting log line into <paramref name="buffer"/> and drops it. Returns its full length, or -1
    /// when no line waits.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeLog(char* buffer, int capacity)
    {
        try
        {
            return Hub.TakeLog(out string line) ? OnlineTextCopy.Buffer(buffer, capacity, line) : -1;
        }
        catch (Exception e)
        {
            Failed(e);
            return -1;
        }
    }

    /// <summary>
    /// The master the hub list comes from and the saved sign-in, used when the online side has not set them yet (a
    /// master or token already set is kept). Null or empty values are skipped.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void ConfigureMaster(char* url, char* inviteKey, char* token)
    {
        string? address = Text(url), key = Text(inviteKey), saved = Text(token);
        Guard(() => Hub.ConfigureMaster(address, key, saved));
    }

    /// <summary>Asks the master for its hubs; follow it with GetListState and GetList (the revision moves when it is in).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void RequestList() => Guard(() => Hub.RequestList());

    /// <summary>Where the hub list is (<see cref="HubListState"/>); its line is GetText(ListMessage).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetListState()
    {
        try
        {
            return (int)Hub.ListState;
        }
        catch (Exception e)
        {
            Failed(e);
            return (int)HubListState.Failed;
        }
    }

    /// <summary>Writes the hub list in the page's order. Returns the number of hubs (only <paramref name="capacity"/> are written).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetList(HubListEntry* entries, int capacity)
    {
        try
        {
            return List(Hub, entries == null || capacity <= 0 ? default : new Span<HubListEntry>(entries, capacity));
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>
    /// Joins the hub at <paramref name="index"/> of GetList as <paramref name="name"/> on <paramref name="team"/>:
    /// the master is asked the way in first (state Connecting, flag AskingMaster). Returns 1 when that started, 0 with
    /// the reason in GetText(Error).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int JoinListed(int index, char* name, int team)
    {
        string? who = Text(name);
        try
        {
            return Hub.JoinListed(index, who, team) ? 1 : 0;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Whether a hub hosted from now on lists itself in the master's hub list (default on).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetHostListed(int listed) => Guard(() => Hub.HostListed = listed != 0);

    // ---- the managed side of the reads (the probe calls these too) ----------------------------------------------

    /// <summary>The hub list as <c>GetList</c> writes it. Returns the number of hubs.</summary>
    public static int List(HubBridge hub, Span<HubListEntry> target)
    {
        IReadOnlyList<ListedHub> hubs = hub.Hubs;
        for (int i = 0; i < hubs.Count && i < target.Length; i++)
        {
            ListedHub h = hubs[i];
            OpenScct.Net.Discovery.ServerEntry e = h.Entry;
            int flags = (e.Trusted ? HubListFlags.Official : 0) | (e.Dedicated ? HubListFlags.Dedicated : 0)
                | (e.RequireAccounts ? HubListFlags.RequireAccounts : 0) | (h.PingMs >= 0 ? HubListFlags.Pinged : 0)
                | h.Problem switch { "full" => HubListFlags.Full, "another build" => HubListFlags.OtherBuild, "another hub map" => HubListFlags.OtherMap, _ => 0 };
            ref HubListEntry entry = ref target[i];
            entry.Players = e.Players;
            entry.MaxPlayers = e.MaxPlayers;
            entry.PingMs = h.PingMs;
            entry.Flags = flags;
            fixed (HubListEntry* p = &entry)
            {
                OnlineTextCopy.Fixed(p->Id, HubListEntry.IdCapacity, e.Id);
                OnlineTextCopy.Fixed(p->Name, HubListEntry.NameCapacity, e.Name);
                OnlineTextCopy.Fixed(p->Region, HubListEntry.RegionCapacity, e.Region);
                OnlineTextCopy.Fixed(p->Problem, HubListEntry.ProblemCapacity, h.Problem);
            }
        }

        return hubs.Count;
    }

    /// <summary>The visit's status as <c>GetStatus</c> writes it.</summary>
    public static HubStatus Status(HubBridge hub)
    {
        OpenScct.Server.Hub.HubClient? client = hub.Client;
        int flags = 0;
        if (client is not null)
        {
            flags |= client.Welcomed ? HubStatusFlags.Welcomed : 0;
        }

        flags |= hub.Round is not null ? HubStatusFlags.RoundActive : 0;
        flags |= hub.NeedsLoadReport ? HubStatusFlags.NeedsLoadReport : 0;
        flags |= hub.Round?.Client.ReportedWaiting == true ? HubStatusFlags.ReportedWaiting : 0;
        flags |= hub.Host is not null ? HubStatusFlags.Hosting : 0;
        flags |= hub.Round?.LocalPawn is not null ? HubStatusFlags.LocalPawn : 0;
        flags |= hub.Host?.Server.Master is not null ? HubStatusFlags.HostListed : 0;
        flags |= hub.IsListedVisit ? HubStatusFlags.ListedHub : 0;
        flags |= hub.AskingMaster ? HubStatusFlags.AskingMaster : 0;
        return new HubStatus
        {
            State = (int)hub.State,
            Flags = flags,
            Revision = hub.Revision,
            PlayerId = client?.PlayerId ?? 0,
            Team = client?.Team ?? -1,
            PlayerCount = client?.Roster.Count ?? 0,
            MaxPlayers = client?.MaxPlayers ?? 0,
            HostPort = hub.Host?.Port ?? 0,
            ConnectSeconds = hub.ConnectSeconds,
            LogWaiting = hub.LogWaiting,
        };
    }

    /// <summary>A text as <c>GetText</c> copies it.</summary>
    public static string TextOf(HubBridge hub, HubText kind) => kind switch
    {
        HubText.Error => hub.Error,
        HubText.HubName => hub.Client is { } client ? client.ServerName.Length > 0 ? client.ServerName : hub.Address : null,
        HubText.MapToLoad => hub.MapToLoad,
        HubText.Address => hub.Address,
        HubText.Status => hub.Status,
        HubText.PlayerName => hub.PlayerName,
        HubText.ListMessage => hub.ListMessage,
        HubText.LoadingPicture => RoundExports.LoadingPicture(hub.Catalog.Maps, hub.MapToLoad, hub.Client?.Team == 1, null),
        _ => null,
    } ?? string.Empty;

    // ---- guards -------------------------------------------------------------------------------------------------

    private static string? Text(char* text) => text == null ? null : new string(text);

    private static void Guard(Action action)
    {
        try
        {
            action();
        }
        catch (Exception e)
        {
            Failed(e);
        }
    }

    /// <summary>
    /// An exception nobody expected: the visit ends as failed with its message (so Unreal leaves the hub level), and
    /// the whole text goes to the main table's LastError.
    /// </summary>
    private static void Failed(Exception e)
    {
        try
        {
            Hub.Fail("The hub visit stopped on an internal error: " + e.Message);
        }
        catch (Exception)
        {
            // Ending the visit failed too; the report below still reaches Unreal.
        }

        Exports.ReportFailure(e.ToString());
    }
}
