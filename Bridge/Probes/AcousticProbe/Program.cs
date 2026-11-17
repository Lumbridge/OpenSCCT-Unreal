using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Game.Equipment;
using OpenScct.Physics.Collision;
using OpenScct.UnrealBridge;

// Usage: AcousticProbe [contentRoot] [map] [spy|merc] [--listener x,y,z] [--seconds S]
//
// Checks sound through walls the way Unreal uses it, through the function table AcousticExports.GetApi fills: a
// local match is started through the main table, the listener is the player's spawn view (or the one given), and a
// search over the map's zone openings near it finds two sources in the room beyond one opening: one seen through the
// opening, one behind the wall beside it. Both are run through Update for 1.5 s of frames in each mode and their
// results printed; the blocked one must be heard from the opening (a way of one hop), the open one straight with
// occlusion 0. Then the match runs with bots for a while and the hearing reach's shrink counts are printed.
// The last line gives the -ScctAudioProbe switch for the same two places in Unreal.
string content = args.Length > 0 && !args[0].StartsWith("--", StringComparison.Ordinal) ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 && !args[1].StartsWith("--", StringComparison.Ordinal) ? args[1] : "AquaE";
bool merc = args.Length > 2 && args[2] == "merc";
FVector? givenListener = null;
double seconds = 8;
for (int i = 0; i + 1 < args.Length; i++)
{
    if (args[i] == "--listener")
    {
        givenListener = ParseVector(args[i + 1]);
    }
    else if (args[i] == "--seconds")
    {
        seconds = double.Parse(args[i + 1], CultureInfo.InvariantCulture);
    }
}

return Probe.Run(content, map, merc, givenListener, seconds);

static FVector ParseVector(string text)
{
    string[] parts = text.Split(',');
    return new FVector(float.Parse(parts[0], CultureInfo.InvariantCulture), float.Parse(parts[1], CultureInfo.InvariantCulture),
        float.Parse(parts[2], CultureInfo.InvariantCulture));
}

internal static unsafe class Probe
{
    private const ETraceFlags Wall = ETraceFlags.TRACE_LevelGeometry | ETraceFlags.TRACE_Level | ETraceFlags.TRACE_Movers;

    public static int Run(string content, string map, bool merc, FVector? givenListener, double seconds)
    {
        BridgeApi bridge = default;
        AcousticApi api = default;
        delegate* unmanaged<BridgeApi*, int, int> getBridge = &Exports.GetApiSized;
        delegate* unmanaged<AcousticApi*, int> getAcoustic = &AcousticExports.GetApi;
        getBridge(&bridge, sizeof(BridgeApi));
        int version = getAcoustic(&api);
        Console.WriteLine($"acoustic table version {version} (table says {api.Version}); record sizes: source {sizeof(BridgeAcousticSource)}, result {sizeof(BridgeAcousticResult)}, info {sizeof(BridgeAcousticInfo)}");
        if (version != AcousticExports.ApiVersion || sizeof(BridgeAcousticSource) != 32 || sizeof(BridgeAcousticResult) != 60 || sizeof(BridgeAcousticInfo) != 48)
        {
            Console.WriteLine("FAILED: the table's version or a record's size differs from Runtime/ScctAcousticApi.h");
            return 1;
        }

        float* listenerBuffer = stackalloc float[3];
        BridgeAcousticSource* sources = stackalloc BridgeAcousticSource[2];
        BridgeAcousticResult* results = stackalloc BridgeAcousticResult[2];
        int idle = api.Update(listenerBuffer, 0.016f, 0, sources, 0, results);
        Console.WriteLine($"no session: Update -> {idle}");

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
            Console.WriteLine("FAILED: StartLocal: " + new string(error));
            return 1;
        }

        // A second of ticks, so the level, its collision and the player's view are there.
        var clock = System.Diagnostics.Stopwatch.StartNew();
        while (clock.Elapsed.TotalSeconds < 1.0)
        {
            bridge.Update();
        }

        BridgeView view = default;
        bridge.GetView(&view);
        FVector listener = givenListener ?? new FVector(view.X, view.Y, view.Z);
        ICollisionWorld? collision = Exports.Current is { } session ? GadgetWorld.Collision(session.Session.Client.World.Level.Info) : null;
        if (collision is null)
        {
            Console.WriteLine("FAILED: the session has no collision");
            return 1;
        }

        BridgeAcousticInfo info = default;
        api.GetInfo(&info);
        Console.WriteLine($"{map}: {info.Portals} openings, {info.Zones} zones in the distance table; listener {(givenListener is null ? "spawn view" : "given")} at {Show(listener)} in zone {collision.GetZoneIndex(listener)}");

