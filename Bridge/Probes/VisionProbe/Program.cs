using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Devices;

// A console check of the vision table, driven only through the exported function tables as Unreal drives them.
// A spy round: the pawns' heat and EMF values each second, the electronics the EMF mode picks out, the level's motion
// vision tweak, and the spy's EMF-visible flag across a pistol draw and holster (PawnVision.TestAndSetEMFVisible).
// A merc round: motion vision on (SwitchHeadset 1), the tracker's state and its locks boxed from the view in the
// 640 x 480 frame, the clean rectangles among them.

string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 10;

// The sizes Source/OpenSCCT/Runtime/ScctVisionApi.h asserts for its mirrors.
Console.WriteLine($"struct sizes: pawn {Unsafe.SizeOf<BridgeVisionPawn>()}, actor {Unsafe.SizeOf<BridgeVisionActor>()}, level {Unsafe.SizeOf<BridgeVisionLevel>()}, " +
    $"camera {Unsafe.SizeOf<BridgeVisionCamera>()}, lock {Unsafe.SizeOf<BridgeVisionLock>()}, table {Unsafe.SizeOf<VisionApi>()}; vision api version {VisionExports.ApiVersion}");

bool ok = Probe.Spy(content, map, seconds) && Probe.Merc(content, map, seconds);
Console.WriteLine(ok ? "OK" : "FAILED");
return ok ? 0 : 1;

internal static unsafe class Probe
{
    private static BridgeApi s_bridge;
    private static VisionApi s_vision;
    private static WorldApi s_world;
    private static bool s_configured;

    private static void Tables(string content)
    {
        if (s_configured)
        {
            return;
        }

        delegate* unmanaged<BridgeApi*, int, int> getBridge = &Exports.GetApiSized;
        delegate* unmanaged<VisionApi*, int> getVision = &VisionExports.GetApi;
        delegate* unmanaged<WorldApi*, int> getWorld = &WorldExports.GetApi;
        fixed (BridgeApi* b = &s_bridge)
        {
            getBridge(b, sizeof(BridgeApi));
        }

        fixed (VisionApi* v = &s_vision)
        {
            Console.WriteLine($"vision table version {getVision(v)}");
        }

        fixed (WorldApi* w = &s_world)
        {
            getWorld(w);
        }

        fixed (char* c = content)
        {
            s_bridge.Configure(c);
        }

        s_configured = true;
    }

    private static bool Start(string content, string map, bool merc)
    {
        Tables(content);
        var pawns = new BridgeVisionPawn[4];
        fixed (BridgeVisionPawn* p = pawns)
        {
            int before = s_vision.GetPawns(p, pawns.Length);
            if (before != 0)
            {
                Console.WriteLine($"FAILED: GetPawns gave {before} pawns without a session");
                return false;
            }
        }

        int started;
        fixed (char* m = map, mode = "Story")
        {
            started = s_bridge.StartLocal(m, mode, merc ? 1 : 0, 3);
        }

        if (started == 0)
        {
            Console.WriteLine("FAILED: the session did not start");
            return false;
        }

        Console.WriteLine();
        Console.WriteLine($"==== {map} as a {(merc ? "merc" : "spy")} with 3 bots");
        return true;
    }

    private static string Name(int id)
    {
        if (id == 0)
        {
            return "";
        }

        char* text = stackalloc char[260];
        s_world.GetName(id, text, 260);
        return new string(text);
    }

    private static string PawnFlags(int f)
    {
        var names = new List<string>();
        void Add(int bit, string name)
        {
            if ((f & bit) != 0)
            {
                names.Add(name);
            }
        }

        Add(BridgeVisionPawnFlags.Merc, "merc");
        Add(BridgeVisionPawnFlags.EmfOn, "emfOn");
        Add(BridgeVisionPawnFlags.EmfShows, "emfShows");
        Add(BridgeVisionPawnFlags.WeaponDrawn, "drawn");
        Add(BridgeVisionPawnFlags.Moving, "moving");
        Add(BridgeVisionPawnFlags.Tracked, "tracked");
        Add(BridgeVisionPawnFlags.ShockImmune, "shockImmune");
        Add(BridgeVisionPawnFlags.ShockStateProtected, "shockByState");
        Add(BridgeVisionPawnFlags.StickySkinned, "sticky");
        Add(BridgeVisionPawnFlags.Wet, "wet");
        Add(BridgeVisionPawnFlags.Hidden, "hidden");
        return string.Join(' ', names);
    }

