using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using OpenScct.Core;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;
using OpenScct.UnrealBridge.Equipment;
using OpenScctGame.Play.Session;

// The console check of the local player's laser while the view turns. A map is hosted as a merc (laser sight on,
// rifle up) and as a spy (pistol drawn, its laser on), and the mouse is turned at a steady rate with frames played
// as Unreal plays them, 240 a second: device state in, session update, the camera of the frame out, the view handed
// over for the pose, the equipment read. For each frame two beams are measured against the camera drawn that frame:
//   - the beam the simulation placed at the last tick (60 a second), which is what Unreal drew before;
//   - the beam the bridge hands over for the frame (EquipmentFeature.DrawnMercBeam / DrawnSpyBeam).
// A beam that keeps up turns by about the camera's own turn every frame. A beam placed by the tick stands still on
// most frames and jumps on the frame after a tick.
// Arguments: content root, map.

string content = args.Length > 0 ? args[0] : DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";

BridgeSession.Configure(content);
ControlsStore.Settings = new ControlSettings();
ControlsStore.Bindings = new ControlBindings();

int failures = 0;
foreach (bool merc in new[] { true, false })
{
    failures += LaserRun.Play(map, merc) ? 0 : 1;
}

Console.WriteLine(failures == 0 ? "LaserProbe: passed" : $"LaserProbe: FAILED ({failures})");
return failures == 0 ? 0 : 1;

internal static class LaserRun
{
    private const double Pace = 1.0 / 240.0;
    private const float Counts = 3f;

