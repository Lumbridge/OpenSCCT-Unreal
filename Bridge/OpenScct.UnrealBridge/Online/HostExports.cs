using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The hosting entry points Unreal calls (Source/OpenSCCT/Runtime/ScctHostApi.h mirrors it): a listen server in the
/// game's own process that the local player joins as the lobby's host. Hosting is part of the online visit: once
/// Host returned, the online table (OnlineExports) follows the host's own player through the lobby and the rounds as
/// it follows any visit, and its Leave closes the server. A table of its own, so the online table keeps its layout.
/// Every entry catches its exceptions. Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HostApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int, char*, int, int, char*, int> Host;
    public delegate* unmanaged[Cdecl]<void> StopHosting;
    public delegate* unmanaged[Cdecl]<int> IsHosting;
    public delegate* unmanaged[Cdecl]<char*, int> HostCommand;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetHostText;
    public delegate* unmanaged[Cdecl]<char*, int, int> TakeHostLine;
    public delegate* unmanaged[Cdecl]<int, void> SetConnectPath;
}

/// <summary>HostApi.Host's flags.</summary>
public static class HostFlags
{
    /// <summary>List the server on the master (when one is set); without it players join by address or server id.</summary>
    public const int Listed = 1;

    /// <summary>Forward the port on the router with UPnP.</summary>
    public const int Upnp = 2;

    /// <summary>Only signed-in players may join (needs a master).</summary>
    public const int RequireAccounts = 4;

    /// <summary>Record every round as a demo into the demo folder.</summary>
    public const int RecordDemos = 8;

    /// <summary>Answer LAN searches on the LAN port.</summary>
    public const int Lan = 16;

    /// <summary>Bind 127.0.0.1 and ::1 only (test runs on one machine: no firewall prompt, no UPnP, no master).</summary>
    public const int LoopbackOnly = 32;
}

/// <summary>HostApi.GetHostText's kinds.</summary>
public enum HostText
{
    /// <summary>The session's phase, map, mode and player count ("Lobby on AquaE (Story), 2/8 players, 0 round(s) started").</summary>
    Status = 0,

    /// <summary>The session's players: "id name (side, host, ready, bot)" separated by "; ".</summary>
    Players = 1,

    /// <summary>The master listing (registered id, reachability, public address, or why not) and the LAN beacon.</summary>
    Master = 2,

    /// <summary>The UPnP mapping's result, empty while the router is still being asked.</summary>
    Upnp = 3,

    /// <summary>Why the last Host failed (the visit's error), or empty.</summary>
    Error = 4,
}

public static unsafe class HostExports
{
    public const int ApiVersion = 1;

