using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Numerics;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Pose;

string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 10;

BridgeSession.Configure(content);
var load = Stopwatch.StartNew();
BridgeSession session = BridgeSession.StartLocal(map, "Story", merc: false, bots: 3);
Console.WriteLine($"{map} loaded in {load.Elapsed.TotalSeconds:F1} s");

for (int kind = 0; kind < 2; kind++)
{
    var read = Stopwatch.StartNew();
    if (session.Character(kind) is not { } character)
    {
        Console.WriteLine($"kind {kind}: no character file");
        continue;
    }

    var info = new BridgeBoneInfo[128];
    int boneCount = BridgeSession.GetSkeleton(character, info);
    Console.WriteLine($"kind {kind}: {System.IO.Path.GetFileName(character.Path)} {boneCount} bones, {character.ClipCount} clips, rotOrigin {character.RotOrigin}, meshScale {character.MeshScale}; bone 4 '{NameOf(info[4])}' parent {info[4].Parent}");
    CheckSampler(character);
}

var pawns = new BridgePawn[64];
var bones = new BridgeBone[128];
var again = new BridgeBone[128];
var last = new Dictionary<int, BridgeBone[]>();
var maxStep = new Dictionary<int, float>();
float worstLength = 0f;
var run = Stopwatch.StartNew();
double nextReport = 0;
int ticks = 0, frames = 0;
double poseMs = 0;
while (run.Elapsed.TotalSeconds < seconds)
{
    ticks += session.Update();
    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    // Every frame, as Unreal would: read each pawn's pose, and keep the largest change of any bone from one frame
    // to the next (a standing pawn must not jitter) and the rotation farthest from unit length.
    int count = session.GetPawns(pawns);
    frames++;
    long posing = Stopwatch.GetTimestamp();
    for (int i = 0; i < count; i++)
    {
        int id = pawns[i].ActorId;
        int n = session.GetPawnPose(id, bones);
        if (n == 0)
        {
            continue;
        }

        if (session.GetPawnPose(id, again) != n || !bones.AsSpan(0, n).SequenceEqual(again.AsSpan(0, n)))
        {
            Console.WriteLine($"FAILED: pawn {id}: a second read of the same frame gave another pose");
            return 1;
        }

        float step = 0f;
        bool had = last.TryGetValue(id, out BridgeBone[]? before);
        before ??= last[id] = new BridgeBone[128];
        for (int b = 0; b < n; b++)
        {
            Quaternion q = Rotation(bones[b]);
            worstLength = MathF.Max(worstLength, MathF.Abs(q.Length() - 1f));
            if (had)
            {
                step = MathF.Max(step, 2f * MathF.Acos(MathF.Min(1f, MathF.Abs(Quaternion.Dot(q, Rotation(before[b]))))) * (180f / MathF.PI));
            }

            before[b] = bones[b];
        }

        maxStep[id] = MathF.Max(maxStep.GetValueOrDefault(id), step);
    }

    poseMs += Stopwatch.GetElapsedTime(posing).TotalMilliseconds;

    if (run.Elapsed.TotalSeconds >= nextReport)
    {
        nextReport += 2;
        BridgeView view = session.GetView();
        Console.WriteLine($"t={run.Elapsed.TotalSeconds:F1} ticks={ticks} frames={frames} pawns={count} posing {poseMs / Math.Max(1, frames):F3} ms/frame, worst |q|-1 {worstLength:E1}; view valid={view.Valid} at ({view.X:F0},{view.Y:F0},{view.Z:F0}) yaw={view.Yaw:F0} fov={view.Fov:F0}");
        bool botShown = false;
        for (int i = 0; i < count; i++)
        {
            BridgePawn p = pawns[i];
            Console.WriteLine($"   #{p.ActorId} kind={p.Kind} team={p.Team} hp={p.Health} flags={p.Flags} at ({p.X:F0},{p.Y:F0},{p.Z:F0}) yaw={p.Yaw:F0} aim=({p.AimYaw:F1},{p.AimPitch:F1}) r={p.CollisionRadius:F0} h={p.CollisionHeight:F0}");
            bool local = (p.Flags & BridgePawnFlags.Local) != 0;
            if (!local && botShown)
            {
                continue;
            }

            botShown |= !local;
            int n = session.GetPawnPose(p.ActorId, bones);
            if (n == 0 || session.PoserOf(p.ActorId) is not { } poser)
            {
                Console.WriteLine("      no pose");
                continue;
            }

            ChannelPose c0 = poser.DrawnChannels[0];
            int blended = 0;
            foreach (ChannelPose ch in poser.DrawnChannels)
            {
                blended += ch.Clip is null ? 0 : 1;
            }

            Console.WriteLine($"      {(local ? "local" : "bot")}: channel 0 '{c0.Sequence}' frame {c0.Frame:F3} ({blended} channels blended), {n} bones, largest bone turn between frames since the last report {maxStep.GetValueOrDefault(p.ActorId):F2} deg");
            maxStep[p.ActorId] = 0f;
            foreach (string name in new[] { poser.Character.BoneNames[0], "B Spine2", "B R Hand" })
            {
                int b = poser.Character.FindBone(name);
                if (b >= 0 && b < n)
                {
                    BridgeBone t = bones[b];
                    Console.WriteLine($"         {name,-9} p=({t.Px:F4},{t.Py:F4},{t.Pz:F4}) q=({t.Qx:F4},{t.Qy:F4},{t.Qz:F4},{t.Qw:F4}) s=({t.Sx:F2},{t.Sy:F2},{t.Sz:F2})");
                }
            }
        }
    }

    Thread.Sleep(4);
}

