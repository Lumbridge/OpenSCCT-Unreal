using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using OpenScct.Core;
using OpenScct.Server;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Events;

// Usage: EventsProbe [contentRoot] [map] [seconds] [spy|merc] [clean]   (clean: no forced dirty feet or flares)
string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 15;
bool merc = args.Length <= 3 || args[3] != "spy";
bool forceProjections = args.Length <= 4 || args[4] != "clean";

// Indices into BridgeInput.Buttons (LocalInput.ButtonNames).
const int Up = 0, Fire = 6, AltFire = 7;

BridgeSession.Configure(content);
var load = Stopwatch.StartNew();
BridgeSession session = BridgeSession.StartLocal(map, "Story", merc, bots: 3);

// An independent count of what the client world raises, taken before the feature starts: the sink that "was there"
// (the feature must pass every tick's events on to it) and the late events before each step. What the feature hands
// out must add up to exactly these, or it delivered something twice or lost it.
var witness = new Witness();
ClientSession client = session.Session.Client;
witness.Next = client.World.EventSink;
client.World.EventSink = witness;
client.BeforeWorldTick += witness.CountLate;
EventsFeature events = EventsFeature.Of(session);
Console.WriteLine($"{map} loaded in {load.Elapsed.TotalSeconds:F1} s as {(merc ? "merc" : "spy")}; library {events.SoundCount} sounds, map audio table {events.HasMapAudio}, {events.EffectClassCount} effect classes");

// The record sizes Source/OpenSCCT/Runtime/ScctEventsApi.h asserts for its mirrors.
unsafe
{
    (string Name, int Size, int Expected)[] sizes =
    [
        (nameof(BridgeSimEvent), sizeof(BridgeSimEvent), 64), (nameof(BridgeSound), sizeof(BridgeSound), 100),
        (nameof(BridgeShot), sizeof(BridgeShot), 72), (nameof(BridgeEffectActor), sizeof(BridgeEffectActor), 148),
        (nameof(BridgeImpactMark), sizeof(BridgeImpactMark), 76), (nameof(BridgeAmbientSound), sizeof(BridgeAmbientSound), 44),
        (nameof(BridgeMarkSurface), sizeof(BridgeMarkSurface), 32), // Runtime/ScctMarkSurfaceApi.h
        (nameof(BridgeProjection), sizeof(BridgeProjection), 96), // Runtime/ScctProjectionsApi.h
    ];
    Console.WriteLine("record sizes: " + string.Join(", ", sizes.Select(s => $"{s.Name} {s.Size}")));
    if (sizes.Any(s => s.Size != s.Expected))
    {
        Console.WriteLine("FAILED: a record's size differs from the C++ header's");
        return 1;
    }
}

var rawBuffer = new BridgeSimEvent[256];
var soundBuffer = new BridgeSound[256];
var shotBuffer = new BridgeShot[64];
var effectBuffer = new BridgeEffectActor[256];
var markBuffer = new BridgeImpactMark[64];
var ambientBuffer = new BridgeAmbientSound[512];
var projectionBuffer = new BridgeProjection[128];
var projectionSerials = new HashSet<int>();
var projectionClasses = new SortedDictionary<string, int>(StringComparer.Ordinal);
ProjectionFeature projections = ProjectionFeature.Of(session);
int projectionPeak = 0;

var rawByKind = new SortedDictionary<int, int>();
var notifies = new SortedDictionary<string, int>(StringComparer.Ordinal);
var soundOps = new SortedDictionary<int, int>();
var files = new Dictionary<int, int>();
var shotKinds = new SortedDictionary<int, int>();
var effectsSeen = new Dictionary<int, BridgeEffectActor>();
var markSerials = new Dictionary<int, int>();
var samples = new List<string>();
var endedEffects = new HashSet<int>();
int lightFades = 0, deletedTwice = 0, aliveAfterEnd = 0, drainedTick = 0, drainedLate = 0;
int raw = 0, late = 0, dropped = 0, sounds = 0, positional = 0, atListener = 0, loops = 0, sequences = 0, heard = 0;
int marksSeen = 0, effectDeleted = 0, missingFiles = 0, frames = 0, ticks = 0, ambientPeak = 0, lastTick = 0, outOfOrder = 0;
double drainMs = 0;

