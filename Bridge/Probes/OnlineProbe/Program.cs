using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using OpenScct.Core;
using OpenScct.Game.SBase;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.Play.Session;

// OnlineProbe <content root> <OpenSCCT checkout> [map] [seconds in the match] [port] [master url] [invite key]
//
// Starts a real dedicated server (svm-server, the OpenSCCT checkout's src/OpenScct.Server.Cli) on localhost with
// bots filling the sides, joins it through OnlineBridge exactly as Unreal would (Update once a frame, the same reads
// the exported table does), goes through the lobby, the travel and the loading gate into a round, and prints the
// pawns as the network moves them. With a master url it also asks that master for its server list.

string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string openScct = args.Length > 1 ? args[1] : @"C:\Users\ryans\source\repos\OSC-unreal-bridge";
string map = args.Length > 2 ? args[2] : "AquaE";
double matchSeconds = args.Length > 3 ? double.Parse(args[3], CultureInfo.InvariantCulture) : 15;
int port = args.Length > 4 ? int.Parse(args[4], CultureInfo.InvariantCulture) : 28931;
string? masterUrl = args.Length > 5 ? args[5] : null;
string? inviteKey = args.Length > 6 ? args[6] : null;

BridgeSession.Configure(content);
OnlineBridge online = OnlineBridge.Instance;
online.SetIdentity("UnrealProbe", null, null);
Console.WriteLine($"identity: name '{online.PlayerName}', install id {online.InstallId}, public key {PlayerKey.Short(online.Key.PublicKey)}...");

string work = Path.Combine(Path.GetTempPath(), "osu-online-probe-" + Environment.ProcessId.ToString(CultureInfo.InvariantCulture));
Directory.CreateDirectory(work);
Process? server = null;
int exit = 1;
try
{
    if (masterUrl is not null)
    {
        ListServers(online, masterUrl, inviteKey);
    }

    server = StartServer(openScct, content, work, map, port);
    exit = Play(online, server, map, port, matchSeconds);
}
finally
{
    online.Dispose();
    if (server is { HasExited: false })
    {
        // "close" tells the players and stops the server; the kill covers a server that did not take it.
        try
        {
            server.StandardInput.WriteLine("close");
            if (!server.WaitForExit(4000))
            {
                server.Kill(entireProcessTree: true);
            }
        }
        catch (Exception e) when (e is IOException or InvalidOperationException)
        {
            server.Kill(entireProcessTree: true);
        }
    }

    server?.Dispose();
    try
    {
        Directory.Delete(work, recursive: true);
    }
    catch (IOException)
    {
        // The server may still hold a file for a moment; the folder is in the temp directory.
    }
}

Console.WriteLine(exit == 0 ? "PROBE OK" : "PROBE FAILED");
return exit;

// The master's list, as the server browser asks for it.
static void ListServers(OnlineBridge online, string url, string? key)
{
    online.Master.Configure(url, key);
    online.Master.RequestList();
    var wait = Stopwatch.StartNew();
    while (online.Master.State == OnlineMasterState.Busy && wait.Elapsed.TotalSeconds < 12)
    {
        online.Update();
        Thread.Sleep(10);
    }

    if (online.Master.State != OnlineMasterState.Ready)
    {
        Console.WriteLine($"master {url}: {online.Master.State}: {online.Master.Error}");
        return;
    }

    var servers = new OnlineServerEntry[32];
    int count = OnlineExports.Servers(online, servers);
    Console.WriteLine($"master {url}: {count} server(s) in {wait.Elapsed.TotalMilliseconds:F0} ms");
    for (int i = 0; i < count && i < servers.Length; i++)
    {
        OnlineServerEntry s = servers[i];
        Console.WriteLine($"   [{i}] '{ServerName(s)}' map {ServerMap(s)} mode {s.Mode} phase {s.Phase} players {s.Players}/{s.MaxPlayers} flags {Flags(s.Flags)}");
    }
}

