using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Devices;
using OpenScct.UnrealBridge.Events;

// Usage: DevicesProbe [contentRoot] [map] [seconds]
// At 2 s every network camera is put out as a spy's shocker does (CamnetOff), at 3 s a PS2 modem is placed ahead of
// the local spy (PlaceModem) and at 6 s every modem is shot (ShootModems), all on the SERVER's world; what is printed
// is what reached the client. Passes when the cameras' markers say Disabled, the modem is followed as a spawned
// prop with its mesh, and its destruction preset is reported as an effect actor.
string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 10;

BridgeSession.Configure(content);
using BridgeSession session = BridgeSession.StartLocal(map, "Story", merc: false, bots: 0);
WorldState world = WorldState.Of(session);
EventsFeature events = EventsFeature.Of(session);

var markers = new BridgeWorldMarker[512];
var actors = new BridgeWorldActor[4096];
var effects = new BridgeEffectActor[256];
var worldEvents = new BridgeWorldEvent[256];
var disabled = new HashSet<int>();
int modemName = 0, modemMesh = 0, modemEffects = 0, modemRemoved = 0, cameras = 0;
bool camnetDone = false, modemDone = false, shotDone = false;

var run = Stopwatch.StartNew();
while (run.Elapsed.TotalSeconds < seconds)
{
    double t = run.Elapsed.TotalSeconds;
    if (!camnetDone && t > 2)
    {
        camnetDone = true;
        Console.WriteLine($"{t:F1} s: CamnetOff queued {WorldDebugExports.CamnetOff(session)}");
    }

    if (!modemDone && t > 3 && session.Session.Client.World.Level.Info.GetLocalPlayerController()?.Pawn is { } me)
    {
        modemDone = true;
        FVector ahead = me.Rotation.Vector();
        ahead.Z = 0f;
        Console.WriteLine($"{t:F1} s: PlaceModem queued {WorldDebugExports.PlaceModem(session, me.Location + new FVector(0f, 0f, 40f), ahead.SafeNormal())}");
    }

    if (!shotDone && t > 6)
    {
        shotDone = true;
        Console.WriteLine($"{t:F1} s: ShootModems queued {WorldDebugExports.ShootModems(session)}");
    }

    var input = default(BridgeInput);
    session.SetInput(in input);
    session.Update();
    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    // The cameras' markers.
    int count = Math.Min(world.GetMarkers(markers), markers.Length);
    cameras = 0;
    for (int i = 0; i < count; i++)
    {
        if (markers[i].Kind != BridgeWorldMarkerKind.CamNetwork)
        {
            continue;
        }

        cameras++;
        if ((markers[i].Flags & BridgeWorldMarkerFlags.Disabled) != 0 && disabled.Add(markers[i].NameId))
        {
            Console.WriteLine($"{t:F1} s: camera {world.NameOf(markers[i].NameId)} out, state {markers[i].State}, {markers[i].TimeLeft:F0} s left");
        }
    }

    // The modem as a followed spawned prop.
    int followed = Math.Min(world.GetActors(actors), actors.Length);
    for (int i = 0; i < followed; i++)
    {
        string name = world.NameOf(actors[i].NameId);
        if (modemName == 0 && name.StartsWith("Ps2Modem_", StringComparison.Ordinal) && world.TryGetActorInfo(actors[i].NameId, out BridgeWorldActorInfo info))
        {
            modemName = actors[i].NameId;
            modemMesh = actors[i].MeshId;
            Console.WriteLine($"{t:F1} s: followed '{name}' class {world.NameOf(info.ClassId)} mesh {world.NameOf(actors[i].MeshId)} flags {info.Flags} at ({actors[i].X:F0}, {actors[i].Y:F0}, {actors[i].Z:F0}), visible {(actors[i].Flags & BridgeWorldActorFlags.Visible) != 0}");
        }
    }

    int got;
    while ((got = world.DrainEvents(worldEvents)) > 0)
    {
        for (int i = 0; i < got; i++)
        {
            if (worldEvents[i].NameId == modemName && modemName != 0 && worldEvents[i].Kind == BridgeWorldEventKind.ShownChanged && worldEvents[i].B == 1)
            {
                modemRemoved++;
                Console.WriteLine($"{t:F1} s: '{world.NameOf(modemName)}' removed (spawned prop gone)");
            }
        }

        if (got < worldEvents.Length)
        {
            break;
        }
    }

    // The destruction preset as an effect actor.
    int fx = events.GetEffectActors(effects);
    for (int i = 0; i < fx; i++)
    {
        if (EventNames.Text(effects[i].ClassId) == "SBase.Ps2ModemEffect" && (effects[i].Flags & BridgeEffectFlags.Deleted) == 0 && modemEffects == 0)
        {
            modemEffects++;
            Console.WriteLine($"{t:F1} s: effect actor {effects[i].ActorId} SBase.Ps2ModemEffect kind {effects[i].Kind} at ({effects[i].X:F0}, {effects[i].Y:F0}, {effects[i].Z:F0}) facing yaw {effects[i].Yaw:F0}");
        }
    }

    Thread.Sleep(4);
}

if (WorldDebugExports.LastFailure is { } thrown)
{
    Console.WriteLine("a check entry threw on the server: " + thrown);
}

bool pass = cameras > 0 && disabled.Count == cameras && modemName != 0 && modemMesh != 0 && modemEffects > 0 && modemRemoved > 0;
Console.WriteLine($"{map}: {disabled.Count} of {cameras} network cameras out; modem followed {modemName != 0} (mesh {world.NameOf(modemMesh)}), removed {modemRemoved > 0}; destruction preset reported {modemEffects > 0}");
Console.WriteLine(pass ? "PASSED" : "FAILED");
return pass ? 0 : 1;
