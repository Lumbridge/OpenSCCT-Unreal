using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Animation;
using OpenScct.Core.Engine;
using OpenScct.Server;
using OpenScct.UnrealBridge.Devices;

namespace OpenScct.UnrealBridge;

[StructLayout(LayoutKind.Sequential)]
public struct StudioPropBone
{
    public int NameId, Channels;
    public BridgeBone Transform;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct StudioPropApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, int, int, float, float, StudioPropBone*, int, int> Read;
}

public static unsafe class StudioPropExports
{
    private sealed class Player
    {
        public readonly Dictionary<int, (int Sequence, float Start)> Loops = new();
    }

    [UnmanagedCallersOnly]
    public static int GetApi(StudioPropApi* api)
    {
        if (api == null) return 0;
        api->Version = 1;
        api->Read = &Read;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Read(int actor, int mesh, int sequence, float frame, float originalDuration, StudioPropBone* output, int capacity)
    {
        try
        {
            if (Exports.Current is not { IsDisposed: false } session) return 0;
            var level = session.Session.Client.World.Level;
            if (level.GetService<AnimationWorld>() is not { } studio) return 0;
            WorldState names = WorldState.Of(session);
            string target = "actor:" + names.NameOf(actor);
            foreach (AnimationController controller in level.AllActors<AnimationController>())
            {
                if (controller.Pose(target, out string? id) is not { } pose ||
                    studio.Project.Skeletons.Find(s => s.Id == id) is not { } skeleton) continue;
                if (pose.Length <= capacity)
                {
                    for (int i = 0; i < pose.Length; i++)
                        output[i] = Bone(names.Intern(skeleton.Bones[i].Name), 7, pose[i]);
                }
                return pose.Length;
            }

            var binding = studio.Binding(names.NameOf(mesh), names.NameOf(sequence));
            if (binding?.Clip is null || studio.Project.Clips.Find(c => c.Id == binding.Clip) is not { } clip) return 0;
            float duration = binding.Mode == ReplacementMode.Visual && originalDuration > 0f ? originalDuration : clip.Duration;
            if (frame < 0f)
            {
                var player = session.Feature(static _ => new Player());
                float now = level.Info.TimeSeconds;
                if (!player.Loops.TryGetValue(actor, out var loop) || loop.Sequence != sequence || now < loop.Start)
                    player.Loops[actor] = loop = (sequence, now);
                frame = duration > 0f ? ((now - loop.Start) % duration) / duration : 0f;
            }
            int count = 0;
            foreach (BoneTrack track in clip.Tracks)
            {
                if (track.Keys.Count == 0) continue;
                if (count < capacity)
                {
                    bool rotation = track.Channel == TrackChannel.Rotation;
                    Vector4 v = ClipEvaluator.Sample(track.Keys, frame * clip.Duration, rotation);
                    int channel = rotation ? 2 : track.Channel == TrackChannel.Translation ? 1 : 4;
                    var transform = rotation ? BoneTransform.Identity with { Rotation = ClipEvaluator.SafeRotation(v) }
                        : channel == 1 ? BoneTransform.Identity with { Position = ClipEvaluator.Xyz(v) }
                        : BoneTransform.Identity with { Scale = ClipEvaluator.Xyz(v) };
                    output[count] = Bone(names.Intern(track.Bone), channel, transform);
                }
                count++;
            }
            return count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    private static StudioPropBone Bone(int name, int channels, BoneTransform v) => new()
    {
        NameId = name, Channels = channels,
        Transform = new BridgeBone
        {
            Px = v.Position.X, Py = v.Position.Y, Pz = v.Position.Z,
            Qx = v.Rotation.X, Qy = v.Rotation.Y, Qz = v.Rotation.Z, Qw = v.Rotation.W,
            Sx = v.Scale.X, Sy = v.Scale.Y, Sz = v.Scale.Z
        }
    };
}