    private static void PrintPawns(double t, BridgeVisionPawn[] rows, int count, int localId)
    {
        for (int i = 0; i < count; i++)
        {
            BridgeVisionPawn p = rows[i];
            Console.WriteLine($"t={t:F1}  pawn #{p.ActorId}{(p.ActorId == localId ? " (local)" : "")}: heat {p.HeatIntensity:F2}/{p.HeatOpacity:F2} emf {p.EMFIntensity:F2}/{p.EMFOpacity:F2} glow {p.EmfGlow:F2} " +
                $"shock {p.ShockFraction:F2} ({p.ShockSeconds:F1} s) cloak {p.Cloak:F2} speed {p.Speed:F0} [{PawnFlags(p.Flags)}]");
        }
    }

    private static int LocalPawnId()
    {
        var pawns = new BridgePawn[64];
        fixed (BridgePawn* p = pawns)
        {
            int n = s_bridge.GetPawns(p, pawns.Length);
            for (int i = 0; i < n; i++)
            {
                if ((pawns[i].Flags & BridgePawnFlags.Local) != 0)
                {
                    return pawns[i].ActorId;
                }
            }
        }

        return 0;
    }

    private static void Command(string name, int arg)
    {
        fixed (char* c = name)
        {
            s_bridge.Command(c, arg);
        }
    }

    public static bool Spy(string content, string map, double seconds)
    {
        if (!Start(content, map, merc: false))
        {
            return false;
        }

        var pawns = new BridgeVisionPawn[64];
        var actors = new BridgeVisionActor[2048];
        var input = default(BridgeInput);
        var run = Stopwatch.StartNew();
        double nextReport = 1, drawAt = seconds * 0.35, holsterAt = seconds * 0.7;
        bool actorsPrinted = false, levelPrinted = false;
        int heatPawns = 0, frames = 0;
        // The local spy's EMF-visible flag over time: (seconds, shows, drawn).
        var emfTrace = new List<(double T, bool Shows, bool Drawn)>();
        while (run.Elapsed.TotalSeconds < seconds)
        {
            double t = run.Elapsed.TotalSeconds;
            // The fire-mode key tapped draws the pistol, tapped again puts it away (EquipmentProbe's spy round).
            bool tap = (t >= drawAt && t < drawAt + 0.1) || (t >= holsterAt && t < holsterAt + 0.1);
            input.Buttons[11] = (byte)(tap ? 1 : 0);
            s_bridge.SetInput(&input);
            s_bridge.Update();
            if (s_bridge.GetState() != (int)BridgeState.Playing)
            {
                Thread.Sleep(4);
                continue;
            }

            frames++;
            int localId = LocalPawnId();
            int pawnCount;
            fixed (BridgeVisionPawn* p = pawns)
            {
                pawnCount = Math.Min(s_vision.GetPawns(p, pawns.Length), pawns.Length);
            }

            for (int i = 0; i < pawnCount; i++)
            {
                if (pawns[i].ActorId == localId)
                {
                    bool shows = (pawns[i].Flags & BridgeVisionPawnFlags.EmfShows) != 0;
                    bool drawn = (pawns[i].Flags & BridgeVisionPawnFlags.WeaponDrawn) != 0;
                    if (emfTrace.Count == 0 || emfTrace[^1].Shows != shows || emfTrace[^1].Drawn != drawn)
                    {
                        emfTrace.Add((t, shows, drawn));
                        Console.WriteLine($"t={t:F2}  local spy #{localId}: weapon {(drawn ? "drawn" : "away")}, EMF {(shows ? "visible" : "not visible")} (emf {pawns[i].EMFIntensity:F2}/{pawns[i].EMFOpacity:F2})");
                    }
                }
            }

            if (!levelPrinted && pawnCount > 0)
            {
                levelPrinted = true;
                BridgeVisionLevel level;
                int got = s_vision.GetLevel(&level);
                Console.WriteLine($"level ({got}): MTVSlope {level.MtvSlope}, MTVOffset {level.MtvOffset}, flags {level.Flags}, time {level.TimeSeconds:F1}");
            }

            if (!actorsPrinted && pawnCount > 0 && t > 2)
            {
                actorsPrinted = true;
                int n;
                fixed (BridgeVisionActor* a = actors)
                {
                    n = Math.Min(s_vision.GetActors(a, actors.Length), actors.Length);
                }

                Console.WriteLine($"t={t:F1}  {n} actors with vision values:");
                foreach (var group in actors.Take(n).GroupBy(a => Name(a.ClassId)).OrderBy(g => g.Key))
                {
                    BridgeVisionActor s = group.First();
                    int shows = group.Count(a => (a.Flags & BridgeVisionActorFlags.EmfShows) != 0 && a.EmfGlow > 0f);
                    int placed = group.Count(a => (a.Flags & BridgeVisionActorFlags.MapActor) != 0);
                    Console.WriteLine($"   {group.Key,-28} x{group.Count(),-3} ({placed} placed, {shows} glowing in EMF) e.g. #{s.ActorId} '{Name(s.NameId)}' owner #{s.OwnerPawnId} " +
                        $"heat {s.HeatIntensity:F2}/{s.HeatOpacity:F2} emf {s.EMFIntensity:F2}/{s.EMFOpacity:F2} glow {s.EmfGlow:F2} flags {s.Flags} at ({s.X:F0},{s.Y:F0},{s.Z:F0})");
                }
            }

            if (t >= nextReport && pawnCount > 0)
            {
                nextReport += 3;
                PrintPawns(t, pawns, pawnCount, localId);
                heatPawns = Math.Max(heatPawns, pawns.Take(pawnCount).Count(p => MathF.Abs(p.HeatIntensity - 1f) < 0.01f));
            }

            Thread.Sleep(4);
        }

        s_bridge.Stop();
        bool flipped = emfTrace.Any(e => e.Shows && e.Drawn) && emfTrace.Any(e => !e.Shows && !e.Drawn);
        bool holsteredHides = emfTrace.Count > 0 && emfTrace[^1] is { Shows: false, Drawn: false };
        Console.WriteLine($"spy round: {frames} frames, {heatPawns} pawns at heat 1, EMF-visible changes {emfTrace.Count}: " +
            string.Join(", ", emfTrace.Select(e => $"{e.T:F1}s {(e.Drawn ? "drawn" : "away")}/{(e.Shows ? "visible" : "hidden")}")));
        if (heatPawns == 0 || !flipped || !holsteredHides)
        {
            Console.WriteLine("FAILED: no pawn at heat 1, or the spy's EMF-visible flag did not follow the pistol");
            return false;
        }

        return true;
    }

