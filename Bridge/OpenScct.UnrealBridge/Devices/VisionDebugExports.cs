using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Server;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Entry points for unattended checks of the goggles only (Source/OpenSCCT/Hud/ScctVisionDebug.h mirrors the table;
/// the game's -ScctVisionBots switch calls them). They move pawns on the SERVER's world of a local match, so what the
/// client's motion tracker then sees and Unreal draws came back through replication. Nothing in normal play calls
/// them, and on a remote round (no server here) every entry returns 0 and does nothing.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct VisionDebugApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<float, int> StandSpyBots;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetReport;
}

public static unsafe class VisionDebugExports
{
    /// <summary>1: StandSpyBots and GetReport.</summary>
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(VisionDebugApi* api)
    {
        api->Version = ApiVersion;
        api->StandSpyBots = &StandSpyBots;
        api->GetReport = &GetReport;
        return ApiVersion;
    }

    /// <summary>What the last action did on the server, and how many actions have reported.</summary>
    private static volatile string s_report = "";
    private static int s_reports;

    /// <summary>
    /// Writes what the last action did on the server (NUL terminated, cut to <paramref name="capacity"/>) and returns
    /// how many actions have reported so far, so the caller sees when a new report came.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetReport(char* buffer, int capacity)
    {
        string text = s_report;
        if (buffer != null && capacity > 0)
        {
            int n = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, n).CopyTo(new Span<char>(buffer, n));
            buffer[n] = '\0';
        }

