using System;
using System.Collections.Generic;
using OpenScct.Animation;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary>
/// The frame rate of each view model clip: a clip that the map's animation project binds to the model in gameplay mode
/// runs at that project clip's own frame rate (AnimationClip.FramesPerSecond), every other clip at the view models'
/// <see cref="ViewModelPose.DefaultFps"/>. A port of how the Godot client's AnimSampler.WithBindings sets
/// AnimClip.NativeFrameRate, which its ViewModelPose then reads; as there, a later binding of the same slot wins.
/// It reads the project and never writes it.
/// The pose samples the matching replacement keys through <see cref="Pose.StudioClips"/>.
/// </summary>
public sealed class ViewModelClipRates
{
    private readonly Dictionary<string, float> _rates = new(StringComparer.OrdinalIgnoreCase);
    private AnimationProject? _project;
    private string? _target;
    private int _bindingCount = -1;

    /// <summary>The project the rates were read from, or null.</summary>
    public AnimationProject? Project => _project;

    /// <summary>
    /// Reads the rates of <paramref name="project"/>'s gameplay bindings for <paramref name="target"/> (the model's
    /// asset, "SWeaponFirst.1DEF_01"); again only when the project, the target or the project's binding count changed.
    /// </summary>
    public void Bind(AnimationProject? project, string target)
    {
        if (ReferenceEquals(project, _project) && string.Equals(target, _target, StringComparison.OrdinalIgnoreCase) &&
            (project?.Bindings.Count ?? 0) == _bindingCount)
        {
            return;
        }

        _project = project;
        _target = target;
        _bindingCount = project?.Bindings.Count ?? 0;
        _rates.Clear();
        if (project is null)
        {
            return;
        }

        foreach (AnimationBinding binding in project.Bindings)
        {
            if (binding.Clip is null || !binding.Target.Equals(target, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            AnimationClip? source = project.Clips.Find(c => c.Id == binding.Clip);
            if (source is null)
            {
                continue;
            }

            if (binding.Mode == ReplacementMode.Gameplay && source.FramesPerSecond > 0f)
            {
                _rates[binding.Slot] = source.FramesPerSecond;
            }
            else
            {
                // A visual replacement keeps the stock rate (NativeFrameRate null in the Godot client).
                _rates.Remove(binding.Slot);
            }
        }
    }

    /// <summary>The frame rate of the clip named <paramref name="clip"/>; <paramref name="fromProject"/> when the project set it.</summary>
    public float RateOf(string clip, out bool fromProject)
    {
        fromProject = _rates.TryGetValue(clip, out float fps);
        return fromProject ? fps : ViewModelPose.DefaultFps;
    }
}
