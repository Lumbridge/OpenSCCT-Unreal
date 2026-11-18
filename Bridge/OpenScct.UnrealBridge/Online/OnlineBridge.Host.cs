using System;
using System.Collections.Generic;
using System.Globalization;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Server;
using OpenScct.Server.Online;

namespace OpenScct.UnrealBridge.Online;

/// <summary>
/// Hosting from the game (the Godot client's OnlineService.Host): a <see cref="ListenHost"/> runs the session on its
/// own thread and the local player joins it over loopback with its host key, so he is the lobby's host and his
/// visit is an ordinary one (lobby, rounds, results) for everything else in the bridge. Leaving, joining another
/// server or a failed visit closes the server.
/// </summary>
public sealed partial class OnlineBridge
{
    /// <summary>Host log lines kept for <see cref="TakeHostLine"/> when nobody takes them (the oldest go first).</summary>
    private const int HostLogCapacity = 256;

    private readonly Queue<string> _hostLog = new();

    /// <summary>The player's own server, when hosting.</summary>
    public ListenHost? Hosting { get; private set; }

    /// <summary>
    /// Hosts a game: the listen server, then the local player in its lobby as the host. The master and invite key
    /// default to the ones <see cref="OnlineMaster.Configure"/> was given. False when the server could not start;
    /// the visit then reads as failed with the reason (<see cref="Error"/>).
    /// </summary>
    public bool Host(HostRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        Master.CancelJoin();
        EndVisit();
        _error = null;
        _stateOverride = OnlineState.Idle;
        request = request with
        {
            HostInstallId = _installId,
            HostAccount = Social.SocialBridge.Existing?.Account.HostIdentity(),
            MasterUrl = request.MasterUrl ?? Master.Url,
            InviteKey = request.InviteKey ?? Master.InviteKey,
        };

        ListenHost host;
        try
        {
            // The server loads its rounds with the server's own loaders over the same folders as the client.
            host = ListenHost.Start(request, new MapCatalog([.. Catalog.Maps.Roots]), Classes);
        }
        catch (InvalidOperationException e)
        {
            FailJoin("Could not host: " + e.Message);
            return false;
        }

        HostLog(string.Create(CultureInfo.InvariantCulture,
            $"Hosting {request.Name} on UDP port {host.Port} ({request.Setup.Map}, {LobbySetup.ModeName(request.Setup.Mode)}, {request.MaxPlayers} players{(request.Password is { Length: > 0 } ? ", password" : "")}{(request.MasterUrl is { Length: > 0 } m ? $", master {m}{(request.Listed ? " listed" : " unlisted")}" : ", no master")}{(request.LanPort > 0 ? $", LAN port {request.LanPort}" : "")}{(request.DemoFolder is not null ? ", recording demos" : "")})."));

        // The host's own player is never the far side of a socket: the loopback target needs no UDP.
        Join(host.LocalTarget, request.Password, ticket: null, preferredTeam: -1, loopbackOnly: true, hostKey: host.HostKey);
        Hosting = host;
        Touch();
        return true;
    }

    /// <summary>Closes the player's server (the players hear it closed, the master unlists it, UPnP unmaps the port).</summary>
    public void StopHosting()
    {
        if (Hosting is not { } host)
        {
            return;
        }

        Hosting = null;
        int port = host.Port;
        host.Dispose();
        DrainHost(host);
        HostLog(string.Create(CultureInfo.InvariantCulture, $"Stopped hosting on UDP port {port}."));
        Touch();
    }

    /// <summary>An admin command on the player's own server (kick, ban, map, set, say...); the answers arrive as system chat lines. False when not hosting.</summary>
    public bool HostCommand(string line)
    {
        if (Hosting is not { } host || string.IsNullOrWhiteSpace(line))
        {
            return false;
        }

        HostLog("> " + line.Trim());
        host.Command(line.Trim());
        return true;
    }

    /// <summary>The next waiting line of the host's log, oldest first; false when there is none.</summary>
    public bool TakeHostLine(out string line) => _hostLog.TryDequeue(out line!);

    /// <summary>Picks up what the host thread wrote since the last frame; a server that stopped ends the visit with the reason.</summary>
    private void PumpHost()
    {
        if (Hosting is not { } host)
        {
            return;
        }

        DrainHost(host);
        if (host.Failure is { } failure)
        {
            FailJoin("Your server stopped: " + failure.Message);
        }
    }

    private void DrainHost(ListenHost host)
    {
        while (host.TryTakeLog(out string? line))
        {
            HostLog(line);
        }

        bool replied = false;
        while (host.TryTakeReply(out string? reply))
        {
            HostLog(reply);
            // The answers of the host's commands read like a dedicated server's answers to an admin: system lines.
            OnMessage(new SessionMessage(reply));
            replied = true;
        }

        if (replied)
        {
            Touch();
        }
    }

    private void HostLog(string line)
    {
        if (_hostLog.Count == HostLogCapacity)
        {
            _hostLog.Dequeue();
        }

        _hostLog.Enqueue(line);
    }

    /// <summary>
    /// The first match of a hosted game from an options line: <c>MapName</c> (else the first playable map),
    /// <c>Mode</c> as a mode key (Story, DiskHunt, Deathmatch...) or <c>GameMode</c> as a number, then that map's own
    /// settings for the mode with whatever else the line names on top ("?TimeLimit=5?SvmBots=2"). Throws
    /// <see cref="InvalidOperationException"/> with a readable reason for a map that is not installed or a mode not known.
    /// </summary>
    public LobbySetup HostSetup(string? options, int maxPlayers)
    {
        string line = options ?? string.Empty;
        PlayCatalog catalog = Catalog;
        string map = UcOptions.ParseOption(line, "MapName");
        if (map.Length == 0)
        {
            foreach (MapListEntry candidate in catalog.Maps.ListMaps())
            {
                if (!MapCatalog.IsMenuMap(candidate.Id) && catalog.CanPlay(candidate.Id))
                {
                    map = candidate.Id;
                    break;
                }
            }
        }

        if (catalog.Maps.FindMap(map) is not { } entry || MapCatalog.IsMenuMap(entry.Id))
        {
            throw new InvalidOperationException(map.Length == 0 ? "no map is installed." : $"the map '{map}' is not installed.");
        }

        string modeKey = UcOptions.ParseOption(line, "Mode");
        EGameMode mode;
        if (modeKey.Length > 0)
        {
            mode = MapCatalog.ModeOfKey(modeKey) ?? throw new InvalidOperationException($"unknown mode '{modeKey}'.");
        }
        else
        {
            mode = (EGameMode)(byte)UcOptions.GetIntOption(line, "GameMode", (int)EGameMode.GM_Multi);
        }

        int players = Math.Clamp(maxPlayers, LobbySetup.MinMaxPlayers, LobbySetup.MaxMaxPlayers);
        LobbySetup defaults = LobbySetup.ForMap(entry.Id, mode, catalog.Maps.GetMapSettings(entry.Id), players);
        return LobbySetup.FromOptions(line, defaults) with { Map = entry.Id, Mode = mode, MaxPlayers = players };
    }
}