var run = Stopwatch.StartNew();
double nextReport = 2;
bool fireHeld = false, altHeld = false;
double lastDirt = -10, lastFlare = -10;
while (run.Elapsed.TotalSeconds < seconds)
{
    // The local player walks forward, turns and fires in bursts (the alternate fire for a second in every five).
    double t = run.Elapsed.TotalSeconds;
    var input = default(BridgeInput);
    unsafe
    {
        input.Buttons[Up] = 1;
        input.Buttons[Fire] = (byte)(t > 2 && t % 1.0 < 0.5 ? 1 : 0);
        input.Buttons[AltFire] = (byte)(t > 2 && t % 5.0 > 4.0 ? 1 : 0);
    }

    // A press is also an exec command, as the Godot client's input sends it.
    bool fire = t > 2 && t % 1.0 < 0.5, alt = t > 2 && t % 5.0 > 4.0;
    if (fire && !fireHeld)
    {
        session.Command("Fire", 0);
    }

    if (alt && !altHeld)
    {
        session.Command("AltFire", 0);
    }

    (fireHeld, altHeld) = (fire, alt);

    // Abandoned projections: dirty feet on every pawn of the client world from 3 s (footprints), and a flare thrown
    // ahead and down from the local pawn every 5 s (its burn mark), through the game's -ScctLevelDo entries.
    if (forceProjections && t > 3 && t - lastDirt > 4)
    {
        lastDirt = t;
        WorldDebugExports.DirtyFeet(session);
    }

    if (forceProjections && t > 4 && t - lastFlare > 5 && client.World.Level.Info.GetLocalPlayerController()?.Pawn is { } me)
    {
        lastFlare = t;
        FRotator aim = me.Rotation;
        aim.Pitch = -3000;
        WorldDebugExports.ThrowFlare(session, me.Location + new FVector(0f, 0f, 40f) + (aim.Vector() * 40f), aim.Vector());
    }
    input.MouseX = 3f;
    session.SetInput(in input);
    ticks += session.Update();
    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    frames++;
    long started = Stopwatch.GetTimestamp();

    // Each drain is called until it no longer fills its buffer, as Unreal would.
    int n;
    do
    {
        n = events.DrainEvents(rawBuffer);
        for (int i = 0; i < n; i++)
        {
            BridgeSimEvent e = rawBuffer[i];
            raw++;
            rawByKind[e.Kind] = rawByKind.GetValueOrDefault(e.Kind) + 1;
            late += (e.Flags & BridgeSimEventFlags.Late) != 0 ? 1 : 0;
            if ((e.Flags & BridgeSimEventFlags.Late) != 0)
            {
                drainedLate++;
            }
            else
            {
                drainedTick++;
            }

            dropped += (e.Flags & BridgeSimEventFlags.Dropped) != 0 ? 1 : 0;
            outOfOrder += e.Tick < lastTick ? 1 : 0;
            lastTick = Math.Max(lastTick, e.Tick);
            if (e.Kind == 3)
            {
                string name = EventNames.Text(e.WhatId);
                notifies[name] = notifies.GetValueOrDefault(name) + 1;
            }
        }
    }
    while (n == rawBuffer.Length);

    do
    {
        n = events.DrainSounds(soundBuffer);
        for (int i = 0; i < n; i++)
        {
            BridgeSound s = soundBuffer[i];
            soundOps[s.Op] = soundOps.GetValueOrDefault(s.Op) + 1;
            if (s.Op is BridgeSoundOp.Play or BridgeSoundOp.PlayMusic)
            {
                sounds++;
                positional += (s.Flags & BridgeSoundFlags.Positional) != 0 ? 1 : 0;
                atListener += (s.Flags & BridgeSoundFlags.AtListener) != 0 ? 1 : 0;
                loops += (s.Flags & BridgeSoundFlags.Loop) != 0 ? 1 : 0;
                heard += (s.Flags & BridgeSoundFlags.Heard) != 0 ? 1 : 0;
                sequences += s.Segment > 0 ? 1 : 0;
                if (!files.ContainsKey(s.FileId))
                {
                    // The file must be there for C++ to open: relative to the content root.
                    string path = Path.Combine(content, EventNames.Text(s.FileId));
                    bool exists = File.Exists(path);
                    missingFiles += exists ? 0 : 1;
                    if (!exists)
                    {
                        Console.WriteLine($"MISSING FILE {path}");
                    }
                }

                files[s.FileId] = files.GetValueOrDefault(s.FileId) + 1;
                bool interesting = s.Op == BridgeSoundOp.PlayMusic || s.Segment > 0 || (s.Flags & (BridgeSoundFlags.Loop | BridgeSoundFlags.Stream)) != 0;
                if (samples.Count < 14 || (interesting && samples.Count < 30))
                {
                    samples.Add(Describe(s));
                }
            }
            else if (samples.Count < 30)
            {
                samples.Add(Describe(s));
            }
        }
    }
    while (n == soundBuffer.Length);

    n = events.DrainShots(shotBuffer);
    for (int i = 0; i < n; i++)
    {
        BridgeShot s = shotBuffer[i];
        if (shotKinds.GetValueOrDefault(s.Kind) < 2)
        {
            Console.WriteLine(s.Kind == BridgeShotKind.Tracer
                ? $"   tracer #{s.SourceActorId} tick {s.Tick} shooter #{s.ShooterId} flags {s.Flags} from ({s.X:F0},{s.Y:F0},{s.Z:F0}) to ({s.ImpactX:F0},{s.ImpactY:F0},{s.ImpactZ:F0}) speed {s.Speed:F0} life {s.Life} hide {s.HideDistance}"
                : $"   muzzle kind {s.Kind} tick {s.Tick} weapon #{s.SourceActorId} '{EventNames.Text(s.WeaponClassId)}' shooter #{s.ShooterId} flags {s.Flags} at ({s.X:F0},{s.Y:F0},{s.Z:F0}) dir ({s.DirX:F2},{s.DirY:F2},{s.DirZ:F2})");
        }

        shotKinds[s.Kind] = shotKinds.GetValueOrDefault(s.Kind) + 1;
    }

    n = events.GetEffectActors(effectBuffer);
    for (int i = 0; i < n; i++)
    {
        BridgeEffectActor a = effectBuffer[i];
        if (!effectsSeen.ContainsKey(a.ActorId))
        {
            Console.WriteLine($"   effect actor #{a.ActorId} '{EventNames.Text(a.ClassId)}' kind {a.Kind} flags {a.Flags} mesh '{EventNames.Text(a.MeshId)}' trail '{EventNames.Text(a.TrailClassId)}' particle '{EventNames.Text(a.ParticleId)}' base #{a.BaseActorId} bone '{EventNames.Text(a.BoneId)}' at ({a.X:F0},{a.Y:F0},{a.Z:F0}) rel ({a.RelX:F0},{a.RelY:F0},{a.RelZ:F0}) rel rot ({a.RelPitch:F0},{a.RelYaw:F0},{a.RelRoll:F0}) scale ({a.ScaleX:F1},{a.ScaleY:F1},{a.ScaleZ:F1}) life {a.LifeSpan:F1} light {a.LightBrightness:F0} reach {a.LightRadius:F0} cm");
        }

        // A frag's flash light must fade (EffectViews.PlaceLight), not stay at its first brightness.
        if (a.Kind == BridgeEffectKind.Light && effectsSeen.TryGetValue(a.ActorId, out BridgeEffectActor before) &&
            a.LightBrightness < before.LightBrightness && lightFades++ < 3)
        {
            Console.WriteLine($"   light #{a.ActorId} '{EventNames.Text(a.ClassId)}' fades: {before.LightBrightness:F0} -> {a.LightBrightness:F0} at age {a.Age:F2} s");
        }

        // An ending must come exactly once, and never for an actor that was not told alive first.
        if ((a.Flags & BridgeEffectFlags.Deleted) != 0)
        {
            effectDeleted++;
            deletedTwice += endedEffects.Add(a.ActorId) ? 0 : 1;
        }
        else if (endedEffects.Contains(a.ActorId))
        {
            aliveAfterEnd++;
        }

        effectsSeen[a.ActorId] = a;
    }

    n = events.GetImpactMarks(markBuffer);
    for (int i = 0; i < n; i++)
    {
        BridgeImpactMark m = markBuffer[i];
        if (markSerials.GetValueOrDefault(m.Slot, -1) != m.Serial)
        {
            markSerials[m.Slot] = m.Serial;
            if (marksSeen++ < 4)
            {
                Console.WriteLine($"   impact slot {m.Slot} serial {m.Serial} '{EventNames.Text(m.TypeId)}' decal '{EventNames.Text(m.MaterialId)}' spark '{EventNames.Text(m.SparkClassId)}' mesh '{EventNames.Text(m.MeshId)}' sound '{EventNames.Text(m.SoundId)}' surface #{m.SurfaceActorId} flags {m.Flags} at ({m.X:F0},{m.Y:F0},{m.Z:F0}) rot ({m.Pitch:F0},{m.Yaw:F0},{m.Roll:F0}) scale {m.DrawScale:F2}");
            }
        }
    }

n = projections.Write(projectionBuffer);
    projectionPeak = Math.Max(projectionPeak, n);
    for (int i = 0; i < n; i++)
    {
        BridgeProjection p = projectionBuffer[i];
        if (projectionSerials.Add(p.Serial) && (projectionClasses[EventNames.Text(p.ClassId)] = projectionClasses.GetValueOrDefault(EventNames.Text(p.ClassId)) + 1) <= 2)
        {
            Console.WriteLine($"   projection {p.Serial} '{EventNames.Text(p.ClassId)}' texture '{EventNames.Text(p.TextureId)}' asset '{EventNames.Text(p.AssetId)}' surface #{p.SurfaceActorId} flags {p.Flags} at ({p.X:F0},{p.Y:F0},{p.Z:F0}) normal ({p.NormalX:F2},{p.NormalY:F2},{p.NormalZ:F2}) u ({p.UX:F2},{p.UY:F2},{p.UZ:F2}) {p.Width:F1} x {p.Height:F1} cm tint ({p.TintR:F2},{p.TintG:F2},{p.TintB:F2},{p.TintA:F2}) life {p.Life:F1} s");
        }
    }

    int ambient = events.GetAmbientSounds(ambientBuffer);
    ambientPeak = Math.Max(ambientPeak, ambient);
    drainMs += Stopwatch.GetElapsedTime(started).TotalMilliseconds;

    if (t >= nextReport)
    {
        nextReport += 4;
        Console.WriteLine($"t={t:F1} ticks={ticks} frames={frames}: {raw} raw events ({late} late, {dropped} dropped sounds), {sounds} files to play, shots {string.Join(' ', shotKinds.Select(k => $"{k.Key}:{k.Value}"))}, {effectsSeen.Count} effect actors, {marksSeen} marks, {ambient} ambient sources; reading {drainMs / frames:F3} ms/frame");
    }

    Thread.Sleep(4);
}

