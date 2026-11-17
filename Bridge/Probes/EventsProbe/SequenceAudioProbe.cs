using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Events;

/// <summary>
/// EventsProbe ... sequence: lists the audio segments of the animation sequences (SequenceAudioExports) once a second.
/// No converted map has an animation sequence with an audio track yet (they come from the OpenSCCT editor's animation
/// projects), so unless SVM_ANIMATION_PACK names one this uses the repo's check pack (Tools/Test/SequenceAudioPack:
/// a 40 s sequence on the slot ScctSequenceAudioCheck with two audio segments on AquaE's placed actors) and fires
/// that slot on the server one second in. Every listing checks that the segment's offset follows the controller's
/// replicated clock: offset = file offset + (clock - segment start) x rate, within the 6 ticks a published clock may lag.
/// </summary>
internal static class SequenceAudioProbe
{
    private const string CheckTag = "ScctSequenceAudioCheck";

    public static int Run(string content, string map, double seconds)
    {
        if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("SVM_ANIMATION_PACK")))
        {
            string? pack = FindCheckPack();
            if (pack is null)
            {
                Console.WriteLine("FAILED: no SVM_ANIMATION_PACK and Tools/Test/SequenceAudioPack not found above " + AppContext.BaseDirectory);
                return 1;
            }

            Environment.SetEnvironmentVariable("SVM_ANIMATION_PACK", pack);
        }

        Console.WriteLine("animation pack: " + Environment.GetEnvironmentVariable("SVM_ANIMATION_PACK"));
        unsafe
        {
            Console.WriteLine($"record size: BridgeSequenceSound {sizeof(BridgeSequenceSound)}");
            if (sizeof(BridgeSequenceSound) != 68)
            {
                Console.WriteLine("FAILED: BridgeSequenceSound differs from Runtime/ScctSequenceAudioApi.h (68 bytes)");
                return 1;
            }
        }

        BridgeSession.Configure(content);
        BridgeSession session = BridgeSession.StartLocal(map, "Story", true, bots: 0);
        var buffer = new BridgeSequenceSound[64];
        var firstSeen = new Dictionary<int, double>();
        var keysSeen = new HashSet<int>();
        int listings = 0, mismatches = 0, triggered = 0;
        double worstError = 0;
        var run = Stopwatch.StartNew();
        double nextList = 1;
        bool fired = false;
        while (run.Elapsed.TotalSeconds < seconds)
        {
            session.Update();
            if (session.Failure is { } failure)
            {
                Console.WriteLine("FAILED: " + failure);
                return 1;
            }

            double t = run.Elapsed.TotalSeconds;
            if (!fired && t >= 1)
            {
                fired = true;
                triggered = SequenceAudioExports.TriggerForCheck(session, CheckTag);
                Console.WriteLine($"{t,5:F1} s: fired {CheckTag} on the server: {(triggered == 1 ? "queued" : "no local server")}");
            }

            int n = SequenceAudioExports.GetSounds(session, buffer);
            for (int i = 0; i < n; i++)
            {
                keysSeen.Add(buffer[i].Key);
                firstSeen.TryAdd(buffer[i].Key, t);
            }

            if (t >= nextList)
            {
                nextList += 1;
                listings++;
                Console.WriteLine($"{t,5:F1} s: {n} sequence audio segment(s)");
                for (int i = 0; i < n; i++)
                {
                    BridgeSequenceSound s = buffer[i];
                    float expected = s.SegmentFileOffset + ((s.Clock - s.SegmentStart) * s.Rate);
                    float error = Math.Abs(expected - s.Offset);
                    worstError = Math.Max(worstError, error);
                    // A published clock lags by up to 6 ticks (0.2 s at 30 Hz); more is a desync.
                    mismatches += error > .25f ? 1 : 0;
                    Console.WriteLine($"   seg={EventNames.Text(s.NameId)} key {s.Key} sound '{EventNames.Text(s.SoundId)}' file {s.FileId} offset {s.Offset:F2} s rate {s.Rate:F2} clock {s.Clock:F2} (expected offset {expected:F2}) at ({s.X:F0}, {s.Y:F0}, {s.Z:F0}) actor {s.ActorId} flags {s.Flags} volume {s.Volume:F2} radius {s.Radius:F0}");
                }
            }

            Thread.Sleep(10);
        }

        Console.WriteLine($"segments seen: {keysSeen.Count}, listings {listings}, offset vs clock worst {worstError:F3} s, {mismatches} beyond 0.25 s, unknown sounds {EventsFeature.Of(session).SequenceUnknownSounds}");
        session.Dispose();
        if (triggered != 1 || keysSeen.Count == 0 || mismatches > 0)
        {
            Console.WriteLine("FAILED: " + (triggered != 1 ? "the trigger was not queued" : keysSeen.Count == 0 ? "no sequence audio segment was listed" : "an offset did not follow the clock"));
            return 1;
        }

        Console.WriteLine("OK");
        return 0;
    }

    private static string? FindCheckPack()
    {
        for (DirectoryInfo? dir = new(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
        {
            string candidate = Path.Combine(dir.FullName, "Tools", "Test", "SequenceAudioPack");
            if (File.Exists(Path.Combine(candidate, "sim", "animations.json")))
            {
                return candidate;
            }
        }

        return null;
    }
}
