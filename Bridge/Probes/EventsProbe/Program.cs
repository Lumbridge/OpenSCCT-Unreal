using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Events;

// Usage: EventsProbe [contentRoot] [map] [seconds] [spy|merc]
string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 15;
bool merc = args.Length <= 3 || args[3] != "spy";

// Indices into BridgeInput.Buttons (LocalInput.ButtonNames).
const int Up = 0, Fire = 6, AltFire = 7;

BridgeSession.Configure(content);
var load = Stopwatch.StartNew();
using BridgeSession session = BridgeSession.StartLocal(map, "Story", merc, bots: 3);
EventsFeature events = EventsFeature.Of(session);
Console.WriteLine($"{map} loaded in {load.Elapsed.TotalSeconds:F1} s as {(merc ? "merc" : "spy")}; library {events.SoundCount} sounds, map audio table {events.HasMapAudio}, {events.EffectClassCount} effect classes");

// The record sizes Source/OpenSCCT/Runtime/ScctEventsApi.h asserts for its mirrors.
unsafe
{
    (string Name, int Size, int Expected)[] sizes =
    [
        (nameof(BridgeSimEvent), sizeof(BridgeSimEvent), 64), (nameof(BridgeSound), sizeof(BridgeSound), 100),
        (nameof(BridgeShot), sizeof(BridgeShot), 72), (nameof(BridgeEffectActor), sizeof(BridgeEffectActor), 132),
        (nameof(BridgeImpactMark), sizeof(BridgeImpactMark), 76), (nameof(BridgeAmbientSound), sizeof(BridgeAmbientSound), 44),
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

var rawByKind = new SortedDictionary<int, int>();
var notifies = new SortedDictionary<string, int>(StringComparer.Ordinal);
var soundOps = new SortedDictionary<int, int>();
var files = new Dictionary<int, int>();
var shotKinds = new SortedDictionary<int, int>();
var effectsSeen = new Dictionary<int, BridgeEffectActor>();
var markSerials = new Dictionary<int, int>();
var samples = new List<string>();
int raw = 0, late = 0, dropped = 0, sounds = 0, positional = 0, atListener = 0, loops = 0, sequences = 0, heard = 0;
int marksSeen = 0, effectDeleted = 0, missingFiles = 0, frames = 0, ticks = 0, ambientPeak = 0, lastTick = 0, outOfOrder = 0;
double drainMs = 0;

var run = Stopwatch.StartNew();
double nextReport = 2;
bool fireHeld = false, altHeld = false;
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
            Console.WriteLine($"   effect actor #{a.ActorId} '{EventNames.Text(a.ClassId)}' kind {a.Kind} flags {a.Flags} mesh '{EventNames.Text(a.MeshId)}' trail '{EventNames.Text(a.TrailClassId)}' base #{a.BaseActorId} bone '{EventNames.Text(a.BoneId)}' at ({a.X:F0},{a.Y:F0},{a.Z:F0}) scale ({a.ScaleX:F1},{a.ScaleY:F1},{a.ScaleZ:F1}) life {a.LifeSpan:F1} light {a.LightBrightness:F0}/{a.LightRadius:F0}");
        }

        effectsSeen[a.ActorId] = a;
        effectDeleted += (a.Flags & BridgeEffectFlags.Deleted) != 0 ? 1 : 0;
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
return missingFiles == 0 && outOfOrder == 0 ? 0 : 1;

static string Describe(BridgeSound s) => s.Op switch
{
    BridgeSoundOp.Play or BridgeSoundOp.PlayMusic =>
        $"{(s.Op == BridgeSoundOp.PlayMusic ? "music" : "play")} tick {s.Tick} call {s.CallId}.{s.Segment} actor #{s.ActorId} follow #{s.FollowActorId} bone '{EventNames.Text(s.FollowBoneId)}' '{EventNames.Text(s.SoundId)}' -> {EventNames.Text(s.FileId)} slot {s.Slot} flags {s.Flags} vol {s.Volume:F2} radius {s.Radius:F0} min {s.MinDistance:F0} pitch {s.Pitch:F2} delay {s.Delay:F2} for {s.PlaySeconds:F2} len {s.Duration:F2} fade {s.FadeSeconds:F1} at ({s.X:F0},{s.Y:F0},{s.Z:F0})",
    BridgeSoundOp.Stop => $"stop tick {s.Tick} actor #{s.ActorId} '{EventNames.Text(s.SoundId)}' flags {s.Flags}",
    BridgeSoundOp.StopCall => $"stop call {s.CallId} (actor #{s.ActorId} gone) '{EventNames.Text(s.SoundId)}'",
    _ => $"op {s.Op} tick {s.Tick} fade {s.FadeSeconds:F1}",
};