session.Dispose();
bool tables = Tables.Run(content, map);
Console.WriteLine(tables ? "PROBE OK" : "PROBE FAILED");
return tables ? 0 : 1;

static Quaternion Rotation(BridgeBone b) => new(b.Qx, b.Qy, b.Qz, b.Qw);

static unsafe string NameOf(BridgeBoneInfo bone) => new(bone.Name);

// The sampler against the file: at time 0 of a clip a bone with tracks is at its first keys, a bone without is at rest.
static void CheckSampler(GlbCharacter character)
{
    int checkedClips = 0, withTracks = 0, atRest = 0;
    float worst = 0f;
    var load = Stopwatch.StartNew();
    foreach (string name in new[] { "WaitStAlFd0", "WaitStAlFd2", "JoggStAlFd0", "JoggStAlFd2", "WalkStSpFd2" })
    {
        if (character.Find(name) is not { } clip)
        {
            continue;
        }

        checkedClips++;
        var applier = new ChannelPoseApplier(character);
        var channels = new ChannelPose[1];
        channels[0] = new ChannelPose { Clip = clip, Sequence = name, Frames = 30, Fps = 30f, Alpha = 1f };
        applier.Apply(channels);
        var expectedPosition = (Vector3[])character.RestPosition.Clone();
        var expectedRotation = (Quaternion[])character.RestRotation.Clone();
        var tracked = new bool[character.BoneCount];
        foreach (GlbTrack track in clip.Tracks)
        {
            tracked[track.Bone] = true;
            if (track.Type == GlbTrackType.Position)
            {
                expectedPosition[track.Bone] = new Vector3(track.Values[0], track.Values[1], track.Values[2]);
            }
            else if (track.Type == GlbTrackType.Rotation)
            {
                expectedRotation[track.Bone] = new Quaternion(track.Values[0], track.Values[1], track.Values[2], track.Values[3]);
            }
        }

        for (int b = 0; b < character.BoneCount; b++)
        {
            withTracks += tracked[b] ? 1 : 0;
            atRest += tracked[b] ? 0 : 1;
            worst = MathF.Max(worst, (applier.Positions[b] - expectedPosition[b]).Length());
            worst = MathF.Max(worst, 1f - MathF.Abs(Quaternion.Dot(applier.Rotations[b], expectedRotation[b])));
        }
    }

    Console.WriteLine($"   sampler check: {checkedClips} clips at time 0, {withTracks} bones on their first keys, {atRest} at rest, worst difference {worst:E1} (first use of the clips took {load.Elapsed.TotalMilliseconds:F1} ms)");
}

