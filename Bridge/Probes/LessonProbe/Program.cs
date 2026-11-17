using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Test;

// LessonProbe <content root>
// Goes through Exports.GetApiSized and LessonExports.GetApi as Unreal does (unmanaged function pointers, raw buffers).
// Vision-cancelling: a baseline drop, back to the ledge, a drop switching to night vision at touchdown while moving;
// both must be measured with a recovery time. Spy-grab: one mercenary must stand in the lane and a grab from behind
// must count. Exit code 1 on a failure.
string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();

// The sizes ScctLessonApi.h asserts.
Console.WriteLine($"sizes: BridgeLessonState {Unsafe.SizeOf<BridgeLessonState>()}, BridgeLessonAttempt {Unsafe.SizeOf<BridgeLessonAttempt>()}, LessonApi {Unsafe.SizeOf<LessonApi>()}");
return Probe.Run(content);

static unsafe class Probe
{
    private static BridgeApi s_bridge;
    private static LessonApi s_api;

    public static int Run(string content)
    {
        BridgeApi bridgeTable;
        LessonApi lessonTable;
        ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApiSized)(&bridgeTable, sizeof(BridgeApi));
        int version = ((delegate* unmanaged<LessonApi*, int>)&LessonExports.GetApi)(&lessonTable);
        s_bridge = bridgeTable;
        s_api = lessonTable;
        if (version != LessonExports.ApiVersion || s_api.Version != version)
        {
            Console.WriteLine("FAILED: the lesson table did not fill");
            return 1;
        }

        BridgeLessonState idle;
        if (s_api.GetState(&idle) != 0 || Call(s_api.StartLesson, "spy-grab") != 0)
        {
            Console.WriteLine("FAILED: a lesson without a session");
            return 1;
        }

        Console.WriteLine("no session: StartLesson refused: " + LessonError());
        fixed (char* c = content)
        {
            if (s_bridge.Configure(c) == 0)
            {
                Console.WriteLine("FAILED: " + BridgeError());
                return 1;
            }
        }

        int result = Vision();
        if (result == 0)
        {
            result = Grab();
        }