Console.WriteLine();
Console.WriteLine($"raw events by kind (1 PlaySound 2 SpawnEmitter 3 Notify 4 StopSound 5 PlayMusic 6 StopMusic 7 SetSwitch): {string.Join(' ', rawByKind.Select(k => $"{k.Key}:{k.Value}"))}; {late} late; {outOfOrder} out of tick order; feature total {events.EventsTotal}, dropped by full queues {events.EventsDropped}");
Console.WriteLine($"notifies: {string.Join(", ", notifies.Select(k => $"{k.Key} x{k.Value}"))}");
Console.WriteLine($"sound calls: {events.SoundCallsStarted} started, {events.SoundCallsSilent} silent, {events.SoundCallsDuplicate} duplicate copies, {events.SoundCallsOutOfReach} out of reach, {events.SoundCallsUnknown} unknown ({string.Join(", ", events.UnknownSounds.Take(8))}); StopSound: {events.SoundStops} passed on, {events.SoundIdleStops} with nothing to stop");
Console.WriteLine($"sound records by op (1 Play 2 Stop 3 PlayMusic 4 StopMusic 5 StopCall): {string.Join(' ', soundOps.Select(k => $"{k.Key}:{k.Value}"))}; {sounds} files: {positional} 3D, {atListener} at the listener, {loops} looping, {heard} from the server, {sequences} later sequence entries; {files.Count} distinct files, {missingFiles} missing on disk");
foreach (string line in samples)
{
    Console.WriteLine("   " + line);
}

