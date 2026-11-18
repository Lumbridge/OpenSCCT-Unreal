using System;
using System.Diagnostics;
using System.Globalization;
using System.Threading;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Backdrop;

// BackdropProbe <content root> [first map] [second map] [seconds each]
string content = args.Length > 0 ? args[0] : DevPaths.ContentRoot();
string first = args.Length > 1 ? args[1] : "AquaE";
string second = args.Length > 2 ? args[2] : "ClubE";
double seconds = args.Length > 3 ? double.Parse(args[3], CultureInfo.InvariantCulture) : 12;

BridgeSession.Configure(content);
foreach (string map in new[] { first, second })
{
    var check = Stopwatch.StartNew();
    Console.WriteLine($"CanPlay {map}: {BackdropMatch.CanPlay(map)} ({check.Elapsed.TotalMilliseconds:F0} ms)");
}

BridgeSession? session = Prepare(first, null);
if (session is null)
{
    return 1;
}

if (!Watch(session, seconds, null))
{
    return 1;
}

// The second map loads on its worker while the first match goes on ticking, as behind the menu.
BridgeSession? next = Prepare(second, session);
if (next is null)
{
    return 1;
}

var stop = Stopwatch.StartNew();
session.Dispose();
Console.WriteLine($"stopping {first} took {stop.Elapsed.TotalMilliseconds:F0} ms");
bool ok = Watch(next, seconds, null);
stop.Restart();
next.Dispose();
Console.WriteLine($"stopping {second} took {stop.Elapsed.TotalMilliseconds:F0} ms");
Console.WriteLine(ok ? "OK" : "FAILED");
return ok ? 0 : 1;

static BridgeSession? Prepare(string map, BridgeSession? ticking)
{
    var load = Stopwatch.StartNew();
    BackdropMatch.Begin(map);
    double worst = 0;
    while (BackdropMatch.State == BackdropPrepareState.Loading)
    {
        if (ticking is not null)
        {
            long before = Stopwatch.GetTimestamp();
            ticking.Update();
            worst = Math.Max(worst, Stopwatch.GetElapsedTime(before).TotalMilliseconds);
            if (ticking.Failure is { } failure)
            {
                Console.WriteLine("FAILED while the next map loaded: " + failure);
                return null;
            }
        }

        Thread.Sleep(8);
    }

    if (BackdropMatch.Take() is not { } session)
    {
        Console.WriteLine($"FAILED to prepare {map}: {BackdropMatch.Error}");
        return null;
    }

    Console.WriteLine($"{map} ready after {load.Elapsed.TotalSeconds:F2} s (backdrop {session.IsBackdrop})" +
        (ticking is null ? "" : $"; the longest update of the match that played meanwhile took {worst:F1} ms"));
    return session;
}

static bool Watch(BridgeSession session, double seconds, object? unused)
{
    var run = Stopwatch.StartNew();
    var pawns = new BridgePawn[64];
    double nextReport = 0, worst = 0, firstView = -1, firstPawn = -1;
    int ticks = 0, cuts = 0;
    bool fired = false;
    int lastTarget = 0;
    while (run.Elapsed.TotalSeconds < seconds)
    {
        long before = Stopwatch.GetTimestamp();
        ticks += session.Update();
        worst = Math.Max(worst, Stopwatch.GetElapsedTime(before).TotalMilliseconds);
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            return false;
        }

        int count = session.GetPawns(pawns);
        BridgeView view = session.GetView();
        SPlayerController? pc = session.Session.LocalController;
        int target = (pc?.ViewTarget as SBasePawn ?? pc?.SpecTarget)?.ActorId ?? 0;
        if (target != lastTarget)
        {
            cuts++;
            lastTarget = target;
        }

        if (firstView < 0 && view.Valid != 0 && target != 0) firstView = run.Elapsed.TotalSeconds;
        if (firstPawn < 0 && count > 0) firstPawn = run.Elapsed.TotalSeconds;
        if (!fired && run.Elapsed.TotalSeconds > seconds * 0.6)
        {
            fired = true;
            session.Command("Fire", 0);
            Console.WriteLine("  Fire (next player)");
        }

        if (run.Elapsed.TotalSeconds >= nextReport)
        {
            nextReport += 2;
            Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"  {run.Elapsed.TotalSeconds,5:F1} s: ticks {ticks}, pawns {count}, view valid {view.Valid} at {view.X:F0} {view.Y:F0} {view.Z:F0} yaw {view.Yaw:F0} pitch {view.Pitch:F0} fov {view.Fov:F0}, state {pc?.GetStateName()}, pawn {(pc?.Pawn is null ? "none" : "OWN")}, target {target} ({(pc?.ViewTarget as SBasePawn ?? pc?.SpecTarget)?.GetType().Name}), hide local {view.HideLocalBody}, first person {view.SpyFirstPerson}, ended {pc?.bGameEnded}"));
        }

        Thread.Sleep(8);
    }

    Console.WriteLine(string.Create(CultureInfo.InvariantCulture,
        $"{session.Map}: {ticks} ticks in {seconds:F0} s, first pawn after {firstPawn:F2} s, first followed view after {firstView:F2} s, {cuts} target changes, longest update {worst:F1} ms"));
    return firstView >= 0 && firstPawn >= 0;
}
