using System;
using System.Diagnostics;
using System.Linq;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.SoftBodies;

// Usage: SoftBodyProbe [contentRoot] [map] [seconds] [bots]
// Hosts the map as a merc with the bots asked for and builds its soft bodies from render/world.glb. Every frame of
// 1/60 s the bodies are stepped as Unreal steps them (all counted as drawn). At 2 s a pawn-sized cylinder walks through
// the first body pawns push (a strip curtain if there is one) for 1.5 s, at 4 s a shot line goes through every body.
// Printed each second: how far the bodies are from rest, the colliders, shot lines and the sounds played, and at the
// end which of the sounds the events feature resolved.
string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "ClubE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 7;
int bots = args.Length > 3 ? int.Parse(args[3], System.Globalization.CultureInfo.InvariantCulture) : 0;

BridgeSession.Configure(content);
using BridgeSession session = BridgeSession.StartLocal(new LocalMatch { Map = map, Mode = "Story", Merc = true, Bots = bots });
OpenScct.UnrealBridge.Events.EventsFeature events = OpenScct.UnrealBridge.Events.EventsFeature.Of(session);
SoftBodyFeature bodies = SoftBodyFeature.Of(session);
Console.WriteLine($"soft bodies {bodies.Count} built in {bodies.LoadMilliseconds:F1} ms, {bodies.Warnings.Count} not");
foreach (string w in bodies.Warnings)
{
    Console.WriteLine("  warning: " + w);
}

int walker = -1, walkerKind = -1;
for (int i = 0; i < bodies.Count; i++)
{
    bodies.Info(i, out BridgeSoftBodyInfo info);
    Console.WriteLine($"  {i} {bodies.Name(i)} {(info.Kind == 1 ? "strip" : "patch")} particles {info.Particles} tris {info.Indices / 3} " +
        $"centre ({info.CenterX:F0},{info.CenterY:F0},{info.CenterZ:F0}) r {info.Radius:F0} flags {info.Flags} normal sign {info.NormalSign}");
    // The first strip curtain pawns push, else the first body they push.
    if ((info.Flags & BridgeSoftBodyFlags.CollActor) != 0 && (walker < 0 || (info.Kind == 1 && walkerKind != 1)))
    {
        walker = i;
        walkerKind = info.Kind;
    }
}

var run = Stopwatch.StartNew();
double next = 1;
bool walked = false, shot = false;
var verts = new float[6 * 1024];
long soundsBefore = events.SoundCallsStarted;
const float Frame = 1f / 60f;
double simTime = 0;
while (simTime < seconds)
{
    var input = default(BridgeInput);
    session.SetInput(in input);
    session.Update();
    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    bodies.Update(Frame, ReadOnlySpan<byte>.Empty);
    simTime += Frame;
    if (!walked && simTime > 2 && walker >= 0)
    {
        bool all = Environment.GetEnvironmentVariable("SB_WALK_ALL") == "1";
        for (int i = 0; i < bodies.Count; i++)
        {
            if (i == walker || (all && bodies.Info(i, out BridgeSoftBodyInfo wi) && (wi.Flags & BridgeSoftBodyFlags.CollActor) != 0))
            {
                walked |= bodies.Walk(i, 1.5f);
                Console.WriteLine($"{simTime:F1} s: walking through {bodies.Name(i)}");
            }
        }
    }

    if (!shot && simTime > 4)
    {
        shot = true;
        for (int i = 0; i < bodies.Count; i++)
        {
            bodies.Shoot(i);
        }

        Console.WriteLine($"{simTime:F1} s: a shot through every body");
    }

    if (simTime >= next)
    {
        next += 1;
        BridgeSoftBodyState s = bodies.State();
        string per = string.Join(" ", Enumerable.Range(0, bodies.Count).Select(i => { bodies.Info(i, out BridgeSoftBodyInfo info); return info.Displacement.ToString("F0"); }));
        Console.WriteLine($"{simTime:F1} s: stepped {s.Stepped} steps {s.Steps} max displacement {s.MaxDisplacement:F1} colliders {s.Colliders} " +
            $"shots {s.ShotLines}/{s.ShotHits} coll sounds {s.CollSounds} shot sounds {s.ShotSounds} update {s.UpdateMilliseconds:F2} ms | {per}");
    }

    // Real time at 60 frames a second, so the simulation's own ticks keep pace.
    while (run.Elapsed.TotalSeconds < simTime)
    {
        System.Threading.Thread.Sleep(1);
    }
}

if (bodies.Count > 0)
{
    int n = bodies.Vertices(0, verts);
    Console.WriteLine($"body 0 particle 0 at ({verts[0]:F1},{verts[1]:F1},{verts[2]:F1}) normal ({verts[3]:F2},{verts[4]:F2},{verts[5]:F2}), {n} particles");
}

Console.WriteLine($"sound calls started since the bodies were built {events.SoundCallsStarted - soundsBefore}, late events {events.LateEvents}, unknown: {string.Join(", ", events.UnknownSounds)}");
return 0;
