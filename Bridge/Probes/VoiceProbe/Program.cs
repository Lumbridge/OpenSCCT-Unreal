using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using OpenScct.Net;
using OpenScct.Net.Voice;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Voice;

// VoiceProbe <content root> <OpenSCCT checkout> [port]
//
// Checks the voice bridge the way Unreal uses it, through the function table VoiceExports.GetApi fills (the same
// unmanaged entries, called through their pointers), once a frame:
//   1. the struct sizes ScctVoiceApi.h asserts;
//   2. the microphone test without a server: a 440 Hz tone into SubmitCapture, two seconds recorded through the
//      bridge's own relay, then read back with ReadFrame, and the same live;
//   3. a real dedicated server (svm-server, the OpenSCCT checkout's src/OpenScct.Server.Cli) on localhost with two
//      players in its lobby: the bridge's visit (OnlineBridge, ticked through the online table's Update, which ticks
//      the visit's voice after it) and a plain OpenSCCT OnlineClient with its own OnlineVoice, as the Godot client and
//      the bots have. First the bridge's player talks (the tone into SubmitCapture) and the other hears him, then the
//      other talks and the bridge's player hears him through GetTalkers and ReadFrame: the same wire Godot uses.
// The server CLI is run with --no-build; build it once beforehand (dotnet build -c Release src/OpenScct.Server.Cli).
//
// VoiceProbe listen <address> <content root> [seconds] [talker name] [tone Hz]
//
// A second player for a game already on a server (Tools/Run-OnlineCheck.ps1 with the Unreal client): a plain OpenSCCT
// visit with its own voice joins, says ready, follows the server from lobby to round, talks a tone the whole time
// (voice activation) and listens. Every two seconds it prints what it heard from each talker; at the end the talker
// named <talker name> (default UnrealCheck) must have been heard at rms > 0.1 and the tone's pitch.
return args.Length > 0 && args[0] == "listen" ? Listener.Run(args) : Probe.Run(args);

internal static unsafe class Probe
{
    private const double ToneHz = 440;
    private const float ToneAmplitude = 0.3f;

    private static VoiceApi s_voice;
    private static OnlineApi s_online;
    private static readonly Stopwatch s_run = Stopwatch.StartNew();
    private static double s_tonePhase;
    private static string s_volumeFile = string.Empty;

    private static double Now => s_run.Elapsed.TotalSeconds;

