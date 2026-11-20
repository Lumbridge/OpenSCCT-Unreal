using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Devices;
using OpenScct.UnrealBridge.Equipment;
using OpenScct.UnrealBridge.Events;
using OpenScct.UnrealBridge.Hud;
using OpenScct.UnrealBridge.Online;

// OnlineProbe <content root> [runtime folder] [first map] [second map] [seconds in the second match] [port] [master url] [invite key]
// OnlineProbe host [content root] [map] [port] [lan port] [upnp]   (hosting from the game: see HostProbe.cs)
// OnlineProbe setup [content root] [map]                           (Create Session's rules: see SetupProbe.cs)
// OnlineProbe endround [content root] [map] [seconds]               (the round table's EndLocalRound: see EndRoundProbe.cs)
// OnlineProbe maps [content root] [map folders] [map]               (the map folders Unreal names: see MapsProbe.cs)
// OnlineProbe rejoin                                                (the rejoin record and table: see RejoinProbe.cs)
// OnlineProbe nade [content root] [map] [port]                      (where the grenade preview is refused: see NadeGateProbe.cs)
//
// Starts a real dedicated server (svm-server, the bundled Runtime's src/OpenScct.Server.Cli) on localhost with
// bots filling the sides, and visits it the way Unreal does: through the function table OnlineExports.GetApi fills
// (the same unmanaged entries, called through their pointers), once a frame. It checks, in this order:
//   1. the struct sizes the C++ header asserts;
//   2. a join that cannot work ends as Failed with a reason, and Leave clears it;
//   3. lobby, chat, side, ready, travel, the load gate and a whole round on the first map (one minute, to its time
//      limit), read through the main table (Exports.GetApi: Update, GetSession, SetInput, GetPawns, GetView,
//      GetPawnPose) exactly as a local match is, with the HUD, events, level-state and equipment tables answering
//      on the same session;
//   4. the results, the way back to the lobby, a setup change, and a second round on another map;
//   5. the main table stopping the round under the visit (the visit leaves), a second visit, and the server
//      closing under it (Failed with a reason).
// With a master url it also asks that master for its server list.
//
// One thing is not the exported path, and only this: the join itself goes through OnlineBridge.Join with a
// loopback-only socket (the exported Join binds every interface, which can raise a firewall prompt on a test
// machine).
return args.Length > 0 && args[0] == "host" ? HostProbe.Run(args[1..]) : args.Length > 0 && args[0] == "setup" ? SetupProbe.Run(args[1..])
    : args.Length > 0 && args[0] == "lan" ? LanProbe.Run(args[1..]) : args.Length > 0 && args[0] == "endround" ? EndRoundProbe.Run(args[1..])
    : args.Length > 0 && args[0] == "maps" ? MapsProbe.Run(args[1..]) : args.Length > 0 && args[0] == "rejoin" ? RejoinProbe.Run(args[1..])
    : args.Length > 0 && args[0] == "download" ? DownloadProbe.Run(args[1..])
    : args.Length > 0 && args[0] == "nade" ? NadeGateProbe.Run(args[1..]) : Probe.Run(args);

internal static unsafe class Probe
{
    private static OnlineApi s_api;
    private static RoundApi s_round;
    private static BridgeApi s_main;
    private static HudApi s_hud;
    private static EventsApi s_events;
    private static WorldApi s_world;
    private static EquipmentApi s_equipment;
    private static BridgeSession? s_adopted;
    private static int s_adoptions, s_releases;
    private static int s_mainTicks;
    private static readonly Stopwatch s_run = new();