        s_bridge.Stop();
        Console.WriteLine(result == 0 ? "OK: both drills measured" : "FAILED");
        return result;
    }

    private static int Vision()
    {
        if (!StartDrill("vision-cancelling", 0))
        {
            return 1;
        }

        BridgeLessonState state;
        // A baseline drop: walk off the tower without switching.
        if (!WaitFor(&state, s => s.Ready != 0, 15, "the lesson to place the spy"))
        {
            return 1;
        }

        Report("placed", state);
        Call(s_api.Action, "forward");
        if (!WaitFor(&state, s => s.HistoryCount >= 1, 15, "the baseline drop to be measured"))
        {
            return 1;
        }

        Report("baseline", state);
        if (float.IsNaN(state.BaselineMs) || float.IsNaN(state.RecoveryMs))
        {
            Console.WriteLine("FAILED: the baseline drop has no recovery time");
            return 1;
        }

        // Back to the ledge, and a drop switching vision at touchdown while holding forward.
        Call(s_api.Action, "reset");
        if (!WaitFor(&state, s => s.Returns >= 1 && s.Phase == LessonPhase.Ready, 5, "the return to the ledge"))
        {
            return 1;
        }

        Report("returned", state);
        Call(s_api.Action, "forward");
        bool switched = false;
        var watch = Stopwatch.StartNew();
        while (watch.Elapsed.TotalSeconds < 15)
        {
            Tick(&state);
            if (!switched && state.Phase == LessonPhase.Landing)
            {
                switched = true;
                Call(s_api.Action, "night");
                Call(s_api.Action, "forward");
            }

            if (state.HistoryCount >= 2)
            {
                break;
            }
        }

        Report("switched", state);
        BridgeLessonAttempt* attempts = stackalloc BridgeLessonAttempt[8];
        int count = s_api.GetAttempts(attempts, 8);
        for (int i = 0; i < count; i++)
        {
            BridgeLessonAttempt a = attempts[i];
            Console.WriteLine(FormattableString.Invariant(
                $"   #{a.Number} {new string(a.Result)}: recovery {a.RecoveryMs:F0} ms, switch {a.SwitchMs:F0} ms, vision {a.VisionMs:F0} ms, saved {a.SavedMs:F0} ms, moving {a.Moving}"));
        }

        if (count < 2 || !switched || float.IsNaN(attempts[1].SwitchMs))
        {
            Console.WriteLine("FAILED: the switching drop was not measured with its switch timing");
            return 1;
        }

        return 0;
    }

    private static int Grab()
    {
        if (!StartDrill("spy-grab", 1))
        {
            return 1;
        }

        BridgeLessonState state;
        if (!WaitFor(&state, s => s.Ready != 0 && !float.IsNaN(s.Distance), 15, "the lesson to place the spy and the mercenary"))
        {
            return 1;
        }

        Report("placed", state);
        Call(s_api.Action, "forward");
        if (!WaitFor(&state, s => s.Distance < 80, 10, "the spy to walk up behind the mercenary"))
        {
            return 1;
        }

        Call(s_api.Action, "stop");
        Report("behind", state);
        var watch = Stopwatch.StartNew();
        while (watch.Elapsed.TotalSeconds < 6 && state.Grabs == 0)
        {
            Call(s_api.Action, "grab");
            for (int i = 0; i < 30 && state.Grabs == 0; i++)
            {
                Tick(&state);
            }

            Console.WriteLine(FormattableString.Invariant($"   grab try at {watch.Elapsed.TotalSeconds:F2} s: state {new string(state.State)}, grabs {state.Grabs}"));
        }

        Report("grab", state);
        if (state.Grabs < 1)
        {
            Console.WriteLine("FAILED: a grab from behind did not count");
            return 1;
        }

        Call(s_api.Action, "moving");
        Tick(&state);
        Report("moving", state);
        return state.Moving == 1 ? 0 : 1;
    }

    private static bool StartDrill(string kind, int bots)
    {
        if (Call(s_api.Arm, kind) == 0)
        {
            Console.WriteLine("FAILED: Arm: " + LessonError());
            return false;
        }

        BridgeStartOptions options = default;
        options.Size = sizeof(BridgeStartOptions);
        options.Bots = 3; // the drill decides: the probe asks for more to see it does
        const string map = "TestRange";
        for (int i = 0; i < map.Length; i++)
        {
            options.Map[i] = map[i];
        }

        if (s_bridge.StartLocalEx(&options) == 0)
        {
            Console.WriteLine("FAILED: start: " + BridgeError());
            return false;
        }

        if (Call(s_api.StartLesson, kind) == 0)
        {
            Console.WriteLine("FAILED: StartLesson: " + LessonError());
            return false;
        }

        Console.WriteLine($"{kind}: started (asked for 3 bots, the drill wants {bots})");
        return true;
    }

    private static void Tick(BridgeLessonState* state)
    {
        s_bridge.Update();
        s_api.GetState(state);
        Thread.Sleep(4);
    }

    private static bool WaitFor(BridgeLessonState* state, Func<BridgeLessonState, bool> done, double seconds, string what)
    {
        var watch = Stopwatch.StartNew();
        while (watch.Elapsed.TotalSeconds < seconds)
        {
            Tick(state);
            if (s_bridge.GetState() != (int)BridgeState.Playing)
            {
                Console.WriteLine("FAILED: " + BridgeError());
                return false;
            }

            if (done(*state))
            {
                return true;
            }
        }

        Report("timeout", *state);
        Console.WriteLine($"FAILED: waited {seconds:F0} s for {what}");
        return false;
    }

    private static void Report(string label, BridgeLessonState s) => Console.WriteLine(FormattableString.Invariant(
        $"   {label}: phase {s.Phase}, state {new string(s.State)}, attempts {s.Attempts}, grabs {s.Grabs}, returns {s.Returns}, recovery {s.RecoveryMs:F0}, baseline {s.BaselineMs:F0}, best {s.BestMs:F0}, distance {s.Distance:F0}, vision {s.Vision}, moving {s.Moving}\n      \"{new string(s.Feedback)}\""));

    private static int Call(delegate* unmanaged[Cdecl]<char*, int> entry, string text)
    {
        fixed (char* c = text)
        {
            return entry(c);
        }
    }

    private static string LessonError()
    {
        char* text = stackalloc char[1024];
        s_api.LastError(text, 1024);
        return new string(text);
    }

    private static string BridgeError()
    {
        char* text = stackalloc char[1024];
        int length = s_bridge.LastError(text, 1024);
        return new string(text, 0, Math.Min(length, 1023));
    }
}