    public static bool Merc(string content, string map, double seconds)
    {
        if (!Start(content, map, merc: true))
        {
            return false;
        }

        var pawns = new BridgeVisionPawn[64];
        var locks = new BridgeVisionLock[32];
        var input = default(BridgeInput);
        var run = Stopwatch.StartNew();
        bool switched = false;
        double nextReport = 1, nextBring = 3;
        int lastLockCount = 0, lookFrames = 0;
        int activeFrames = 0, lockFrames = 0, cleanFrames = 0, trackedPawnFrames = 0, maxLocks = 0;
        string lastLocks = "";
        while (run.Elapsed.TotalSeconds < seconds * 1.5)
        {
            double t = run.Elapsed.TotalSeconds;
            if (!switched && t >= 1.5)
            {
                switched = true;
                Command("SwitchHeadset", 1);
                Console.WriteLine($"t={t:F1}  motion vision on (SwitchHeadset 1)");
            }

            s_bridge.SetInput(&input);
            s_bridge.Update();
            if (s_bridge.GetState() != (int)BridgeState.Playing)
            {
                Thread.Sleep(4);
                continue;
            }

            BridgeVisionLevel level;
            s_vision.GetLevel(&level);
            activeFrames += (level.Flags & BridgeVisionLevelFlags.TrackerActive) != 0 ? 1 : 0;
            if (switched && t >= nextBring)
            {
                // The bots seldom run past the merc on their own: a spy bot is stood beside him on the server, still
                // running its route, so the tracker has something moving in range and in sight.
                nextBring += 3;
                Console.WriteLine($"t={t:F1}  server: {OnServer(BringSpyBot)}");
            }

            BridgeView view;
            s_bridge.GetView(&view);
            // A 1280 x 720 screen with the frame stretched over it (Godot's MotionHudFrame with the HUD stretched).
            var camera = new BridgeVisionCamera
            {
                X = view.X, Y = view.Y, Z = view.Z, Pitch = view.Pitch, Yaw = view.Yaw, Roll = view.Roll, FovX = view.Fov,
                ScreenWidth = 1280f, ScreenHeight = 720f, FrameScaleX = 2f, FrameScaleY = 1.5f, FrameOffsetX = 0f,
            };
            if (LookAtLocked(ref camera, locks[0].ActorId, lastLockCount > 0))
            {
                lookFrames++;
            }
            int lockCount;
            fixed (BridgeVisionLock* l = locks)
            {
                lockCount = Math.Min(s_vision.GetMotionLocks(&camera, l, locks.Length), locks.Length);
            }

            lastLockCount = lockCount;
            maxLocks = Math.Max(maxLocks, lockCount);
            lockFrames += lockCount > 0 ? 1 : 0;
            cleanFrames += locks.Take(lockCount).Any(l => (l.Flags & BridgeVisionLockFlags.Clean) != 0) ? 1 : 0;
            if (lockCount > 0)
            {
                string text = string.Join("; ", locks.Take(lockCount).Select(l =>
                    $"#{l.ActorId} flags {l.Flags} x {l.X1:F0}..{l.X2:F0} y {l.Y1:F0}..{l.Y2:F0} start {l.StartTime:F1} end {l.EndTime:F1}"));
                string shape = string.Join(",", locks.Take(lockCount).Select(l => $"{l.ActorId}:{l.Flags}"));
                if (shape != lastLocks)
                {
                    lastLocks = shape;
                    Console.WriteLine($"t={t:F2}  locks ({lockCount}): {text}");
                }
            }

            int pawnCount;
            fixed (BridgeVisionPawn* p = pawns)
            {
                pawnCount = Math.Min(s_vision.GetPawns(p, pawns.Length), pawns.Length);
            }

            trackedPawnFrames += pawns.Take(pawnCount).Any(p => (p.Flags & BridgeVisionPawnFlags.Tracked) != 0) ? 1 : 0;
            if (t >= nextReport && pawnCount > 0)
            {
                nextReport += 3;
                Console.WriteLine($"t={t:F1}  level: MTVSlope {level.MtvSlope}, MTVOffset {level.MtvOffset}, flags {level.Flags}, locks {level.LockCount}, focus {level.FocusTime:F2} s, out {level.OutTime:F2} s");
                PrintPawns(t, pawns, pawnCount, LocalPawnId());
            }

            Thread.Sleep(4);
        }

        s_bridge.Stop();
        Console.WriteLine($"merc round: tracker active in {activeFrames} frames; locks in {lockFrames} frames (at most {maxLocks}), a clean rectangle in {cleanFrames}, " +
            $"a pawn flagged tracked in {trackedPawnFrames}; the camera turned to the lock in {lookFrames} frames");
        if (activeFrames == 0)
        {
            Console.WriteLine("FAILED: motion vision never turned the tracker on");
            return false;
        }

        return true;
    }

