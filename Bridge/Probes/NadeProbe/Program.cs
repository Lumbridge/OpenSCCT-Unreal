using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Equipment;

// NadeProbe [content root] [map]
// For a merc and then a spy on a map without bots: for every grenade gadget the player carries, at several aims and
// charge values, the preview's prediction is taken and the real throw is made in the same instant (the gadget's own
// Use on the player's machine, which sends the throw to the host as a played throw does). The host's projectile is
// then followed tick by tick until it is gone. Printed per throw: the largest distance between the real and the
// predicted position on any tick, and the distance between where each ended. Exit code 1 when any is over 1 unit.

string content = args.Length > 0 ? args[0] : DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
BridgeSession.Configure(content);
Console.WriteLine($"sizes: BridgeNadePreview {Unsafe.SizeOf<BridgeNadePreview>()}, BridgeNadePoint {Unsafe.SizeOf<BridgeNadePoint>()}, NadePreviewApi {Unsafe.SizeOf<NadePreviewApi>()}");
bool ok = NadeRun.Side(map, merc: true);
ok &= NadeRun.Side(map, merc: false);
Console.WriteLine(ok ? "PASSED" : "FAILED");
return ok ? 0 : 1;

internal static class NadeRun
{
    private const float Tolerance = 1f;

    // Yaw offset from the spawn facing, pitch (65536 a turn, up positive), charge value.
    private static readonly (int Yaw, int Pitch, float V)[] s_cases =
    [
        (0, 0, 1f), (0, 2500, 0.5f), (16384, 1000, 0f), (32768, 4000, 1f), (-16384, -1500, 0.5f), (8192, 6000, 0.25f),
    ];

    private static readonly bool[] s_held = new bool[BridgeInput.ButtonCount];
    private static BridgeSession? s_session;
    private static readonly HashSet<SProjGrenade> s_known = new();
    private static SProjGrenade? s_tracked;
    private static readonly List<FVector> s_real = new();
    private static bool s_gone;
    private static bool s_watching;