Console.WriteLine("most played files:");
foreach (KeyValuePair<int, int> f in files.OrderByDescending(f => f.Value).Take(8))
{
    Console.WriteLine($"   x{f.Value} {EventNames.Text(f.Key)}");
}

Console.WriteLine($"shots (1 SMuzzleSticky 2 third-person muzzle 3 first-person muzzle 4 tracer): {string.Join(' ', shotKinds.Select(k => $"{k.Key}:{k.Value}"))}; feature counts {events.MuzzleFlashes} flashes, {events.Tracers} tracers");
Console.WriteLine($"effect actors: {effectsSeen.Count} seen, {effectDeleted} told deleted; by class: {string.Join(", ", events.EffectActorsSeen.Select(k => $"{k.Key} x{k.Value}"))}");
Console.WriteLine($"impact marks: {marksSeen} new marks seen");
Console.WriteLine($"abandoned projections: {projectionSerials.Count} seen (at most {projectionPeak} at once), feature placed {projections.Placed}, missed {projections.Missed}; by class: {string.Join(", ", projectionClasses.Select(k => $"{k.Key} x{k.Value}"))}");
int ambientCount = events.GetAmbientSounds(ambientBuffer);
Console.WriteLine($"ambient sources now {ambientCount} (peak {ambientPeak}):");
for (int i = 0; i < ambientCount; i++)
{
    BridgeAmbientSound a = ambientBuffer[i];
    bool exists = a.FileId != 0 && File.Exists(Path.Combine(content, EventNames.Text(a.FileId)));
    if (i < 10 || (a.Flags & BridgeAmbientFlags.ListenerInZone) != 0 || (a.FileId != 0 && !exists))
    {
        Console.WriteLine($"   key {a.Key} '{EventNames.Text(a.SoundId)}' file '{EventNames.Text(a.FileId)}'{(exists ? "" : " (NO FILE)")} flags {a.Flags} at ({a.X:F0},{a.Y:F0},{a.Z:F0}) vol {a.Volume:F2} radius {a.Radius:F0} pitch {a.Pitch:F2} repeat {a.RepeatInterval:F1}");
    }
}

