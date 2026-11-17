using System;
using DiscordRPC;
using DiscordRPC.Message;
using OpenScct.Net.Discovery;

namespace OpenScct.UnrealBridge.Social;

/// <summary>
/// Discord Rich Presence (the Godot client's game/Scripts/Discord/DiscordPresence.cs without its node): shows what the
/// player is doing on his Discord profile through the Discord app running on this PC, and takes a friend's Join back
/// into the game. <see cref="SocialBridge"/> starts and stops it as the Online page's "Discord status" option says,
/// hands it the state Unreal described, and calls <see cref="Update"/> once a frame on the game thread.
/// <para>The library connects on its own thread and keeps retrying quietly while Discord is not running; its events
/// wait for <see cref="DiscordRpcClient.Invoke"/> in <see cref="Update"/>, so they run on the game thread.</para>
/// </summary>
public sealed class DiscordPresence : IDisposable
{
    /// <summary>The OpenSCCT application on the Discord Developer Portal (the Godot client's).</summary>
    public const string ApplicationId = "1564377811014389862";

    private const double UpdateSeconds = 1.0;

    private readonly Action<string> _log;
    private readonly Action<DiscordJoinSecret?> _join;
    private DiscordRpcClient? _client;
    private bool _readyLogged;
    private bool _failLogged;
    private bool _mergeErrorLogged;
    private double _sinceUpdate = UpdateSeconds;
    private PresenceState? _wanted;
    private PresenceState? _shown;
    private DateTime _since = DateTime.UtcNow;

    public DiscordPresence(Action<string> log, Action<DiscordJoinSecret?> join)
    {
        _log = log;
        _join = join;
    }

    public bool Running => _client is not null;

    public DiscordState State { get; private set; }

    /// <summary>The game is registered with Discord (discord-&lt;app&gt;://): join secrets are shown and Joins taken.</summary>
    public bool CanJoin { get; private set; }

    /// <summary>The Discord user the status shows on, once connected.</summary>
    public string? User { get; private set; }

    /// <summary>What was last sent to Discord, or null.</summary>
    public PresenceState? Shown => _shown;

    /// <summary>
    /// Connects to the Discord app. <paramref name="launchCommand"/> is how Discord starts this game for a friend's
    /// Join ("C:\...\OpenSCCT.exe", or the editor with the project and -game); empty registers nothing, and then no
    /// join secret is shown either (a friend could not start the game from it).
    /// </summary>
    public void Start(string? launchExe, string? launchCommand)
    {
        if (_client is not null)
        {
            return;
        }

        var client = new DiscordRpcClient(ApplicationId, -1, null, false, new DiscordInvitePipe())
        {
            SkipIdenticalPresence = true,
        };
        client.OnReady += (_, e) =>
        {
            State = DiscordState.Ready;
            User = e.User?.Username;
            if (!_readyLogged)
            {
                _readyLogged = true;
                _log("Discord: ready, connected as " + (User ?? "?") + (CanJoin ? ", friends can join" : ", Join off (the game is not registered with Discord)"));
            }

            _failLogged = false;
        };
        client.OnConnectionFailed += (_, _) =>
        {
            if (State != DiscordState.Ready)
            {
                State = DiscordState.NotRunning;
            }

            if (!_failLogged)
            {
                // Discord is not running: the client keeps retrying quietly.
                _failLogged = true;
                _log("Discord: not running (no Discord app to talk to); the status is off until it starts");
            }
        };
        client.OnJoin += OnJoin;

        CanJoin = RegisterLaunch(client, launchExe, launchCommand);
        try
        {
            client.Initialize();
            if (CanJoin)
            {
                client.Subscribe(EventType.Join);
            }
        }
        catch (Exception e) when (e is InvalidOperationException or ArgumentException)
        {
            _log("Discord: the status could not start: " + e.Message);
            client.Dispose();
            CanJoin = false;
            return;
        }

        _client = client;
        State = DiscordState.Connecting;
        _shown = null;
        _sinceUpdate = UpdateSeconds;
        _log("Discord: starting the status (application " + ApplicationId + ")");
    }