        if (!FindPair(collision, Path.Combine(content, "svmap", map, "render", "lighting.json"), listener, out FVector open, out FVector blocked,
            out FVector doorway, out int portalIndex))
        {
            Console.WriteLine("FAILED: no opening near the listener has a room beyond it with a wall beside the opening");
            return 1;
        }

        Console.WriteLine($"opening {portalIndex} centred at {Show(doorway)}: source in sight {Show(open)} (zone {collision.GetZoneIndex(open)}), behind the wall {Show(blocked)} (zone {collision.GetZoneIndex(blocked)})");

        bool ok = true;
        listenerBuffer[0] = listener.X;
        listenerBuffer[1] = listener.Y;
        listenerBuffer[2] = listener.Z;
        foreach ((int Mode, string Name) pass in new[] { (0, "material transmission"), (1, "original (rule 18)"), (2, "off") })
        {
            // Fresh keys per mode, so each starts from nothing.
            sources[0] = new BridgeAcousticSource { Key = 100 + pass.Mode, X = blocked.X, Y = blocked.Y, Z = blocked.Z, MinDistance = 300f, Radius = 60f };
            sources[1] = new BridgeAcousticSource { Key = 200 + pass.Mode, X = open.X, Y = open.Y, Z = open.Z, MinDistance = 300f, Radius = 60f };
            for (int frame = 0; frame < 90; frame++)
            {
                api.Update(listenerBuffer, 1f / 60f, pass.Mode, sources, 2, results);
            }

            Console.WriteLine($"-- mode {pass.Mode}, {pass.Name}, after 1.5 s:");
            Console.WriteLine("   behind the wall: " + Show(results[0], listener));
            Console.WriteLine("   in sight:        " + Show(results[1], listener));
            if (pass.Mode == 0)
            {
                var heardFrom = new FVector(results[0].X, results[0].Y, results[0].Z);
                bool atOpening = FVector.Dist(heardFrom, doorway) < 400f && collision.GetZoneIndex(heardFrom) != 0;
                bool behindOk = (results[0].Flags & 1) != 0 && results[0].Occlusion > 0f && results[0].Hops >= 1 && atOpening &&
                    results[0].Distance > FVector.Dist(listener, blocked);
                bool openOk = (results[1].Flags & 1) == 0 && results[1].Occlusion == 0f && results[1].Hops == 0 && results[1].LowPassHz >= 20000f &&
                    results[1].VolumeScale > 0.999f && FVector.Dist(new FVector(results[1].X, results[1].Y, results[1].Z), open) < 1f;
                Console.WriteLine($"   behind: blocked, occluded, heard from the opening ({FVector.Dist(heardFrom, doorway):F0} uu from its centre) by a longer way: {behindOk}; in sight: straight, unfiltered: {openOk}");
                ok &= behindOk && openOk;
            }
            else if (pass.Mode == 1)
            {
                bool shrunk = results[0].OccludedMinDistance < 300f && results[0].VolumeScale < 1f && results[0].LowPassHz >= 20000f &&
                    results[1].OccludedMinDistance == 300f;
                Console.WriteLine($"   behind: minimum distance shrunk, quieter, not filtered, not moved: {shrunk}");
                ok &= shrunk;
            }
            else
            {
                bool flat = results[0].VolumeScale == 1f && results[0].LowPassHz >= 20000f && results[0].Hops == 0;
                Console.WriteLine($"   walls change nothing: {flat}");
                ok &= flat;
            }
        }

        // The settle of rule 18: the minimum distance moves a part of the way each frame and is there after 1 s.
        sources[0] = new BridgeAcousticSource { Key = 300, X = open.X, Y = open.Y, Z = open.Z, MinDistance = 300f, Radius = 60f };
        api.Update(listenerBuffer, 1f / 60f, 1, sources, 1, results);
        sources[0].X = blocked.X;
        sources[0].Y = blocked.Y;
        sources[0].Z = blocked.Z;
        var settle = new List<string>();
        for (int frame = 1; frame <= 70; frame++)
        {
            api.Update(listenerBuffer, 1f / 60f, 1, sources, 1, results);
            if (frame is 1 or 15 or 30 or 45 or 59 or 61 or 70)
            {
                settle.Add($"{frame / 60f:F2}s {results[0].OccludedMinDistance:F0}");
            }
        }

        Console.WriteLine("rule 18 settle when the source goes behind the wall (minimum distance): " + string.Join(", ", settle));

