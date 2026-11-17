using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Net.Discovery;
using OpenScct.Net.Voice;
using OpenScct.Server;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Voice;

/// <summary>
/// <c>VoiceProbe hack [content root] [port] [map]</c>: hacked comms in a real round, end to end. An OpenSCCT online
/// server runs in this process (so its controllers and pawns can be read and the merc tagged with the game's own spy
/// mark, DetectedByMarqueurSpy), on UDP loopback. The bridge's visit plays the spy, driven as Unreal drives it: the
/// controls table's SetSettings for the auto-hack option and its Frame, once a frame, with the H key (Hack Comm) down or
/// up; the voice table for what is heard. A plain OpenSCCT client plays a merc who talks a 440 Hz tone on the team
/// channel the whole time, as a bot client or a Godot player would. Rule 13 lets the spy hear him only while the merc
/// is tagged by the spies and the spy's server-side controller holds the key or has auto-hack on, so:
///   A. tagged, auto-hack on, no key: heard, the talker row says Hacked, the server's member has HackCommKey;
///   B. tagged, auto-hack off, no key: not heard, HackCommKey off;
///   C. tagged, auto-hack off, H held: heard, the server's copy of bSpecialsChat is 1;
///   D. untagged, auto-hack on: not heard (the key and the option only open what the spy mark allows);
///   E. the merc lying on the ground beside the spy, who says something with push to talk on and no talk key: nothing goes;
///   F. the same with H held (the HUD's Speak prompt answered): the bridge talks on the all channel, the server makes the
///      spy the downed merc's chat partner (rule 9) and the merc hears him.
/// </summary>
internal static unsafe class HackProbe
{
    private const double ToneHz = 440;
    private const float ToneAmplitude = 0.3f;
    private const string SpyName = "UnrealSpy";
    private const string MercName = "MercBot";

    private static readonly Stopwatch s_run = Stopwatch.StartNew();

    private static double Now => s_run.Elapsed.TotalSeconds;

