using System;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// The heart sensor's cone in the spy radar and the two sounds it asks for, as the Godot client's HeartSensorDriver
/// (game/Scripts/Hud/Gadgets/GadgetAreaDrivers.cs) drives the Radar/Heart and Radar/Heart1 areas:
/// <list type="bullet">
/// <item>Hidden (not the selected gadget): nothing; the idle cone's clock stops if it was the one shown.</item>
/// <item>Idle (selected, weapon holstered): the idle cone at frame 0, stopped; drawing the weapon again resets the
/// detected cone's clock too.</item>
/// <item>Scanning: the idle cone plays (A_Heart, 20 frames at 10 a second); Detected: the red cone plays (A_Heart1,
/// 18 frames). A cone that takes over starts at the other's frame, so the wave carries on.</item>
/// <item>Each time the cone shown wraps to its start the scan ping (Snd_Check) plays; while something is detected the
/// heartbeat (Snd_DetectedPawn) plays at most every 1.5 s.</item>
/// </list>
/// The original's HUD plays both through the audio's HUD helper (no actor, flat, radius 4096); this raises the same
/// call as a PlaySound event of the client world after the tick, which the events feature takes with the next tick's
/// late events and resolves like any other sound. The detected mercs themselves are radar blips already
/// (<see cref="HudReader"/> adds a pawn with bDetectedByHBS as an enemy while the cone is in Detected).
/// </summary>
public sealed class HudHeartSensor : IDisposable
{
    /// <summary>Frames of the idle cone clip (A_Heart's onde3 goes to frame 0 at 20) and of the detected one (A_Heart1, 18).</summary>
    public const int IdleLoopFrames = 20;

    public const int DetectedLoopFrames = 18;

    /// <summary>Both clips play at 10 frames a second (the areas' frameRate).</summary>
    public const float FramesPerSecond = 10f;

    /// <summary>Radius of the HUD sound helper's calls (game 0x10b26920: PlaySound(None, ..., 4096, ...)).</summary>
    public const float HudSoundRadius = 4096f;

    private sealed class ConeClock
    {
        public float Frame;
        public bool Playing;

        public void Advance(float seconds, int loop)
        {
            if (!Playing)
            {
                return;
            }

            Frame += seconds * FramesPerSecond;
            while (Frame >= loop)
            {
                Frame -= loop;
            }
        }

        /// <summary>Native GetFrame (0x10ba48a0) rounds halfway up.</summary>
        public int Rounded => (int)MathF.Floor(Frame + .5f);
    }

    private readonly BridgeSession _bridge;
    private readonly PlaySession _session;
    private readonly ConeClock _idle = new();
    private readonly ConeClock _hit = new();
    private ConeClock? _shown;
    private float _lastLevelTime = -1f;
    private float _lastDetectedSound = -100f;
    private int _lastFrame;
    private bool _ended;

    private HudHeartSensor(BridgeSession bridge)
    {
        _bridge = bridge;
        _session = bridge.Session;
        bridge.Ticked += OnTicked;
    }

    /// <summary>The sensor driver of <paramref name="session"/>, started on first use (Unreal asks for it with the HUD's first read).</summary>
    public static HudHeartSensor Of(BridgeSession session) => session.Feature(static s => new HudHeartSensor(s));

    /// <summary>The cone shown after the last tick (as HudState.HeartCone: 0 hidden, 1 idle, 2 scanning, 3 detected).</summary>
    public HeartCone Cone { get; private set; }

    /// <summary>The shown cone's frame (0 up to its loop length) and that loop length; 0 frames when no cone is shown.</summary>
    public float Frame => _shown?.Frame ?? 0f;

    public int LoopFrames => ReferenceEquals(_shown, _hit) ? DetectedLoopFrames : ReferenceEquals(_shown, _idle) ? IdleLoopFrames : 0;

    /// <summary>Scan pings and heartbeats asked for so far, and how many of those calls had a sound to name.</summary>
    public int CheckSounds { get; private set; }

    public int DetectedSounds { get; private set; }

    public int SoundsRaised { get; private set; }

    public void Dispose()
    {
        if (_ended)
        {
            return;
        }

        _ended = true;
        _bridge.Ticked -= OnTicked;
    }

    private void OnTicked()
    {
        if (_ended || _session.LocalController is not { bDeleteMe: false } pc)
        {
            return;
        }

        float now = pc.Level.TimeSeconds;
        float dt = _lastLevelTime < 0f ? 0f : Math.Clamp(now - _lastLevelTime, 0f, 0.5f);
        _lastLevelTime = now;

        // Both clocks run while their cone is hidden (the original's captures show the hidden cone's phase moving on).
        _idle.Advance(dt, IdleLoopFrames);
        _hit.Advance(dt, DetectedLoopFrames);

        Cone = GadgetHudRules.Heart(pc);
        SGadHeartBeatSensor? sensor = pc.SBPawn?.SelectedGadget as SGadHeartBeatSensor;
        switch (Cone)
        {
            case HeartCone.Hidden:
                // Deselection stops the idle cone only when it was the one up; the detected cone keeps its phase.
                if (ReferenceEquals(_shown, _idle))
                {
                    _idle.Playing = false;
                }

                _shown = null;
                return;
            case HeartCone.Idle:
                _idle.Frame = 0f;
                _idle.Playing = false;
                _shown = _idle;
                // Holstering resets only the idle cone; with the weapon out (state 0 of the scan) both start again.
                if (sensor?.OwnerPawn is { WeaponLoaded: not 0 })
                {
                    _hit.Frame = 0f;
                    _hit.Playing = false;
                    _lastFrame = 0;
                }

                return;
            case HeartCone.Detected:
                ShowPlaying(_hit, _idle);
                if (GadgetHudRules.HeartDetectedSoundDue(now, _lastDetectedSound))
                {
                    _lastDetectedSound = now;
                    DetectedSounds++;
                    Play(sensor?.Snd_DetectedPawn);
                }

                break;
            default:
                ShowPlaying(_idle, _hit);
                break;
        }

        int frame = _shown!.Rounded;
        if (frame < _lastFrame)
        {
            CheckSounds++;
            Play(sensor?.Snd_Check);
        }

        _lastFrame = frame;
    }

    /// <summary>Shows <paramref name="to"/> playing; when it was not up and playing it starts at <paramref name="other"/>'s frame.</summary>
    private void ShowPlaying(ConeClock to, ConeClock other)
    {
        if (!ReferenceEquals(_shown, to) || !to.Playing)
        {
            to.Frame = other.Rounded;
        }

        to.Playing = true;
        _shown = to;
    }

    private void Play(AssetRef? sound)
    {
        if (sound is not { IsNone: false } s || s.Path is not { Length: > 0 } path)
        {
            return;
        }

        // The HUD helper's call: no actor, flat (not placed in the world), not attenuated, radius 4096.
        _session.Client.World.Events.PlaySound(null, path, radius: HudSoundRadius, attenuate: false, flat: true);
        SoundsRaised++;
    }
}