        // The hearing reach: bot calls behind walls get the shrunk radius.
        clock.Restart();
        while (clock.Elapsed.TotalSeconds < seconds)
        {
            bridge.Update();
        }

        api.GetInfo(&info);
        Console.WriteLine($"after {seconds:F0} s with bots: {info.ReachShrunk} attenuated calls had their reach shrunk by a wall, {info.ReachDropped} of them put out of reach; {info.LineChecks} line checks, {info.PortalProbes} opening probes, {info.TrackedVoices} voices tracked");

        bridge.Stop();
        Console.WriteLine(FormattableString.Invariant($"Unreal: -ScctAudioProbe=\"{blocked.X:F0},{blocked.Y:F0},{blocked.Z:F0};{open.X:F0},{open.Y:F0},{open.Z:F0}\" (listener {listener.X:F0},{listener.Y:F0},{listener.Z:F0})"));
        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    /// <summary>
    /// An opening of the listener's zone with nothing between the listener and it, a point in sight beyond it, and a
    /// point beside that one, in the same room, that a wall hides from the listener.
    /// </summary>
    private static bool FindPair(ICollisionWorld collision, string lighting, FVector listener, out FVector open, out FVector blocked,
        out FVector doorway, out int portalIndex)
    {
        open = blocked = doorway = default;
        portalIndex = -1;
        int zone = collision.GetZoneIndex(listener);
        using JsonDocument json = JsonDocument.Parse(File.ReadAllBytes(lighting));
        var candidates = new List<(float Distance, int Index, FVector Center, FVector Normal)>();
        int index = 0;
        foreach (JsonElement portal in json.RootElement.GetProperty("fogPortals").EnumerateArray())
        {
            int back = portal.GetProperty("back").GetInt32(), front = portal.GetProperty("front").GetInt32();
            JsonElement plane = portal.GetProperty("plane");
            FVector center = default;
            int count = 0;
            foreach (JsonElement v in portal.GetProperty("vertices").EnumerateArray())
            {
                center += new FVector(v[0].GetSingle(), v[1].GetSingle(), v[2].GetSingle());
                count++;
            }

            center /= Math.Max(count, 1);
            if (back == zone || front == zone)
            {
                candidates.Add((FVector.Dist(center, listener), index,
                    center, new FVector(plane[0].GetSingle(), plane[1].GetSingle(), plane[2].GetSingle()).SafeNormal()));
            }

            index++;
        }

        candidates.Sort((a, b) => a.Distance.CompareTo(b.Distance));
        foreach ((float _, int Index, FVector Center, FVector Normal) c in candidates)
        {
            // At ear height of the listener, or the opening's middle when that is out of it.
            FVector n = ((c.Center - listener) | c.Normal) < 0f ? -c.Normal : c.Normal;
            FVector door = c.Center;
            if (!Clear(collision, listener, door))
            {
                continue;
            }

            FVector tangent = (n ^ new FVector(0f, 0f, 1f)).SafeNormal();
            foreach (float depth in new[] { 200f, 320f, 480f })
            {
                FVector o = door + (n * depth);
                int beyond = collision.GetZoneIndex(o);
                if (beyond == 0 || beyond == zone || !Clear(collision, listener, o))
                {
                    continue;
                }

                foreach (float side in new[] { 300f, -300f, 450f, -450f, 600f, -600f })
                {
                    FVector w = o + (tangent * side);
                    if (collision.GetZoneIndex(w) == beyond && Clear(collision, o, w) && !Clear(collision, listener, w))
                    {
                        open = o;
                        blocked = w;
                        doorway = door;
                        portalIndex = c.Index;
                        return true;
                    }
                }
            }
        }

        return false;
    }

    private static bool Clear(ICollisionWorld collision, FVector from, FVector to) =>
        !collision.LineCheck(out FCheckResult hit, null, to, from, default, Wall) || !hit.bHit;

    private static string Show(FVector v) => FormattableString.Invariant($"({v.X:F0}, {v.Y:F0}, {v.Z:F0})");

    private static string Show(in BridgeAcousticResult r, FVector listener) => FormattableString.Invariant(
        $"flags {r.Flags} occlusion {r.Occlusion:F2} min {r.OccludedMinDistance:F0} gain {r.DirectGain:F2} HF {r.DirectHF:F2} LPF {r.LowPassHz:F0} Hz volume x{r.VolumeScale:F2} heard from {Show(new FVector(r.X, r.Y, r.Z))} ({FVector.Dist(new FVector(r.X, r.Y, r.Z), listener):F0} uu away) way {r.Distance:F0} uu, {r.Hops} hop(s), mix {r.PropagationMix:F2}, zone {r.SourceZone}");
}