    public static int Run(string[] args)
    {
        string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
        string openScct = args.Length > 1 ? args[1] : @"C:\Users\ryans\source\repos\OSC-unreal-bridge";
        int port = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 28951;

        bool ok = CheckLayout();
        VoiceApi voice = default;
        delegate* unmanaged<VoiceApi*, int> getVoice = &VoiceExports.GetApi;
        int version = getVoice(&voice);
        s_voice = voice;
        Console.WriteLine($"voice table: version {version} (header expects {VoiceBridge.ApiVersion}), talker row {voice.TalkerInfoSize} bytes");
        OnlineApi online = default;
        delegate* unmanaged<OnlineApi*, int> getOnline = &OnlineExports.GetApi;
        getOnline(&online);
        s_online = online;

        // The options a test tone needs: the clean-up off (noise suppression takes a steady tone away), voice activation.
        VoiceSettingsBlock settings = default;
        s_voice.GetSettings(&settings);
        settings.NoiseSuppression = 0;
        settings.AutoGain = 0;
        settings.EchoCancellation = 0;
        settings.PushToTalk = 0;
        s_voice.ApplySettings(&settings);
        s_voice.SetCaptureRate(VoiceK.SampleRate);

        ok &= EchoTest(recordSeconds: 2);
        ok &= EchoTest(recordSeconds: 0);
        ok &= Meter();

        BridgeSession.Configure(content);
        string work = Path.Combine(Path.GetTempPath(), "osu-voice-probe-" + Environment.ProcessId.ToString(CultureInfo.InvariantCulture));
        Directory.CreateDirectory(work);
        Process? server = null;
        try
        {
            server = StartServer(openScct, content, work, port);
            s_volumeFile = Path.Combine(work, "VoiceVolumes.json");
            fixed (char* path = s_volumeFile)
            {
                s_voice.SetVolumeFile(path);
            }

            ok &= Lobby(port);
        }
        finally
        {
            s_online.Leave();
            OnlineBridge.Instance.Dispose();
            VoiceBridge.Instance.Dispose();
            if (server is { HasExited: false })
            {
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

        string error = Error();
        if (error.Length > 0)
        {
            Console.WriteLine("voice error: " + error);
            ok = false;
        }

        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    // ---- 1. layouts ---------------------------------------------------------------------------------------------

    private static bool CheckLayout()
    {
        (string Name, int Size, int Expected)[] sizes =
        [
            ("VoiceSettingsBlock", sizeof(VoiceSettingsBlock), 48),
            ("VoiceStatus", sizeof(VoiceStatus), 64),
            ("VoiceTalkerInfo", sizeof(VoiceTalkerInfo), 176),
            ("VoicePlayerInfo", sizeof(VoicePlayerInfo), 28),
            ("VoiceContext", sizeof(VoiceContext), 16),
            ("VoiceApi", sizeof(VoiceApi), 8 + 24 * sizeof(void*)),
        ];
        bool ok = true;
        foreach ((string name, int size, int expected) in sizes)
        {
            ok &= size == expected;
            Console.WriteLine($"layout: {name} {size} bytes{(size == expected ? string.Empty : $" BUT THE HEADER ASSERTS {expected}")}");
        }

        return ok;
    }

    // ---- 2. the microphone test ---------------------------------------------------------------------------------

    private static bool EchoTest(int recordSeconds)
    {
        string mode = recordSeconds > 0 ? $"recorded {recordSeconds} s" : "live";
        if (s_voice.StartEchoTest(recordSeconds) == 0)
        {
            Console.WriteLine($"echo test ({mode}): refused");
            return false;
        }

        var heard = new List<float>();
        var frame = new float[VoiceK.FrameSamples];
        double from = Now;
        double fed = 0;
        double owed = 0;
        double lastPaced = Now;
        VoiceStatus status = default;
        double limit = recordSeconds > 0 ? recordSeconds * 2 + 4 : 3;
        while (Now - from < limit)
        {
            if (recordSeconds == 0 || Now - from < recordSeconds)
            {
                fed = FeedTone(from, fed, frame);
            }

            s_voice.Update();
            // Read as an audio output would: 50 frames a second.
            owed += (Now - lastPaced) * VoiceK.FramesPerSecond;
            lastPaced = Now;
            int paced = (int)Math.Min(owed, VoiceK.MaxJitterFrames);
            owed -= paced;
            fixed (float* f = frame)
            {
                for (int i = 0; i < paced && s_voice.ReadFrame(VoiceBridge.EchoTalker, f) != 0; i++)
                {
                    heard.AddRange(frame);
                }
            }

            s_voice.GetState(&status);
            if (status.EchoPhase is (int)VoiceEchoPhase.Done or (int)VoiceEchoPhase.NothingCaptured)
            {
                break;
            }

            Thread.Sleep(10);
        }

        VoiceTalkerInfo row = default;
        int talkers = s_voice.GetTalkers(&row, 1);
        s_voice.StopEchoTest();
        double rms = Rms(heard, heard.Count / 4);
        double hz = ZeroCrossingHz(heard, heard.Count / 4);
        Console.WriteLine($"echo test ({mode}): phase {(VoiceEchoPhase)status.EchoPhase}, sent {status.FramesSent} frames, came back {heard.Count / VoiceK.FrameSamples} frames, " +
            $"rms={rms:F3} tone={hz:F0} Hz, talkers {talkers} ('{Name(ref row)}', decode errors {row.DecodeErrors})");
        bool done = recordSeconds == 0 || status.EchoPhase == (int)VoiceEchoPhase.Done;
        return done && heard.Count >= (recordSeconds > 0 ? recordSeconds * 35 : 80) * VoiceK.FrameSamples && rms is > 0.15 and < 0.3 &&
            Math.Abs(hz - ToneHz) < 15 && row.DecodeErrors == 0;
    }

    /// <summary>
    /// The Voice page's level meter: with no visit and no echo test, the tone's level after the gain comes back in the
    /// state (0.21 at gain 1, 0.42 at gain 2, 0 at gain 0), and nothing once the meter is off.
    /// </summary>
    private static bool Meter()
    {
        VoiceSettingsBlock saved = default;
        s_voice.GetSettings(&saved);
        VoiceSettingsBlock settings = saved;
        var scratch = new float[VoiceK.FrameSamples];
        float LevelAt(float gain, bool meter)
        {
            VoiceSettingsBlock s = settings;
            s.InputGain = gain;
            s_voice.ApplySettings(&s);
            s_voice.SetMeter(meter ? 1 : 0);
            double from = Now;
            double fed = 0;
            VoiceStatus status = default;
            while (Now - from < 0.5)
            {
                fed = FeedTone(from, fed, scratch);
                s_voice.Update();
                Thread.Sleep(10);
            }

            s_voice.GetState(&status);
            return status.InputLevel;
        }

        float one = LevelAt(1f, true);
        float two = LevelAt(2f, true);
        float zero = LevelAt(0f, true);
        float off = LevelAt(1f, false);
        s_voice.ApplySettings(&saved);
        Console.WriteLine($"meter: level {one:F3} at gain 1, {two:F3} at gain 2, {zero:F3} at gain 0, {off:F3} with the meter off");
        return one is > 0.18f and < 0.24f && two is > 0.38f and < 0.46f && zero < 0.001f && off == 0f;
    }

    /// <summary>Feeds the tone that is due by the clock since <paramref name="from"/> into SubmitCapture.</summary>
    private static double FeedTone(double from, double fed, float[] scratch)
    {
        int due = (int)((Now - from) * VoiceK.SampleRate - fed);
        while (due > 0)
        {
            int n = Math.Min(due, scratch.Length);
            for (int i = 0; i < n; i++)
            {
                scratch[i] = ToneAmplitude * (float)Math.Sin(s_tonePhase);
                s_tonePhase += 2 * Math.PI * ToneHz / VoiceK.SampleRate;
            }

            fixed (float* s = scratch)
            {
                s_voice.SubmitCapture(s, n);
            }

            fed += n;
            due -= n;
        }

        return fed;
    }

    // ---- 3. the lobby of a real server --------------------------------------------------------------------------

    private static bool Lobby(int port)
    {
        string address = "127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture);
        OnlineBridge.Instance.SetIdentity("UnrealVoice", null, null);
        OnlineBridge.Instance.Join(address, password: null, preferredTeam: 0, loopbackOnly: true);

        // The other player: a plain OpenSCCT visit with its own voice, ticked here at 60 Hz.
        var otherClock = new ProbeClock();
        var other = new OnlineClient(JoinTarget.FromAddress(address), new OnlineClientSettings
        {
            PlayerName = "GodotVoice",
            InstallId = "00000000000000aa",
            Key = PlayerKey.Create(),
            PreferredTeam = 1,
            Maps = OnlineBridge.Instance.Catalog.Maps,
            Classes = OnlineBridge.Instance.Classes,
            LoopbackOnly = true,
        }, otherClock.NowMicroseconds);
        var otherVoice = new OnlineVoice(other, new VoiceClient(new VoiceSettings(), otherClock));
        var otherHeard = new Dictionary<ushort, List<float>>();
        long otherTicks = 0;
        long otherStart = otherClock.NowMicroseconds;
        var frame = new float[VoiceK.FrameSamples];
        var tone = new float[VoiceK.FrameSamples];
        double otherFed = 0;
        bool otherTalks = false;
        double otherFrom = 0;

        // Playback is paced as an audio output paces it: 50 frames a second (Godot's clock pacing in VoiceAudio).
        // Reading faster than the talker sends would hand out concealment frames between the real ones.
        var heardByBridge = new List<float>();
        ushort bridgeListensTo = 0;
        double owed = 0;
        double lastPaced = Now;

        void Step()
        {
            s_online.Update();
            long due = (otherClock.NowMicroseconds - otherStart) * 60 / 1_000_000;
            while (otherTicks < due)
            {
                otherTicks++;
                other.Tick(otherStart + otherTicks * 1_000_000 / 60);
            }

            if (otherTalks)
            {
                int n = (int)((Now - otherFrom) * VoiceK.SampleRate - otherFed);
                while (n > 0)
                {
                    int take = Math.Min(n, tone.Length);
                    for (int i = 0; i < take; i++)
                    {
                        tone[i] = ToneAmplitude * (float)Math.Sin(s_tonePhase);
                        s_tonePhase += 2 * Math.PI * ToneHz / VoiceK.SampleRate;
                    }

                    otherVoice.Voice.SubmitCapture(tone.AsSpan(0, take));
                    otherFed += take;
                    n -= take;
                }
            }

            otherVoice.Update();
            owed += (Now - lastPaced) * VoiceK.FramesPerSecond;
            lastPaced = Now;
            int paced = (int)Math.Min(owed, VoiceK.MaxJitterFrames);
            owed -= paced;
            if (bridgeListensTo != 0)
            {
                fixed (float* f = frame)
                {
                    for (int i = 0; i < paced && s_voice.ReadFrame(bridgeListensTo, f) != 0; i++)
                    {
                        heardByBridge.AddRange(frame);
                    }
                }
            }

            foreach (VoiceTalker t in otherVoice.Voice.Talkers)
            {
                for (int i = 0; i < paced && t.ReadFrame(frame); i++)
                {
                    if (!otherHeard.TryGetValue(t.Id, out List<float>? list))
                    {
                        otherHeard[t.Id] = list = new List<float>();
                    }

                    list.AddRange(frame);
                }
            }

            Thread.Sleep(8);
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

        try
        {
            VoiceStatus status = default;
            bool inLobby = Wait(() =>
            {
                status = State();
                return OnlineBridge.Instance.State == OnlineState.Lobby && other.State == OnlineClientState.Lobby &&
                    status.Connected != 0 && status.LocalTalker != 0 && otherVoice.IsConnected && otherVoice.Voice.LocalTalker != 0 &&
                    otherVoice.TalkerOf("UnrealVoice") is not null;
            }, 30);
            Console.WriteLine($"t={Now:F1} lobby: bridge {OnlineBridge.Instance.State} voice connected {status.Connected} talker {status.LocalTalker}; " +
                $"other {other.State} voice connected {otherVoice.IsConnected} talker {otherVoice.Voice.LocalTalker}; error '{OnlineBridge.Instance.Error}'");
            if (!inLobby)
            {
                return false;
            }

            // The bridge's player talks: the tone goes into the exported SubmitCapture, the other player decodes it.
            ushort unrealTalker = otherVoice.TalkerOf("UnrealVoice")!.Value;
            double from = Now;
            double fed = 0;
            var scratch = new float[VoiceK.FrameSamples];
            Wait(() =>
            {
                fed = FeedTone(from, fed, scratch);
                return otherHeard.TryGetValue(unrealTalker, out List<float>? h) && h.Count > 150 * VoiceK.FrameSamples;
            }, 15);
            status = State();
            List<float> heardByOther = otherHeard.GetValueOrDefault(unrealTalker) ?? new List<float>();
            VoiceTalker? otherRow = otherVoice.Voice.FindTalker(unrealTalker);
            double rmsA = Rms(heardByOther, heardByOther.Count / 4);
            double hzA = ZeroCrossingHz(heardByOther, heardByOther.Count / 4);
            long errorsA = otherRow?.DecodeErrors ?? -1;
            Console.WriteLine($"t={Now:F1} bridge talks: transmitting {status.Transmitting}, frames sent {status.FramesSent}, input level {status.InputLevel:F3}");
            Console.WriteLine($"voice heard talker={unrealTalker} name={otherVoice.NameOf(unrealTalker)} rms={rmsA:F3} tone={hzA:F0}Hz " +
                $"frames={heardByOther.Count / VoiceK.FrameSamples} decoded={otherRow?.FramesDecoded} decode errors={errorsA} (by the other player)");
            bool okA = heardByOther.Count > 100 * VoiceK.FrameSamples && rmsA is > 0.15 and < 0.3 && Math.Abs(hzA - ToneHz) < 15 && errorsA == 0;

            // Silence from the bridge, the other player talks: the bridge's player hears him through the voice table.
            from = Now;
            Wait(() => false, 1.0);
            otherTalks = true;
            otherFrom = Now;
            ushort otherTalker = otherVoice.Voice.LocalTalker;
            var rows = new VoiceTalkerInfo[8];
            heardByBridge.Clear();
            bridgeListensTo = otherTalker;
            Wait(() => heardByBridge.Count > 150 * VoiceK.FrameSamples, 15);
            int count;
            fixed (VoiceTalkerInfo* r = rows)
            {
                count = s_voice.GetTalkers(r, rows.Length);
            }

            status = State();
            int index = Array.FindIndex(rows, 0, Math.Min(count, rows.Length), r => r.Talker == otherTalker);
            VoiceTalkerInfo row = index >= 0 ? rows[index] : default;
            double rmsB = Rms(heardByBridge, heardByBridge.Count / 4);
            double hzB = ZeroCrossingHz(heardByBridge, heardByBridge.Count / 4);
            Console.WriteLine($"t={Now:F1} other talks: frames sent {otherVoice.Voice.Stats.FramesSent}; bridge packets received {status.PacketsReceived}, talkers {count}");
            Console.WriteLine($"voice heard talker={otherTalker} name={Name(ref row)} team={row.Team} speaking={row.Speaking} rms={rmsB:F3} tone={hzB:F0}Hz " +
                $"frames={heardByBridge.Count / VoiceK.FrameSamples} decoded={row.FramesDecoded} decode errors={row.DecodeErrors} (by the bridge, through ReadFrame)");
            bool okB = heardByBridge.Count > 100 * VoiceK.FrameSamples && rmsB is > 0.15 and < 0.3 && Math.Abs(hzB - ToneHz) < 15 &&
                index >= 0 && row.DecodeErrors == 0 && Name(ref row) == "GodotVoice" && row.Speaking != 0;

            // The bridge's player turns him down to 40% and then mutes him, by talker id as Unreal's overlay will.
            s_voice.SetVolume(otherTalker, 0.4f);
            heardByBridge.Clear();
            Wait(() => heardByBridge.Count > 60 * VoiceK.FrameSamples, 10);
            double quiet = Rms(heardByBridge, heardByBridge.Count / 4);
            s_voice.SetMuted(otherTalker, 1);
            Wait(() => false, 1.0);
            long before = State().PacketsReceived;
            heardByBridge.Clear();
            Wait(() => false, 2.0);
            status = State();
            Console.WriteLine($"t={Now:F1} volume 40%: rms={quiet:F3} ({quiet / Math.Max(rmsB, 1e-9):F2} of full); muted: {heardByBridge.Count / VoiceK.FrameSamples} frames in 2 s, " +
                $"{status.PacketsReceived - before} packets (the server stops sending)");
            bool okC = quiet / rmsB is > 0.36 and < 0.44 && heardByBridge.Count == 0 && status.PacketsReceived - before < 5;
            s_voice.SetMuted(otherTalker, 0);
            s_voice.SetVolume(otherTalker, 1f);

            // "Restrict Mic to Team" through the voice table: the server takes it and every client sees it.
            int knownBefore = s_voice.GetTeamVoiceOnly();
            int sent = s_voice.SetTeamVoiceOnly(1);
            bool restricted = Wait(() => s_voice.GetTeamVoiceOnly() == 1 &&
                other.Lobby.Players.Any(p => p.Name == "UnrealVoice" && p.TeamVoiceOnly), 10);
            s_voice.SetTeamVoiceOnly(0);
            bool unrestricted = Wait(() => s_voice.GetTeamVoiceOnly() == 0 &&
                other.Lobby.Players.Any(p => p.Name == "UnrealVoice" && !p.TeamVoiceOnly), 10);
            Console.WriteLine($"t={Now:F1} restrict mic to team: before {knownBefore}, sent {sent}, restricted {restricted}, unrestricted {unrestricted}");
            bool okD = knownBefore == 0 && sent == 1 && restricted && unrestricted;

            // By name, as the overlay and the lobby do: volume 0 is still received but heard as silence, and saved.
            VoicePlayerInfo PlayerVoice(string name)
            {
                VoicePlayerInfo info = default;
                fixed (char* n = name)
                {
                    s_voice.GetPlayerVoice(n, &info);
                }

                return info;
            }

            void ByName(string name, float? volume, bool? muted)
            {
                fixed (char* n = name)
                {
                    if (volume is { } v)
                    {
                        s_voice.SetPlayerVolume(n, v);
                    }

                    if (muted is { } m)
                    {
                        s_voice.SetPlayerMuted(n, m ? 1 : 0);
                    }
                }
            }

            Wait(() => false, 1.0);
            ByName("GodotVoice", 0f, null);
            before = State().PacketsReceived;
            heardByBridge.Clear();
            Wait(() => heardByBridge.Count > 60 * VoiceK.FrameSamples, 10);
            double silent = Rms(heardByBridge, 0);
            long packetsAtZero = State().PacketsReceived - before;
            VoicePlayerInfo other0 = PlayerVoice("GodotVoice");
            VoicePlayerInfo self = PlayerVoice("UnrealVoice");
            string savedAtZero = File.Exists(s_volumeFile) ? File.ReadAllText(s_volumeFile) : "(no file)";
            VoiceContext context = default;
            s_voice.GetContext(&context);
            Console.WriteLine($"t={Now:F1} by name at volume 0: {heardByBridge.Count / VoiceK.FrameSamples} frames rms={silent:F4}, {packetsAtZero} packets; " +
                $"row visit={other0.Visit} talker={other0.Talker} speaking={other0.Speaking} volume={other0.Volume:F2} muted={other0.Muted}; " +
                $"own row local={self.Local} talker={self.Talker}; context visit={context.Visit} round={context.InRound} team={context.LocalTeam}; saved {savedAtZero}");
            bool okE = heardByBridge.Count > 40 * VoiceK.FrameSamples && silent < 1e-6 && packetsAtZero > 40 && other0.Visit == 1 &&
                other0.Talker == otherTalker && other0.Speaking != 0 && other0.Volume == 0f && self.Local == 1 &&
                context.Visit == 1 && context.InRound == 0 && context.LocalTeam == 0 && savedAtZero.Contains("\"GodotVoice\":0", StringComparison.Ordinal);

            ByName("GodotVoice", null, true);
            Wait(() => false, 1.0);
            before = State().PacketsReceived;
            Wait(() => false, 2.0);
            long packetsMuted = State().PacketsReceived - before;
            bool mutedRow = PlayerVoice("GodotVoice").Muted == 1;
            ByName("GodotVoice", 1f, false);
            Wait(() => false, 1.0);
            string savedAtOne = File.ReadAllText(s_volumeFile);
            int banSent = s_voice.SetVoiceBanned(other.Lobby.LocalId, 1);
            Console.WriteLine($"t={Now:F1} by name muted: {packetsMuted} packets in 2 s, row muted {mutedRow}; back to 100%: saved {savedAtOne}; voice ban sent {banSent} (the server checks the sender)");
            bool okF = packetsMuted < 5 && mutedRow && !savedAtOne.Contains("GodotVoice", StringComparison.Ordinal) && banSent == 1;

            Console.WriteLine($"bridge talks {(okA ? "ok" : "FAILED")}, bridge hears {(okB ? "ok" : "FAILED")}, volume and mute {(okC ? "ok" : "FAILED")}, team only {(okD ? "ok" : "FAILED")}, " +
                $"by name {(okE ? "ok" : "FAILED")}, by name muted {(okF ? "ok" : "FAILED")}");
            return okA && okB && okC && okD && okE && okF;
        }
        finally
        {
            otherVoice.Dispose();
            other.Leave();
            other.Dispose();
        }
    }

    // A dedicated server as an operator runs it: a config file, the map folder, and commands on standard input.
    private static Process StartServer(string openScct, string content, string work, int port)
    {
        string config = Path.Combine(work, "svm-server.json");
        File.WriteAllText(config, $$"""
            {
              "name": "VoiceProbe server",
              "port": {{port}},
              "lanPort": 0,
              "loopbackOnly": true,
              "maxPlayers": 4,
              "maps": {{System.Text.Json.JsonSerializer.Serialize(Path.Combine(content, "svmap"))}},
              "rotation": [ { "map": "AquaE", "mode": "Story" } ],
              "minPlayersToLaunch": 4,
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

        return server;
    }

    // ---- helpers ------------------------------------------------------------------------------------------------

    private static VoiceStatus State()
    {
        VoiceStatus status;
        s_voice.GetState(&status);
        return status;
    }

    private static string Name(ref VoiceTalkerInfo row)
    {
        fixed (char* name = row.Name)
        {
            return new string(name);
        }
    }

    private static string Error()
    {
        int length = s_voice.GetError(null, 0);
        if (length == 0)
        {
            return string.Empty;
        }

        var buffer = new char[length + 1];
        fixed (char* b = buffer)
        {
            s_voice.GetError(b, buffer.Length);
        }

        return new string(buffer, 0, length);
    }

    private static double Rms(List<float> samples, int skip)
    {
        double sum = 0;
        int n = 0;
        for (int i = skip; i < samples.Count; i++)
        {
            sum += samples[i] * (double)samples[i];
            n++;
        }

        return n == 0 ? 0 : Math.Sqrt(sum / n);
    }

    private static double ZeroCrossingHz(List<float> samples, int skip)
    {
        int crossings = 0;
        for (int i = skip + 1; i < samples.Count; i++)
        {
            if ((samples[i - 1] < 0f) != (samples[i] < 0f))
            {
                crossings++;
            }
        }

        double seconds = (samples.Count - skip) / (double)VoiceK.SampleRate;
        return seconds <= 0 ? 0 : crossings / 2.0 / seconds;
    }

    private sealed class ProbeClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}

/// <summary>
/// <c>VoiceProbe listen</c>: a second player who talks a tone and listens, for a check with the Unreal client on the
/// same server (see the top of this file).
/// </summary>
internal static class Listener
{
    public static int Run(string[] args)
    {
        string address = args.Length > 1 ? args[1] : "127.0.0.1:28961";
        string content = args.Length > 2 ? args[2] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
        double seconds = args.Length > 3 ? double.Parse(args[3], CultureInfo.InvariantCulture) : 60;
        string expect = args.Length > 4 ? args[4] : "UnrealCheck";
        double toneHz = args.Length > 5 ? double.Parse(args[5], CultureInfo.InvariantCulture) : 660;
        const float amplitude = 0.3f;

        BridgeSession.Configure(content);
        var clock = new ListenerClock();
        var client = new OnlineClient(JoinTarget.FromAddress(address), new OnlineClientSettings
        {
            PlayerName = "VoiceListener",
            InstallId = "00000000000000bb",
            Key = PlayerKey.Create(),
            PreferredTeam = 0,
            Maps = OnlineBridge.Instance.Catalog.Maps,
            Classes = OnlineBridge.Instance.Classes,
            LoopbackOnly = true,
        }, clock.NowMicroseconds);
        // The clean-up off, as for any synthetic microphone; voice activation, so the tone is always sent.
        var settings = new VoiceSettings { NoiseSuppression = false, AutoGain = false, EchoCancellation = false, PushToTalk = false };
        var voice = new OnlineVoice(client, new VoiceClient(settings, clock));
        var window = new Dictionary<ushort, List<float>>();
        var total = new Dictionary<ushort, List<float>>();
        var frame = new float[VoiceK.FrameSamples];
        var tone = new float[VoiceK.FrameSamples];
        var run = Stopwatch.StartNew();
        long start = clock.NowMicroseconds;
        long ticks = 0;
        double fed = 0;
        double phase = 0;
        double owed = 0;
        double lastPaced = 0;
        double nextReport = 2;
        bool saidReady = false;
        double? heardEnough = null;
        OnlineClientState lastState = (OnlineClientState)(-1);
        string lastMap = string.Empty;
        try
        {
            while (run.Elapsed.TotalSeconds < seconds)
            {
                double now = run.Elapsed.TotalSeconds;
                long due = (clock.NowMicroseconds - start) * 60 / 1_000_000;
                while (ticks < due)
                {
                    ticks++;
                    client.Tick(start + ticks * 1_000_000 / 60);
                }

                if (client.State != lastState || (client.CurrentMap ?? string.Empty) != lastMap)
                {
                    lastState = client.State;
                    lastMap = client.CurrentMap ?? string.Empty;
                    Console.WriteLine($"t={now:F1} listener: {client.State} map '{lastMap}'{(client.Error is { } e ? " error: " + e : string.Empty)}");
                }

                // Ready once the talker to check is in the lobby too, so the round starts with both in it (a player
                // who joins a round already under way watches, and watchers hear no one).
                if (client.State == OnlineClientState.Lobby && !saidReady &&
                    client.Lobby.Players.Any(p => p.Name.StartsWith(expect, StringComparison.Ordinal)))
                {
                    client.SetReady(true);
                    saidReady = true;
                    Console.WriteLine($"t={now:F1} listener: {expect} is in the lobby; saying ready");
                }

                // Done once the talker was heard for ten seconds and has heard us as long.
                if (heardEnough is null && total.Any(kv => voice.NameOf(kv.Key)?.StartsWith(expect, StringComparison.Ordinal) == true && kv.Value.Count >= 500 * VoiceK.FrameSamples))
                {
                    heardEnough = now;
                }

                if (heardEnough is { } since && now - since > 10)
                {
                    break;
                }

                if (client.State is OnlineClientState.Failed or OnlineClientState.Left)
                {
                    break;
                }

                // The tone the clock says a microphone would have delivered by now.
                int n = (int)(now * VoiceK.SampleRate - fed);
                while (n > 0)
                {
                    int take = Math.Min(n, tone.Length);
                    for (int i = 0; i < take; i++)
                    {
                        tone[i] = amplitude * (float)Math.Sin(phase);
                        phase += 2 * Math.PI * toneHz / VoiceK.SampleRate;
                    }

                    voice.Voice.SubmitCapture(tone.AsSpan(0, take));
                    fed += take;
                    n -= take;
                }

                voice.Update();

                // Read as an audio output would: 50 frames a second per talker.
                owed += (now - lastPaced) * VoiceK.FramesPerSecond;
                lastPaced = now;
                int paced = (int)Math.Min(owed, VoiceK.MaxJitterFrames);
                owed -= paced;
                foreach (VoiceTalker t in voice.Voice.Talkers)
                {
                    for (int i = 0; i < paced && t.ReadFrame(frame); i++)
                    {
                        (window.TryGetValue(t.Id, out List<float>? w) ? w : window[t.Id] = new List<float>()).AddRange(frame);
                        (total.TryGetValue(t.Id, out List<float>? a) ? a : total[t.Id] = new List<float>()).AddRange(frame);
                    }
                }

                if (now >= nextReport)
                {
                    nextReport += 2;
                    Console.WriteLine($"t={now:F1} listener: voice connected {voice.IsConnected} talker {voice.Voice.LocalTalker}, sent {voice.Voice.Stats.FramesSent} frames, talkers {voice.Voice.Talkers.Count}");
                    foreach ((ushort id, List<float> heard) in window)
                    {
                        Console.WriteLine($"   heard talker={id} name={voice.NameOf(id)} rms={Rms(heard):F3} tone={Hz(heard):F0}Hz frames={heard.Count / VoiceK.FrameSamples}");
                    }

                    window.Clear();
                }

                Thread.Sleep(5);
            }
        }
        finally
        {
            voice.Dispose();
            client.Leave();
            client.Dispose();
        }

        bool ok = false;
        foreach ((ushort id, List<float> heard) in total)
        {
            string? name = voice.NameOf(id);
            // The last half of what was heard: the tone's start and the jitter buffer filling are left out.
            List<float> tail = heard.GetRange(heard.Count / 2, heard.Count - heard.Count / 2);
            double rms = Rms(tail);
            Console.WriteLine($"voice heard talker={id} name={name} rms={rms:F3} tone={Hz(tail):F0}Hz frames={heard.Count / VoiceK.FrameSamples} (by the listener)");
            ok |= name is not null && name.StartsWith(expect, StringComparison.Ordinal) && rms > 0.1 && heard.Count > 50 * VoiceK.FrameSamples;
        }

        Console.WriteLine(ok ? $"LISTEN OK: heard {expect}" : $"LISTEN FAILED: {expect} not heard");
        return ok ? 0 : 1;
    }

    private static double Rms(List<float> samples)
    {
        double sum = 0;
        foreach (float s in samples)
        {
            sum += s * (double)s;
        }

        return samples.Count == 0 ? 0 : Math.Sqrt(sum / samples.Count);
    }

    private static double Hz(List<float> samples)
    {
        int crossings = 0;
        for (int i = 1; i < samples.Count; i++)
        {
            if ((samples[i - 1] < 0f) != (samples[i] < 0f))
            {
                crossings++;
            }
        }

        return samples.Count == 0 ? 0 : crossings / 2.0 / (samples.Count / (double)VoiceK.SampleRate);
    }

    private sealed class ListenerClock : IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
