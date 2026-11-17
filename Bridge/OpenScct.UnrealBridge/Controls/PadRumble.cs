using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Combat;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Controls;

/// <summary>
/// One rumble of the original's <c>SPlayerController.Vibration_*</c> set: its priority for
/// <c>Vibration_CheckSiPossible</c>, the two motor strengths (0 to 65535: the low-frequency left motor, the
/// high-frequency right one) and how long it lasts. Ported from OpenSCCT game/Scripts/Options/PadRumble.cs.
/// </summary>
public readonly record struct RumbleShape(int Priority, int Low, int High, float Seconds);

/// <summary>
/// The rules of the controller's vibration (rl:input.rumble), without any device: the shape of each Vibration_*
/// notification (the strengths and lengths of the script's NativeRumblePadStart calls) and
/// <c>Vibration_CheckSiPossible</c>, which lets a new rumble start when the last one has run out or when it ranks
/// higher. Ported from OpenSCCT game/Scripts/Options/PadRumble.cs; the device part is Unreal's (Play/ScctRumble).
/// </summary>
public sealed class PadRumbleRules
{
    private static readonly Dictionary<Name, RumbleShape> Shapes = new()
    {
        [CombatFx.Vibration_CoupParDefaut] = new(1, 65535, 32000, 0.1f),
        [CombatFx.Vibration_Tir] = new(2, 0, 32000, 0.12f),
        [CombatFx.Vibration_Gadget] = new(2, 0, 19200, 0.12f),
        [CombatFx.Vibration_SautHaut] = new(3, 65535, 0, 0.2f),
        [CombatFx.Vibration_Bousculade] = new(5, 65535, 0, 0.35f),
        [CombatFx.Vibration_Blast] = new(7, 65535, 65535, 0.35f),
        [CombatFx.Vibration_Grab_Kill] = new(5, 65535, 0, 0.2f),
        [CombatFx.Vibration_Charge_Course] = new(1, 40000, 0, 0.1f),
        [CombatFx.Vibration_Charge_Obstacle] = new(3, 65535, 0, 0.2f),
    };

    /// <summary>Reloaded's trigger test and the Options screen's TEST: the blast's rumble (both motors, 0.35 s).</summary>
    public static readonly RumbleShape Test = new(7, 65535, 65535, 0.35f);

    /// <summary>Vibration_StickyShock: priority 5, (20000, 65535), for the freeze's length (the event's first parameter).</summary>
    public static RumbleShape StickyShock(float seconds) => new(5, 20000, 65535, seconds);

    /// <summary>Vibration_Grabbed: priority 1 for 0.2 s, motors 2000 + 0..5000 and 5000 + 0..10000 drawn each time.</summary>
    public static RumbleShape Grabbed(float r1, float r2) => new(1, (int)(2000f + r1 * 5000f), (int)(5000f + r2 * 10000f), 0.2f);

    private readonly Random _random = new();
    private int _priority = -1;
    private float _until;

    /// <summary>The rumble shape of a notification, or null when it is not a rumble.</summary>
    public RumbleShape? ShapeOf(in SimEvent e)
    {
        if (e.What == CombatFx.Vibration_StickyShock)
        {
            return StickyShock(e.Location.X);
        }

        if (e.What == CombatFx.Vibration_Grabbed)
        {
            return Grabbed((float)_random.NextDouble(), (float)_random.NextDouble());
        }

        return Shapes.TryGetValue(e.What, out RumbleShape s) ? s : null;
    }

    /// <summary>Vibration_CheckSiPossible: true (and the rumble taken) when the last has run out or this one ranks higher.</summary>
    public bool Take(RumbleShape shape, float now)
    {
        if (now > _until || shape.Priority > _priority)
        {
            _priority = shape.Priority;
            _until = now + shape.Seconds;
            return true;
        }

        return false;
    }

    /// <summary>Vibration_StopTout: forgets the rumble running.</summary>
    public void Reset()
    {
        _priority = -1;
        _until = 0f;
    }
}

/// <summary>Why a rumble of <see cref="BridgeRumble"/> was or was not played.</summary>
public static class BridgeRumbleResult
{
    public const int Played = 0;
    public const int VibrationOff = 1;
    public const int ControllerOff = 2;
    public const int Outranked = 3;
    public const int RoundEnded = 4;
}

/// <summary>
/// One rumble for Unreal to play on the controller in use. Source/OpenSCCT/Runtime/ScctPadApi.h mirrors it field for
/// field (92 bytes).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeRumble
{
    public const int NameLength = 32;

    public int Priority;

    /// <summary>The low-frequency (left, large) motor, 0 to 1, already times <see cref="Strength"/>.</summary>
    public float Low;

    /// <summary>The high-frequency (right, small) motor, 0 to 1, already times <see cref="Strength"/>.</summary>
    public float High;

    public float Seconds;

    /// <summary>The strength setting the motors were scaled by.</summary>
    public float Strength;

    /// <summary>1 when the controller should play it; 0 when the settings or a stronger rumble running hold it back.</summary>
    public int Play;

    /// <summary><see cref="BridgeRumbleResult"/>.</summary>
    public int Result;

    /// <summary>The notification's name ("Vibration_Blast"), zero terminated.</summary>
    public fixed char Name[NameLength];
}

