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
using BridgeSession session = BridgeSession.StartLocal(map, "Story", merc: false, bots: 3);
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

return 0;

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