    public static int Run(string[] args)
    {
        string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
        string openScct = args.Length > 1 ? args[1] : OpenScct.UnrealBridge.DevPaths.RuntimeRoot();
        string firstMap = args.Length > 2 ? args[2] : "AquaE";
        string secondMap = args.Length > 3 ? args[3] : "ClubE";
        double secondMatchSeconds = args.Length > 4 ? double.Parse(args[4], CultureInfo.InvariantCulture) : 10;
        int port = args.Length > 5 ? int.Parse(args[5], CultureInfo.InvariantCulture) : 28931;
        string? masterUrl = args.Length > 6 ? args[6] : null;
        string? inviteKey = args.Length > 7 ? args[7] : null;

        if (!CheckLayout())
        {
            Console.WriteLine("PROBE FAILED");
            return 1;
        }

        // The server list is asked for before the content folder is set, as a menu may do: the catalog must still
        // see the content once it is configured (the has-map flags below and the join after it).
        OnlineApi api = default;
        delegate* unmanaged<OnlineApi*, int> getApi = &OnlineExports.GetApi;
        int version = getApi(&api);
        s_api = api;
        Console.WriteLine($"table: version {version} (header expects {OnlineBridge.ApiVersion}), {sizeof(OnlineApi)} bytes");
        RoundApi round = default;
        int roundVersion = ((delegate* unmanaged<RoundApi*, int>)&RoundExports.GetApi)(&round);
        s_round = round;
        Console.WriteLine($"round table: version {roundVersion}, {sizeof(RoundApi)} bytes; EndLocalRound without a local match -> {s_round.EndLocalRound()}");
        Console.WriteLine($"installed maps before the content is configured: '{Text(OnlineText.InstalledMaps).Replace("\n", " ", StringComparison.Ordinal)}'");
        BridgeSession.Configure(content);
        string installed = Text(OnlineText.InstalledMaps);
        Console.WriteLine($"installed maps after: {installed.Split('\n').Length} ({(installed.Length > 60 ? installed[..60].Replace("\n", " ", StringComparison.Ordinal) + "..." : installed)})");

        if (!MainTables())
        {
            Console.WriteLine("PROBE FAILED");
            return 1;
        }

        fixed (char* name = "UnrealProbe")
        {
            s_api.SetIdentity(name, null, null);
        }

        Console.WriteLine($"identity: name '{Text(OnlineText.PlayerName)}', install id {Text(OnlineText.InstallId)}, private key of {Text(OnlineText.PrivateKey).Length} characters (not printed)");

        string work = Path.Combine(Path.GetTempPath(), "osu-online-probe-" + Environment.ProcessId.ToString(CultureInfo.InvariantCulture));
        Directory.CreateDirectory(work);
        Process? server = null;
        bool ok = false;
        s_run.Start();
        try
        {
            if (masterUrl is not null)
            {
                ListServers(masterUrl, inviteKey);
            }

            server = StartServer(openScct, content, work, firstMap, secondMap, port);
            ok = FailedJoin(port + 1) && Visit(server, firstMap, secondMap, port, secondMatchSeconds) && ServerCloses(server, port);
        }
        finally
        {
            OnlineBridge.Instance.Dispose();
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

        Console.WriteLine($"rounds adopted {s_adoptions}, released {s_releases}");
        ok &= s_adoptions == 2 && s_releases == 2;
        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    // ---- 1. layouts ---------------------------------------------------------------------------------------------

    /// <summary>The sizes ScctOnlineApi.h asserts for its mirrors.</summary>
    private static bool CheckLayout()
    {
        (string Name, int Size, int Expected)[] sizes =
        [
            ("OnlineStatus", sizeof(OnlineStatus), 88),
            ("OnlinePlayer", sizeof(OnlinePlayer), 120),
            ("OnlineChatLine", sizeof(OnlineChatLine), 620),
            ("OnlineResults", sizeof(OnlineResults), 24),
            ("OnlineResultRow", sizeof(OnlineResultRow), 120),
            ("OnlineServerEntry", sizeof(OnlineServerEntry), 424),
            ("OnlineApi", sizeof(OnlineApi), 8 + 26 * sizeof(void*)),
            ("HostApi", sizeof(HostApi), 8 + 8 * sizeof(void*)),
            ("LobbyApi", sizeof(LobbyApi), 8 + 11 * sizeof(void*)),
            ("LobbyScoreboardInfo", sizeof(OpenScct.UnrealBridge.Online.LobbyScoreboardInfo), 48),
            ("LobbyScoreRow", sizeof(OpenScct.UnrealBridge.Online.LobbyScoreRow), 40 + 48 * 2),
            ("LobbyBriefingMark", sizeof(OpenScct.UnrealBridge.Online.LobbyBriefingMark), 12),
            ("BrowserApi", sizeof(BrowserApi), 8 + 7 * sizeof(void*)),
            ("RoundApi", sizeof(RoundApi), 8 + 5 * sizeof(void*)),
            ("RoundWaitLine", sizeof(RoundWaitLine), 104),
            ("RoundResultExtra", sizeof(RoundResultExtra), 16),
            ("BrowserRowData", sizeof(BrowserRowData), 8 * 4 + (96 + 64 + 32 + 16 + 64 + 48) * 2),
        ];
        bool ok = true;
        foreach ((string name, int size, int expected) in sizes)
        {
            ok &= size == expected;
            Console.WriteLine($"layout: {name} {size} bytes{(size == expected ? string.Empty : $" BUT THE HEADER ASSERTS {expected}")}");
        }

        return ok;
    }

    // ---- the main table and the feature tables, as Unreal gets them ---------------------------------------------

    /// <summary>
    /// Asks for the main table the safe way (the version first, then the table with its size) and for the feature
    /// tables, and counts the sessions that enter and leave the main slot: each online round must do both once.
    /// </summary>
    private static bool MainTables()
    {
        int version = ((delegate* unmanaged<int>)&Exports.GetApiVersion)();
        BridgeApi main = default;
        int filled = ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(&main, sizeof(BridgeApi));

        // A caller built against version 2 (the old one-argument GetApi, a 104-byte table): nothing past its size
        // may be written.
        BridgeApi* guard = stackalloc BridgeApi[2];
        new Span<byte>(guard, 2 * sizeof(BridgeApi)).Fill(0xCD);
        int shortSize = sizeof(int) + 4 + 12 * sizeof(void*);
        int shortFilled = ((delegate* unmanaged<BridgeApi*, int>)&Exports.GetApi)(guard);
        bool untouched = true;
        foreach (byte b in new ReadOnlySpan<byte>((byte*)guard + shortSize, 2 * sizeof(BridgeApi) - shortSize))
        {
            untouched &= b == 0xCD;
        }

        int refused = ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(null, sizeof(BridgeApi));
        Console.WriteLine($"main table: GetApiVersion {version}, GetApiSized {filled} (table says {main.Version}, {sizeof(BridgeApi)} bytes); a version 2 caller's one-argument GetApi ({shortSize} bytes): returned {shortFilled}, bytes past its table untouched {untouched}, GetPawnPose filled {guard->GetPawnPose != null}; null table -> {refused}");
        s_main = main;

        HudApi hud = default;
        EventsApi events = default;
        WorldApi world = default;
        EquipmentApi equipment = default;
        ((delegate* unmanaged<HudApi*, int>)&HudExports.GetApi)(&hud);
        ((delegate* unmanaged<EventsApi*, int>)&EventsExports.GetApi)(&events);
        ((delegate* unmanaged<WorldApi*, int>)&WorldExports.GetApi)(&world);
        ((delegate* unmanaged<EquipmentApi*, int>)&EquipmentExports.GetApi)(&equipment);
        s_hud = hud;
        s_events = events;
        s_world = world;
        s_equipment = equipment;

        Exports.SessionStarted += session =>
        {
            s_adoptions++;
            s_adopted = session;
        };
        Exports.SessionEnded += session =>
        {
            s_releases++;
            if (ReferenceEquals(s_adopted, session))
            {
                s_adopted = null;
            }
        };
        return version == BridgeSession.ApiVersion && filled == version && main.Version == version && shortFilled == version &&
            untouched && guard->GetPawnPose != null && refused == 0;
    }

    /// <summary>One frame as Unreal runs it: the online table's Update, then the main table's.</summary>
    private static OnlineStatus Frame()
    {
        s_api.Update();
        s_mainTicks += s_main.Update();
        OnlineStatus status;
        s_api.GetStatus(&status);
        return status;
    }

    // ---- the master ---------------------------------------------------------------------------------------------

    private static void ListServers(string url, string? key)
    {
        fixed (char* u = url, k = key)
        {
            s_api.SetMaster(u, k);
        }

        s_api.RequestServerList();
        var wait = Stopwatch.StartNew();
        OnlineStatus status = Frame();
        while (status.MasterState == (int)OnlineMasterState.Busy && wait.Elapsed.TotalSeconds < 12)
        {
            Thread.Sleep(10);
            status = Frame();
        }

        if (status.MasterState != (int)OnlineMasterState.Ready)
        {
            Console.WriteLine($"master {url}: state {(OnlineMasterState)status.MasterState}: {Text(OnlineText.MasterError)}");
            return;
        }

        var servers = new OnlineServerEntry[32];
        int count;
        fixed (OnlineServerEntry* s = servers)
        {
            count = s_api.GetServers(s, servers.Length);
        }

        Console.WriteLine($"master {url}: {count} server(s) in {wait.Elapsed.TotalMilliseconds:F0} ms (status says {status.ServerCount})");
        for (int i = 0; i < count && i < servers.Length; i++)
        {
            OnlineServerEntry s = servers[i];
            Console.WriteLine($"   [{i}] '{new string(s.Name)}' map {new string(s.Map)} mode {s.Mode} phase {s.Phase} players {s.Players}/{s.MaxPlayers} flags {ServerFlags(s.Flags)}");
        }
    }

    private static string ServerFlags(int flags)
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

    // ---- the server ---------------------------------------------------------------------------------------------

    // A dedicated server as an operator runs it: a config file, the map folder, and commands on standard input.
    private static Process StartServer(string openScct, string content, string work, string firstMap, string secondMap, int port)
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
              "rotation": [ { "map": "{{firstMap}}", "mode": "Story", "timeLimit": 1 }, { "map": "{{secondMap}}", "mode": "Story" } ],
              "minPlayersToLaunch": 1,
              "launchCountdownSeconds": 2,
              "resultsSeconds": 6,
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

    // ---- 2. a join that cannot work -----------------------------------------------------------------------------

    private static bool FailedJoin(int deadPort)
    {
        double from = Now;
        OnlineBridge.Instance.Join("127.0.0.1:" + deadPort.ToString(CultureInfo.InvariantCulture), password: null, preferredTeam: 0, loopbackOnly: true);
        OnlineStatus status = Frame();
        Console.WriteLine($"t={Now:F1} joining a port nobody listens on: state {(OnlineState)status.State}");
        while (status.State == (int)OnlineState.Connecting && Now - from < 20)
        {
            Thread.Sleep(8);
            status = Frame();
        }

        string error = Text(OnlineText.Error);
        Console.WriteLine($"t={Now:F1} state {(OnlineState)status.State} after {Now - from:F1} s, error: '{error}'");
        Console.WriteLine("   connect log: " + Text(OnlineText.ConnectLog).Replace("\n", " | ", StringComparison.Ordinal));
        s_api.Leave();
        OnlineStatus after = Frame();
        Console.WriteLine($"t={Now:F1} after Leave: state {(OnlineState)after.State}, error '{Text(OnlineText.Error)}'");
        return status.State == (int)OnlineState.Failed && error.Length > 0 && after.State == (int)OnlineState.Idle;
    }

    // ---- 3 and 4. the visit: two rounds on two maps -------------------------------------------------------------

    private static bool Visit(Process server, string firstMap, string secondMap, int port, double secondMatchSeconds)
    {
        OnlineBridge.Instance.Join("127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture), password: null, preferredTeam: 0, loopbackOnly: true);

        var players = new OnlinePlayer[32];
        var pawns = new BridgePawn[32];
        var bones = new BridgeBone[256];
        var rounds = new List<RoundSeen>();
        RoundSeen? round = null;
        var shown = (OnlineState)(-1);
        int shownRevision = -1, lobbies = 0;
        bool readied = false, chatted = false, launched = false, setupAsked = false, results = false, travelShown = false, backInLobby = false;
        double lobbySince = -1, nextReport = 0;
        int timeLimitBefore = -1;
        double deadline = Now + 280;

        while (Now < deadline)
        {
            OnlineStatus status = Frame();
            var state = (OnlineState)status.State;
            double t = Now;
            bool roundActive = (status.Flags & OnlineStatusFlags.RoundActive) != 0;

            if (state != shown)
            {
                shown = state;
                Console.WriteLine($"t={t:F1} state {state} (server phase {status.Phase}), current map '{Text(OnlineText.CurrentMap)}', map to load '{Text(OnlineText.MapToLoad)}', round active {roundActive}");
                if (state == OnlineState.Lobby)
                {
                    lobbies++;
                    lobbySince = t;
                    readied = launched = false;
                    travelShown = false;
                    if (lobbies == 1)
                    {
                        Console.WriteLine("   connect log: " + Text(OnlineText.ConnectLog).Replace("\n", " | ", StringComparison.Ordinal));
                    }
                    else
                    {
                        backInLobby = true;
                        // The rotation's next setup starts without the console's bot fill.
                        server.StandardInput.WriteLine("set bots fill");
                    }
                }
            }

            if (state == OnlineState.Failed)
            {
                Console.WriteLine("FAILED: " + Text(OnlineText.Error));
                return false;
            }

            if (server.HasExited)
            {
                Console.WriteLine("FAILED: the server stopped.");
                return false;
            }

            // The lobby as a menu would redraw it: only when the revision moved.
            if (state == OnlineState.Lobby && status.Revision != shownRevision && t - lobbySince > 0.5)
            {
                shownRevision = status.Revision;
                int count;
                fixed (OnlinePlayer* p = players)
                {
                    count = s_api.GetPlayers(p, players.Length);
                }

                Console.WriteLine($"t={t:F1} lobby '{Text(OnlineText.ServerName)}': {Text(OnlineText.SetupDescription)}");
                Console.WriteLine($"   setup map '{Text(OnlineText.SetupMap)}' mode {status.Mode} '{Text(OnlineText.ModeName)}' time {status.TimeLimit} min, bots {status.Bots}; local id {status.LocalId} team {status.LocalTeam}, {count}/{status.MaxPlayers} players, countdown {status.Countdown:F1} s, flags {StatusFlags(status.Flags)}");
                for (int i = 0; i < count && i < players.Length; i++)
                {
                    OnlinePlayer p = players[i];
                    Console.WriteLine($"   #{p.Id} '{new string(p.Name)}' team {p.Team} flags {(OpenScct.Server.Online.SessionPlayerFlags)p.Flags} ping {p.PingMs} map state {p.MapState}");
                }
            }

            if (state == OnlineState.Lobby && t - lobbySince > 1.0 && !chatted)
            {
                chatted = true;
                fixed (char* text = "hello from the Unreal bridge")
                {
                    s_api.SendChat(text, 0);
                }
            }

            // Back in the lobby: a setup change, if this player may make one (anyone may on an open dedicated server).
            if (state == OnlineState.Lobby && lobbies == 2 && t - lobbySince > 1.0 && !setupAsked)
            {
                setupAsked = true;
                timeLimitBefore = status.TimeLimit;
                int sent;
                fixed (char* options = "?TimeLimit=7")
                {
                    sent = s_api.SetSetupOptions(options);
                }

                Console.WriteLine($"t={t:F1} asked for a 7 minute time limit (was {timeLimitBefore}); sent {sent}; may change the setup: {(status.Flags & OnlineStatusFlags.CanControl) != 0}");
                Console.WriteLine($"   setup options before: {Text(OnlineText.SetupOptions)}");
            }

            if (state == OnlineState.Lobby && t - lobbySince > 2.5 && !readied)
            {
                readied = true;
                if (setupAsked)
                {
                    Console.WriteLine($"t={t:F1} the lobby's time limit is now {status.TimeLimit} min");
                }

                s_api.SetTeam(0);
                s_api.SetReady(1);
                Console.WriteLine($"t={t:F1} picked the spies and reported ready");
            }

            // A dedicated lobby launches on its own once enough people are ready; ask if it has not.
            if (state == OnlineState.Lobby && readied && !launched && t - lobbySince > 7.0 && status.Countdown <= 0f &&
                (status.Flags & OnlineStatusFlags.PreparingBots) == 0)
            {
                launched = true;
                s_api.Launch();
                Console.WriteLine($"t={t:F1} asked for the launch");
            }

            OnlineChatLine line;
            while (s_api.TakeChat(&line) != 0)
            {
                Console.WriteLine($"t={t:F1} chat {(line.System != 0 ? "[system]" : $"<{new string(line.Sender)}> team {line.Team}")} {new string(line.Text)}");
            }

            if (state == OnlineState.Travelling && !travelShown)
            {
                travelShown = true;
                Console.WriteLine($"t={t:F1} travel: Unreal must load '{Text(OnlineText.MapToLoad)}' (loading picture seed {status.LoadingPictureSeed})");
            }

            BridgeSessionInfo info;
            int sessionId = s_main.GetSession(&info);
            if (roundActive && sessionId != 0)
            {
                if (round is null)
                {
                    round = new RoundSeen { Map = Text(OnlineText.MapToLoad), Since = t, SessionId = sessionId, TicksAtStart = s_mainTicks };
                    rounds.Add(round);
                    Console.WriteLine($"t={t:F1} round entered on '{round.Map}': main table session {info.Id} on '{new string(info.Map)}', state {(BridgeState)info.State}, remote {(info.Flags & BridgeSessionFlags.Remote) != 0}, merc {(info.Flags & BridgeSessionFlags.Merc) != 0}; needs the load report: {(status.Flags & OnlineStatusFlags.NeedsLoadReport) != 0}");
                    round.Remote = (info.Flags & BridgeSessionFlags.Remote) != 0 && info.State == (int)BridgeState.Playing &&
                        string.Equals(new string(info.Map), round.Map, StringComparison.OrdinalIgnoreCase);
                }

                // The round's waiting list, as the match overlay reads it every frame while the round loads.
                if (state == OnlineState.Loading)
                {
                    var wait = new RoundWaitLine[32];
                    int waitCount;
                    var waitText = new System.Text.StringBuilder();
                    fixed (RoundWaitLine* w = wait)
                    {
                        waitCount = s_round.GetWaitList(w, wait.Length);
                        waitText.Append(waitCount);
                        for (int i = 0; i < waitCount && i < wait.Length; i++)
                        {
                            waitText.Append(" | ").Append(new string(w[i].Name)).Append(' ').Append((RoundWaitState)w[i].State).Append(" team ").Append(w[i].Team);
                        }
                    }

                    if (waitText.ToString() != round.WaitShown)
                    {
                        round.WaitShown = waitText.ToString();
                        round.WaitLines = Math.Max(round.WaitLines, waitCount);
                        Console.WriteLine($"t={t:F1} waiting list: {waitText}");
                    }

                    if (round.Picture is null)
                    {
                        round.Picture = RoundTextOf(RoundText.LoadingPicture);
                        Console.WriteLine($"t={t:F1} loading picture (seed {status.LoadingPictureSeed}): '{round.Picture}', exists {File.Exists(round.Picture)}");
                    }
                }

                // Unreal's level streams in for a moment, then it reports.
                if (!round.Reported && t - round.Since > 1.0)
                {
                    round.Reported = true;
                    s_api.ReportLoaded();
                    Console.WriteLine($"t={t:F1} reported the level loaded");
                }

                round.Frames++;
                round.Ticks = info.Ticks;
                round.MainTicks = s_mainTicks - round.TicksAtStart;
                round.SameSession &= sessionId == round.SessionId;
                if (state == OnlineState.Match)
                {
                    if (round.MatchSince < 0)
                    {
                        round.MatchSince = t;
                        Console.WriteLine($"t={t:F1} the match started; the server knew this player was waiting: {(status.Flags & OnlineStatusFlags.ReportedWaiting) != 0}");
                    }

                    // Walk forward for a while: the local pawn moves by prediction and the server's corrections.
                    BridgeInput input = default;
                    input.Buttons[0] = (byte)(t - round.MatchSince is > 3.0 and < 9.0 ? 1 : 0);
                    s_main.SetInput(&input);

                    int count;
                    fixed (BridgePawn* p = pawns)
                    {
                        count = s_main.GetPawns(p, pawns.Length);
                    }

                    round.Sample(pawns.AsSpan(0, count));
                    BridgeView view;
                    s_main.GetView(&view);
                    round.ViewValid |= view.Valid != 0;
                    round.HideLocalBody |= view.HideLocalBody != 0;
                    fixed (BridgeBone* b = bones)
                    {
                        foreach (BridgePawn pawn in pawns.AsSpan(0, count))
                        {
                            round.PoseBones = Math.Max(round.PoseBones, s_main.GetPawnPose(pawn.ActorId, b, bones.Length));
                        }
                    }

                    round.Features();

                    if (t >= nextReport)
                    {
                        nextReport = t + 10;
                        round.Report(t, view);
                    }

                    if (rounds.Count == 2 && t - round.MatchSince >= secondMatchSeconds)
                    {
                        round.Report(t, view);
                        break;
                    }
                }
            }
            else if (round is not null)
            {
                Console.WriteLine($"t={t:F1} the round on '{round.Map}' was released after {round.Ticks} client ticks over {round.Frames} frames (state {state})");
                round.Report(t, default);
                round = null;
                nextReport = 0;
            }

            if (!results && (status.Flags & OnlineStatusFlags.ResultsReady) != 0)
            {
                results = true;
                var rows = new OnlineResultRow[32];
                OnlineResults head;
                int count;
                fixed (OnlineResultRow* r = rows)
                {
                    count = s_api.GetResults(&head, r, rows.Length);
                }

                Console.WriteLine($"t={t:F1} results of '{Text(OnlineText.ResultsMap)}': winning team {head.WinningTeam}, reason {head.WinningReason}, objectives {head.ObjectivesTaken}/{head.ObjectivesNeeded}, games left {head.GamesLeft}, {count} rows (head says {head.RowCount}); next in {status.Countdown:F1} s");
                for (int i = 0; i < count && i < rows.Length; i++)
                {
                    OnlineResultRow r = rows[i];
                    Console.WriteLine($"   #{r.Id} '{new string(r.Name)}' team {r.Team} score {r.Score} kills {r.Kills} deaths {r.Deaths} lives {r.Lives}");
                }

                var extras = new RoundResultExtra[32];
                int extraCount;
                fixed (RoundResultExtra* e = extras)
                {
                    extraCount = s_round.GetResultExtras(e, extras.Length);
                }

                bool sameOrder = extraCount == count;
                for (int i = 0; i < extraCount && i < extras.Length; i++)
                {
                    sameOrder &= extras[i].Id == rows[i].Id;
                    Console.WriteLine($"   extras #{extras[i].Id}: objectives {extras[i].Objectives}, left early {extras[i].LeftEarly}, rank {extras[i].Rank}");
                }

                int demo = s_round.GetDemoFlags();
                Console.WriteLine($"   {extraCount} extra line(s) in the results' order {sameOrder}; demo flags {demo}, status '{RoundTextOf(RoundText.DemoStatus)}', last demo '{RoundTextOf(RoundText.LastDemoPath)}'");
                results = count > 0 && sameOrder;
            }

            Thread.Sleep(4);
        }

        bool ok = rounds.Count == 2 && results && backInLobby;
        foreach (RoundSeen seen in rounds)
        {
            (int moved, int others) = seen.Moved();
            Console.WriteLine($"round on '{seen.Map}': {seen.Pawns.Count} pawns seen, {moved} moved more than a metre ({others} of them other players, replicated from the server), view valid {seen.ViewValid}, pose bones {seen.PoseBones}");
            Console.WriteLine($"   main table: session {seen.SessionId} remote {seen.Remote}, the same session all round {seen.SameSession}, Update reported {seen.MainTicks} of {seen.Ticks} client ticks, local body hidden at some point {seen.HideLocalBody}; teams of the pawns: {seen.Teams()}");
            Console.WriteLine($"   features on the round: HUD valid {seen.HudValid} side {seen.HudSide}, {seen.HudMessages} HUD messages, {seen.Sounds} sound records, {seen.RawEvents} raw events, {seen.WorldActors} level actors, {seen.WorldLights} lights, {seen.Equipment} equipment rows");
            ok &= others > 0 && seen.ViewValid && seen.PoseBones > 0 && seen.MatchSince >= 0;
            ok &= seen.Remote && seen.SameSession && seen.MainTicks > 0 && seen.TeamsRight() && seen.HudValid && seen.WorldActors > 0 &&
                seen.Equipment > 0 && seen.RawEvents > 0;
            Console.WriteLine($"   while it loaded: waiting list of up to {seen.WaitLines} line(s), loading picture '{seen.Picture}'");
            ok &= seen.WaitLines > 0 && seen.Picture is { Length: > 0 } picture && File.Exists(picture);
        }

        ok &= rounds.Count == 2 && !string.Equals(rounds[0].Map, rounds[1].Map, StringComparison.OrdinalIgnoreCase);
        if (!ok)
        {
            Console.WriteLine($"FAILED: rounds {rounds.Count}, results {results}, back in the lobby {backInLobby}");
        }

        // The main table's stop in the middle of the round (the player quit to the menu and Unreal only stopped its
        // session). A stop addressed to another session must do nothing; the session's own stop ends it, and the
        // visit must notice and leave the server, not sit there unticked until the server times the player out.
        int current = s_main.GetSession(null);
        int stale = s_main.StopSession(current + 1000);
        int staleUpdate = s_main.UpdateSession(current + 1000);
        bool stillThere = s_main.GetSession(null) == current && current != 0;
        int stopped = s_main.StopSession(current);
        OnlineStatus left = Frame();
        Console.WriteLine($"t={Now:F1} StopSession for another id: {stale} (UpdateSession {staleUpdate}), session still there {stillThere}; StopSession({current}): {stopped}; state {(OnlineState)left.State}, round active {(left.Flags & OnlineStatusFlags.RoundActive) != 0}, players {left.PlayerCount}, chat waiting {left.ChatWaiting}, main state {(BridgeState)s_main.GetState()}");
        return ok && stale == 0 && staleUpdate == -1 && stillThere && stopped == 1 && left.State == (int)OnlineState.Idle && s_adopted is null &&
            s_main.GetState() == (int)BridgeState.Idle;
    }

    /// <summary>The feature tables on the session in the main slot, for <see cref="RoundSeen.Features"/>.</summary>
    internal static void ReadFeatures(RoundSeen round)
    {
        HudState hud;
        if (s_hud.GetState(&hud) != 0 || hud.Valid != 0)
        {
            round.HudValid |= hud.Valid != 0;
            round.HudSide = hud.Side;
        }

        HudMessage* messages = stackalloc HudMessage[8];
        int taken;
        do
        {
            taken = s_hud.DrainMessages(messages, 8);
            round.HudMessages += taken;
        }
        while (taken == 8);

        BridgeSound* sounds = stackalloc BridgeSound[64];
        do
        {
            taken = s_events.DrainSounds(sounds, 64);
            round.Sounds += taken;
        }
        while (taken == 64);

        BridgeSimEvent* events = stackalloc BridgeSimEvent[256];
        do
        {
            taken = s_events.DrainEvents(events, 256);
            round.RawEvents += taken;
        }
        while (taken == 256);

        round.WorldActors = Math.Max(round.WorldActors, s_world.GetActors(null, 0));
        BridgeWorldLight* lights = stackalloc BridgeWorldLight[256];
        round.WorldLights = Math.Max(round.WorldLights, s_world.GetLights(lights, 256));
        BridgeEquipment* equipment = stackalloc BridgeEquipment[32];
        round.Equipment = Math.Max(round.Equipment, s_equipment.GetEquipment(equipment, 32));
    }

    // ---- 5. a second visit, and the server closing under it -----------------------------------------------------

    private static bool ServerCloses(Process server, int port)
    {
        OnlineBridge.Instance.Join("127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture), password: null, preferredTeam: -1, loopbackOnly: true);
        double from = Now;
        OnlineStatus status = Frame();
        while (status.State is (int)OnlineState.Connecting && Now - from < 30)
        {
            Thread.Sleep(8);
            status = Frame();
        }

        Console.WriteLine($"t={Now:F1} second visit: state {(OnlineState)status.State} (server phase {status.Phase}), local id {status.LocalId}, {status.PlayerCount} players, current map '{Text(OnlineText.CurrentMap)}'");
        if (status.State is (int)OnlineState.Failed or (int)OnlineState.Connecting)
        {
            Console.WriteLine("FAILED: " + Text(OnlineText.Error));
            return false;
        }

        server.StandardInput.WriteLine("close");
        from = Now;
        while (status.State != (int)OnlineState.Failed && Now - from < 30)
        {
            Thread.Sleep(8);
            status = Frame();
        }

        OnlineChatLine line;
        while (s_api.TakeChat(&line) != 0)
        {
            Console.WriteLine($"   chat {(line.System != 0 ? "[system]" : $"<{new string(line.Sender)}>")} {new string(line.Text)}");
        }

        string error = Text(OnlineText.Error);
        Console.WriteLine($"t={Now:F1} the server closed: state {(OnlineState)status.State} after {Now - from:F1} s, kicked {(status.Flags & OnlineStatusFlags.Kicked) != 0}, error: '{error}'");
        s_api.Leave();
        OnlineStatus after = Frame();
        Console.WriteLine($"t={Now:F1} after Leave: state {(OnlineState)after.State}");
        return status.State == (int)OnlineState.Failed && error.Length > 0 && after.State == (int)OnlineState.Idle;
    }

    // ---- helpers ------------------------------------------------------------------------------------------------

    private static double Now => s_run.Elapsed.TotalSeconds;

    /// <summary>A text through the exported GetText, with the buffer grown to the length it reports.</summary>
    private static string RoundTextOf(RoundText kind)
    {
        char* buffer = stackalloc char[1024];
        int length = s_round.GetText((int)kind, buffer, 1024);
        return new string(buffer, 0, Math.Min(length, 1023));
    }

    private static string Text(OnlineText kind)
    {
        char* small = stackalloc char[256];
        int length = s_api.GetText((int)kind, small, 256);
        if (length < 256)
        {
            return new string(small, 0, length);
        }

        char[] large = new char[length + 1];
        fixed (char* buffer = large)
        {
            s_api.GetText((int)kind, buffer, large.Length);
            return new string(buffer, 0, length);
        }
    }

    private static string StatusFlags(int flags)
    {
        var names = new List<string>();
        void Add(int bit, string name)
        {
            if ((flags & bit) != 0)
            {
                names.Add(name);
            }
        }

        Add(OnlineStatusFlags.Dedicated, "dedicated");
        Add(OnlineStatusFlags.Kicked, "kicked");
        Add(OnlineStatusFlags.RoundActive, "round-active");
        Add(OnlineStatusFlags.PreparingBots, "preparing-bots");
        Add(OnlineStatusFlags.CanControl, "can-control");
        Add(OnlineStatusFlags.CanLaunch, "can-launch");
        Add(OnlineStatusFlags.CanStart, "can-start");
        Add(OnlineStatusFlags.NeedsLoadReport, "needs-load-report");
        Add(OnlineStatusFlags.ReportedWaiting, "reported-waiting");
        Add(OnlineStatusFlags.ResultsReady, "results-ready");
        Add(OnlineStatusFlags.LocalReady, "local-ready");
        Add(OnlineStatusFlags.RoundRefused, "round-refused");
        Add(OnlineStatusFlags.Practice, "practice");
        Add(OnlineStatusFlags.Revenge, "revenge");
        return string.Join(",", names);
    }
}

/// <summary>What the probe saw of one round, from the pawns as the main table would hand them to Unreal.</summary>
internal sealed class RoundSeen
{
    public string Map = string.Empty;
    public double Since, MatchSince = -1;
    public bool Reported, ViewValid, Remote, HideLocalBody, HudValid;
    public bool SameSession = true;
    public int Frames, PoseBones, SessionId, TicksAtStart, MainTicks, HudSide, HudMessages, Sounds, RawEvents, WorldActors, WorldLights, Equipment;
    public long Ticks;

    /// <summary>The round table while the round loaded: the waiting list last printed, its most lines, the loading picture.</summary>
    public string WaitShown = string.Empty;
    public int WaitLines;
    public string? Picture;

    /// <summary>The other tables read the same session: the HUD, the events, the level state and the equipment.</summary>
    public void Features() => Probe.ReadFeatures(this);

    /// <summary>"spy 0, merc 1, ..." for the pawns seen.</summary>
    public string Teams()
    {
        var text = new System.Text.StringBuilder();
        foreach (Track track in Pawns.Values)
        {
            text.Append(text.Length > 0 ? ", " : string.Empty).Append(track.Merc ? "merc " : "spy ").Append(track.Team);
        }

        return text.ToString();
    }

    /// <summary>Every merc is on team 1 and every spy on team 0, and both sides were seen (the bots fill both).</summary>
    public bool TeamsRight()
    {
        bool spies = false, mercs = false;
        foreach (Track track in Pawns.Values)
        {
            if (track.Team != (track.Merc ? 1 : 0))
            {
                return false;
            }

            spies |= !track.Merc;
            mercs |= track.Merc;
        }

        return spies && mercs;
    }
    public readonly Dictionary<int, Track> Pawns = new();

    /// <summary>Every frame: where each drawn pawn is, and how far it has gone.</summary>
    public void Sample(ReadOnlySpan<BridgePawn> pawns)
    {
        foreach (BridgePawn pawn in pawns)
        {
            if (!Pawns.TryGetValue(pawn.ActorId, out Track? track))
            {
                track = Pawns[pawn.ActorId] = new Track { X = pawn.X, Y = pawn.Y, Z = pawn.Z };
            }

            float dx = pawn.X - track.X, dy = pawn.Y - track.Y, dz = pawn.Z - track.Z;
            float step = MathF.Sqrt(dx * dx + dy * dy + dz * dz);
            // A step of metres in one frame is a respawn, not walking.
            track.Path += step < 300f ? step : 0f;
            (track.X, track.Y, track.Z) = (pawn.X, pawn.Y, pawn.Z);
            track.Local = (pawn.Flags & BridgePawnFlags.Local) != 0;
            track.Merc = pawn.Kind == 1;
            track.Health = pawn.Health;
            track.Team = pawn.Team;
            track.Yaw = pawn.Yaw;
        }
    }

    public void Report(double t, BridgeView view)
    {
        Console.WriteLine($"t={t:F1} round on '{Map}': {Ticks} client ticks over {Frames} frames; view valid={view.Valid} at ({view.X:F0},{view.Y:F0},{view.Z:F0}) yaw {view.Yaw:F0} fov {view.Fov:F0}");
        foreach (KeyValuePair<int, Track> pair in Pawns)
        {
            Track p = pair.Value;
            Console.WriteLine($"   #{pair.Key} {(p.Merc ? "merc" : "spy ")} team {p.Team} {(p.Local ? "LOCAL " : "remote")} hp {p.Health} at ({p.X:F0},{p.Y:F0},{p.Z:F0}) yaw {p.Yaw:F0} walked {p.Path / 100f:F1} m so far");
        }
    }

    public (int Moved, int Others) Moved()
    {
        int moved = 0, others = 0;
        foreach (Track track in Pawns.Values)
        {
            if (track.Path > 100f)
            {
                moved++;
                others += track.Local ? 0 : 1;
            }
        }

        return (moved, others);
    }
}

internal sealed class Track
{
    public float X, Y, Z, Yaw, Path;
    public bool Local, Merc;
    public int Health, Team;
}
