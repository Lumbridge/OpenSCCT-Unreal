using System;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Test;

/// <summary>Where a drill is, as the coach panel tells it.</summary>
public static class LessonPhase
{
    /// <summary>The lesson has not placed the players yet.</summary>
    public const int Preparing = 0;

    /// <summary>On the ledge (vision-cancelling) or in the lane (spy-grab), nothing measured since.</summary>
    public const int Ready = 1;

    /// <summary>Falling from the tower.</summary>
    public const int Falling = 2;

    /// <summary>Feet on the floor, still in the landing recovery.</summary>
    public const int Landing = 3;

    /// <summary>The recovery of this drop is measured; the next one starts back on the ledge.</summary>
    public const int Result = 4;
}

/// <summary>A drill's coach state as of the last tick: what the Godot client's InteractiveLessonCoach shows.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeLessonState
{
    public const int FeedbackCapacity = 512;
    public const int StateCapacity = 64;

    /// <summary>1 while a drill runs on the current session.</summary>
    public int Active;

    /// <summary>0 vision-cancelling, 1 spy-grab.</summary>
    public int Kind;

    /// <summary>1 once the lesson has placed the players.</summary>
    public int Ready;

    /// <summary><see cref="LessonPhase"/>.</summary>
    public int Phase;

    /// <summary>1 while the spy-grab target walks up and down the lane.</summary>
    public int Moving;

    /// <summary>The lesson's debug flag (the grab area shown, in spy-grab).</summary>
    public int Debug;

    /// <summary>Drops measured (vision-cancelling) or grabs tried with the lesson's grab (spy-grab).</summary>
    public int Attempts;

    public int Grabs;

    /// <summary>Times the player was put back on the ledge.</summary>
    public int Returns;

    /// <summary>How many recent attempts <c>GetAttempts</c> has (at most six).</summary>
    public int HistoryCount;

    /// <summary>The local controller's vision mode (RendMap).</summary>
    public int Vision;

    /// <summary>Milliseconds from touchdown to the end of the landing recovery of this drop; NaN before it ends.</summary>
    public float RecoveryMs;

    /// <summary>The recovery of the first drop without a vision switch; NaN until there was one.</summary>
    public float BaselineMs;

    /// <summary>The fastest recovery with a switch while moving; NaN until there was one.</summary>
    public float BestMs;

    /// <summary>The last attempt's switch input after touchdown in ms (negative: before it); NaN when it had none.</summary>
    public float LastSwitchMs;

    /// <summary>The last attempt's time saved against the baseline in ms; NaN when there is no baseline.</summary>
    public float LastSavedMs;

    /// <summary>Spy-grab: the distance from the local spy to the mercenary in cm; NaN when there is none.</summary>
    public float Distance;

    /// <summary>The walk-in return volume at the foot of the tower (vision-cancelling): its centre and radius in cm.</summary>
    public float PortalX, PortalY, PortalZ, PortalRadius;

    /// <summary>The coach's instruction and feedback line.</summary>
    public fixed char Feedback[FeedbackCapacity];

    /// <summary>The local controller's state name.</summary>
    public fixed char State[StateCapacity];
}

/// <summary>One recent vision-cancelling attempt (<see cref="BrowserLesson.VisionAttempt"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeLessonAttempt
{
    public const int ResultCapacity = 24;

    public int Number;

    /// <summary>1 when a movement direction was held through the landing.</summary>
    public int Moving;

    public float RecoveryMs;

    /// <summary>Switch input after touchdown in ms (negative: before it); NaN for no switch.</summary>
    public float SwitchMs;

    /// <summary>When the vision mode really changed after touchdown in ms; NaN when it did not.</summary>
    public float VisionMs;

    /// <summary>Saved against the baseline in ms; NaN with no baseline.</summary>
    public float SavedMs;

    /// <summary>Baseline, Too early, Keep moving, Cancelled, Vision switched or Try earlier.</summary>
    public fixed char Result[ResultCapacity];
}

/// <summary>
/// The drill of one session (a session feature, so it ends with the match): the Godot client's
/// <see cref="BrowserLesson"/>, the same code the Tutorials menu and the wiki's browser lessons run, on the bridge's
/// play session.
/// </summary>
public sealed class LessonFeature : IDisposable
{
    private readonly BridgeSession _session;

    public LessonFeature(BridgeSession session) => _session = session;

    public BrowserLesson? Lesson { get; private set; }

