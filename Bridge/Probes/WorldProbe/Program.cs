using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.World;

// Hosts a map with bots and reads the world state every frame as Unreal would. Part of the way in it fires the
// movers' and trigger lights' events and damages the breakable props on the SERVER's world, so what is printed is
// what reached the client through replication. At the end every name handed out is looked up in the map's render
// files, read here on their own.

string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 16;

BridgeSession.Configure(content);
var load = Stopwatch.StartNew();
using BridgeSession session = BridgeSession.StartLocal(map, "Story", merc: false, bots: 3);
WorldState world = WorldState.Of(session);
Console.WriteLine($"{map} loaded in {load.Elapsed.TotalSeconds:F1} s");

var actors = new BridgeWorldActor[4096];
var lights = new BridgeWorldLight[1024];
var lasers = new BridgeWorldLaser[256];
var markers = new BridgeWorldMarker[256];
var animated = new BridgeWorldAnimated[256];
var events = new BridgeWorldEvent[256];

var first = new Dictionary<int, BridgeWorldActor>();
var farthest = new Dictionary<int, float>();
var largestStep = new Dictionary<int, float>();
var last = new Dictionary<int, BridgeWorldActor>();
var movedByBots = new HashSet<int>();
var lightSeen = new Dictionary<int, (float Min, float Max, int Flags)>();
var eventCounts = new SortedDictionary<int, int>();
var eventSamples = new Dictionary<int, List<string>>();
int frames = 0, ticks = 0, maxLasers = 0;
bool described = false, fired = false;
string serverReport = "";
double fireAt = Math.Min(4, seconds / 3);
var run = Stopwatch.StartNew();
double nextReport = 0;
long readBytes = 0;

while (run.Elapsed.TotalSeconds < seconds)
{
    ticks += session.Update();
    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    if (!world.Bound)
    {
        Thread.Sleep(4);
        continue;
    }

    if (!described)
    {
        described = true;
        Describe();
    }

    if (!fired && run.Elapsed.TotalSeconds >= fireAt)
    {
        fired = true;
        foreach (int id in farthest.Where(p => p.Value > 2f).Select(p => p.Key))
        {
            movedByBots.Add(id);
        }

        // On the server's own thread, just before its next world step. The props nearest the local player are the
        // ones damaged: the server replicates a prop's state only while it is relevant to the player.
        FVector near = session.Session.LocalPawn?.Location ?? default;
        var done = new ManualResetEventSlim();
        void Fire(ServerHost host)
        {
            host.BeforeWorldTick -= Fire;
            try
            {
                serverReport = FireOnServer(host, near);
            }
            catch (Exception e)
            {
                serverReport = "server-side trigger failed: " + e;
            }

            done.Set();
        }

        session.Session.Host.BeforeWorldTick += Fire;
        done.Wait(2000);
        Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} server: {serverReport}");
    }

    frames++;
    long readStart = GC.GetAllocatedBytesForCurrentThread();
    world.GetActors(actors);
    world.GetLights(lights);
    world.GetLasers(lasers);
    world.GetMarkers(markers);
    world.GetAnimated(animated);
    readBytes += GC.GetAllocatedBytesForCurrentThread() - readStart;
    int actorCount = Math.Min(world.GetActors(actors), actors.Length);
    for (int i = 0; i < actorCount; i++)
    {
        BridgeWorldActor a = actors[i];
        if ((a.Flags & BridgeWorldActorFlags.Attached) != 0)
        {
            continue;
        }

        if (!first.TryGetValue(a.NameId, out BridgeWorldActor start))
        {
            first[a.NameId] = start = a;
        }

        float away = Distance(a, start) + AngleBetween(a, start);
        farthest[a.NameId] = MathF.Max(farthest.GetValueOrDefault(a.NameId), away);
        if (last.TryGetValue(a.NameId, out BridgeWorldActor before))
        {
            largestStep[a.NameId] = MathF.Max(largestStep.GetValueOrDefault(a.NameId), Distance(a, before));
        }

        last[a.NameId] = a;
    }

    int lightCount = Math.Min(world.GetLights(lights), lights.Length);
    for (int i = 0; i < lightCount; i++)
    {
        BridgeWorldLight l = lights[i];
        (float min, float max, int flags) = lightSeen.TryGetValue(l.NameId, out var seen) ? seen : (float.MaxValue, float.MinValue, 0);
        lightSeen[l.NameId] = (MathF.Min(min, l.Scale), MathF.Max(max, l.Scale), flags | l.Flags);
    }

    int laserCount = world.GetLasers(lasers);
    maxLasers = Math.Max(maxLasers, laserCount);
    int markerCount = Math.Min(world.GetMarkers(markers), markers.Length);
    int animatedCount = Math.Min(world.GetAnimated(animated), animated.Length);
    int got;
    while ((got = world.DrainEvents(events)) > 0)
    {
        for (int i = 0; i < got; i++)
        {
            BridgeWorldEvent e = events[i];
            eventCounts[e.Kind] = eventCounts.GetValueOrDefault(e.Kind) + 1;
            if (!eventSamples.TryGetValue(e.Kind, out List<string>? samples))
            {
                eventSamples[e.Kind] = samples = [];
            }

            if (samples.Count < 4)
            {
                samples.Add($"t={run.Elapsed.TotalSeconds:F1} {EventName(e.Kind)} {world.NameOf(e.NameId)} {EventText(e)}");
            }
        }
    }

    if (run.Elapsed.TotalSeconds >= nextReport)
    {
        nextReport += 3;
        int moving = 0, visible = 0, movedNow = 0, gone = 0, attached = 0;
        for (int i = 0; i < actorCount; i++)
        {
            moving += (actors[i].Flags & BridgeWorldActorFlags.Moving) != 0 ? 1 : 0;
            visible += (actors[i].Flags & BridgeWorldActorFlags.Visible) != 0 ? 1 : 0;
            movedNow += (actors[i].Flags & BridgeWorldActorFlags.Moved) != 0 ? 1 : 0;
            gone += (actors[i].Flags & BridgeWorldActorFlags.Gone) != 0 ? 1 : 0;
            attached += (actors[i].Flags & BridgeWorldActorFlags.Attached) != 0 ? 1 : 0;
        }

        int lit = 0;
        for (int i = 0; i < lightCount; i++)
        {
            lit += (lights[i].Flags & BridgeWorldLightFlags.On) != 0 ? 1 : 0;
        }

        Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} ticks={ticks} frames={frames} actors={actorCount} (visible {visible}, moved {movedNow}, moving {moving}, gone {gone}, carried {attached}) lights on {lit}/{lightCount} lasers={laserCount} markers={markerCount} skeletal={animatedCount} names={world.NameCount} rebinds={world.Rebinds}");
    }

    Thread.Sleep(4);
}