    /// <summary>
    /// Turns the probe's camera to the locked pawn (the merc's own view looks wherever he was put): the box of a lock
    /// looked at must be in front, about the middle of the frame, and clean once focused.
    /// </summary>
    private static bool LookAtLocked(ref BridgeVisionCamera camera, int actorId, bool anyLock)
    {
        if (!anyLock)
        {
            return false;
        }

        var pawns = new BridgePawn[64];
        fixed (BridgePawn* p = pawns)
        {
            int n = s_bridge.GetPawns(p, pawns.Length);
            for (int i = 0; i < n; i++)
            {
                if (pawns[i].ActorId != actorId)
                {
                    continue;
                }

                float dx = pawns[i].X - camera.X, dy = pawns[i].Y - camera.Y, dz = pawns[i].Z - camera.Z;
                camera.Yaw = MathF.Atan2(dy, dx) * (180f / MathF.PI);
                camera.Pitch = MathF.Atan2(dz, MathF.Sqrt((dx * dx) + (dy * dy))) * (180f / MathF.PI);
                camera.Roll = 0f;
                return true;
            }
        }

        return false;
    }

    /// <summary>Runs <paramref name="action"/> on the local match's server thread before its next world step; returns at once.</summary>
    private static string OnServer(Func<ServerHost, string> action)
    {
        if (Exports.Current is not { IsRemote: false } session || session.Session.Host is not { } host)
        {
            return "no local server";
        }

        void Run(ServerHost running)
        {
            running.BeforeWorldTick -= Run;
            try
            {
                Console.WriteLine("   (server) " + action(running));
            }
            catch (Exception e)
            {
                Console.WriteLine("   (server) failed: " + e.Message);
            }
        }

        host.BeforeWorldTick += Run;
        return "queued";
    }

    /// <summary>Stands the nearest spy bot 350 to 500 uu from the merc player, where it fits.</summary>
    private static string BringSpyBot(ServerHost host)
    {
        SBasePawn? merc = null;
        var spies = new List<SBasePawn>();
        foreach (SBasePawn pawn in host.World.Level.AllActors<SBasePawn>())
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
            return "no merc player or no spy bot";
        }

        spies.Sort((a, b) => FVector.DistSquared(a.Location, merc.Location).CompareTo(FVector.DistSquared(b.Location, merc.Location)));
        SBasePawn spy = spies[0];
        foreach (FVector offset in new[] { new FVector(400f, 0f, 0f), new FVector(-400f, 0f, 0f), new FVector(0f, 400f, 0f), new FVector(0f, -400f, 0f),
                     new FVector(300f, 300f, 0f), new FVector(-300f, -300f, 0f), new FVector(500f, 0f, 0f), new FVector(0f, 500f, 0f) })
        {
            if (spy.SetLocation(merc.Location + offset))
            {
                return $"stood spy bot #{spy.ActorId} at {spy.Location}, {offset.Size():F0} uu from the merc at {merc.Location}";
            }
        }

        return $"spy bot #{spy.ActorId} does not fit beside the merc at {merc.Location}";
    }
}