        return Volatile.Read(ref s_reports);
    }

    private static void Report(string text)
    {
        s_report = text;
        Interlocked.Increment(ref s_reports);
    }

    /// <summary>
    /// For <paramref name="holdSeconds"/>: walks the nearest spy bot back and forth across the merc player's view 500 uu
    /// in front of him, faster than the tracker's 150 uu/s (out of the physics, its place and velocity set each server
    /// step), and holds the next nearest still 700 uu in front and to the side (its speed and acceleration taken away
    /// and its place kept, so its velocity stays 0 and the tracker never takes it). Calling again starts both over.
    /// <see cref="GetReport"/> says what was done. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StandSpyBots(float holdSeconds) => OnServer(host =>
    {
        Level level = host.World.Level;
        SBasePawn? merc = null;
        var spies = new List<SBasePawn>();
        foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            // The player's controller is the one with a player (his connection or viewport); a bot's has none.
            if (pawn.SPawnType == PawnType.SPAWNDEFENSE && pawn.Controller is PlayerController { Player: not null } && merc is null)
            {
                merc = pawn;
            }
            else if (pawn.SPawnType != PawnType.SPAWNDEFENSE)
            {
                spies.Add(pawn);
            }
        }

        if (merc is null || spies.Count == 0)
        {
            Report("no merc player or no spy bot on the server");
            return;
        }

        Release();
        FRotator view = merc.Controller?.Rotation ?? merc.Rotation;
        new FRotator(0, view.Yaw, 0).GetAxes(out FVector forward, out FVector right, out _);
        spies.Sort((a, b) => FVector.DistSquared(a.Location, merc.Location).CompareTo(FVector.DistSquared(b.Location, merc.Location)));
        float until = level.Info.TimeSeconds + holdSeconds;
        string text = "";

        // The walker: strides across the merc's view and back, 500 uu in front, at 260 uu/s.
        SBasePawn walker = spies[0];
        if (Stand(walker, merc.Location, forward * 500f, right))
        {
            s_walker = new Held(walker, walker.Location, right, until, walker.GroundSpeed, walker.AccelRate, walker.Physics);
            text += $"walking spy bot #{walker.ActorId} across the view from {walker.Location}";
        }
        else
        {
            text += $"spy bot #{walker.ActorId} does not fit in front of the merc at {merc.Location}";
        }

        // The still one: further in and to the side, its speed taken away.
        if (spies.Count > 1)
        {
            SBasePawn still = spies[1];
            if (Stand(still, merc.Location, (forward * 700f) - (right * 220f), right))
            {
                s_still = new Held(still, still.Location, right, until, still.GroundSpeed, still.AccelRate, still.Physics);
                text += $"; spy bot #{still.ActorId} held still at {still.Location}";
            }
            else
            {
                text += $"; spy bot #{still.ActorId} does not fit beside it";
            }
        }

        text += $"; merc at {merc.Location} facing yaw {view.Yaw & 65535}, for {holdSeconds:F0} s";
        if (s_walker is not null || s_still is not null)
        {
            host.BeforeWorldTick += Hold;
        }

        Report(text);
    });

    /// <summary>A pawn moved or held by the check, with what it had before.</summary>
    private sealed record Held(SBasePawn Pawn, FVector At, FVector Across, float Until, float GroundSpeed, float AccelRate, EPhysics Physics)
    {
        public float Stride;
        public float Direction = 1f;
        public float LastTime = -1f;
    }

    private static Held? s_walker;
    private static Held? s_still;

    /// <summary>The half width of the walker's stride, uu, and its speed, uu/s (above the tracker's 150).</summary>
    private const float StrideHalf = 260f;
    private const float WalkSpeed = 260f;

    /// <summary>Gives the held pawns their speed back and stops holding them.</summary>
    private static void Release()
    {
        foreach (Held? held in new[] { s_walker, s_still })
        {
            if (held is { Pawn.bDeleteMe: false })
            {
                held.Pawn.GroundSpeed = held.GroundSpeed;
                held.Pawn.AccelRate = held.AccelRate;
                if (held.Pawn.Physics != held.Physics)
                {
                    held.Pawn.SetPhysics(held.Physics);
                }
            }
        }

        s_walker = null;
        s_still = null;
    }

    /// <summary>Stands a pawn at the point or, where it does not fit, a little to either side of it.</summary>
    private static bool Stand(SBasePawn pawn, FVector origin, FVector offset, FVector right)
    {
        foreach (float side in new[] { 0f, 120f, -120f, 240f, -240f })
        {
            if (pawn.SetLocation(origin + offset + (right * side)))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// Each server step until the time is up: walks the walker along its stride (its place set and its velocity the
    /// walk's, so the tracker reads it moving) and keeps the still one where it was stood with no speed at all. Then
    /// gives both their speed back.
    /// </summary>
    private static void Hold(ServerHost host)
    {
        float now = host.World.Level.Info.TimeSeconds;
        bool walking = s_walker is { Pawn.bDeleteMe: false } && now < s_walker.Until;
        bool still = s_still is { Pawn.bDeleteMe: false } && now < s_still.Until;
        if (!walking && !still)
        {
            host.BeforeWorldTick -= Hold;
            Release();
            return;
        }

        try
        {
            if (walking)
            {
                Held w = s_walker!;
                float dt = w.LastTime < 0f ? 0f : Math.Clamp(now - w.LastTime, 0f, 0.1f);
                w.LastTime = now;
                w.Stride += w.Direction * WalkSpeed * dt;
                if (MathF.Abs(w.Stride) > StrideHalf)
                {
                    w.Stride = Math.Clamp(w.Stride, -StrideHalf, StrideHalf);
                    w.Direction = -w.Direction;
                }

                // Out of the physics while it walks, so neither its own AI nor the walking physics changes the
                // velocity given here before it is sent to the clients.
                if (w.Pawn.Physics != EPhysics.PHYS_None)
                {
                    w.Pawn.SetPhysics(EPhysics.PHYS_None);
                }

                w.Pawn.SetLocation(w.At + (w.Across * w.Stride));
                w.Pawn.Velocity = w.Across * (w.Direction * WalkSpeed);
                w.Pawn.Acceleration = FVector.Zero;
            }

            if (still)
            {
                Held s = s_still!;
                s.Pawn.GroundSpeed = 0f;
                s.Pawn.AccelRate = 0f;
                s.Pawn.Velocity = FVector.Zero;
                s.Pawn.Acceleration = FVector.Zero;
                if (FVector.DistSquared(s.Pawn.Location, s.At) > 1f)
                {
                    s.Pawn.SetLocation(s.At);
                }
            }
        }
        catch (Exception)
        {
            // A check that fails must not take the server down.
        }
    }

    /// <summary>
    /// Runs <paramref name="action"/> once on the local match's server thread, just before its next world step (as
    /// WorldDebugExports does). A failure there is dropped: these are checks, and a failed one shows as nothing happening.
    /// </summary>
    private static int OnServer(Action<ServerHost> action)
    {
        try
        {
            if (Exports.Current is not { IsRemote: false, IsDisposed: false } session || session.Session.Host is not { } host)
            {
                return 0;
            }

            void Run(ServerHost running)
            {
                running.BeforeWorldTick -= Run;
                try
                {
                    action(running);
                }
                catch (Exception)
                {
                    // See the summary: a check that fails must not take the server down.
                }
            }

            host.BeforeWorldTick += Run;
            return 1;
        }
        catch (Exception)
        {
            return 0;
        }
    }
}
