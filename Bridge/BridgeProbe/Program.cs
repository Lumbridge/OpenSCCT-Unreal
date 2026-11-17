using System;
using System.Diagnostics;
using System.Threading;
using OpenScct.UnrealBridge;

string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 8;

BridgeSession.Configure(content);
var load = Stopwatch.StartNew();
using BridgeSession session = BridgeSession.StartLocal(map, "Story", merc: false, bots: 3);
Console.WriteLine($"{map} loaded in {load.Elapsed.TotalSeconds:F1} s");

var pawns = new BridgePawn[64];
var run = Stopwatch.StartNew();
double nextReport = 0;
int ticks = 0;
while (run.Elapsed.TotalSeconds < seconds)
{
    ticks += session.Update();
    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    if (run.Elapsed.TotalSeconds >= nextReport)
    {
        nextReport += 2;
        int count = session.GetPawns(pawns);
        BridgeView view = session.GetView();
        Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} ticks={ticks} pawns={count} view valid={view.Valid} at ({view.X:F0},{view.Y:F0},{view.Z:F0}) yaw={view.Yaw:F0} fov={view.Fov:F0}");
        for (int i = 0; i < count; i++)
        {
            BridgePawn p = pawns[i];
            Console.WriteLine($"   #{p.ActorId} kind={p.Kind} team={p.Team} hp={p.Health} flags={p.Flags} at ({p.X:F0},{p.Y:F0},{p.Z:F0}) yaw={p.Yaw:F0} r={p.CollisionRadius:F0} h={p.CollisionHeight:F0}");
        }
    }

    Thread.Sleep(4);
}

return 0;