    public static int Run(string[] args)
    {
        string content = args.Length > 1 ? args[1] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
        int port = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 28953;
        string map = args.Length > 3 ? args[3] : "AquaE";

        VoiceApi voiceTable = default;
        delegate* unmanaged<VoiceApi*, int> getVoice = &VoiceExports.GetApi;
        int version = getVoice(&voiceTable);
        OnlineApi onlineTable = default;
        delegate* unmanaged<OnlineApi*, int> getOnline = &OnlineExports.GetApi;
        getOnline(&onlineTable);
        ControlsApi controlsTable = default;
        delegate* unmanaged<ControlsApi*, int> getControls = &ControlsExports.GetApi;
        getControls(&controlsTable);
        VoiceApi voice = voiceTable;
        OnlineApi online = onlineTable;
        ControlsApi controls = controlsTable;
        Console.WriteLine($"voice table version {version}, talker row {voice.TalkerInfoSize} bytes");

        // The spy only listens: the clean-up off and push to talk on with no key held, so he sends nothing.
        VoiceSettingsBlock settings = default;
        voice.GetSettings(&settings);
        settings.NoiseSuppression = 0;
        settings.AutoGain = 0;
        settings.EchoCancellation = 0;
        settings.PushToTalk = 1;
        voice.ApplySettings(&settings);
        voice.SetCaptureRate(VoiceK.SampleRate);

        void AutoHack(bool on)
        {
            BridgeControlSettings s = default;
            controls.GetSettings(&s);
            s.AutoHackComms = on ? 1 : 0;
            controls.SetSettings(&s);
        }

        AutoHack(false);
        BridgeSession.Configure(content);
        var clock = new HackClock();
        var udp = LiteNetLibTransport.Listen(port, 8, loopbackOnly: true);
        var server = new OnlineServer(udp, new OnlineServerSettings
        {
            Name = "VoiceProbe hack server",
            Dedicated = true,
            MaxPlayers = 4,
            Setup = new LobbySetup { Map = map, MaxPlayers = 4 },
            // The maps as a dedicated server finds them (svm-server: the content's svmap folder, then the defaults).
            Maps = new MapCatalog([System.IO.Path.Combine(content, "svmap"), .. MapCatalog.CreateDefault().Roots]),
            MinPlayersToLaunch = 2,
            LaunchCountdownSeconds = 0f,
            LanPort = 0,
            LanLoopbackOnly = true,
        }, clock.NowMicroseconds, udp);
        server.Logged += line => Console.WriteLine("   [server] " + line);

        string address = "127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture);
        OnlineBridge.Instance.SetIdentity(SpyName, null, null);
        OnlineBridge.Instance.Join(address, password: null, preferredTeam: 0, loopbackOnly: true);
        var merc = new OnlineClient(JoinTarget.FromAddress(address), new OnlineClientSettings
        {
            PlayerName = MercName,
            InstallId = "00000000000000cc",
            Key = PlayerKey.Create(),
            PreferredTeam = 1,
            Maps = OnlineBridge.Instance.Catalog.Maps,
            Classes = OnlineBridge.Instance.Classes,
            LoopbackOnly = true,
        }, clock.NowMicroseconds);
        // Team push to talk held for the whole run: the merc talks to his team only.
        var mercVoice = new OnlineVoice(merc, new VoiceClient(new VoiceSettings
        {
            PushToTalk = true, NoiseSuppression = false, AutoGain = false, EchoCancellation = false,
        }, clock));
        mercVoice.Voice.TeamKey = true;

        // Everything on this one thread, as the simulation's worlds expect: the server, the merc and the bridge each
        // take their due 60 Hz ticks in turn. The mark goes on and the server is read after each server tick.
        var view = new ServerView(SpyName);
        long start = clock.NowMicroseconds;
        long serverTicks = 0;
        long mercTicks = 0;
        double toneFed = 0;
        double phase = 0;
        var tone = new float[VoiceK.FrameSamples];
        var frame = new float[VoiceK.FrameSamples];
        var heard = new List<float>();
        var mercHeard = new List<float>();
        var spyTone = new float[VoiceK.FrameSamples];
        bool spySpeaks = false;
        double spyFrom = 0;
        double spyFed = 0;
        double spyPhase = 0;
        ushort mercTalker = 0;
        ushort spyTalker = 0;
        double owed = 0;
        double lastPaced = Now;
        double lastFrame = Now;
        bool hackKey = false;
        bool saidReady = false;
        string lastStates = string.Empty;
        object? mercReady = null;
        double nextStatus = 0;

        void Step()
        {
            // The server and the merc at 60 Hz by the clock; a long stall (a map load) drops its backlog.
            long due = (clock.NowMicroseconds - start) * 60 / 1_000_000;
            if (due - serverTicks > 30)
            {
                serverTicks = due - 1;
            }

            while (serverTicks < due)
            {
                serverTicks++;
                server.Tick(start + serverTicks * 1_000_000 / 60);
                view.AfterTick(server);
            }

            if (due - mercTicks > 30)
            {
                mercTicks = due - 1;
            }

            while (mercTicks < due)
            {
                mercTicks++;
                merc.Tick(start + mercTicks * 1_000_000 / 60);
            }

            // The bridge's player as Unreal runs him: one raw input a frame (H for Hack Comm), then the online update.
            BridgeRawInput raw = default;
            raw.Flags = BridgeRawInputFlags.Play;
            raw.DeltaSeconds = (float)(Now - lastFrame);
            lastFrame = Now;
            raw.Keys[(int)BridgeKey.H] = (byte)(hackKey ? 1 : 0);
            if (Exports.Current is not null)
            {
                controls.Frame(&raw);
            }

            double before = Now;
            online.Update();
            if (OnlineBridge.Instance.NeedsLoadReport)
            {
                // Unreal says so once its level is up; here there is no level to wait for.
                online.ReportLoaded();
            }

            if (merc.State == OnlineClientState.Loading && merc.Session is { } mercSession && !ReferenceEquals(mercSession, mercReady))
            {
                // The merc's own client has no level either (a bot client says it at once).
                mercReady = mercSession;
                mercSession.MarkReadyToPlay();
            }

            double took = Now - before;
            string states = $"bridge {OnlineBridge.Instance.State} merc {merc.State} server {view.Read().Phase}";
            if (Now > nextStatus)
            {
                nextStatus = Now + 10;
                ServerSnapshot s = view.Read();
                Console.WriteLine($"t={Now:F1} server ticks {s.Ticks}: {s.Progress}");
            }

            if (states != lastStates || took > 1.0)
            {
                lastStates = states;
                Console.WriteLine($"t={Now:F1} {states}{(took > 1.0 ? $" (the bridge's update took {took:F1} s)" : string.Empty)}" +
                    $"{(OnlineBridge.Instance.Error is { Length: > 0 } e ? " bridge error: " + e : string.Empty)}{(merc.Error is { } me ? " merc error: " + me : string.Empty)}");
            }

            if (!saidReady && OnlineBridge.Instance.State == OnlineState.Lobby && merc.State == OnlineClientState.Lobby)
            {
                saidReady = true;
                online.SetReady(1);
                merc.SetReady(true);
            }

            int n = (int)((Now - 0) * VoiceK.SampleRate - toneFed);
            while (n > 0)
            {
                int take = Math.Min(n, tone.Length);
                for (int i = 0; i < take; i++)
                {
                    tone[i] = ToneAmplitude * (float)Math.Sin(phase);
                    phase += 2 * Math.PI * ToneHz / VoiceK.SampleRate;
                }

                mercVoice.Voice.SubmitCapture(tone.AsSpan(0, take));
                toneFed += take;
                n -= take;
            }

            // The spy's microphone: the tone while the phase has him say something (push to talk decides if it goes).
            if (spySpeaks)
            {
                int m = (int)((Now - spyFrom) * VoiceK.SampleRate - spyFed);
                while (m > 0)
                {
                    int take = Math.Min(m, spyTone.Length);
                    for (int i = 0; i < take; i++)
                    {
                        spyTone[i] = ToneAmplitude * (float)Math.Sin(spyPhase);
                        spyPhase += 2 * Math.PI * ToneHz / VoiceK.SampleRate;
                    }

                    fixed (float* s = spyTone)
                    {
                        voice.SubmitCapture(s, take);
                    }

                    spyFed += take;
                    m -= take;
                }
            }

            // Both playbacks paced as an audio output reads: 50 frames a second.
            owed += (Now - lastPaced) * VoiceK.FramesPerSecond;
            lastPaced = Now;
            int paced = (int)Math.Min(owed, VoiceK.MaxJitterFrames);
            owed -= paced;
            mercVoice.Update();
            foreach (VoiceTalker t in mercVoice.Voice.Talkers)
            {
                for (int i = 0; i < paced && t.ReadFrame(frame); i++)
                {
                    if (t.Id == spyTalker)
                    {
                        mercHeard.AddRange(frame);
                    }
                }
            }
            if (mercTalker != 0)
            {
                fixed (float* f = frame)
                {
                    for (int i = 0; i < paced && voice.ReadFrame(mercTalker, f) != 0; i++)
                    {
                        heard.AddRange(frame);
                    }
                }
            }

            Thread.Sleep(5);
        }

        bool Wait(Func<bool> done, double seconds)
        {
            double end = Now + seconds;
            while (Now < end)
            {
                Step();
                if (done())
                {
                    return true;
                }
            }

            return done();
        }

        VoiceTalkerInfo Row(ushort talker)
        {
            var rows = new VoiceTalkerInfo[8];
            int count;
            fixed (VoiceTalkerInfo* r = rows)
            {
                count = voice.GetTalkers(r, rows.Length);
            }

            int index = Array.FindIndex(rows, 0, Math.Min(count, rows.Length), r => r.Talker == talker);
            return index >= 0 ? rows[index] : default;
        }

        bool ok = false;
        try
        {
            bool inRound = Wait(() =>
                view.Read() is { Phase: SessionPhase.Match, SpyPawn: true, MercPawn: true } && merc.State == OnlineClientState.Match && Exports.Current is not null &&
                mercVoice.IsConnected && mercVoice.Voice.LocalTalker != 0 && State(voice).Connected != 0 && State(voice).LocalTalker != 0, 120);
            Console.WriteLine($"t={Now:F1} round: server {view.Read().Phase} on {map}, bridge {OnlineBridge.Instance.State}, merc {merc.State}, " +
                $"bridge session {(Exports.Current is null ? "none" : "up")}, merc talker {mercVoice.Voice.LocalTalker}, spy talker {State(voice).LocalTalker}; error '{OnlineBridge.Instance.Error}'");
            if (!inRound)
            {
                return 1;
            }

            mercTalker = mercVoice.Voice.LocalTalker;
            spyTalker = (ushort)State(voice).LocalTalker;
            Wait(() => false, 2.0);

            (double Rms, int Frames, VoiceTalkerInfo Row, ServerSnapshot Server) Measure(string label, bool auto, bool key, bool tagged)
            {
                AutoHack(auto);
                hackKey = key;
                view.Tag = tagged;
                // The option and the key reach the server through the round (UpdateSpecialsChat's calls), the roster
                // is rebuilt a few times a second and the jitter buffer fills: two seconds, then two to measure.
                Wait(() => false, 2.0);
                heard.Clear();
                Wait(() => false, 2.0);
                VoiceTalkerInfo row = Row(mercTalker);
                ServerSnapshot s = view.Read();
                double rms = Rms(heard);
                VoiceContext context = default;
                voice.GetContext(&context);
                Console.WriteLine($"t={Now:F1} {label}: heard talker={mercTalker} name={Name(ref row)} rms={rms:F3} frames={heard.Count / VoiceK.FrameSamples} " +
                    $"channel={(VoiceLink)row.Link} packet channel={(row.Channel != 0 ? "team" : "all")} speaking={row.Speaking}; " +
                    $"server: spy bSpecialsChat={s.SpecialsChat} bAutoHackComms={s.AutoHack} member.HackCommKey={s.MemberKey} " +
                    $"relay roster HackCommKey={s.RelayKey} merc SpiedByTeam={s.MercSpiedBy}; client context HackComm={context.HackComm} talk link={(VoiceLink)context.TalkLink}");
                return (rms, heard.Count / VoiceK.FrameSamples, row, s);
            }

            var a = Measure("A tagged, auto-hack on", auto: true, key: false, tagged: true);
            var b = Measure("B tagged, auto-hack off", auto: false, key: false, tagged: true);
            var c = Measure("C tagged, auto-hack off, Hack Comm held", auto: false, key: true, tagged: true);
            var d = Measure("D untagged, auto-hack on", auto: true, key: false, tagged: false);

            // Downed chat (rule 9): the merc lies on the ground next to the spy. The spy says something with push to talk
            // on and no talk key: without Hack Comm nothing goes out; with it held, the HUD's Speak prompt is answered,
            // the bridge talks on the all channel and the server makes the spy the merc's chat partner, so he hears it.
            (double Rms, int Frames, VoiceStatus Status, VoiceContext Context, ServerSnapshot Server) Downed(string label, bool key)
            {
                AutoHack(false);
                view.Tag = false;
                view.Downed = true;
                hackKey = key;
                if (!spySpeaks)
                {
                    spySpeaks = true;
                    spyFrom = Now;
                    spyFed = 0;
                }

                Wait(() => false, 2.0);
                mercHeard.Clear();
                Wait(() => false, 2.0);
                VoiceStatus status = State(voice);
                VoiceContext context = default;
                voice.GetContext(&context);
                ServerSnapshot s = view.Read();
                double rms = Rms(mercHeard);
                Console.WriteLine($"t={Now:F1} {label}: spy transmitting={status.Transmitting} channel={(status.Channel != 0 ? "team" : "all")} talk link={(VoiceLink)context.TalkLink}; " +
                    $"merc heard talker={spyTalker} rms={rms:F3} frames={mercHeard.Count / VoiceK.FrameSamples}; server: merc lying={s.MercLying} spy {s.Distance:F0} uu away, " +
                    $"spy bSpecialsChat={s.SpecialsChat}, merc chats with the spy={s.MercChatsWithSpy}");
                return (rms, mercHeard.Count / VoiceK.FrameSamples, status, context, s);
            }

            var e = Downed("E merc downed beside the spy, push to talk, no key", key: false);
            var f = Downed("F merc downed beside the spy, Hack Comm held (the Speak prompt)", key: true);
            hackKey = false;
            view.Downed = false;
            spySpeaks = false;

            bool okE = e.Frames == 0 && e.Status.Transmitting == 0 && e.Context.TalkLink == (int)VoiceLink.All && e.Server.MercLying;
            bool okF = f.Rms > 0.1 && f.Frames > 50 && f.Status.Transmitting != 0 && f.Status.Channel == 0 &&
                f.Context.TalkLink == (int)VoiceLink.Downed && f.Server.MercChatsWithSpy;
            bool okA = a.Rms > 0.1 && a.Frames > 50 && a.Row.Link == (int)VoiceLink.Hacked && a.Server.MemberKey && a.Server.RelayKey;
            bool okB = b.Frames == 0 && !b.Server.MemberKey && !b.Server.RelayKey;
            bool okC = c.Rms > 0.1 && c.Frames > 50 && c.Row.Link == (int)VoiceLink.Hacked && c.Server.MemberKey && c.Server.SpecialsChat == 1;
            bool okD = d.Frames == 0 && d.Server.MemberKey && d.Server.MercSpiedBy < 0;
            Console.WriteLine($"auto-hack heard {(okA ? "ok" : "FAILED")}, auto-hack off silent {(okB ? "ok" : "FAILED")}, " +
                $"Hack Comm key heard {(okC ? "ok" : "FAILED")}, untagged silent {(okD ? "ok" : "FAILED")}, " +
                $"downed without the key silent {(okE ? "ok" : "FAILED")}, downed chat with the key heard {(okF ? "ok" : "FAILED")}");
            ok = okA && okB && okC && okD && okE && okF;
            return ok ? 0 : 1;
        }
        finally
        {
            AutoHack(false);
            mercVoice.Dispose();
            merc.Leave();
            merc.Dispose();
            online.Leave();
            OnlineBridge.Instance.Dispose();
            VoiceBridge.Instance.Dispose();
            server.Dispose();
            Console.WriteLine(ok ? "HACK PROBE OK" : "HACK PROBE FAILED");
        }
    }

    /// <summary>
    /// <c>VoiceProbe hackhost [content root] [port] [seconds] [spy name]</c>: the same server and talking merc for the
    /// Unreal client to join as the spy (Tools/Run-OnlineCheck.ps1 -NoServer -Port, with -ScctVoiceAutoHack=1): the
    /// merc stays tagged by the spies and talks on the team channel, and every two seconds the server's view of the spy
    /// is printed (bSpecialsChat, bAutoHackComms, the HackCommKey his voice member is routed by).
    /// </summary>
    public static int Host(string[] args)
    {
        string content = args.Length > 1 ? args[1] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
        int port = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 28962;
        double seconds = args.Length > 3 ? double.Parse(args[3], CultureInfo.InvariantCulture) : 120;
        string spyName = args.Length > 4 ? args[4] : "UnrealCheck";
        string map = args.Length > 5 ? args[5] : "AquaE";

        BridgeSession.Configure(content);
        var clock = new HackClock();
        var udp = LiteNetLibTransport.Listen(port, 8, loopbackOnly: true);
        using var server = new OnlineServer(udp, new OnlineServerSettings
        {
            Name = "VoiceProbe hack host",
            Dedicated = true,
            MaxPlayers = 4,
            Setup = new LobbySetup { Map = map, MaxPlayers = 4 },
            Maps = new MapCatalog([System.IO.Path.Combine(content, "svmap"), .. MapCatalog.CreateDefault().Roots]),
            MinPlayersToLaunch = 2,
            LaunchCountdownSeconds = 0f,
            LanPort = 0,
            LanLoopbackOnly = true,
        }, clock.NowMicroseconds, udp);
        server.Logged += line => Console.WriteLine("   [server] " + line);
        Console.WriteLine($"hack host on 127.0.0.1:{port}, {map}, waiting for {spyName}");
        string address = "127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture);
        using var merc = new OnlineClient(JoinTarget.FromAddress(address), new OnlineClientSettings
        {
            PlayerName = MercName,
            InstallId = "00000000000000dd",
            Key = PlayerKey.Create(),
            PreferredTeam = 1,
            Maps = OnlineBridge.Instance.Catalog.Maps,
            Classes = OnlineBridge.Instance.Classes,
            LoopbackOnly = true,
        }, clock.NowMicroseconds);
        using var mercVoice = new OnlineVoice(merc, new VoiceClient(new VoiceSettings
        {
            PushToTalk = true, NoiseSuppression = false, AutoGain = false, EchoCancellation = false,
        }, clock));
        mercVoice.Voice.TeamKey = true;
        var view = new ServerView(spyName);
        var tone = new float[VoiceK.FrameSamples];
        var frame = new float[VoiceK.FrameSamples];
        long start = clock.NowMicroseconds;
        long ticks = 0;
        double fed = 0;
        double phase = 0;
        double nextReport = 2;
        bool ready = false;
        object? loaded = null;
        bool heardKey = false;
        while (Now < seconds)
        {
            long due = (clock.NowMicroseconds - start) * 60 / 1_000_000;
            if (due - ticks > 30)
            {
                ticks = due - 1;
            }

            while (ticks < due)
            {
                ticks++;
                long at = start + ticks * 1_000_000 / 60;
                server.Tick(at);
                view.AfterTick(server);
                merc.Tick(at);
            }

            if (!ready && merc.State == OnlineClientState.Lobby && merc.Lobby.Players.Any(p => p.Name.StartsWith(spyName, StringComparison.Ordinal)))
            {
                ready = true;
                merc.SetReady(true);
            }

            if (merc.State == OnlineClientState.Loading && merc.Session is { } session && !ReferenceEquals(session, loaded))
            {
                loaded = session;
                session.MarkReadyToPlay();
            }

            int n = (int)(Now * VoiceK.SampleRate - fed);
            while (n > 0)
            {
                int take = Math.Min(n, tone.Length);
                for (int i = 0; i < take; i++)
                {
                    tone[i] = ToneAmplitude * (float)Math.Sin(phase);
                    phase += 2 * Math.PI * 660 / VoiceK.SampleRate;
                }

                mercVoice.Voice.SubmitCapture(tone.AsSpan(0, take));
                fed += take;
                n -= take;
            }

            mercVoice.Update();
            foreach (VoiceTalker t in mercVoice.Voice.Talkers)
            {
                for (int i = 0; i < 3 && t.ReadFrame(frame); i++)
                {
                }
            }

            if (Now >= nextReport)
            {
                nextReport += 2;
                ServerSnapshot s = view.Read();
                heardKey |= s.MemberKey && s.RelayKey;
                Console.WriteLine($"t={Now:F1} {s.Phase} merc {merc.State} sent {mercVoice.Voice.Stats.FramesSent} frames (team channel); server: spy bSpecialsChat={s.SpecialsChat} " +
                    $"bAutoHackComms={s.AutoHack} member.HackCommKey={s.MemberKey} relay roster HackCommKey={s.RelayKey} merc SpiedByTeam={s.MercSpiedBy}");
            }

            Thread.Sleep(5);
        }

        merc.Leave();
        Console.WriteLine(heardKey ? "HACK HOST: the spy's HackCommKey reached the relay" : "HACK HOST: the spy never had HackCommKey");
        return heardKey ? 0 : 1;
    }

    private static int SpiedBy(SPlayerController pc) =>
        pc.SBPawn is { bSpyBulletOn: true } pawn && pawn.iTeamSpying >= 0f ? (int)pawn.iTeamSpying : -1;

    /// <summary>What the probe reads of the server, taken after a server tick.</summary>
    private readonly record struct ServerSnapshot(
        SessionPhase Phase, bool SpyPawn, bool MercPawn, int SpecialsChat, bool AutoHack, bool MemberKey, bool RelayKey, int MercSpiedBy, long Ticks, string Progress,
        bool MercLying, float Distance, bool MercChatsWithSpy);

    /// <summary>
    /// The server side of the check, run after each server tick: the merc is marked by the spies (the
    /// game's own spy mark) while <see cref="Tag"/> says so, and the spy's controller and his voice member are read: what
    /// SessionVoiceLinks makes of his controller now, and the member the relay routed by.
    /// </summary>
    private sealed class ServerView
    {
        private readonly object _lock = new();
        private readonly SessionVoiceLinks _links = new();
        private ServerSnapshot _snapshot;
        private readonly string _spyName;
        private volatile bool _tag = true;
        private volatile bool _downed;
        private bool _placeFailed;

        /// <summary>The merc lies on the ground beside the spy (bLyingOnGround, put back each tick, and moved next to him).</summary>
        public bool Downed
        {
            get => _downed;
            set => _downed = value;
        }
        private long _ticks;

        /// <param name="spyName">The spy's player name, or the start of it.</param>
        public ServerView(string spyName) => _spyName = spyName;

        public bool Tag
        {
            get => _tag;
            set => _tag = value;
        }

        public ServerSnapshot Read()
        {
            lock (_lock)
            {
                return _snapshot;
            }
        }

        public void AfterTick(OnlineServer server)
        {
            SPlayerController? spy = server.Host.Players.FirstOrDefault(p => p.Name.StartsWith(_spyName, StringComparison.Ordinal))?.Controller as SPlayerController;
            SPlayerController? merc = Controller(server, MercName);
            if (server.Phase == SessionPhase.Match && merc?.SBPawn is { } mercPawn && spy?.PlayerReplicationInfo?.Team is { } spyTeam)
            {
                if (_tag && !mercPawn.bSpyBulletOn)
                {
                    mercPawn.DetectedByMarqueurSpy(spyTeam.TeamIndex, true);
                }
                else if (!_tag && mercPawn.bSpyBulletOn)
                {
                    mercPawn.DetectedByMarqueurSpy(spyTeam.TeamIndex, false);
                }
            }

            if (server.Phase == SessionPhase.Match && merc?.SBPawn is { } downed && spy?.Pawn is { } spyPawn)
            {
                if (_downed)
                {
                    downed.bLyingOnGround = true;
                    if ((downed.Location - spyPawn.Location).Size() > 150f)
                    {
                        // A free spot beside him: around him at 90 to 150 uu, a little above the floor.
                        bool placed = false;
                        for (int ring = 0; ring < 3 && !placed; ring++)
                        {
                            for (int step = 0; step < 8 && !placed; step++)
                            {
                                double angle = step * Math.PI / 4;
                                float reach = 90f + ring * 30f;
                                placed = downed.SetLocation(spyPawn.Location + new FVector((float)(Math.Cos(angle) * reach), (float)(Math.Sin(angle) * reach), 10f));
                            }
                        }

                        if (!placed)
                        {
                            // The collision test refuses every spot (the lying pawn's own box): put him there as a
                            // teleport would, past the test; the downed chat only measures the distance.
                            downed.Location = spyPawn.Location + new FVector(110f, 0f, 0f);
                            if (!_placeFailed)
                            {
                                _placeFailed = true;
                                Console.WriteLine($"   the merc is put beside the spy at {spyPawn.Location} past the collision test");
                            }
                        }
                    }
                }
                else if (downed.bLyingOnGround && downed.Health > 0)
                {
                    downed.bLyingOnGround = false;
                }
            }

            bool memberKey = false;
            bool relayKey = false;
            if (spy is not null)
            {
                VoiceMember member = VoiceMember.Create(0, spy.PlayerReplicationInfo?.Team?.TeamIndex ?? -1);
                _links.ReadLinks(spy, ref member);
                memberKey = member.HackCommKey;
                if (server.VoiceRelay is { } relay && spy.PlayerReplicationInfo is { } pri)
                {
                    for (int i = 0; i < relay.Roster.Count; i++)
                    {
                        if (relay.Roster[i].Talker == VoiceK.TalkerOf(pri.PlayerID))
                        {
                            relayKey = relay.Roster[i].HackCommKey;
                        }
                    }
                }
            }

            var snapshot = new ServerSnapshot(server.Phase, spy?.Pawn is not null, merc?.SBPawn is not null, spy?.bSpecialsChat ?? 0,
                spy?.bAutoHackComms ?? false, memberKey, relayKey, merc is null ? -1 : SpiedBy(merc), ++_ticks,
                $"spy loaded {spy?.PlayerReplicationInfo?.bWaitingPlayer}, merc loaded {merc?.PlayerReplicationInfo?.bWaitingPlayer}, match started {server.Host.StartGate?.Started}",
                merc?.SBPawn?.bLyingOnGround ?? false,
                merc?.Pawn is { } m && spy?.Pawn is { } p ? (m.Location - p.Location).Size() : -1f,
                merc is not null && spy is not null && merc.PCChattingWith.Any(c => ReferenceEquals(c, spy)));
            lock (_lock)
            {
                _snapshot = snapshot;
            }
        }

        private static SPlayerController? Controller(OnlineServer server, string name) =>
            server.Host.Players.FirstOrDefault(p => p.Name == name)?.Controller as SPlayerController;
    }

    private static VoiceStatus State(VoiceApi voice)
    {
        VoiceStatus status;
        voice.GetState(&status);
        return status;
    }

    private static string Name(ref VoiceTalkerInfo row)
    {
        fixed (char* name = row.Name)
        {
            return new string(name);
        }
    }

    private static double Rms(List<float> samples)
    {
        int skip = samples.Count / 4;
        double sum = 0;
        int n = 0;
        for (int i = skip; i < samples.Count; i++)
        {
            sum += samples[i] * (double)samples[i];
            n++;
        }

        return n == 0 ? 0 : Math.Sqrt(sum / n);
    }

    private sealed class HackClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