    public void Stop()
    {
        if (_client is not { } client)
        {
            return;
        }

        _client = null;
        _shown = null;
        State = DiscordState.Off;
        CanJoin = false;
        try
        {
            client.ClearPresence();
        }
        catch (InvalidOperationException)
        {
            // Not connected: nothing to clear.
        }

        client.Dispose();
        _log("Discord: the status is off");
    }

    /// <summary>The state to show from now on (sent within a second when it differs from what Discord shows).</summary>
    public void Show(PresenceState state) => _wanted = state;

    /// <summary>
    /// Tells Discord how to start the game for a friend's Join (discord-&lt;app id&gt;:// under the current user's
    /// classes). The command carries no URI argument: the join itself arrives over Discord's pipe once the game is up.
    /// </summary>
    private bool RegisterLaunch(DiscordRpcClient client, string? exe, string? command)
    {
        if (!OperatingSystem.IsWindows() || string.IsNullOrWhiteSpace(exe) || string.IsNullOrWhiteSpace(command))
        {
            return false;
        }

        try
        {
            if (!client.RegisterUriScheme(null, exe))
            {
                return false;
            }

            using Microsoft.Win32.RegistryKey? key = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(
                @"Software\Classes\discord-" + ApplicationId + @"\shell\open\command");
            key?.SetValue(string.Empty, command);
            _log("Discord: registered discord-" + ApplicationId + ":// to start " + command);
            return true;
        }
        catch (Exception e) when (e is UnauthorizedAccessException or System.Security.SecurityException or System.IO.IOException)
        {
            _log("Discord: Join is off, the game could not be registered with Discord (" + e.Message + ")");
            return false;
        }
    }

    /// <summary>Runs Discord's events and sends the status when it changed (at most once a second). Game thread.</summary>
    public void Update(double delta)
    {
        if (_client is not { } client)
        {
            return;
        }

        try
        {
            client.Invoke();
        }
        catch (NullReferenceException e)
        {
            // DiscordRichPresence 1.6.1.70 throws in Assets.Merge when Discord echoes a presence back without its
            // assets (fixed upstream after that release). Only that echo is lost; the next frame carries on.
            if (!_mergeErrorLogged)
            {
                _mergeErrorLogged = true;
                _log("Discord: the library failed to read Discord's reply (" + e.Message + "); ignored from now on");
            }
        }

        _sinceUpdate += delta;
        if (_sinceUpdate < UpdateSeconds || !client.IsInitialized || _wanted is not { } state)
        {
            return;
        }

        _sinceUpdate = 0;
        if (!CanJoin && state.Join is not null)
        {
            state = state with { Join = null };
        }

        if (state == _shown)
        {
            return;
        }

        if (state.Details != _shown?.Details)
        {
            _since = DateTime.UtcNow;
        }

        _shown = state;
        client.SetPresence(ToRichPresence(state, _since));
        _log("Discord: status " + state.Describe());
    }

    public static RichPresence ToRichPresence(PresenceState state, DateTime since)
    {
        var presence = new RichPresence
        {
            Details = state.Details,
            State = state.State.Length > 0 ? state.State : null,
            Timestamps = new Timestamps(since),
            Assets = new Assets
            {
                LargeImageKey = "logo",
                LargeImageText = "OpenSCCT " + BuildInfo.Version,
                SmallImageKey = state.Side,
                SmallImageText = state.Side switch
                {
                    "spy" => "Spy",
                    "merc" => "Mercenary",
                    _ => null,
                },
            },
        };

        if (state.PartyId is { } partyId && state.PartyMax > 0)
        {
            presence.Party = new Party
            {
                ID = partyId,
                Size = state.PartySize,
                Max = state.PartyMax,
                Privacy = Party.PrivacySetting.Public,
            };
            if (state.Join?.Format() is { } secret)
            {
                presence.Secrets = new Secrets { JoinSecret = secret };
            }
        }

        return presence;
    }

    private void OnJoin(object sender, JoinMessage message)
    {
        DiscordJoinSecret? join = DiscordJoinSecret.Parse(message.Secret);
        _log(join is null ? "Discord: ignored a Join this build does not understand" : "Discord: a friend's Join to " + join.Kind);
        _join(join);
    }

    public void Dispose() => Stop();
}