Console.WriteLine();
Console.WriteLine($"== {map}: {frames} frames, {ticks} ticks; one frame's five reads (actors, lights, lasers, markers, skeletal) allocated {readBytes} bytes over the whole run");

// ---- movers and other actors that changed place --------------------------------------------------------------
Console.WriteLine();
Console.WriteLine("-- actors that left their first place (distance in units + turn in degrees):");
int shownMovers = 0, moversMoved = 0, moversTotal = 0, moversBefore = 0;
foreach ((int nameId, float away) in farthest.OrderByDescending(p => p.Value))
{
    world.TryGetActorInfo(nameId, out BridgeWorldActorInfo info);
    bool mover = (info.Flags & BridgeWorldActorInfoFlags.Mover) != 0;
    moversTotal += mover ? 1 : 0;
    if (away <= 2f)
    {
        continue;
    }

    moversMoved += mover ? 1 : 0;
    moversBefore += mover && movedByBots.Contains(nameId) ? 1 : 0;
    if (shownMovers++ < 24)
    {
        BridgeWorldActor a = last[nameId], s = first[nameId];
        Console.WriteLine($"   {world.NameOf(nameId),-28} {world.NameOf(info.ClassId),-22} farthest {away,7:F1}  now ({a.X:F0},{a.Y:F0},{a.Z:F0}) yaw {a.Yaw:F0}  first ({s.X:F0},{s.Y:F0},{s.Z:F0}) yaw {s.Yaw:F0}  largest step between frames {largestStep.GetValueOrDefault(nameId):F2}{(movedByBots.Contains(nameId) ? "  [before the trigger]" : "")}");
    }
}

Console.WriteLine($"   movers that moved: {moversMoved} of {moversTotal}; of those, before the server-side trigger (bots or the map's own script): {moversBefore}");

// ---- lights ---------------------------------------------------------------------------------------------------
Console.WriteLine();
Console.WriteLine("-- lights whose brightness scale changed:");
int changedLights = 0;
foreach ((int nameId, (float min, float max, int flags)) in lightSeen)
{
    if (max - min > 0.001f && changedLights++ < 16)
    {
        Console.WriteLine($"   {world.NameOf(nameId),-24} scale {min:F2}..{max:F2}{((flags & BridgeWorldLightFlags.Triggered) != 0 ? " triggered" : "")}{((flags & BridgeWorldLightFlags.Breaking) != 0 ? " was breaking" : "")}");
    }
}