    public static bool Play(string map, bool merc)
    {
        var load = Stopwatch.StartNew();
        using BridgeSession bridge = BridgeSession.StartLocal(map, "Story", merc, bots: 0);
        ControlsFeature controls = ControlsFeature.Of(bridge);
        EquipmentFeature feature = EquipmentFeature.For(bridge);
        string side = merc ? "merc" : "spy";
        Console.WriteLine();
        Console.WriteLine($"==== {map} as a {side}, loaded in {load.Elapsed.TotalSeconds:F1} s");

        // After the controls' own handler, so what is set here is what the tick takes.
        var commands = new List<(string Name, int Arg)>();
        bool holdDraw = false;
        bridge.BeforeTick += () =>
        {
            foreach ((string name, int arg) in commands)
            {
                bridge.Session.Input.Commands.Add(new InputCommand(name, arg));
            }

            commands.Clear();
            if (holdDraw)
            {
                bridge.Session.Input.Set(LocalInput.ButtonNames[11], true);
            }
        };

        var clock = Stopwatch.StartNew();
        double last = 0;
        var pawns = new BridgePawn[32];
        var bones = new BridgeBone[256];
        var equipment = new BridgeEquipment[32];
        float mouseX = 0f;
        var rows = new List<Row>();
        bool record = false;

        void Frame()
        {
            double now = clock.Elapsed.TotalSeconds;
            var raw = new BridgeRawInput { MouseX = mouseX, DeltaSeconds = (float)(now - last), Flags = BridgeRawInputFlags.Play };
            last = now;
            controls.Frame(in raw);
            int ticks = bridge.Update();
            BridgeLookView view = controls.GetView(16f / 9f, 0f);
            SBasePawn? local = bridge.Session.LocalPawn;

            // What Unreal's session subsystem does before it reads a pose: the view of the frame for the local spy.
            if (local is not null && bridge.PoserOf(local.ActorId) is { } poser)
            {
                poser.DrawnView = view.Valid != 0 && local is not SPAWNDEFENSE ? new FRotator(Units(view.AimPitch), Units(view.AimYaw), 0) : null;
            }

            int pawnCount = bridge.GetPawns(pawns);
            for (int i = 0; i < Math.Min(pawnCount, pawns.Length); i++)
            {
                bridge.GetPawnPose(pawns[i].ActorId, bones);
            }

            int equipped = feature.GetEquipment(equipment);
            feature.GetFirstPerson(out _);
            if (!record || local is null || view.Valid == 0)
            {
                return;
            }

            for (int i = 0; i < equipped; i++)
            {
                BridgeEquipment e = equipment[i];
                if (e.ActorId != local.ActorId || (e.Flags & BridgeEquipmentFlags.LaserBeam) == 0 || TickBeam(local) is not { } tick)
                {
                    continue;
                }

                var eye = new FVector(view.X, view.Y, view.Z);
                FVector forward = new FRotator(Units(view.Pitch), Units(view.Yaw), 0).Vector();
                var start = new FVector(e.LaserStartX, e.LaserStartY, e.LaserStartZ);
                var end = new FVector(e.LaserEndX, e.LaserEndY, e.LaserEndZ);
                rows.Add(new Row(ticks, view.Yaw, Yaw(tick.End - tick.Start), Yaw(end - start), Off(forward, tick.End - eye), Off(forward, end - eye),
                    (end - start).Size()));
            }
        }

        void Run(double seconds, bool paced)
        {
            double until = clock.Elapsed.TotalSeconds + seconds;
            do
            {
                double next = clock.Elapsed.TotalSeconds + Pace;
                Frame();
                if (paced)
                {
                    while (clock.Elapsed.TotalSeconds < next)
                    {
                        Thread.SpinWait(50);
                    }
                }
                else
                {
                    Thread.Sleep(4);
                }
            }
            while (clock.Elapsed.TotalSeconds < until && bridge.Failure is null);
        }

        for (int i = 0; i < 2000 && bridge.Session.LocalPawn is null && bridge.Failure is null; i++)
        {
            Frame();
            Thread.Sleep(4);
        }

        if (bridge.Session.LocalPawn is null || bridge.Failure is not null)
        {
            Console.WriteLine("FAILED: no local pawn: " + bridge.Failure);
            return false;
        }

        Run(1.0, paced: false);
        if (merc)
        {
            commands.Add(("GadLaserSwitch", 0));
        }
        else
        {
            // The fire mode key tapped draws the pistol; its laser is on when drawn.
            holdDraw = true;
            Run(0.1, paced: false);
            holdDraw = false;
        }

        Run(2.5, paced: false);
        if (!merc && TickBeam(bridge.Session.LocalPawn!) is null)
        {
            Console.WriteLine("   the pistol's laser was off after the draw: switched on");
            commands.Add(("SwitchLaser", 0));
            Run(1.0, paced: false);
        }

        // The look-ahead proves itself on the first ticks that take counts; then a second is measured.
        mouseX = Counts;
        Run(0.3, paced: true);
        record = true;
        Run(1.0, paced: true);
        record = false;
        mouseX = 0f;
        if (bridge.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return false;
        }

        SBasePawn pawn = bridge.Session.LocalPawn!;
        if (rows.Count < 100)
        {
            Console.WriteLine($"FAILED: the {side}'s laser beam was read in only {rows.Count} frames (weapon loaded {pawn.WeaponLoaded}, look-ahead {controls.LookAheadActive})");
            return false;
        }

        float[] view = Steps(rows.Select(r => r.ViewYaw));
        float[] tickSteps = Steps(rows.Select(r => r.TickYaw));
        float[] drawnSteps = Steps(rows.Select(r => r.DrawnYaw));
        float turn = MathF.Abs(view.Average());
        int tickFrames = rows.Count(r => r.Ticks > 0);
        Console.WriteLine($"   {rows.Count} frames with a beam, {tickFrames} of them after a tick; the camera turned {turn:F3} deg a frame " +
            $"(min {view.Min(MathF.Abs):F3}, max {view.Max(MathF.Abs):F3}); look-ahead {(controls.LookAheadActive ? "on" : "off")}; beam about {rows.Average(r => r.Length):F0} uu long");
        Report("beam placed by the last tick (drawn before)", tickSteps, rows.Select(r => r.TickOff), turn);
        Report("beam of the frame (drawn now)             ", drawnSteps, rows.Select(r => r.DrawnOff), turn);

        int tickStill = tickSteps.Count(s => MathF.Abs(s) < turn * 0.25f);
        int drawnStill = drawnSteps.Count(s => MathF.Abs(s) < turn * 0.25f);
        // The beam's own yaw also moves when what it hits is nearer or farther (the muzzle is beside the eye), so a
        // few steps are larger than the camera's; nine frames in ten are what tells a beam that keeps up.
        float tickJump = Percentile(tickSteps, 0.9f), drawnJump = Percentile(drawnSteps, 0.9f);
        bool cause = tickStill > tickSteps.Length / 3 && tickJump > turn * 2f;
        bool smooth = drawnStill == 0 && drawnJump < turn * 2f;
        Console.WriteLine($"   {(cause ? "ok    " : "FAILED")} the tick's beam stands still while the camera turns and then jumps ({tickStill} still frames, nine steps in ten under {tickJump / turn:F1} frames' worth)");
        Console.WriteLine($"   {(smooth ? "ok    " : "FAILED")} the frame's beam turns with the camera every frame ({drawnStill} still frames, nine steps in ten under {drawnJump / turn:F1} frames' worth)");
        return cause && smooth;
    }

