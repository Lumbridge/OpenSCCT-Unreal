using System;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Events;

/// <summary>
/// The events feature of one session: it listens to the client world's presentation events and keeps what Unreal
/// takes each frame (the raw events, the resolved sounds, the shots) and what it reads each frame (effect actors,
/// impact marks, ambient sounds).
/// <para>
/// How the events are taken, as the Godot client's PlayAudioRouter and HudEventTap take them:
/// each tick's events come through <see cref="World.EventSink"/> right after the tick, exactly once. Events raised
/// between two ticks (a network call received before the step, an input command) are in no tick's events and the
/// next step drops them, so they are taken from <c>World.Events.Late</c> just before each step, on
/// <see cref="ClientSession.BeforeWorldTick"/>. That hook is used instead of BridgeSession.BeforeTick because the
/// play session runs the player's commands after BeforeTick; this feature subscribes after the play session did, so
/// it also sees the events those commands raise. Nothing is recorded while the world replays ticks for prediction
/// (SimEvents.Add drops them).
/// </para>
/// Everything here runs on Unreal's game thread, which is the thread the client world ticks on.
/// </summary>
public sealed class EventsFeature : ISimEventSink, IDisposable
{
    private const float RotatorToDegrees = 360f / 65536f;

    /// <summary>Records kept when Unreal does not drain: about a minute of a busy match.</summary>
    private const int QueueCapacity = 8192;

    private readonly PlaySession _session;
    private readonly ISimEventSink? _next;
    private readonly DrainQueue<BridgeSimEvent> _events = new(QueueCapacity);
    private readonly DrainQueue<BridgeSound> _sounds = new(QueueCapacity);
    private readonly DrainQueue<BridgeShot> _shots = new(QueueCapacity);
    private readonly SoundLibrary? _library;
    private readonly SoundResolver? _resolver;
    private readonly AmbientSounds? _ambient;
    private readonly EffectTracker _effects;
    private readonly BridgeSession _bridge;
    private bool _ended;
    private long _lateTick = -1;
    private int _lateTaken;

    private EventsFeature(BridgeSession bridge)
    {
        _bridge = bridge;
        _session = bridge.Session;
        string? sharedRoot = SharedContent.FindRoot(_session.MapFolder);
        if (sharedRoot is not null && System.IO.File.Exists(System.IO.Path.Combine(sharedRoot, "audio", "audio.json")))
        {
            _library = SoundLibrary.Load(sharedRoot, _session.MapFolder);
            _resolver = new SoundResolver(_library, _session, _sounds);
            _ambient = new AmbientSounds(_session, _library, _resolver);
        }

        _effects = new EffectTracker(_session, sharedRoot, _shots);

        ClientSession client = _session.Client;
        World world = client.World;
        _next = world.EventSink;
        world.EventSink = this;
        client.BeforeWorldTick += TakeLate;
        bridge.Ticked += AfterTick;

        // The engine's map load plays the LevelInfo's Song as music with no fade.
        _resolver?.PlayLevelSong(world.TickCount);
    }

    /// <summary>The feature of <paramref name="session"/>, started on first use. Events raised before that are not seen.</summary>
    public static EventsFeature Of(BridgeSession session) => session.Feature(static s => new EventsFeature(s));

    /// <summary>Sounds the content's library holds; 0 when the content has no shared/audio.</summary>
    public int SoundCount => _library?.Count ?? 0;

    /// <summary>False when the map folder has no audio table (its streams and ambience are then missing).</summary>
    public bool HasMapAudio => _library?.HasMapTable ?? false;

    /// <summary>Effect classes known from shared/fx.</summary>
    public int EffectClassCount => _effects.ClassCount;

    // Counts for checks and the probe.
    public long EventsTotal => _events.Total;

    public long SoundsTotal => _sounds.Total;

    public long ShotsTotal => _shots.Total;

    public long EventsDropped => _events.Dropped + _sounds.Dropped + _shots.Dropped;

    public long LateEvents { get; private set; }

    public long SoundCallsStarted => _resolver?.Started ?? 0;

    public long SoundCallsSilent => _resolver?.Silent ?? 0;

    public long SoundCallsDuplicate => _resolver?.Duplicates ?? 0;

    public long SoundCallsOutOfReach => _resolver?.OutOfReach ?? 0;

    public long SoundCallsUnknown => _resolver?.UnknownCount ?? 0;

    public long SoundStops => _resolver?.Stops ?? 0;

    /// <summary>StopSound events that could stop nothing and gave no sound record (their raw events carry Dropped).</summary>
    public long SoundIdleStops => _resolver?.IdleStops ?? 0;

    public System.Collections.Generic.IReadOnlyList<string> UnknownSounds => _resolver?.Unknown ?? (System.Collections.Generic.IReadOnlyList<string>)[];

    public System.Collections.Generic.IReadOnlyDictionary<string, int> EffectActorsSeen => _effects.Seen;

    public long MuzzleFlashes => _effects.MuzzleFlashes;