Console.WriteLine($"   {changedLights} of {lightSeen.Count} lights changed");

// ---- events ---------------------------------------------------------------------------------------------------
Console.WriteLine();
Console.WriteLine("-- events:");
foreach ((int kind, int count) in eventCounts)
{
    Console.WriteLine($"   {EventName(kind)}: {count}");
    foreach (string sample in eventSamples[kind])
    {
        Console.WriteLine("      " + sample);
    }
}

// ---- markers, lasers, skeletal props ---------------------------------------------------------------------------
int finalMarkers = Math.Min(world.GetMarkers(markers), markers.Length);
Console.WriteLine();
Console.WriteLine($"-- markers ({finalMarkers}):");
foreach (IGrouping<int, BridgeWorldMarker> group in markers.Take(finalMarkers).GroupBy(m => m.Kind).OrderBy(g => g.Key))
{
    BridgeWorldMarker m = group.First();
    Console.WriteLine($"   kind {group.Key} x{group.Count()}: states [{string.Join(",", group.Select(x => x.State))}] gone {group.Count(x => (x.Flags & BridgeWorldMarkerFlags.Gone) != 0)}; e.g. {world.NameOf(m.NameId)} flags {m.Flags} at ({m.X:F0},{m.Y:F0},{m.Z:F0}) owner {m.OwnerPawnId} progress {m.Progress:F2} left {m.TimeLeft:F1}");
}

int finalLasers = Math.Min(world.GetLasers(lasers), lasers.Length);
Console.WriteLine();
Console.WriteLine($"-- lasers now {finalLasers} (most at once {maxLasers}):");
for (int i = 0; i < Math.Min(3, finalLasers); i++)
{
    BridgeWorldLaser l = lasers[i];
    float dx = l.EndX - l.StartX, dy = l.EndY - l.StartY, dz = l.EndZ - l.StartZ;
    Console.WriteLine($"   #{l.Id} kind {l.Kind} {world.NameOf(l.NameId)} ({l.StartX:F0},{l.StartY:F0},{l.StartZ:F0}) -> ({l.EndX:F0},{l.EndY:F0},{l.EndZ:F0}) length {MathF.Sqrt((dx * dx) + (dy * dy) + (dz * dz)):F0} flags {l.Flags} heat {l.Heat:F1}");
}

int finalAnimated = Math.Min(world.GetAnimated(animated), animated.Length);
Console.WriteLine();
Console.WriteLine($"-- skeletal props ({finalAnimated}):");
foreach (BridgeWorldAnimated a in animated.Take(finalAnimated).Take(4))
{
    Console.WriteLine($"   {world.NameOf(a.NameId),-20} mesh {world.NameOf(a.MeshId)} sequence '{world.NameOf(a.SequenceId)}' frame {a.Frame:F3} rate {a.Rate:F2} flags {a.Flags} at ({a.X:F0},{a.Y:F0},{a.Z:F0}) yaw {a.Yaw:F0}");
}

// ---- every name against the render files, read here on their own ----------------------------------------------
string mapFolder = session.Session.MapFolder;
(HashSet<string> placed, HashSet<string> unplaced) = ReadInstanceNames(Path.Combine(mapFolder, "render", "world.glb"));
(HashSet<string> lightNames, HashSet<string> hiddenLights) = ReadLightNames(Path.Combine(mapFolder, "render", "lighting.json"));
var notInLevel = new SortedDictionary<string, List<string>>(StringComparer.Ordinal);
int actorNames = 0, actorNamesFound = 0, wrongFlag = 0;
for (int i = 0, n = Math.Min(world.GetActors(actors), actors.Length); i < n; i++)
{
    string name = world.NameOf(actors[i].NameId);
    world.TryGetActorInfo(actors[i].NameId, out BridgeWorldActorInfo info);
    actorNames++;
    bool inLevel = placed.Contains(name);
    actorNamesFound += inLevel ? 1 : 0;
    wrongFlag += inLevel != ((info.Flags & BridgeWorldActorInfoFlags.InLevel) != 0) ? 1 : 0;
    if (!inLevel)
    {
        string why = (info.Flags & BridgeWorldActorInfoFlags.Spawned) != 0 ? "spawned during play (mesh " + world.NameOf(info.MeshId) + ")"
            : unplaced.Contains(name) ? "hidden instance in world.glb (the importer skips it)"
            : "no instance in world.glb (class mesh " + world.NameOf(info.MeshId) + ")";
        string key = world.NameOf(info.ClassId) + ": " + why;
        if (!notInLevel.TryGetValue(key, out List<string>? list))
        {
            notInLevel[key] = list = [];
        }

        list.Add(name);
    }
}