    public static bool Side(string map, bool merc)
    {
        Array.Clear(s_held);
        using BridgeSession session = BridgeSession.StartLocal(new LocalMatch { Map = map, Mode = "Story", Merc = merc, Bots = 0, InlineHost = true, UnlimitedGear = true });
        s_session = session;
        s_tracked = null;
        s_known.Clear();
        s_watching = false;
        // The host's world just before each of its ticks: where the followed projectile is after the tick before.
        session.Session.Host.BeforeWorldTick += _ => Watch();
        Console.WriteLine();
        Console.WriteLine($"==== {map} as a {(merc ? "merc" : "spy")}");
        if (!Pump(session, 20, () => session.Session.LocalPawn is { Health: > 0 } && session.Session.ServerController?.Pawn is not null))
        {
            Console.WriteLine("FAILED: no local pawn");
            return false;
        }

        Pump(session, 2.0, null);
        SBasePawn pawn = session.Session.LocalPawn!;
        SPlayerController pc = session.Session.LocalController!;
        if (!merc)
        {
            // The spy draws the pistol (the fire mode key tapped): grenades leave from it.
            s_held[11] = true;
            Pump(session, 0.15, null);
            s_held[11] = false;
            Pump(session, 3.0, () => pawn.WeaponLoaded != 0);
            Pump(session, 1.0, null);
        }

        Console.WriteLine($"pawn at {pawn.Location}, yaw {pc.Rotation.Yaw}, WeaponLoaded {pawn.WeaponLoaded}, weapon {pawn.MyWeapon?.GetType().Name ?? "none"}");
        var gadgets = new List<SGadGrenLauncher>();
        foreach (SGadGrenLauncher g in pawn.XLevel!.AllActors<SGadGrenLauncher>())
        {
            if (ReferenceEquals(g.OwnerPawn, pawn) && !g.bDeleteMe)
            {
                gadgets.Add(g);
            }
        }

        Console.WriteLine("grenade gadgets: " + (gadgets.Count == 0 ? "none" : string.Join(", ", gadgets.Select(g => $"{g.GetType().Name} ({g.ProjectileClass.Path}, charge {g.Charge})"))));
        if (gadgets.Count == 0)
        {
            Console.WriteLine("FAILED: nothing to throw");
            return false;
        }

        int baseYaw = pc.Rotation.Yaw;
        bool ok = true;

        // The feature as Unreal reads it: a frame's head and points, the second read of a still view costs no flight.
        NadePreviewFeature feature = NadePreviewFeature.Of(session);
        pawn.SelectedGadget = gadgets[0];
        gadgets[0].LastUsed = -100f;
        var points = new BridgeNadePoint[192];
        int count = feature.Read(out BridgeNadePreview head, points);
        int revision = feature.Revision;
        long allocated = GC.GetAllocatedBytesForCurrentThread();
        var clock = Stopwatch.StartNew();
        for (int i = 0; i < 100; i++)
        {
            feature.Read(out _, points);
        }

        double each = clock.Elapsed.TotalMilliseconds / 100;
        allocated = GC.GetAllocatedBytesForCurrentThread() - allocated;
        Console.WriteLine($"feature: state {head.State} flags {head.Flags} kind {head.Kind} charge {head.Charge:F2}, {count} points, {head.Bounces} bounces, {head.FlightSeconds:F2} s, " +
            $"lands ({head.LandX:F0} {head.LandY:F0} {head.LandZ:F0}) normal ({head.NormalX:F2} {head.NormalY:F2} {head.NormalZ:F2}) radius {head.Radius:F0}, " +
            $"camera ({head.PipX:F0} {head.PipY:F0} {head.PipZ:F0}); 100 more reads of the still view: {feature.Revision - revision} flights, {each:F4} ms each, {allocated} bytes");
        if (head.State != NadePreviewState.Aimed || count < 2 || allocated > 4096)
        {
            Console.WriteLine("FAILED: the feature gave no preview, or its still reads allocate");
            ok = false;
        }

        var flight = new NadeFlight();
        foreach (SGadGrenLauncher gadget in gadgets)
        {
            if (gadget.ProjectileDefaults is null || MakeDetached(gadget) is not { } detached)
            {
                Console.WriteLine($"FAILED: {gadget.GetType().Name} has no projectile class");
                ok = false;
                continue;
            }

            foreach ((int yaw, int pitch, float v) in s_cases)
            {
                var view = new FRotator(pitch, (baseYaw + yaw) & 0xFFFF, 0);
                pc.SetViewRotation(view);
                Pump(session, 0.4, null);
                pc.SetViewRotation(view);
                if (session.Session.ServerController?.Pawn is SBasePawn { XLevel: { } hostLevel } serverPawn)
                {
                    foreach (SGadGrenLauncher g in hostLevel.AllActors<SGadGrenLauncher>())
                    {
                        if (ReferenceEquals(g.OwnerPawn, serverPawn))
                        {
                            g.Charge = Math.Max(g.Charge, 5);
                            g.LastUsed = -100f;
                        }
                    }
                }

                gadget.Charge = Math.Max(gadget.Charge, 5);
                gadget.LastUsed = -100f;
                FRotator aim = merc ? pc.MyDesiredRotation : pc.Rotation;
                long before = Stopwatch.GetTimestamp();
                bool launched = NadePredictor.Launch(pawn, gadget, aim, default, v, out NadeLaunch launch);
                if (launched)
                {
                    NadePredictor.Fly(pawn, detached, launch, flight);
                }

                double ms = Stopwatch.GetElapsedTime(before).TotalMilliseconds;
                if (!launched)
                {
                    Console.WriteLine($"FAILED: {gadget.GetType().Name}: the prediction says no throw (WeaponLoaded {pawn.WeaponLoaded})");
                    ok = false;
                    continue;
                }

                s_tracked = null;
                s_real.Clear();
                s_gone = false;
                s_watching = true;
                gadget.Use(v);
                if (!Pump(session, 14, () => s_gone))
                {
                    Console.WriteLine($"FAILED: {gadget.GetType().Name} yaw {yaw} pitch {pitch} v {v}: {(s_tracked is null ? "no projectile appeared on the host" : "the projectile never went")}");
                    ok = false;
                    s_watching = false;
                    Pump(session, 2.5, null);
                    continue;
                }

                s_watching = false;
                // The first sight of the projectile is after its first ticks; find which predicted tick that is.
                int bestShift = 0;
                float bestWorst = float.MaxValue;
                for (int shift = 0; shift <= 3; shift++)
                {
                    float worst = 0f;
                    for (int i = 0; i < s_real.Count - 1 && i + shift < flight.Points.Count; i++)
                    {
                        worst = MathF.Max(worst, (s_real[i] - flight.Points[i + shift]).Size());
                    }

                    if (worst < bestWorst)
                    {
                        bestWorst = worst;
                        bestShift = shift;
                    }
                }

                FVector realEnd = s_real[^1];
                float landing = (realEnd - flight.Points[^1]).Size();
                int realTicks = s_real.Count - 1 + bestShift;
                bool pass = bestWorst <= Tolerance && landing <= Tolerance;
                ok &= pass;
                Console.WriteLine($"{(pass ? "ok  " : "FAIL")} {gadget.GetType().Name,-18} yaw {yaw,6} pitch {pitch,5} v {v:F2} force {launch.Force,4}: " +
                    $"predicted {flight.Points.Count - 1} ticks ({flight.Seconds:F2} s), {flight.BouncePoints.Count} bounces, ends {flight.End} at {Text(flight.Points[^1])}; " +
                    $"real {realTicks} ticks ends at {Text(realEnd)}; worst tick {bestWorst:F3} uu (first seen at tick {bestShift}), landing {landing:F3} uu; " +
                    $"range {(flight.Land - launch.Origin).Size():F0} uu; {ms:F2} ms");
                Pump(session, 2.0, null);
            }
        }

        return ok;
    }

    private static SProjGrenade? MakeDetached(SGadGrenLauncher gadget) =>
        OpenScct.Game.Equipment.PawnInventory.IndexOf(gadget).Find(gadget.ProjectileClass)?.Create() as SProjGrenade;

    private static string Text(FVector v) => $"({v.X:F1} {v.Y:F1} {v.Z:F1})";

    private static void Watch()
    {
        if (s_session?.Session.ServerController is not { XLevel: { } level })
        {
            return;
        }

        if (s_tracked is null)
        {
            foreach (SProjGrenade g in level.AllActors<SProjGrenade>())
            {
                if (s_known.Add(g) && s_watching && s_tracked is null)
                {
                    s_tracked = g;
                }
            }
        }

        if (s_tracked is { } tracked && !s_gone)
        {
            s_real.Add(tracked.Location);
            s_gone = tracked.bDeleteMe;
        }
    }

    private static unsafe bool Pump(BridgeSession session, double seconds, Func<bool>? until)
    {
        var clock = Stopwatch.StartNew();
        while (clock.Elapsed.TotalSeconds < seconds)
        {
            var input = default(BridgeInput);
            for (int b = 0; b < BridgeInput.ButtonCount; b++)
            {
                input.Buttons[b] = (byte)(s_held[b] ? 1 : 0);
            }

            session.SetInput(in input);
            session.Update();
            if (session.Failure is { } failure)
            {
                Console.WriteLine("FAILED: " + failure);
                return false;
            }

            if (until?.Invoke() == true)
            {
                return true;
            }

            Thread.Sleep(2);
        }

        return until is null;
    }
}