    public long Tracers => _effects.Tracers;

    /// <summary>The tick's own events, right after the tick (the world's event sink).</summary>
    public void OnSimEvents(long tick, ReadOnlySpan<SimEvent> events)
    {
        _next?.OnSimEvents(tick, events);
        if (!_ended)
        {
            Take(tick, events, late: false);
        }
    }

    /// <summary>
    /// The session ended (BridgeSession disposes its features). A remote round's connection and world live on after
    /// the round, so the feature takes itself off them: the tick hooks go and the world's event sink is put back as
    /// it was (when another sink was chained over this one since, this one only passes events on from now).
    /// </summary>
    public void Dispose()
    {
        if (_ended)
        {
            return;
        }

        _ended = true;
        ClientSession client = _session.Client;
        client.BeforeWorldTick -= TakeLate;
        _bridge.Ticked -= AfterTick;
        if (ReferenceEquals(client.World.EventSink, this))
        {
            client.World.EventSink = _next;
        }
    }

    /// <summary>PlayAudioRouter.TakeLate: the events raised since the last tick; they are presented with the next one.</summary>
    private void TakeLate(ClientSession client)
    {
        World world = client.World;
        ReadOnlySpan<SimEvent> late = world.Events.Late;
        if (world.TickCount != _lateTick)
        {
            _lateTick = world.TickCount;
            _lateTaken = 0;
        }

        if (late.Length > _lateTaken)
        {
            ReadOnlySpan<SimEvent> fresh = late[_lateTaken..];
            _lateTaken = late.Length;
            LateEvents += fresh.Length;
            Take(world.TickCount + 1, fresh, late: true);
        }
    }

    private void Take(long tick, ReadOnlySpan<SimEvent> events, bool late)
    {
        foreach (ref readonly SimEvent e in events)
        {
            bool played = _resolver?.Handle(tick, e) ?? true;
            if (e.Kind == SimEventKind.Notify)
            {
                _effects.OnNotify(e);
            }

            _events.Add(new BridgeSimEvent
            {
                Kind = (int)e.Kind,
                Tick = (int)tick,
                ActorId = e.ActorId,
                WhatId = EventNames.Id(e.What),
                X = e.Location.X,
                Y = e.Location.Y,
                Z = e.Location.Z,
                Pitch = e.Rotation.Pitch * RotatorToDegrees,
                Yaw = e.Rotation.Yaw * RotatorToDegrees,
                Roll = e.Rotation.Roll * RotatorToDegrees,
                Slot = e.Slot,
                Volume = e.Volume,
                Radius = e.Radius,
                PitchScale = e.Pitch,
                BoneId = EventNames.Id(e.Bone),
                Flags = (e.NoOverride ? BridgeSimEventFlags.NoOverride : 0) | (e.NoAttenuate ? BridgeSimEventFlags.NoAttenuate : 0) |
                    (e.Owned ? BridgeSimEventFlags.Owned : 0) | (e.Heard ? BridgeSimEventFlags.Heard : 0) |
                    (e.Flat ? BridgeSimEventFlags.Flat : 0) | (late ? BridgeSimEventFlags.Late : 0) |
                    (played ? 0 : BridgeSimEventFlags.Dropped),
            });
        }
    }

    private void AfterTick()
    {
        Level level = _session.Client.World.Level;
        _resolver?.AfterTick(level.TickCount, level);
        _effects.OnTick();
    }

    /// <summary>Takes the oldest waiting raw events. Returns how many were written; call again while it fills the buffer.</summary>
    public int DrainEvents(Span<BridgeSimEvent> into) => _events.Drain(into);

    /// <summary>Takes the oldest waiting sound records.</summary>
    public int DrainSounds(Span<BridgeSound> into) => _sounds.Drain(into);

    /// <summary>Takes the oldest waiting muzzle flashes and tracers.</summary>
    public int DrainShots(Span<BridgeShot> into) => _shots.Drain(into);

    /// <summary>The effect actors as drawn this frame (blended between the last two ticks like the pawns).</summary>
    public int GetEffectActors(Span<BridgeEffectActor> into) => _effects.Write(into, _bridge.Alpha);

    /// <summary>The live impact marks.</summary>
    public int GetImpactMarks(Span<BridgeImpactMark> into) => _effects.WriteMarks(into);

    /// <summary>The ambient sound sources of this frame.</summary>
    public int GetAmbientSounds(Span<BridgeAmbientSound> into) => _ambient?.Write(into) ?? 0;

    /// <summary>
    /// Where an actor is now (the simulation's location of the last tick), for a sound that follows its actor.
    /// False when the actor is gone: a one-shot then stays where it was, a loop is stopped by its StopCall record.
    /// </summary>
    public bool TryGetActorLocation(int actorId, out FVector location)
    {
        Actor? actor = _session.Client.World.Level.FindActor(actorId);
        location = actor?.Location ?? default;
        return actor is { bDeleteMe: false };
    }
}