    private static OnlineBridge Online => OnlineBridge.Instance;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HostApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Host = &Host;
        api->StopHosting = &StopHosting;
        api->IsHosting = &IsHosting;
        api->HostCommand = &HostCommand;
        api->GetHostText = &GetHostText;
        api->TakeHostLine = &TakeHostLine;
        api->SetConnectPath = &SetConnectPath;
        return ApiVersion;
    }

    /// <summary>
    /// Hosts a game and joins it as the host. <paramref name="setupOptions"/> is the first match as an options line
    /// ("?MapName=AquaE?Mode=Story?TimeLimit=10"; see OnlineBridge.HostSetup). The master and its invite key are the
    /// ones the online table's SetMaster gave. Returns the UDP port it listens on, or 0 when it could not start (the
    /// online status is Failed with the reason in its Error text).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Host(char* name, char* password, int port, int maxPlayers, char* setupOptions, int flags, int lanPort, char* demoFolder)
    {
        string? title = Text(name), pass = Text(password), options = Text(setupOptions), demos = Text(demoFolder);
        try
        {
            return Host(Online, title, pass, port, maxPlayers, options, flags, lanPort, demos);
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>The managed side of <c>Host</c> (the probe calls it too).</summary>
    public static int Host(OnlineBridge online, string? name, string? password, int port, int maxPlayers, string? setupOptions, int flags,
        int lanPort, string? demoFolder)
    {
        ArgumentNullException.ThrowIfNull(online);
        bool loopback = (flags & HostFlags.LoopbackOnly) != 0;
        int players = Math.Clamp(maxPlayers <= 0 ? LobbySetup.DefaultMaxPlayers : maxPlayers, LobbySetup.MinMaxPlayers, LobbySetup.MaxMaxPlayers);
        LobbySetup setup;
        try
        {
            setup = online.HostSetup(setupOptions, players);
        }
        catch (InvalidOperationException e)
        {
            online.FailJoin("Could not host: " + e.Message);
            return 0;
        }

        var request = new HostRequest
        {
            Name = string.IsNullOrWhiteSpace(name) ? online.PlayerName + "'s game" : name.Trim(),
            Password = string.IsNullOrEmpty(password) ? null : password,
            Port = port is >= 0 and < 65536 ? port : OpenScct.Net.Discovery.BuildInfo.DefaultGamePort,
            MaxPlayers = players,
            Setup = setup,
            Listed = (flags & HostFlags.Listed) != 0,
            Upnp = (flags & HostFlags.Upnp) != 0 && !loopback,
            RequireAccounts = (flags & HostFlags.RequireAccounts) != 0,
            DemoFolder = (flags & HostFlags.RecordDemos) != 0 && !string.IsNullOrWhiteSpace(demoFolder) ? demoFolder : null,
            LanPort = (flags & HostFlags.Lan) != 0 && lanPort is > 0 and < 65536 ? lanPort : 0,
            LoopbackOnly = loopback,
            // A loopback test host stays off the master: nobody outside could reach it.
            MasterUrl = loopback ? string.Empty : null,
        };
        return online.Host(request) ? online.Hosting!.Port : 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void StopHosting() => Guard(() => Online.StopHosting());

    /// <summary>The UDP port of the player's server while it runs, else 0.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int IsHosting()
    {
        try
        {
            return Online.Hosting is { IsRunning: true } host ? host.Port : 0;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>An admin command on the player's own server; the answers arrive as system chat lines. Returns 1 when queued.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int HostCommand(char* line)
    {
        string? command = Text(line);
        try
        {
            return Online.HostCommand(command ?? string.Empty) ? 1 : 0;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Copies a text (<see cref="HostText"/>) into <paramref name="buffer"/>. Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetHostText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, TextOf(Online, (HostText)kind));
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>A host text as <c>GetHostText</c> copies it.</summary>
    public static string TextOf(OnlineBridge online, HostText kind)
    {
        ListenHost? host = online.Hosting;
        return kind switch
        {
            HostText.Status => host?.StatusLine,
            HostText.Players => host?.PlayersLine,
            HostText.Master => host?.MasterLine,
            HostText.Upnp => host?.UpnpLine,
            HostText.Error => online.Error,
            _ => null,
        } ?? string.Empty;
    }

    /// <summary>
    /// Writes the next line of the host's log (joins, launches, the master, UPnP, command answers) into
    /// <paramref name="buffer"/>, cut to fit. Returns its full length, or -1 when no line waits.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeHostLine(char* buffer, int capacity)
    {
        try
        {
            return Online.TakeHostLine(out string line) ? OnlineTextCopy.Buffer(buffer, capacity, line) : -1;
        }
        catch (Exception e)
        {
            Failed(e);
            return -1;
        }
    }

    /// <summary>How listed servers are reached: 0 as the master suggests, 1 direct only, 2 NAT punch only, 3 relay only.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetConnectPath(int path) =>
        Guard(() => Online.Master.Path = path is >= 0 and <= 3 ? (ConnectPath)path : ConnectPath.Auto);

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

    private static void Failed(Exception e)
    {
        try
        {
            Online.FailJoin("Hosting stopped on an internal error: " + e.Message);
        }
        catch (Exception)
        {
            // Ending the visit failed too; the report below still reaches Unreal.
        }

        Exports.ReportFailure(e.ToString());
    }
}