Console.WriteLine($"name table: {EventNames.Count} names");

// Exactly once: the feature's records against the independent count.
bool exact = drainedTick == witness.TickEvents && drainedLate == witness.LateEvents && events.EventsDropped == 0;
Console.WriteLine($"exactly once: tick events drained {drainedTick} / raised {witness.TickEvents}, late events drained {drainedLate} / raised {witness.LateEvents}, " +
    $"{witness.Replaying} sink calls while replaying, effect endings told twice {deletedTwice}, alive after their ending {aliveAfterEnd} -> {(exact && deletedTwice == 0 && aliveAfterEnd == 0 ? "OK" : "FAILED")}");

// The managed session ends here; the native tables start a session of their own.
session.Dispose();
int native = NativeTable(content, map, merc);
return missingFiles == 0 && outOfOrder == 0 && exact && deletedTwice == 0 && aliveAfterEnd == 0 && native == 0 ? 0 : 1;

// The same feature through the function tables Unreal gets, called the way native code calls them (unmanaged
// function pointers), in a second session of the same process: Configure, StartLocal, Begin, a few seconds of
// Update with every drain and list, the name table, bad arguments, then Stop.
static unsafe int NativeTable(string content, string map, bool merc)
{
    BridgeApi bridge = default;
    EventsApi api = default;
    delegate* unmanaged<BridgeApi*, int, int> getBridge = &Exports.GetApiSized;
    delegate* unmanaged<EventsApi*, int> getEvents = &EventsExports.GetApi;
    int bridgeVersion = getBridge(&bridge, sizeof(BridgeApi));
    int version = getEvents(&api);
    int nullTable = getEvents(null);
    Console.WriteLine();
    Console.WriteLine($"native tables: bridge version {bridgeVersion}, events version {version} (table says {api.Version}), GetApi(null) -> {nullTable}");
    if (version != EventsExports.ApiVersion || api.Version != version || nullTable != 0)
    {
        Console.WriteLine("FAILED: the events table did not come back as version " + EventsExports.ApiVersion);
        return 1;
    }

    // Without a session every entry answers 0 and writes nothing.
    BridgeSimEvent* rawBuffer = stackalloc BridgeSimEvent[256];
    BridgeSound* soundBuffer = stackalloc BridgeSound[256];
    BridgeShot* shotBuffer = stackalloc BridgeShot[64];
    BridgeEffectActor* effectBuffer = stackalloc BridgeEffectActor[256];
    BridgeImpactMark* markBuffer = stackalloc BridgeImpactMark[64];
    BridgeAmbientSound* ambientBuffer = stackalloc BridgeAmbientSound[512];
    float* location = stackalloc float[3];
    int idle = api.Begin() + api.DrainEvents(rawBuffer, 256) + api.DrainSounds(soundBuffer, 256) + api.DrainShots(shotBuffer, 64) +
        api.GetEffectActors(effectBuffer, 256) + api.GetImpactMarks(markBuffer, 64) + api.GetAmbientSounds(ambientBuffer, 512) +
        api.GetActorLocation(1, location);
    Console.WriteLine($"   no session: all entries together returned {idle}");

    int started;
    fixed (char* c = content, m = map, mode = "Story")
    {
        bridge.Configure(c);
        started = bridge.StartLocal(m, mode, merc ? 1 : 0, 3);
    }

    if (started == 0)
    {
        char* error = stackalloc char[2048];
        bridge.LastError(error, 2048);
        Console.WriteLine("FAILED: StartLocal through the table: " + new string(error));
        return 1;
    }

    int begun = api.Begin();
    int raw = 0, sounds = 0, shots = 0, effects = 0, marks = 0, ambient = 0, ticks = 0, located = 0, followed = 0, firstFile = 0;
    var run = Stopwatch.StartNew();
    bool fired = false;
    while (run.Elapsed.TotalSeconds < 5)
    {
        if (!fired && run.Elapsed.TotalSeconds > 2.5)
        {
            fired = true;
            fixed (char* fire = "Fire")
            {
                bridge.Command(fire, 0);
            }
        }

        ticks += bridge.Update();
        int n;
        do
        {
            n = api.DrainEvents(rawBuffer, 256);
            raw += n;
        }
        while (n == 256);

        do
        {
            n = api.DrainSounds(soundBuffer, 256);
            sounds += n;
            for (int i = 0; i < n; i++)
            {
                if (soundBuffer[i].Op == BridgeSoundOp.Play && firstFile == 0)
                {
                    firstFile = soundBuffer[i].FileId;
                }

                if (soundBuffer[i].FollowActorId != 0)
                {
                    followed++;
                    located += api.GetActorLocation(soundBuffer[i].FollowActorId, location);
                }
            }
        }
        while (n == 256);

        shots += api.DrainShots(shotBuffer, 64);
        effects = Math.Max(effects, api.GetEffectActors(effectBuffer, 256));
        marks = Math.Max(marks, api.GetImpactMarks(markBuffer, 64));
        ambient = Math.Max(ambient, api.GetAmbientSounds(ambientBuffer, 512));
        Thread.Sleep(4);
    }

    // The name table: a full read, a buffer that is too small (cut, still terminated, full length returned), no
    // buffer at all, and an id that was never given out.
    char* name = stackalloc char[512];
    int length = api.GetName(firstFile, name, 512);
    string full = new(name);
    char* small = stackalloc char[8];
    int cutLength = api.GetName(firstFile, small, 8);
    string cut = new(small);
    int noBuffer = api.GetName(firstFile, null, 0);
    int unknown = api.GetName(int.MaxValue, name, 512);
    bool fileThere = full.Length > 0 && File.Exists(Path.Combine(content, full));
    int badArgs = api.DrainEvents(null, 256) + api.DrainSounds(soundBuffer, 0) + api.GetEffectActors(null, 4) + api.GetActorLocation(1, null);
    int state = bridge.GetState();
    Console.WriteLine($"   session through the table: StartLocal {started}, Begin {begun}, {ticks} ticks in 5 s, state {state}: {raw} raw events, {sounds} sound records, {shots} shots, " +
        $"up to {effects} effect actors, {marks} marks, {ambient} ambient sources; GetActorLocation found {located} of {followed} followed actors");
    Console.WriteLine($"   GetName({firstFile}) -> {length} chars '{full}' (file there: {fileThere}); into 8 chars -> {cutLength} '{cut}'; no buffer -> {noBuffer}; unknown id -> {unknown}; bad arguments -> {badArgs}");
    bridge.Stop();
    int after = api.DrainEvents(rawBuffer, 256) + api.DrainSounds(soundBuffer, 256) + api.GetEffectActors(effectBuffer, 256) + api.Begin();
    int nameAfter = api.GetName(firstFile, name, 512);
    Console.WriteLine($"   after Stop: entries returned {after}, state {bridge.GetState()}, GetName still {nameAfter} chars");

    bool ok = idle == 0 && begun == 1 && state == 1 && ticks > 200 && raw > 0 && sounds > 0 && ambient > 0 && length == full.Length && length > 0 && fileThere &&
        cutLength == length && cut.Length == 7 && full.StartsWith(cut, StringComparison.Ordinal) && noBuffer == length && unknown == 0 && badArgs == 0 &&
        after == 0 && nameAfter == length;
    Console.WriteLine(ok ? "   native table: OK" : "   native table: FAILED");
    return ok ? 0 : 1;
}

