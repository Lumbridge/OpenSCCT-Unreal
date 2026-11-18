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
using OpenScct.UnrealBridge.Devices;

// Hosts a map with bots and reads the world state every frame as Unreal would. Part of the way in it breaks three
// trigger lights, then fires the movers' and trigger lights' events and damages the breakable props, all on the
// SERVER's world, so what is printed is what reached the client through replication. For a second and a half after
// the trigger it reads as fast as it can, to show the blend between ticks. At the end the events are checked against
// the state (every change must have been said once), the placed actors against the importer's own placement
// formula, and every name handed out against the map's render files, read here on their own.

string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 20;

BridgeSession.Configure(content);
var load = Stopwatch.StartNew();
using BridgeSession session = BridgeSession.StartLocal(map, "Story", merc: false, bots: 3);
WorldState world = WorldState.Of(session);
Console.WriteLine($"{map} loaded in {load.Elapsed.TotalSeconds:F1} s");
// The sizes Source/OpenSCCT/Runtime/ScctWorldApi.h asserts for its mirrors.
Console.WriteLine($"struct sizes: actor info {System.Runtime.CompilerServices.Unsafe.SizeOf<BridgeWorldActorInfo>()}, actor {System.Runtime.CompilerServices.Unsafe.SizeOf<BridgeWorldActor>()}, light {System.Runtime.CompilerServices.Unsafe.SizeOf<BridgeWorldLight>()}, laser {System.Runtime.CompilerServices.Unsafe.SizeOf<BridgeWorldLaser>()}, marker {System.Runtime.CompilerServices.Unsafe.SizeOf<BridgeWorldMarker>()}, event {System.Runtime.CompilerServices.Unsafe.SizeOf<BridgeWorldEvent>()}, skeletal {System.Runtime.CompilerServices.Unsafe.SizeOf<BridgeWorldAnimated>()}; world api version {WorldExports.ApiVersion}");

var actors = new BridgeWorldActor[4096];
var lights = new BridgeWorldLight[1024];
var lasers = new BridgeWorldLaser[256];
var markers = new BridgeWorldMarker[256];
var animated = new BridgeWorldAnimated[256];
var events = new BridgeWorldEvent[256];

// What the events add up to, from the state at first sight: must equal the state at the end.
var meshByEvents = new Dictionary<int, int>();
var shownByEvents = new Dictionary<int, bool>();
var skinsByEvents = new Dictionary<int, int[]>();
var lightOnByEvents = new Dictionary<int, bool>();
var markerByEvents = new Dictionary<int, (int State, int Flags)>();
var glassEvents = new HashSet<int>();
int skinnedAtFirstSight = 0;
float placeError = 0f, turnError = 0f, pivotError = 0f;
string placeWorst = "";
int placeChecked = 0, pivotChecked = 0;

// The blend between ticks, measured on the first mover seen moving while the loop does not sleep.
int subject = 0, subjectFrames = 0, subjectTicks = 0, subjectPlaces = 0;
BridgeWorldActor subjectLast = default;

var first = new Dictionary<int, BridgeWorldActor>();
var farthest = new Dictionary<int, float>();
var largestStep = new Dictionary<int, float>();
var last = new Dictionary<int, BridgeWorldActor>();
var movedByBots = new HashSet<int>();
var lightSeen = new Dictionary<int, (float Min, float Max, int Flags)>();
var eventCounts = new SortedDictionary<int, int>();
var eventSamples = new Dictionary<int, List<string>>();
int frames = 0, ticks = 0, maxLasers = 0;
bool described = false, fired = false, brokeLights = false, movedToGlass = false;
double fireAt = Math.Min(8, seconds / 2);
double breakLightsAt = fireAt - 2;
var run = Stopwatch.StartNew();
double nextReport = 0;
long readBytes = 0;