static string Flags(int flags)
{
    var names = new List<string>();
    void Add(int bit, string name)
    {
        if ((flags & bit) != 0)
        {
            names.Add(name);
        }
    }

    Add(OnlineServerFlags.Password, "password");
    Add(OnlineServerFlags.Dedicated, "dedicated");
    Add(OnlineServerFlags.RequireAccounts, "accounts-only");
    Add(OnlineServerFlags.Trusted, "trusted");
    Add(OnlineServerFlags.Practice, "practice");
    Add(OnlineServerFlags.SameBuild, "same-build");
    Add(OnlineServerFlags.HasMap, "has-map");
    Add(OnlineServerFlags.SameMapVersion, "same-map-version");
    Add(OnlineServerFlags.Full, "full");
    return string.Join(",", names);
}

// A dedicated server as an operator runs it: a config file, the map folder, and commands on standard input.
static Process StartServer(string openScct, string content, string work, string map, int port)
{
    string config = Path.Combine(work, "svm-server.json");
    File.WriteAllText(config, $$"""
        {
          "name": "OnlineProbe server",
          "port": {{port}},
          "lanPort": 0,
          "loopbackOnly": true,
          "maxPlayers": 4,
          "maps": {{System.Text.Json.JsonSerializer.Serialize(Path.Combine(content, "svmap"))}},
          "rotation": [ { "map": "{{map}}", "mode": "Story" } ],
          "minPlayersToLaunch": 1,
          "launchCountdownSeconds": 2,
          "bansFile": null,
          "matchLog": null
        }
        """);
    var start = new ProcessStartInfo("dotnet")
    {
        WorkingDirectory = work,
        RedirectStandardInput = true,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false,
        CreateNoWindow = true,
    };
    // --no-build: the probe never rebuilds the OpenSCCT projects other work shares; build the CLI once beforehand
    // (dotnet build -c Release --no-dependencies src/OpenScct.Server.Cli).
    foreach (string arg in new[]
    {
        "run", "--no-build", "-c", "Release", "--project", Path.Combine(openScct, "src", "OpenScct.Server.Cli"), "--",
        "--config", config,
    })
    {
        start.ArgumentList.Add(arg);
    }

    start.Environment["SVM_CONTENT"] = content;
    Process server = Process.Start(start) ?? throw new InvalidOperationException("Could not start the server.");
    var up = new ManualResetEventSlim();
    void Line(string? line)
    {
        if (line is null)
        {
            return;
        }

        Console.WriteLine("   [server] " + line);
        if (line.Contains("on UDP port", StringComparison.Ordinal))
        {
            up.Set();
        }
    }

    server.OutputDataReceived += (_, e) => Line(e.Data);
    server.ErrorDataReceived += (_, e) => Line(e.Data);
    server.BeginOutputReadLine();
    server.BeginErrorReadLine();
    if (!up.Wait(TimeSpan.FromSeconds(60)) || server.HasExited)
    {
        throw new InvalidOperationException("The server did not come up.");
    }

    // The original has no bots in a network session; ours fill the empty places of both sides.
    server.StandardInput.WriteLine("set bots fill");
    server.StandardInput.WriteLine("set botskill normal");
    return server;
}