static string Describe(BridgeSound s) => s.Op switch
{
    BridgeSoundOp.Play or BridgeSoundOp.PlayMusic =>
        $"{(s.Op == BridgeSoundOp.PlayMusic ? "music" : "play")} tick {s.Tick} call {s.CallId}.{s.Segment} actor #{s.ActorId} follow #{s.FollowActorId} bone '{EventNames.Text(s.FollowBoneId)}' '{EventNames.Text(s.SoundId)}' -> {EventNames.Text(s.FileId)} slot {s.Slot} flags {s.Flags} vol {s.Volume:F2} radius {s.Radius:F0} min {s.MinDistance:F0} pitch {s.Pitch:F2} delay {s.Delay:F2} for {s.PlaySeconds:F2} len {s.Duration:F2} fade {s.FadeSeconds:F1} at ({s.X:F0},{s.Y:F0},{s.Z:F0})",
    BridgeSoundOp.Stop => $"stop tick {s.Tick} actor #{s.ActorId} '{EventNames.Text(s.SoundId)}' flags {s.Flags}",
    BridgeSoundOp.StopCall => $"stop call {s.CallId} (actor #{s.ActorId} gone) '{EventNames.Text(s.SoundId)}'",
    _ => $"op {s.Op} tick {s.Tick} fade {s.FadeSeconds:F1}",
};

/// <summary>Counts what the client world raises, apart from the feature: each tick's events and the late ones.</summary>
internal sealed class Witness : ISimEventSink
{
    private long _lateTick = -1;
    private int _lateTaken;

    public ISimEventSink? Next;
    public long TickEvents, LateEvents, Replaying;

    public void OnSimEvents(long tick, ReadOnlySpan<SimEvent> events)
    {
        Next?.OnSimEvents(tick, events);
        TickEvents += events.Length;
    }

    public void CountLate(ClientSession client)
    {
        World world = client.World;
        Replaying += world.IsReplaying ? 1 : 0;
        if (world.TickCount != _lateTick)
        {
            _lateTick = world.TickCount;
            _lateTaken = 0;
        }

        int count = world.Events.Late.Length;
        if (count > _lateTaken)
        {
            LateEvents += count - _lateTaken;
            _lateTaken = count;
        }
    }
}