    private static void Report(string what, float[] steps, IEnumerable<float> off, float turn)
    {
        float[] offs = off.ToArray();
        Console.WriteLine($"   {what}: yaw step a frame min {steps.Min(MathF.Abs):F3} max {steps.Max(MathF.Abs):F3} deg, " +
            $"{steps.Count(s => MathF.Abs(s) < turn * 0.25f)} of {steps.Length} frames still; its end is {offs.Min():F2} to {offs.Max():F2} deg from the centre of the picture " +
            $"(swing {offs.Max() - offs.Min():F2})");
    }

    /// <summary>The beam the simulation placed at the last tick for the local pawn, or null.</summary>
    private static (FVector Start, FVector End)? TickBeam(SBasePawn local)
    {
        if (local is SPAWNDEFENSE merc)
        {
            return merc.TorcheLaser is { bDeleteMe: false, bInUse: true } laser ? (laser.BeamStart, laser.HitLocation) : null;
        }

        var spy = local as SPawnAttaque;
        SWeaponAttaque? pistol = spy?.MyWeapon as SWeaponAttaque ?? spy?.WeaponDummy;
        return pistol?.LaserBeam is { bDeleteMe: false } beam
            ? (beam.Location, beam.Location + (beam.Rotation.Vector() * MathF.Max(0f, beam.DrawScale3D.X - 1f)))
            : null;
    }

    private static float Percentile(float[] steps, float share)
    {
        float[] sorted = steps.Select(MathF.Abs).Order().ToArray();
        return sorted[Math.Clamp((int)(sorted.Length * share), 0, sorted.Length - 1)];
    }

    private static int Units(float degrees) => (int)MathF.Round(degrees * (65536f / 360f));

    private static float Yaw(FVector direction) => MathF.Atan2(direction.Y, direction.X) * (180f / MathF.PI);

    /// <summary>The angle between two directions, degrees.</summary>
    private static float Off(FVector forward, FVector to)
    {
        FVector a = forward.SafeNormal(), b = to.SafeNormal();
        return MathF.Acos(Math.Clamp(a | b, -1f, 1f)) * (180f / MathF.PI);
    }

    private static float[] Steps(IEnumerable<float> yaws)
    {
        var steps = new List<float>();
        float? before = null;
        foreach (float yaw in yaws)
        {
            if (before is { } b)
            {
                float d = (yaw - b) % 360f;
                steps.Add(d > 180f ? d - 360f : d < -180f ? d + 360f : d);
            }

            before = yaw;
        }

        return steps.ToArray();
    }

    private readonly record struct Row(int Ticks, float ViewYaw, float TickYaw, float DrawnYaw, float TickOff, float DrawnOff, float Length);
}