static int Play(OnlineBridge online, Process server, string map, int port, double matchSeconds)
{
    var run = Stopwatch.StartNew();
    online.Join("127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture), password: null, preferredTeam: 0, loopbackOnly: true);

    var players = new OnlinePlayer[32];
    var seen = new Dictionary<int, Track>();
    OnlineState shown = OnlineState.Idle;
    int shownRevision = -1;
    bool readied = false, chatted = false, reported = false, launched = false;
    double lobbySince = -1, matchSince = -1, roundSince = -1, nextPawns = 0, leftAt = -1;
    int framesInRound = 0, ticksInRound = 0;
    string? mapToLoad = null;

    while (run.Elapsed.TotalSeconds < 240)
    {
        online.Update();
        OnlineStatus status = OnlineExports.Status(online);
        var state = (OnlineState)status.State;
        double t = run.Elapsed.TotalSeconds;

        if (state != shown)
        {
            shown = state;
            Console.WriteLine($"t={t:F1} state {state} (server phase {status.Phase}), current map '{OnlineExports.TextOf(online, OnlineText.CurrentMap)}', map to load '{OnlineExports.TextOf(online, OnlineText.MapToLoad)}'");
            if (state == OnlineState.Lobby && lobbySince < 0)
            {
                lobbySince = t;
                Console.WriteLine("   connect log: " + OnlineExports.TextOf(online, OnlineText.ConnectLog).Replace("\n", " | ", StringComparison.Ordinal));
            }
        }

        if (state == OnlineState.Failed)
        {
            Console.WriteLine("FAILED: " + OnlineExports.TextOf(online, OnlineText.Error));
            return 1;
        }

        if (server.HasExited && leftAt < 0)
        {
            Console.WriteLine("FAILED: the server stopped.");
            return 1;
        }

        // The lobby as a menu would redraw it: only when the revision moved.
        if (state is OnlineState.Lobby && status.Revision != shownRevision && t - lobbySince > 0.5)
        {
            shownRevision = status.Revision;
            int count = OnlineExports.Players(online, players);
            Console.WriteLine($"t={t:F1} lobby '{OnlineExports.TextOf(online, OnlineText.ServerName)}': {OnlineExports.TextOf(online, OnlineText.SetupDescription)}");
            Console.WriteLine($"   local id {status.LocalId} team {status.LocalTeam}, {count}/{status.MaxPlayers} players, countdown {status.Countdown:F1} s, flags {status.Flags}");
            for (int i = 0; i < count && i < players.Length; i++)
            {
                OnlinePlayer p = players[i];
                Console.WriteLine($"   #{p.Id} '{PlayerName(p)}' team {p.Team} flags {(SessionPlayerFlags)p.Flags} ping {p.PingMs} map {(MapState)p.MapState}");
            }
        }

        if (state == OnlineState.Lobby && t - lobbySince > 1.0 && !chatted)
        {
            chatted = true;
            online.Chat("hello from the Unreal bridge", teamOnly: false);
        }

        if (state == OnlineState.Lobby && t - lobbySince > 2.0 && !readied)
        {
            readied = true;
            online.SetTeam(0);
            online.SetReady(true);
            Console.WriteLine($"t={t:F1} picked the spies and reported ready");
        }

        // A dedicated lobby launches on its own once enough people are ready; ask if it has not.
        if (state == OnlineState.Lobby && readied && !launched && t - lobbySince > 6.0 && status.Countdown <= 0f &&
            (status.Flags & OnlineStatusFlags.PreparingBots) == 0)
        {
            launched = true;
            online.Launch();
            Console.WriteLine($"t={t:F1} asked for the launch");
        }

        var line = default(OnlineChatLine);
        while (online.TakeChat(out SessionMessage message))
        {
            OnlineExports.Write(message, ref line);
            Console.WriteLine($"t={t:F1} chat {(line.System != 0 ? "[system]" : $"<{ChatSender(line)}> team {line.Team}")} {ChatText(line)}");
        }

        if (state == OnlineState.Travelling && mapToLoad is null)
        {
            mapToLoad = OnlineExports.TextOf(online, OnlineText.MapToLoad);
            Console.WriteLine($"t={t:F1} travel: Unreal must load '{mapToLoad}' (loading picture seed {status.LoadingPictureSeed})");
        }

        if (online.Round is { } round)
        {
            if (roundSince < 0)
            {
                roundSince = t;
                Console.WriteLine($"t={t:F1} round entered on '{round.Settings.Map}' as {round.Settings.Side}, mode {round.Settings.Mode}; remote session: {round.IsRemote}; waiting for the level load report: {(status.Flags & OnlineStatusFlags.NeedsLoadReport) != 0}");
            }

            // Unreal's level streams in for a moment, then it reports.
            if (!reported && t - roundSince > 1.0)
            {
                reported = true;
                online.ReportLoaded();
                Console.WriteLine($"t={t:F1} reported the level loaded");
            }

            framesInRound++;
            ticksInRound = (int)round.TicksRun;
            if (state == OnlineState.Match)
            {
                if (matchSince < 0)
                {
                    matchSince = t;
                    Console.WriteLine($"t={t:F1} the match started ({(status.Flags & OnlineStatusFlags.ReportedWaiting) != 0}: the server knew this player was waiting)");
                }

                // Walk forward for a while: the local pawn moves by prediction and the server's corrections.
                round.Input.Set(LocalInput.ButtonNames[0], t - matchSince is > 3.0 and < 9.0);
                Sample(round, seen);
                if (t >= nextPawns)
                {
                    nextPawns = t + 2.5;
                    Report(round, seen, t, framesInRound, ticksInRound);
                }

                if (t - matchSince >= matchSeconds)
                {
                    break;
                }
            }
        }

        Thread.Sleep(4);
    }

    if (matchSince < 0)
    {
        Console.WriteLine("FAILED: the visit never reached a match.");
        return 1;
    }

    if (online.Round is { } last)
    {
        Report(last, seen, run.Elapsed.TotalSeconds, framesInRound, ticksInRound);
    }

    int moved = 0, remoteMoved = 0;
    foreach (Track track in seen.Values)
    {
        if (track.Path > 100f)
        {
            moved++;
            remoteMoved += track.Local ? 0 : 1;
        }
    }

    Console.WriteLine($"{seen.Count} pawns seen, {moved} moved more than a metre ({remoteMoved} of them other players, replicated from the server)");
    online.Leave();
    leftAt = run.Elapsed.TotalSeconds;
    Console.WriteLine($"t={leftAt:F1} left; state {online.State}");
    return remoteMoved > 0 && online.State == OnlineState.Idle ? 0 : 1;
}

// Every frame: where each pawn of the client's world is, and how far it has gone.
static void Sample(PlaySession round, Dictionary<int, Track> seen)
{
    SBasePawn? local = round.LocalPawn;
    foreach (SBasePawn pawn in round.Client.World.Level.AllActors<SBasePawn>())
    {
        if (pawn.bDeleteMe)
        {
            continue;
        }

        FVector at = pawn.Location;
        if (!seen.TryGetValue(pawn.ActorId, out Track? track))
        {
            track = seen[pawn.ActorId] = new Track { Last = at };
        }

        float dx = at.X - track.Last.X, dy = at.Y - track.Last.Y, dz = at.Z - track.Last.Z;
        float step = MathF.Sqrt(dx * dx + dy * dy + dz * dz);
        // A step of metres in one frame is a respawn, not walking.
        track.Path += step < 300f ? step : 0f;
        track.Last = at;
        track.Local = ReferenceEquals(pawn, local);
        track.Merc = pawn is SPAWNDEFENSE;
        track.Health = pawn.Health;
        track.Team = SGameInfo.TeamIndexOf(pawn.Controller);
        track.Name = pawn.PlayerReplicationInfo?.PlayerName ?? string.Empty;
    }
}

static void Report(PlaySession round, Dictionary<int, Track> seen, double t, int frames, int ticks)
{
    ViewSample view = round.View;
    Console.WriteLine($"t={t:F1} round: {ticks} client ticks over {frames} frames, alpha {round.Alpha:F2}; view valid={view.Valid} at ({view.Location.X:F0},{view.Location.Y:F0},{view.Location.Z:F0}) fov {view.Fov:F0}");
    foreach (KeyValuePair<int, Track> pair in seen)
    {
        Track p = pair.Value;
        Console.WriteLine($"   #{pair.Key} {(p.Merc ? "merc" : "spy ")} '{p.Name}' {(p.Local ? "LOCAL " : "remote")} hp {p.Health} at ({p.Last.X:F0},{p.Last.Y:F0},{p.Last.Z:F0}) walked {p.Path / 100f:F1} m so far");
    }
}

static unsafe string PlayerName(OnlinePlayer p) => new(p.Name);

static unsafe string ChatSender(OnlineChatLine l) => new(l.Sender);

static unsafe string ChatText(OnlineChatLine l) => new(l.Text);

static unsafe string ServerName(OnlineServerEntry s) => new(s.Name);

static unsafe string ServerMap(OnlineServerEntry s) => new(s.Map);

internal sealed class Track
{
    public FVector Last;
    public float Path;
    public bool Local, Merc;
    public int Health, Team;
    public string Name = string.Empty;
}