/// <summary>
/// The main table as Unreal uses it (unmanaged function pointers, raw buffers): the safe hand-over, a start that
/// fails and does not stick, a local match with the start options, the session id and the stops that honour it,
/// teams and the hidden local body, and the shutdown stop.
/// </summary>
internal static unsafe class Tables
{
    public static bool Run(string content, string map)
    {
        Console.WriteLine();
        Console.WriteLine("==== the main table through its exported entries");
        (string Name, int Size, int Expected)[] sizes =
        [
            ("BridgePawn", sizeof(BridgePawn), 64),
            ("BridgeBoneInfo", sizeof(BridgeBoneInfo), 132),
            ("BridgeBone", sizeof(BridgeBone), 40),
            ("BridgeView", sizeof(BridgeView), 40),
            ("BridgeInput", sizeof(BridgeInput), 32),
            ("BridgeSessionInfo", sizeof(BridgeSessionInfo), 80),
            ("BridgeStartOptions", sizeof(BridgeStartOptions), 244),
            ("BridgeApi", sizeof(BridgeApi), 8 + 17 * sizeof(void*)),
        ];
        bool ok = true;
        foreach ((string name, int size, int expected) in sizes)
        {
            ok &= size == expected;
            Console.WriteLine($"layout: {name} {size} bytes{(size == expected ? string.Empty : $" BUT ScctBridgeApi.h ASSERTS {expected}")}");
        }

        int version = ((delegate* unmanaged<int>)&Exports.GetApiVersion)();
        BridgeApi api = default;
        int filled = ((delegate* unmanaged<BridgeApi*, int, int>)&Exports.GetApi)(&api, sizeof(BridgeApi));
        Console.WriteLine($"GetApiVersion {version}, GetApi {filled}, table version {api.Version}");
        ok &= version == BridgeSession.ApiVersion && filled == version && api.Version == version && api.Shutdown != null;

        char* error = stackalloc char[2048];
        fixed (char* c = content, missing = "NoSuchMap", mode = "Story")
        {
            // A start that fails: the state is Failed with a reason, and the next start is judged on its own.
            int failedStart = api.StartLocal(missing, mode, 0, 0);
            int failedState = api.GetState();
            api.LastError(error, 2048);
            string reason = new string(error);
            Console.WriteLine($"a start on a map that does not exist: returned {failedStart}, state {(BridgeState)failedState}, session {api.GetSession(null)}, error '{reason[..Math.Min(reason.Length, 90)].Replace("\r", string.Empty).Replace("\n", " ")}...'");
            ok &= failedStart == 0 && failedState == (int)BridgeState.Failed && reason.Length > 0;
            api.Stop();
            ok &= api.GetState() == (int)BridgeState.Idle;
            int configured = api.Configure(c);
            Console.WriteLine($"after Stop: state {(BridgeState)api.GetState()}; Configure again: {configured}");
            ok &= configured == 1;
        }

        BridgeStartOptions options = default;
        options.Size = sizeof(BridgeStartOptions);
        options.Merc = 1;
        options.Bots = 3;
        options.Flags = BridgeStartFlags.Sidearms;
        map.AsSpan(0, Math.Min(map.Length, BridgeStartOptions.MapCapacity - 1)).CopyTo(new Span<char>(options.Map, BridgeStartOptions.MapCapacity));
        "ProbeMerc".AsSpan().CopyTo(new Span<char>(options.PlayerName, BridgeStartOptions.NameCapacity));
        int id = api.StartLocalEx(&options);
        BridgeSessionInfo info;
        int asked = api.GetSession(&info);
        api.LastError(error, 2048);
        Console.WriteLine($"StartLocalEx (merc, 3 bots, sidearms): session {id}; GetSession {asked}: state {(BridgeState)info.State}, map '{new string(info.Map)}', remote {(info.Flags & BridgeSessionFlags.Remote) != 0}, merc {(info.Flags & BridgeSessionFlags.Merc) != 0}{(id == 0 ? ", error: " + new string(error) : string.Empty)}");
        ok &= id != 0 && asked == id && info.State == (int)BridgeState.Playing && (info.Flags & BridgeSessionFlags.Remote) == 0 &&
            (info.Flags & BridgeSessionFlags.Merc) != 0 && string.Equals(new string(info.Map), map, StringComparison.OrdinalIgnoreCase);
        if (id == 0)
        {
            return false;
        }

        BridgePawn* pawns = stackalloc BridgePawn[64];
        int ticks = 0, frames = 0, count = 0, hidden = 0, flagged = 0, wrongTeams = 0, spies = 0, mercs = 0, staleUpdates = 0;
        float worstAngle = 0f;
        BridgeView view = default;
        var run = Stopwatch.StartNew();
        while (run.Elapsed.TotalSeconds < 6)
        {
            // A world that holds another session's id drives nothing.
            staleUpdates += api.UpdateSession(id + 1000) == -1 ? 0 : 1;
            ticks += api.UpdateSession(id);
            frames++;
            count = api.GetPawns(pawns, 64);
            api.GetView(&view);
            hidden += view.HideLocalBody;
            worstAngle = MathF.Max(worstAngle, MathF.Max(MathF.Abs(view.Yaw), MathF.Max(MathF.Abs(view.Pitch), MathF.Abs(view.Roll))));
            int frameSpies = 0, frameMercs = 0;
            for (int i = 0; i < count; i++)
            {
                BridgePawn p = pawns[i];
                wrongTeams += p.Team == p.Kind ? 0 : 1;
                frameSpies += p.Kind == 0 ? 1 : 0;
                frameMercs += p.Kind == 1 ? 1 : 0;
                worstAngle = MathF.Max(worstAngle, MathF.Abs(p.Yaw));
                bool first = (p.Flags & BridgePawnFlags.FirstPerson) != 0;
                flagged += first ? 1 : 0;
                // The flag is the view's rule on the local pawn, and on no other.
                ok &= first == ((p.Flags & BridgePawnFlags.Local) != 0 && view.HideLocalBody != 0);
            }

            spies = Math.Max(spies, frameSpies);
            mercs = Math.Max(mercs, frameMercs);
            Thread.Sleep(4);
        }

        api.GetSession(&info);
        Console.WriteLine($"6 s through UpdateSession({id}): {ticks} ticks over {frames} frames (GetSession says {info.Ticks}), {count} pawns (at most {spies} spies and {mercs} mercs at once), pawns whose team is not their side's: {wrongTeams}, largest angle {worstAngle:F0} deg");
        Console.WriteLine($"   the local merc's body hidden in {hidden} of {frames} frames (first-person flag on a pawn in {flagged}); updates taken for another session's id: {staleUpdates}");
        ok &= ticks > 300 && Math.Abs(info.Ticks - ticks) <= 2 && spies == 2 && mercs == 2 && wrongTeams == 0 && worstAngle <= 180.01f &&
            hidden > 0 && flagged == hidden && staleUpdates == 0;

        int stale = api.StopSession(id + 1000);
        bool alive = api.GetSession(null) == id;
        int own = api.StopSession(id);
        Console.WriteLine($"StopSession for another id: {stale}, session still playing {alive}; StopSession({id}): {own}, state {(BridgeState)api.GetState()}, session {api.GetSession(null)}");
        ok &= stale == 0 && alive && own == 1 && api.GetState() == (int)BridgeState.Idle && api.GetSession(null) == 0;

        // The module's shutdown with a match still running.
        fixed (char* m = map, mode = "Story")
        {
            int again = api.StartLocal(m, mode, 0, 1);
            int second = api.GetSession(null);
            var stop = Stopwatch.StartNew();
            int shut = api.Shutdown(5000);
            Console.WriteLine($"a second match: StartLocal {again}, session {second} (a new id: {second != id && second != 0}); Shutdown: {shut} in {stop.Elapsed.TotalMilliseconds:F0} ms, state {(BridgeState)api.GetState()}, a second Shutdown: {api.Shutdown(5000)}");
            ok &= again == 1 && second != 0 && second != id && shut == 1 && api.GetState() == (int)BridgeState.Idle;
        }

        return ok;
    }
}