Console.WriteLine();
Console.WriteLine($"-- names against render/world.glb and lighting.json: {actorNamesFound} of {actorNames} followed actors have a placed instance; InLevel flag wrong on {wrongFlag}");
foreach ((string key, List<string> names) in notInLevel)
{
    Console.WriteLine($"   NOT IN LEVEL {key}: {names.Count}: {string.Join(", ", names.Take(8))}{(names.Count > 8 ? ", ..." : "")}");
}

int lightsMissing = 0, lightsHidden = 0;
for (int i = 0, n = Math.Min(world.GetLights(lights), lights.Length); i < n; i++)
{
    string name = world.NameOf(lights[i].NameId);
    if (!lightNames.Contains(name))
    {
        lightsMissing++;
        Console.WriteLine("   LIGHT NOT IN lighting.json: " + name);
    }
    else if (hiddenLights.Contains(name))
    {
        lightsHidden++;
    }
}

Console.WriteLine($"   lights: {lightSeen.Count} emitted, {lightsMissing} not in lighting.json, {lightsHidden} hidden there (the importer places no light for those)");
List<string> markerOnly = markers.Take(finalMarkers).Select(m => world.NameOf(m.NameId)).Where(n => !placed.Contains(n)).ToList();
Console.WriteLine($"   markers without a placed instance (C++ draws them from their location): {markerOnly.Count}: {string.Join(", ", markerOnly.Take(10))}{(markerOnly.Count > 10 ? ", ..." : "")}");
return 0;

// ---------------------------------------------------------------------------------------------------------------

void Describe()
{
    int n = Math.Min(world.GetActors(actors), actors.Length);
    var byClass = new SortedDictionary<string, int>(StringComparer.Ordinal);
    for (int i = 0; i < n; i++)
    {
        world.TryGetActorInfo(actors[i].NameId, out BridgeWorldActorInfo info);
        string c = world.NameOf(info.ClassId);
        byClass[c] = byClass.GetValueOrDefault(c) + 1;
    }

    Console.WriteLine($"bound: {world.ActorCount} actors, {world.LightCount} lights, {world.MarkerCount} markers, {world.AnimatedCount} skeletal props; render files: {world.Render.Instances.Count} instances, {world.Render.Lights.Count} lights");
    Console.WriteLine("   followed by class: " + string.Join(", ", byClass.Select(p => p.Key + " " + p.Value)));
}

// Runs on the server thread: fires the tag of every mover and trigger light and damages the breakables nearest <near>.
static string FireOnServer(ServerHost host, FVector near)
{
    Level level = host.World.Level;
    var tags = new HashSet<Name>();
    int movers = 0, triggerLights = 0, broken = 0, glass = 0;
    foreach (Mover m in level.AllActors<Mover>())
    {
        movers++;
        tags.Add(m.Tag);
    }

    foreach (STriggerLight l in level.AllActors<STriggerLight>())
    {
        triggerLights++;
        tags.Add(l.Tag);
    }

    var targets = new List<SGamePlayObject>();
    foreach (SGamePlayObject o in level.AllActors<SGamePlayObject>())
    {
        if (o.bDamageable && !o.bDeleteMe && o is not SObjectiveTrigger)
        {
            targets.Add(o);
        }
    }

    targets.Sort((a, b) => FVector.DistSquared(a.Location, near).CompareTo(FVector.DistSquared(b.Location, near)));
    foreach (Name tag in tags)
    {
        if (!tag.IsNone)
        {
            level.TriggerEvent(tag, null, null);
        }
    }

    foreach (SGamePlayObject o in targets)
    {
        if (o is SVitre ? glass >= 2 : broken >= 12)
        {
            continue;
        }

        SDamageType type = o.TakeDamageTypes.Count > 0 ? o.TakeDamageTypes[0] : SDamageType.SDTBase;
        o.eventTakeDamage(10000, null, o.Location + new FVector(0f, 0f, 4f), new FVector(300f, 0f, 0f), type);
        glass += o is SVitre ? 1 : 0;
        broken += o is SVitre ? 0 : 1;
    }

    return $"fired {tags.Count} tags ({movers} movers, {triggerLights} trigger lights), damaged the nearest {broken} props and {glass} glass panes of {targets.Count} damageable (player at {near})";
}