while (run.Elapsed.TotalSeconds < seconds)
{
    int ran = session.Update();
    ticks += ran;
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

    if (!brokeLights && run.Elapsed.TotalSeconds >= breakLightsAt)
    {
        brokeLights = true;
        FVector near = session.Session.LocalPawn?.Location ?? default;
        Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} server: {OnServer(host => BreakLightsOnServer(host, near))}");
    }

    if (!movedToGlass && run.Elapsed.TotalSeconds >= fireAt - 1)
    {
        // A pane's state is replicated only while the pane is relevant to the player: stand the player next to one.
        movedToGlass = true;
        FVector near = session.Session.LocalPawn?.Location ?? default;
        Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} server: {OnServer(host => MovePlayerToGlass(host, near))}");
    }

    if (!fired && run.Elapsed.TotalSeconds >= fireAt)
    {
        fired = true;
        foreach (int id in farthest.Where(p => p.Value > 2f).Select(p => p.Key))
        {
            movedByBots.Add(id);
        }

        // The props nearest the local player are the ones damaged: the server replicates a prop's state only while
        // it is relevant to the player.
        FVector near = session.Session.LocalPawn?.Location ?? default;
        Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} server: {OnServer(host => FireOnServer(host, near))}");
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
    bool fast = fired && run.Elapsed.TotalSeconds >= fireAt + 0.3 && run.Elapsed.TotalSeconds < fireAt + 1.8;
    for (int i = 0; i < actorCount; i++)
    {
        BridgeWorldActor a = actors[i];
        if (!meshByEvents.ContainsKey(a.NameId))
        {
            meshByEvents[a.NameId] = a.MeshId;
            shownByEvents[a.NameId] = (a.Flags & BridgeWorldActorFlags.Visible) != 0;
            int[] worn = new int[WorldState.SkinSlots];
            world.GetActorSkins(a.NameId, worn);
            skinsByEvents[a.NameId] = worn;
            skinnedAtFirstSight += worn.Any(id => id != 0) ? 1 : 0;
            CheckPlacement(a);
        }

        if ((a.Flags & BridgeWorldActorFlags.Attached) != 0)
        {
            continue;
        }

        if (fast)
        {
            if (subject == 0 && (a.Flags & BridgeWorldActorFlags.Moving) != 0 &&
                world.TryGetActorInfo(a.NameId, out BridgeWorldActorInfo moverInfo) && (moverInfo.Flags & BridgeWorldActorInfoFlags.Mover) != 0)
            {
                subject = a.NameId;
                subjectLast = a;
            }
            else if (subject == a.NameId && (a.Flags & BridgeWorldActorFlags.Moving) != 0)
            {
                subjectFrames++;
                subjectTicks += ran;
                subjectPlaces += Distance(a, subjectLast) + AngleBetween(a, subjectLast) > 0f ? 1 : 0;
                subjectLast = a;
            }
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
        lightOnByEvents.TryAdd(l.NameId, (l.Flags & BridgeWorldLightFlags.On) != 0);
    }

    int laserCount = world.GetLasers(lasers);
    maxLasers = Math.Max(maxLasers, laserCount);
    int markerCount = Math.Min(world.GetMarkers(markers), markers.Length);
    for (int i = 0; i < markerCount; i++)
    {
        markerByEvents.TryAdd(markers[i].NameId, (markers[i].State, markers[i].Flags));
    }

    int animatedCount = Math.Min(world.GetAnimated(animated), animated.Length);
    int got;
    while ((got = world.DrainEvents(events)) > 0)
    {
        for (int i = 0; i < got; i++)
        {
            BridgeWorldEvent e = events[i];
            Apply(e);
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

    if (!fast)
    {
        Thread.Sleep(4);
    }
}

Console.WriteLine();
foreach (string warning in world.Render.Warnings)
{
    Console.WriteLine("RENDER FILE WARNING: " + warning);
}

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

Console.WriteLine(subject == 0
    ? "   blend between ticks: no mover was moving in the fast window"
    : $"   blend between ticks: {world.NameOf(subject)} read {subjectFrames} times over {subjectTicks} ticks while moving and stood at a new place or angle in {subjectPlaces} of those reads");
Console.WriteLine($"   placement: {placeChecked} placed, unmoved actors checked against the importer's formula (Location - PrePivot x Scale rotated, Unreal's rotation matrix): largest difference {placeError:F3} units, {turnError:F3} degrees{(placeWorst.Length > 0 ? " (" + placeWorst + ")" : "")}; the {pivotChecked} of them with a pivot: largest {pivotError:F3} units");
Console.WriteLine($"   skins: {skinnedAtFirstSight} actors already wore a skin when first seen (GetActorSkins; no event says those)");

// ---- lights ---------------------------------------------------------------------------------------------------
Console.WriteLine();
Console.WriteLine("-- lights that moved or hang on a bone:");
int placedLights = 0;
for (int i = 0, n = Math.Min(world.GetLights(lights), lights.Length); i < n; i++)
{
    BridgeWorldLight l = lights[i];
    if ((l.Flags & (BridgeWorldLightFlags.Moved | BridgeWorldLightFlags.OnBone)) != 0 && placedLights++ < 12)
    {
        RenderLight? placedAt = world.Render.FindLight(world.NameOf(l.NameId));
        Console.WriteLine((l.Flags & BridgeWorldLightFlags.OnBone) != 0
            ? $"   {world.NameOf(l.NameId),-24} on bone '{world.NameOf(l.AttachBoneId)}' of {world.NameOf(l.AttachNameId)}"
            : $"   {world.NameOf(l.NameId),-24} moved: now ({l.X:F0},{l.Y:F0},{l.Z:F0}) yaw {l.Yaw:F0}, placed ({placedAt?.Location.X:F0},{placedAt?.Location.Y:F0},{placedAt?.Location.Z:F0})");
    }
}

Console.WriteLine($"   {placedLights} lights moved or on a bone");
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

// ---- the events against the state -------------------------------------------------------------------------------
// One more read after the last drain: what the events added up to must be what the state says now.
int finalActors = Math.Min(world.GetActors(actors), actors.Length);
int finalLights = Math.Min(world.GetLights(lights), lights.Length);
int finalMarkerCount = Math.Min(world.GetMarkers(markers), markers.Length);
int late;
while ((late = world.DrainEvents(events)) > 0)
{
    for (int i = 0; i < late; i++)
    {
        Apply(events[i]);
    }
}

var mismatches = new List<string>();
var worn2 = new int[WorldState.SkinSlots];
for (int i = 0; i < finalActors; i++)
{
    BridgeWorldActor a = actors[i];
    string name = world.NameOf(a.NameId);
    if (!meshByEvents.TryGetValue(a.NameId, out int mesh))
    {
        continue;
    }

    if (mesh != a.MeshId)
    {
        mismatches.Add($"{name}: mesh by events '{world.NameOf(mesh)}', state '{world.NameOf(a.MeshId)}'");
    }

    if (shownByEvents[a.NameId] != ((a.Flags & BridgeWorldActorFlags.Visible) != 0))
    {
        mismatches.Add($"{name}: shown by events {shownByEvents[a.NameId]}, state flags {a.Flags}");
    }

    world.GetActorSkins(a.NameId, worn2);
    if (!worn2.AsSpan().SequenceEqual(skinsByEvents[a.NameId]))
    {
        mismatches.Add($"{name}: skins by events [{string.Join(",", skinsByEvents[a.NameId])}], state [{string.Join(",", worn2)}]");
    }
}

for (int i = 0; i < finalLights; i++)
{
    if (lightOnByEvents.TryGetValue(lights[i].NameId, out bool on) && on != ((lights[i].Flags & BridgeWorldLightFlags.On) != 0))
    {
        mismatches.Add($"{world.NameOf(lights[i].NameId)}: light on by events {on}, state flags {lights[i].Flags}");
    }
}

for (int i = 0; i < finalMarkerCount; i++)
{
    BridgeWorldMarker m = markers[i];
    if (markerByEvents.TryGetValue(m.NameId, out (int State, int Flags) said) && (said.State != m.State || said.Flags != m.Flags))
    {
        mismatches.Add($"{world.NameOf(m.NameId)}: marker by events state {said.State} flags {said.Flags}, state {m.State} flags {m.Flags}");
    }
}

Console.WriteLine();
Console.WriteLine($"-- events against state ({finalActors} actors' mesh, shown and skins, {finalLights} lights' on, {finalMarkerCount} markers' state and flags): {mismatches.Count} differences");
foreach (string line in mismatches.Take(12))
{
    Console.WriteLine("   DIFFERENT " + line);
}

// ---- glass and spawned props, from the client world itself ------------------------------------------------------
Level clientLevel = session.Session.Client.World.Level;
Console.WriteLine();
Console.WriteLine("-- glass panes on the client:");
foreach (SVitre pane in clientLevel.AllActors<SVitre>())
{
    int id = world.FindName(SimName(pane));
    bool broken = pane.bAlreadyDestroyed || pane.DrawType == EDrawType.DT_None || pane.StaticMesh.IsNone;
    BridgeWorldActor shown = actors.Take(finalActors).FirstOrDefault(a => a.NameId == id);
    Console.WriteLine($"   {world.NameOf(id),-16} broken on the client {broken}; bridge visible {(shown.Flags & BridgeWorldActorFlags.Visible) != 0}; GlassBroken event {(glassEvents.Contains(id) ? "seen" : "none")}");
}

int unfollowed = 0, duplicates = 0;
foreach (SBasicGamePlayObject prop in clientLevel.DynamicActors<SBasicGamePlayObject>())
{
    if (prop.bDeleteMe || prop.GetType() != typeof(SBasicGamePlayObject) || prop.StaticMesh.IsNone || world.IsFollowed(prop))
    {
        continue;
    }

    // A client can hold two copies of one prop (its own spawn and the server's): one is followed per owner.
    string twin = "";
    for (int i = 0; i < finalActors && twin.Length == 0; i++)
    {
        if (world.TryGetActorInfo(actors[i].NameId, out BridgeWorldActorInfo other) && (other.Flags & BridgeWorldActorInfoFlags.Spawned) != 0 &&
            world.NameOf(other.MeshId) == prop.StaticMesh.Path && FVector.DistSquared(new FVector(other.X, other.Y, other.Z), prop.Location) <= 9f)
        {
            twin = world.NameOf(other.NameId);
        }
    }

    if (twin.Length > 0)
    {
        duplicates++;
        Console.WriteLine($"   second copy of {twin} (not followed, as meant): mesh {prop.StaticMesh.Path} owner {prop.Owner?.GetType().Name ?? "none"} at {prop.Location}");
        continue;
    }

    unfollowed++;
    Console.WriteLine($"   SPAWNED PROP NOT FOLLOWED: mesh {prop.StaticMesh.Path} owner {prop.Owner?.GetType().Name ?? "none"} at {prop.Location}");
}

int followedSpawned = 0;
for (int i = 0; i < finalActors; i++)
{
    world.TryGetActorInfo(actors[i].NameId, out BridgeWorldActorInfo spawnedInfo);
    followedSpawned += (spawnedInfo.Flags & BridgeWorldActorInfoFlags.Spawned) != 0 ? 1 : 0;
}

Console.WriteLine($"   spawned plain props on the client: {followedSpawned} followed, {duplicates} second copies, {unfollowed} not followed");
for (int i = 0; i < finalActors; i++)
{
    if (world.TryGetActorInfo(actors[i].NameId, out BridgeWorldActorInfo spawned) && (spawned.Flags & BridgeWorldActorInfoFlags.Spawned) != 0)
    {
        Console.WriteLine($"   {world.NameOf(spawned.NameId),-28} mesh {world.NameOf(spawned.MeshId)} at ({actors[i].X:F0},{actors[i].Y:F0},{actors[i].Z:F0}) pitch {actors[i].Pitch:F0} flags {actors[i].Flags}");
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
// A fish can move and turn randomly before its first rendered frame. Its bind transform must still
// describe the authored pose, so the importer correction never contains part of its swimming motion.
int movedFromPlacement = 0;
foreach (BridgeWorldAnimated a in animated.Take(finalAnimated))
{
    string name = world.NameOf(a.NameId);
    var placedProp = session.Session.Client.Map?.Loaded?.Gameplay.FindActor(name);
    if (placedProp is null || !world.TryGetActorInfo(a.NameId, out var info) ||
        (new FVector(info.X, info.Y, info.Z) - placedProp.Location).Size() > 0.01f ||
        MathF.Abs(MathF.IEEERemainder(info.Yaw - placedProp.Rotation.Yaw * (360f / 65536f), 360f)) > 0.01f)
    {
        Console.WriteLine($"FAILED: skeletal placement for {name} differs from the map");
        return 1;
    }
    if ((new FVector(a.X, a.Y, a.Z) - placedProp.Location).Size() > 1f)
        movedFromPlacement++;
}
Console.WriteLine($"   checked {finalAnimated} authored prop transforms, {movedFromPlacement} props away from their original location");
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

// The name the bridge knows a client map actor by: looked up through the actor list's own infos, by placement.
string SimName(Actor actor)
{
    int n = Math.Min(world.GetActors(actors), actors.Length);
    for (int i = 0; i < n; i++)
    {
        if (world.TryGetActorInfo(actors[i].NameId, out BridgeWorldActorInfo info) &&
            MathF.Abs(info.X - actor.Location.X) < 1f && MathF.Abs(info.Y - actor.Location.Y) < 1f && MathF.Abs(info.Z - actor.Location.Z) < 1f &&
            world.NameOf(info.ClassId) == actor.GetType().Name)
        {
            return world.NameOf(info.NameId);
        }
    }

    return "";
}

// Runs <action> on the server's own thread, just before its next world step, and returns what it reports.
string OnServer(Func<ServerHost, string> action)
{
    string report = "the server did not run it in time";
    var done = new ManualResetEventSlim();
    void Run(ServerHost host)
    {
        host.BeforeWorldTick -= Run;
        try
        {
            report = action(host);
        }
        catch (Exception e)
        {
            report = "failed on the server: " + e;
        }

        done.Set();
    }

    session.Session.Host.BeforeWorldTick += Run;
    done.Wait(2000);
    return report;
}

// What the events add up to.
void Apply(in BridgeWorldEvent e)
{
    switch (e.Kind)
    {
        case BridgeWorldEventKind.MeshChanged:
            meshByEvents[e.NameId] = e.A;
            break;
        case BridgeWorldEventKind.ShownChanged:
            shownByEvents[e.NameId] = e.A != 0;
            break;
        case BridgeWorldEventKind.SkinChanged:
            if (skinsByEvents.TryGetValue(e.NameId, out int[]? worn) && (uint)e.A < (uint)worn.Length)
            {
                worn[e.A] = e.B;
            }

            break;
        case BridgeWorldEventKind.LightSwitched:
            lightOnByEvents[e.NameId] = e.A != 0;
            break;
        case BridgeWorldEventKind.MarkerChanged:
            markerByEvents[e.NameId] = (e.A, e.B);
            break;
        case BridgeWorldEventKind.GlassBroken:
            glassEvents.Add(e.NameId);
            break;
    }
}

// A placed actor that has not moved must stand exactly where Tools/Import/import_map.py put the level actor:
// instance location - RotateVector(prePivot x scale), with Unreal's own rotation matrix, written out here.
void CheckPlacement(in BridgeWorldActor a)
{
    if ((a.Flags & (BridgeWorldActorFlags.Moved | BridgeWorldActorFlags.Attached)) != 0 ||
        !world.Render.Instances.TryGetValue(world.NameOf(a.NameId), out RenderInstance? instance) || !instance.InLevel)
    {
        return;
    }

    const float toDegrees = 360f / 65536f;
    float pitch = instance.Rotation.Pitch * toDegrees, yaw = instance.Rotation.Yaw * toDegrees, roll = instance.Rotation.Roll * toDegrees;
    (float sp, float cp) = MathF.SinCos(pitch * MathF.PI / 180f);
    (float sy, float cy) = MathF.SinCos(yaw * MathF.PI / 180f);
    (float sr, float cr) = MathF.SinCos(roll * MathF.PI / 180f);
    float vx = instance.PrePivot.X * instance.Scale.X, vy = instance.PrePivot.Y * instance.Scale.Y, vz = instance.PrePivot.Z * instance.Scale.Z;
    // FRotationMatrix rows: X (cp cy, cp sy, sp), Y (sr sp cy - cr sy, sr sp sy + cr cy, -sr cp), Z (-(cr sp cy + sr sy), cy sr - cr sp sy, cr cp).
    float x = instance.Location.X - ((vx * cp * cy) + (vy * ((sr * sp * cy) - (cr * sy))) + (vz * -((cr * sp * cy) + (sr * sy))));
    float y = instance.Location.Y - ((vx * cp * sy) + (vy * ((sr * sp * sy) + (cr * cy))) + (vz * ((cy * sr) - (cr * sp * sy))));
    float z = instance.Location.Z - ((vx * sp) + (vy * -sr * cp) + (vz * cr * cp));
    float error = MathF.Sqrt(((a.X - x) * (a.X - x)) + ((a.Y - y) * (a.Y - y)) + ((a.Z - z) * (a.Z - z)));
    float turn = MathF.Abs(Wrap(a.Pitch - pitch)) + MathF.Abs(Wrap(a.Yaw - yaw)) + MathF.Abs(Wrap(a.Roll - roll));
    placeChecked++;
    turnError = MathF.Max(turnError, turn);
    if (vx != 0f || vy != 0f || vz != 0f)
    {
        pivotChecked++;
        pivotError = MathF.Max(pivotError, error);
    }

    if (error > placeError)
    {
        placeError = error;
        placeWorst = $"{world.NameOf(a.NameId)}: bridge ({a.X:F2},{a.Y:F2},{a.Z:F2}), formula ({x:F2},{y:F2},{z:F2}), pivot ({instance.PrePivot.X:F1},{instance.PrePivot.Y:F1},{instance.PrePivot.Z:F1}), rotation {instance.Rotation}";
    }
}

// Runs on the server thread: stands the pawn nearest <near> (the local player's) beside the nearest glass pane.
static string MovePlayerToGlass(ServerHost host, FVector near)
{
    Level level = host.World.Level;
    SBasePawn? player = null;
    foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
    {
        if (!pawn.bDeleteMe && (player is null || FVector.DistSquared(pawn.Location, near) < FVector.DistSquared(player.Location, near)))
        {
            player = pawn;
        }
    }

    SVitre? pane = null;
    foreach (SVitre v in level.AllActors<SVitre>())
    {
        if (!v.bDeleteMe && player is not null && (pane is null || FVector.DistSquared(v.Location, player.Location) < FVector.DistSquared(pane.Location, player.Location)))
        {
            pane = v;
        }
    }

    if (player is null || pane is null)
    {
        return "no glass pane (or no player pawn) on this map: the player stays";
    }

    foreach (FVector offset in new[] { new FVector(120f, 0f, 20f), new FVector(-120f, 0f, 20f), new FVector(0f, 120f, 20f), new FVector(0f, -120f, 20f) })
    {
        if (player.SetLocation(pane.Location + offset))
        {
            return $"stood the player at {player.Location}, beside the pane at {pane.Location}";
        }
    }

    return $"could not stand the player beside the pane at {pane.Location}";
}

// Runs on the server thread: the three lit trigger lights nearest <near> are broken as a shot-out or chaffed device
// breaks them (STriggerLight.LightOff(true): half a second of flicker, then dark).
static string BreakLightsOnServer(ServerHost host, FVector near)
{
    var lit = new List<STriggerLight>();
    foreach (STriggerLight l in host.World.Level.AllActors<STriggerLight>())
    {
        if (!l.bDeleteMe && l.LightType != ELightType.LT_None)
        {
            lit.Add(l);
        }
    }

    lit.Sort((a, b) => FVector.DistSquared(a.Location, near).CompareTo(FVector.DistSquared(b.Location, near)));
    int broken = 0;
    foreach (STriggerLight l in lit.Take(3))
    {
        l.LightOff(true);
        broken++;
    }

    return $"broke {broken} of {lit.Count} lit trigger lights (LightOff(true))";
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
        if (o is not SVitre && broken >= 12)
        {
            continue;
        }

        SDamageType type = o.TakeDamageTypes.Count > 0 ? o.TakeDamageTypes[0] : SDamageType.SDTBase;
        o.eventTakeDamage(10000, null, o.Location + new FVector(0f, 0f, 4f), new FVector(300f, 0f, 0f), type);
        glass += o is SVitre ? 1 : 0;
        broken += o is SVitre ? 0 : 1;
    }

    return $"fired {tags.Count} tags ({movers} movers, {triggerLights} trigger lights), damaged the nearest {broken} props and all {glass} glass panes of {targets.Count} damageable (player at {near})";
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
    BridgeWorldEventKind.LightBreaking => $"type {e.B} at ({e.X:F0},{e.Y:F0},{e.Z:F0})",
    BridgeWorldEventKind.MarkerChanged => $"state {e.A} flags {e.B}",
    BridgeWorldEventKind.MoverMoving => $"moving {e.A} key {e.B} at ({e.X:F0},{e.Y:F0},{e.Z:F0})",
    BridgeWorldEventKind.ShownChanged => $"shown {e.A}{(e.B != 0 ? " (destroyed)" : "")}",
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