    /// <summary>Starts <paramref name="kind"/> on the session, replacing a drill already running.</summary>
    public void Start(string kind)
    {
        if (!LessonLaunch.IsKind(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, "A drill is vision-cancelling or spy-grab.");
        }

        Stop();
        // BrowserLesson refuses a session that is not a local TestRange match with Play Level rules and the inline host.
        Lesson = new BrowserLesson(_session.Session, kind);
    }

    public void Stop()
    {
        Lesson?.Dispose();
        Lesson = null;
    }

    public unsafe void Read(ref BridgeLessonState state)
    {
        state = default;
        state.RecoveryMs = state.BaselineMs = state.BestMs = state.LastSwitchMs = state.LastSavedMs = state.Distance = float.NaN;
        if (Lesson is not { } lesson)
        {
            return;
        }

        state.Active = 1;
        state.Kind = lesson.Kind == "spy-grab" ? 1 : 0;
        state.Ready = lesson.Ready ? 1 : 0;
        state.Moving = lesson.Moving ? 1 : 0;
        state.Debug = lesson.Debug ? 1 : 0;
        state.Attempts = lesson.Attempts;
        state.Grabs = lesson.Grabs;
        state.Returns = lesson.Returns;
        state.HistoryCount = lesson.History.Count;
        state.Vision = lesson.Vision;
        state.RecoveryMs = Ms(lesson.RecoveryMs);
        state.BaselineMs = Ms(lesson.BaselineMs);
        state.BestMs = Ms(lesson.BestMs);
        if (lesson.History.Count > 0)
        {
            BrowserLesson.VisionAttempt last = lesson.History[^1];
            state.LastSwitchMs = Ms(last.SwitchMs);
            state.LastSavedMs = Ms(last.SavedMs);
        }

        state.Distance = lesson.Distance ?? float.NaN;
        FVector portal = BrowserLesson.ReturnPortal;
        state.PortalX = portal.X;
        state.PortalY = portal.Y;
        state.PortalZ = portal.Z;
        state.PortalRadius = BrowserLesson.PortalRadius;
        state.Phase = PhaseOf(lesson);
        fixed (char* feedback = state.Feedback)
        {
            Write(lesson.Feedback, feedback, BridgeLessonState.FeedbackCapacity);
        }

        fixed (char* name = state.State)
        {
            Write(lesson.State, name, BridgeLessonState.StateCapacity);
        }
    }

    public unsafe int ReadAttempts(Span<BridgeLessonAttempt> attempts)
    {
        if (Lesson is not { } lesson)
        {
            return 0;
        }

        int count = Math.Min(attempts.Length, lesson.History.Count);
        for (int i = 0; i < count; i++)
        {
            BrowserLesson.VisionAttempt a = lesson.History[i];
            ref BridgeLessonAttempt target = ref attempts[i];
            target = default;
            target.Number = a.Number;
            target.Moving = a.Moving ? 1 : 0;
            target.RecoveryMs = (float)a.RecoveryMs;
            target.SwitchMs = Ms(a.SwitchMs);
            target.VisionMs = Ms(a.VisionMs);
            target.SavedMs = Ms(a.SavedMs);
            fixed (char* result = target.Result)
            {
                Write(a.Result, result, BridgeLessonAttempt.ResultCapacity);
            }
        }

        return count;
    }

    /// <summary>
    /// The phase from what the lesson and the local player show; the lesson's own counters are private to it. A drop
    /// is falling below the tower top (as the lesson ignores the settling at spawn), landing while the controller is in
    /// PlayerLanding, and measured once the lesson has its recovery.
    /// </summary>
    private int PhaseOf(BrowserLesson lesson)
    {
        if (!lesson.Ready)
        {
            return LessonPhase.Preparing;
        }

        if (lesson.Kind == "spy-grab")
        {
            return LessonPhase.Ready;
        }

        if (lesson.RecoveryMs is not null)
        {
            return LessonPhase.Result;
        }

        if (_session.Session.LocalController is { } pc && pc.IsInState("PlayerLanding"))
        {
            return LessonPhase.Landing;
        }

        return _session.Session.LocalPawn is { Physics: EPhysics.PHYS_Falling } pawn && pawn.Location.Z < 640
            ? LessonPhase.Falling
            : LessonPhase.Ready;
    }

    private static float Ms(double? value) => value is { } v ? (float)v : float.NaN;

    private static unsafe void Write(string text, char* target, int capacity)
    {
        int length = Math.Min(text.Length, capacity - 1);
        text.AsSpan(0, length).CopyTo(new Span<char>(target, length));
        new Span<char>(target + length, capacity - length).Clear();
    }

    public void Dispose() => Stop();
}