static float Distance(in BridgeWorldActor a, in BridgeWorldActor b) =>
    MathF.Sqrt(((a.X - b.X) * (a.X - b.X)) + ((a.Y - b.Y) * (a.Y - b.Y)) + ((a.Z - b.Z) * (a.Z - b.Z)));

static float AngleBetween(in BridgeWorldActor a, in BridgeWorldActor b) =>
    MathF.Abs(Wrap(a.Yaw - b.Yaw)) + MathF.Abs(Wrap(a.Pitch - b.Pitch)) + MathF.Abs(Wrap(a.Roll - b.Roll));

static float Wrap(float degrees)
{
    degrees %= 360f;
    return degrees > 180f ? degrees - 360f : degrees < -180f ? degrees + 360f : degrees;
}

static string EventName(int kind) => kind switch
{
    BridgeWorldEventKind.MeshChanged => "MeshChanged",
    BridgeWorldEventKind.SkinChanged => "SkinChanged",
    BridgeWorldEventKind.GlassBroken => "GlassBroken",
    BridgeWorldEventKind.LightSwitched => "LightSwitched",
    BridgeWorldEventKind.LightBreaking => "LightBreaking",
    BridgeWorldEventKind.MarkerChanged => "MarkerChanged",
    BridgeWorldEventKind.MoverMoving => "MoverMoving",
    BridgeWorldEventKind.ShownChanged => "ShownChanged",
    _ => "kind " + kind,
};

string EventText(in BridgeWorldEvent e) => e.Kind switch
{
    BridgeWorldEventKind.MeshChanged => $"'{world.NameOf(e.B)}' -> '{world.NameOf(e.A)}'",
    BridgeWorldEventKind.SkinChanged => $"slot {e.A} -> '{world.NameOf(e.B)}'",
    BridgeWorldEventKind.GlassBroken => $"hit given {e.A} at ({e.X:F0},{e.Y:F0},{e.Z:F0}) direction ({e.DX:F2},{e.DY:F2},{e.DZ:F2})",
    BridgeWorldEventKind.LightSwitched => $"on {e.A} type {e.B}",
    BridgeWorldEventKind.MarkerChanged => $"state {e.A} flags {e.B}",
    BridgeWorldEventKind.MoverMoving => $"moving {e.A} key {e.B} at ({e.X:F0},{e.Y:F0},{e.Z:F0})",
    BridgeWorldEventKind.ShownChanged => $"shown {e.A}",
    _ => $"a {e.A} b {e.B}",
};

// The render files read without the bridge's own reader, so the check does not agree with itself.
static (HashSet<string> Placed, HashSet<string> Unplaced) ReadInstanceNames(string glb)
{
    var placed = new HashSet<string>(StringComparer.Ordinal);
    var unplaced = new HashSet<string>(StringComparer.Ordinal);
    using FileStream file = File.OpenRead(glb);
    byte[] header = new byte[20];
    file.ReadExactly(header);
    byte[] json = new byte[BinaryPrimitives.ReadInt32LittleEndian(header.AsSpan(12))];
    file.ReadExactly(json);
    using JsonDocument doc = JsonDocument.Parse(json);
    foreach (JsonElement i in doc.RootElement.GetProperty("extras").GetProperty("svmap").GetProperty("instances").EnumerateArray())
    {
        string name = i.GetProperty("actor").GetString() ?? "";
        bool skipped = (i.TryGetProperty("hidden", out JsonElement h) && h.ValueKind == JsonValueKind.True) ||
            (i.TryGetProperty("skyZone", out JsonElement s) && s.ValueKind == JsonValueKind.True);
        (skipped ? unplaced : placed).Add(name);
    }

    unplaced.ExceptWith(placed);
    return (placed, unplaced);
}

static (HashSet<string> Names, HashSet<string> Hidden) ReadLightNames(string path)
{
    var names = new HashSet<string>(StringComparer.Ordinal);
    var hidden = new HashSet<string>(StringComparer.Ordinal);
    using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path));
    foreach (JsonElement l in doc.RootElement.GetProperty("lights").EnumerateArray())
    {
        string name = l.GetProperty("name").GetString() ?? "";
        names.Add(name);
        if (l.TryGetProperty("hidden", out JsonElement h) && h.ValueKind == JsonValueKind.True)
        {
            hidden.Add(name);
        }
    }

    return (names, hidden);
}
