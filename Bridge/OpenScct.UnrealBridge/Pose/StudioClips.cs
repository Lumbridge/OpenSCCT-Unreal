using System;
using System.Collections.Generic;
using System.Numerics;
using OpenScct.Animation;

namespace OpenScct.UnrealBridge.Pose;

/// <summary>A per-player overlay on the shared, immutable stock clips. Track indices use the source skeleton's bone order.</summary>
public sealed class StudioClips(GlbCharacter character)
{
    private readonly Dictionary<string, GlbClip> _clips = new(StringComparer.OrdinalIgnoreCase);
    private AnimationProject? _project;
    private string? _target;
    private int _bindings = -1;

    public GlbClip? Find(string name) => _clips.TryGetValue(name, out GlbClip? clip) ? clip : character.Find(name);

    public static bool ApplyPose(AnimationProject project, SequencePose? pose, GlbCharacter character,
        Vector3[] positions, Quaternion[] rotations, Vector3[] scales)
    {
        if (pose is null || project.Skeletons.Find(s => s.Id == pose.Skeleton) is not { } rig) return false;
        for (int i = 0; i < rig.Bones.Count; i++)
        {
            int bone = character.FindBone(rig.Bones[i].Name);
            if (bone < 0) continue;
            BoneTransform value = pose.Bones[i];
            positions[bone] = value.Position;
            rotations[bone] = value.Rotation;
            scales[bone] = value.Scale;
        }
        return true;
    }

    public bool Bind(AnimationProject? project, string target)
    {
        int count = project?.Bindings.Count ?? 0;
        if (ReferenceEquals(_project, project) && _bindings == count && string.Equals(target, _target, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        _project = project;
        _target = target;
        _bindings = count;
        _clips.Clear();
        if (project is null) return true;

        foreach (AnimationBinding binding in project.Bindings)
        {
            if (binding.Clip is null || !target.Equals(binding.Target, StringComparison.OrdinalIgnoreCase) ||
                project.Clips.Find(c => c.Id == binding.Clip) is not { } source) continue;

            var descriptors = new List<GlbTrack>();
            var tracks = new List<BoneTrack>();
            foreach (BoneTrack track in source.Tracks)
            {
                int bone = character.FindBone(track.Bone);
                if (bone < 0 || track.Keys.Count == 0) continue;
                GlbTrackType type = track.Channel switch
                {
                    TrackChannel.Translation => GlbTrackType.Position,
                    TrackChannel.Rotation => GlbTrackType.Rotation,
                    _ => GlbTrackType.Scale,
                };
                descriptors.Add(new GlbTrack(bone, type, false, [], []));
                tracks.Add(track);
            }
            if (tracks.Count == 0) continue;

            float length = binding.Mode == ReplacementMode.Visual && character.Find(binding.Slot) is { Length: > 0f } original
                ? original.Length : source.Duration;
            _clips[binding.Slot] = new GlbClip(character, binding.Slot, length, source.Duration, descriptors.ToArray(), tracks.ToArray());
        }
        return true;
    }
}