/// <summary>
/// The controller's vibration in a match: the local player's <c>Vibration_*</c> notifications (raised by the
/// simulation as CombatFx notifications on the player's controller or pawn), with the settings' on/off and strength
/// and the priority rule applied, kept for Unreal to take each frame (PadExports.DrainRumbles). It reads the world's
/// events as the HUD does (HudFeature): the late ones right before each step, the tick's own after it, so a rumble
/// raised by an input command or a server call between two ticks is not missed.
/// </summary>
public sealed class RumbleFeature : IDisposable
{
    private const int QueueCapacity = 64;

    private readonly BridgeSession _bridge;
    private readonly PlaySession _session;
    private readonly PadRumbleRules _rules = new();
    private readonly List<BridgeRumble> _queue = new();
    private SPlayerController? _lastController;
    private bool _ended;

    private RumbleFeature(BridgeSession bridge)
    {
        _bridge = bridge;
        _session = bridge.Session;
        _session.Client.BeforeWorldTick += OnBeforeWorldTick;
        bridge.Ticked += OnTicked;
    }

    /// <summary>The session's rumble, started on first use.</summary>
    public static RumbleFeature Of(BridgeSession session) => session.Feature(static s => new RumbleFeature(s));

    /// <summary>Rumbles seen so far (played or not).</summary>
    public long Seen { get; private set; }

    private SPlayerController? Controller => _session.LocalController is { bDeleteMe: false } pc ? pc : null;

    public void Dispose()
    {
        if (_ended)
        {
            return;
        }

        _ended = true;
        _session.Client.BeforeWorldTick -= OnBeforeWorldTick;
        _bridge.Ticked -= OnTicked;
    }

    /// <summary>Takes the waiting rumbles, oldest first. Returns how many were written.</summary>
    public int Drain(Span<BridgeRumble> into)
    {
        int count = Math.Min(into.Length, _queue.Count);
        for (int i = 0; i < count; i++)
        {
            into[i] = _queue[i];
        }

        _queue.RemoveRange(0, count);
        return count;
    }

    /// <summary>
    /// Raises <paramref name="what"/> (a Vibration_* name) on the local player's controller through the simulation, as
    /// the script's Vibration_ functions do (CombatFx.Notify), for a check. False without a local player.
    /// </summary>
    public bool RaiseForCheck(string what, float a)
    {
        if (Controller is not { } pc)
        {
            return false;
        }

        CombatFx.Notify(pc, new Name(what), a);
        return true;
    }

    private void OnBeforeWorldTick(ClientSession client)
    {
        if (Controller is { } pc && ReferenceEquals(pc.XLevel.World, client.World))
        {
            Take(pc, client.World.Events.Late, pc.Level.TimeSeconds);
        }
    }

    private void OnTicked()
    {
        SPlayerController? pc = Controller;
        if (!ReferenceEquals(pc, _lastController))
        {
            _lastController = pc;
            _rules.Reset();
        }

        if (pc is null)
        {
            return;
        }

        SimEvents events = pc.XLevel.World.Events;
        ReadOnlySpan<SimEvent> current = events.Current;
        Take(pc, current.Slice(0, current.Length - events.Late.Length), pc.Level.TimeSeconds);
    }

    private void Take(SPlayerController pc, ReadOnlySpan<SimEvent> events, float now)
    {
        int pawnId = pc.Pawn?.ActorId ?? -1;
        foreach (ref readonly SimEvent e in events)
        {
            if (e.Kind != SimEventKind.Notify || e.ActorId == 0 || (e.ActorId != pc.ActorId && e.ActorId != pawnId) ||
                _rules.ShapeOf(e) is not { } shape)
            {
                continue;
            }

            Seen++;
            PadTuning pad = ControlsStore.Settings.Pad;
            int result;
            if (pc.bGameEnded)
            {
                // The end of the round turns script rumble off (GameEnded: NativeRumbleEnable(false)).
                _rules.Reset();
                result = BridgeRumbleResult.RoundEnded;
            }
            else if (!pad.Enabled)
            {
                result = BridgeRumbleResult.ControllerOff;
            }
            else if (!pad.Rumble)
            {
                result = BridgeRumbleResult.VibrationOff;
            }
            else
            {
                result = _rules.Take(shape, now) ? BridgeRumbleResult.Played : BridgeRumbleResult.Outranked;
            }

            Enqueue(e.What.ToString(), shape, pad.RumbleStrength, result);
        }
    }

    private unsafe void Enqueue(string name, RumbleShape shape, float strength, int result)
    {
        float s = Math.Clamp(strength, 0f, 1f);
        var record = new BridgeRumble
        {
            Priority = shape.Priority,
            Low = shape.Low / 65535f * s,
            High = shape.High / 65535f * s,
            Seconds = shape.Seconds,
            Strength = s,
            Play = result == BridgeRumbleResult.Played ? 1 : 0,
            Result = result,
        };
        int count = Math.Min(name.Length, BridgeRumble.NameLength - 1);
        for (int i = 0; i < count; i++)
        {
            record.Name[i] = name[i];
        }

        record.Name[count] = '\0';
        if (_queue.Count >= QueueCapacity)
        {
            _queue.RemoveAt(0);
        }

        _queue.Add(record);
    }
}
