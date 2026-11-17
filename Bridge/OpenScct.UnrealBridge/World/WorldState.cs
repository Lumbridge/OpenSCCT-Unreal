using System;
using System.Collections.Generic;
using OpenScct.Content;
using OpenScct.Content.Loading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Lighting;
using OpenScct.Game.Objectives;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.World;

/// <summary>
/// The dynamic state of the level for Unreal: every map actor that moves, hides or changes what it shows, the map's
/// lights as they shine now, the laser beams, the gameplay objects a client marks, and the state changes between
/// frames as events. It follows the client world's actors after each tick and hands them out blended between the
/// last two ticks. It reads the simulation and never writes it.
/// <para>A port of the Godot client's device drawing without the drawing: game/Scripts/Views/DeviceViews.cs
/// (MeshFollower, AnimatedMeshView, the trigger light switches, Rebind, the drop zone lids),
/// Views/CameraMountViews.cs, Views/GlassBreakViews.cs (when a pane breaks), Fx/LaserTriggerView.cs and
/// Render/WorldLights.cs (Modulation). Not ported: conveyors (Views/ConveyorViews.cs, an OpenSCCT addition the
/// stock maps do not use), the emitters and lights that ride a mover (Views/AttachedEffects.cs), the PS2 objective
/// computers' own animation, and the animation studio's poses.</para>
/// </summary>
public sealed class WorldState
{
    /// <summary>An actor that moved farther than this in one tick was placed, not moved: no blend (as the pawns).</summary>
    private const float CutDistanceSquared = 40000f;

    /// <summary>What replication rounds a location to: within this of the map placement is "not moved" (DeviceViews).</summary>
    private const float PlacementSlack = 1f;

    /// <summary>What replication rounds a rotation to (DeviceViews).</summary>
    private const int RotationSlack = 256;

    /// <summary>How often the level is searched again for the server's copies of destroyed map actors (DeviceViews).</summary>
    private const int RebindRescanTicks = 30;

    private const int MaxSkinSlots = 8;
    private const int MaxEvents = 4096;
    private const float RotatorToDegrees = 360f / 65536f;
    private const float TickSeconds = 1f / 60f;

    /// <summary>
    /// The classes whose placed mesh a game mode shows or leaves out (GameplayObjects.ModeDependent): the simulation's
    /// bHidden decides for them, not the map's.
    /// </summary>
    private static readonly HashSet<string> s_modeDependent = new(StringComparer.OrdinalIgnoreCase)
    {
        "SDisk", "SFlag", "SFlagDropZone", "SComputerObjectiveTrigger",
    };

    private readonly BridgeSession _session;
    private readonly MapRenderIndex _render;
    private readonly List<string> _names = [""];
    private readonly Dictionary<string, int> _nameIds = new(StringComparer.Ordinal);
    private readonly List<Slot> _slots = [];
    private readonly List<Follower> _followers = [];
    private readonly Dictionary<int, Follower> _followerByName = new();
    private readonly List<LightState> _lights = [];
    private readonly List<Marker> _markers = [];
    private readonly List<Animated> _animated = [];
    private readonly List<BridgeWorldEvent> _events = [];
    private readonly Dictionary<Actor, int> _nameOfActor = new(ReferenceEqualityComparer.Instance);
    private readonly HashSet<Actor> _boundActors = new(ReferenceEqualityComparer.Instance);
    private readonly HashSet<Actor> _spawnedActors = new(ReferenceEqualityComparer.Instance);
    private MapLoadResult? _loaded;
    private int _rebindActorCount = -1;
    private int _spawnScanActorCount = -1;
    private float _time;
    private float _strobeTime = float.NaN;
    private bool _strobeOff;

    private WorldState(BridgeSession session)
    {
        _session = session;
        _render = MapRenderIndex.Load(session.Session.MapFolder);
        session.Ticked += OnTick;
        // A session that already ticked has its map: bind now so the first read is not empty.
        TryBind();
    }

    /// <summary>The session's world state, made (and hooked to its ticks) on first use.</summary>
    public static WorldState Of(BridgeSession session) => session.Feature(s => new WorldState(s));

    /// <summary>The render part's names (for the probe's cross-check).</summary>
    public MapRenderIndex Render => _render;

    /// <summary>False until the client has loaded its map (nothing is followed before that).</summary>
    public bool Bound => _loaded is not null;

    public int ActorCount => _followers.Count;
    public int LightCount => _lights.Count;
    public int MarkerCount => _markers.Count;
    public int AnimatedCount => _animated.Count;
    public int PendingEvents => _events.Count;

    /// <summary>Times a destroyed map actor's slot took over the server's copy (for the probe).</summary>
    public int Rebinds { get; private set; }

    // ---- names -----------------------------------------------------------------------------------------------

    /// <summary>How many names the table holds, id 0 (none) included: valid ids are 0 to this minus one.</summary>
    public int NameCount => _names.Count;

    /// <summary>The name of <paramref name="id"/>, "" for 0 or an unknown id.</summary>
    public string NameOf(int id) => (uint)id < (uint)_names.Count ? _names[id] : "";

    /// <summary>The id of <paramref name="name"/>, added when new; 0 for null or empty.</summary>
    public int Intern(string? name)
    {
        if (string.IsNullOrEmpty(name))
        {
            return 0;
        }

        if (!_nameIds.TryGetValue(name, out int id))
        {
            id = _names.Count;
            _names.Add(name);
            _nameIds[name] = id;
        }

        return id;
    }

    /// <summary>The id of a name already in the table, 0 when it is not (nothing is added).</summary>
    public int FindName(string name) => _nameIds.GetValueOrDefault(name);

    // ---- binding ---------------------------------------------------------------------------------------------

    private void TryBind()
    {
        if (_loaded is not null || _session.Session.Client.Map?.Loaded is not { } loaded)
        {
            return;
        }

        _loaded = loaded;
        _time = loaded.Level.Info.TimeSeconds;
        foreach (ActorRecord record in loaded.Gameplay.Actors)
        {
            if (loaded.ActorOf(record) is not { } actor)
            {
                continue;
            }

            _render.Instances.TryGetValue(record.Id, out RenderInstance? instance);
            bool skeletal = IsSkeletal(actor);
            // A followed mesh: an instance the converter marked as gameplay (or a mover), or a static mesh actor the
            // render file does not place (class meshes of the shared pack: disks, drop zones; GameplayObjects.Build).
            bool mesh = instance is { Gameplay: true } or { Kind: "mover" } ||
                (instance is null && !skeletal && actor.DrawType == EDrawType.DT_StaticMesh && !actor.StaticMesh.IsNone);
            int markerKind = MarkerKindOf(actor);
            bool light = actor is STriggerLight;
            if (!mesh && !skeletal && markerKind == 0 && !light)
            {
                continue;
            }

            var slot = new Slot(record.Id, Intern(record.Id), actor);
            _slots.Add(slot);
            _nameOfActor[actor] = slot.NameId;
            if (mesh)
            {
                AddFollower(new Follower(this, slot, record, instance));
            }

            if (skeletal)
            {
                _animated.Add(new Animated(this, slot));
            }

            if (markerKind != 0)
            {
                _markers.Add(new Marker(slot, markerKind));
            }
        }

        foreach (RenderLight light in _render.Lights)
        {
            Slot? slot = null;
            foreach (Slot s in _slots)
            {
                if (s.Actor is STriggerLight && string.Equals(s.Id, light.Name, StringComparison.OrdinalIgnoreCase))
                {
                    slot = s;
                    break;
                }
            }

            _lights.Add(new LightState(light, Intern(light.Name), slot));
        }

        // The first state is the map's, not a change: take it without events.
        AfterTick(loaded.Level, first: true);
    }

    private void AddFollower(Follower follower)
    {
        _followers.Add(follower);
        _followerByName[follower.Slot.NameId] = follower;
    }

    /// <summary>
    /// The skeletal mesh actors drawn as models, not as static meshes: every SAnimatedMesh and the fish drawn with a
    /// mesh (DT_Mesh: sharks, mantas, dolphins). DeviceViews.IsSkeletal, less the PS2 objective computers.
    /// </summary>
    private static bool IsSkeletal(Actor actor) =>
        !actor.Mesh.IsNone && (actor is SAnimatedMesh || (actor is SFish or SFish_Follow && actor.DrawType == EDrawType.DT_Mesh));

    private static int MarkerKindOf(Actor actor) => actor switch
    {
        SComputerObjectiveTrigger => BridgeWorldMarkerKind.Computer,
        SBombTargetObjectiveTrigger => BridgeWorldMarkerKind.BombTarget,
        SFlag => BridgeWorldMarkerKind.Flag,
        SFlagDropZone => BridgeWorldMarkerKind.FlagDropZone,
        SDisk => BridgeWorldMarkerKind.Disk,
        SObjectiveTrigger => BridgeWorldMarkerKind.ObjectiveTrigger,
        SAlarm => BridgeWorldMarkerKind.Alarm,
        SCamera => BridgeWorldMarkerKind.Camera,
        SVolumetricSensor => BridgeWorldMarkerKind.VolumetricSensor,
        SLaserTrigger => BridgeWorldMarkerKind.LaserTrigger,
        _ => 0,
    };

    // ---- per tick --------------------------------------------------------------------------------------------

    private void OnTick()
    {
        if (_loaded is null)
        {
            TryBind();
            return;
        }

        AfterTick(_session.Session.Client.World.Level, first: false);
    }

    private void AfterTick(Level level, bool first)
    {
        _time = level.Info.TimeSeconds;
        Rebind(level);
        FollowSpawned(level);
        for (int i = _followers.Count - 1; i >= 0; i--)
        {
            Follower f = _followers[i];
            if (f.Spawned && f.Slot.Actor.bDeleteMe)
            {
                // A spawned prop that went (its camera or drop zone was destroyed) is dropped; its name stays reserved.
                _spawnedActors.Remove(f.Slot.Actor);
                _nameOfActor.Remove(f.Slot.Actor);
                _followerByName.Remove(f.Slot.NameId);
                _followers.RemoveAt(i);
                continue;
            }

            f.OnTick(first);
        }

        foreach (Animated a in _animated)
        {
            a.OnTick();
        }

        foreach (LightState l in _lights)
        {
            l.OnTick(this, first);
        }

        foreach (Marker m in _markers)
        {
            m.OnTick(this, first);
        }
    }

    /// <summary>
    /// UE2 client map setup destroys every map actor that is neither bStatic nor bNoDelete (SLaserTrigger,
    /// SVolumetricSensor, glass...) and the server sends its own copy while it is relevant. A slot whose actor is
    /// gone follows the live, unbound actor of the same class at its map placement; when that copy goes too (out of
    /// relevancy) the slot reports Gone until the next one. DeviceViews.Rebind.
    /// </summary>
    private void Rebind(Level level)
    {
        bool orphans = false;
        foreach (Slot s in _slots)
        {
            if (s.Actor.bDeleteMe && s.Replaceable)
            {
                orphans = true;
                break;
            }
        }

        if (!orphans || (level.ActorCount == _rebindActorCount && level.TickCount % RebindRescanTicks != 0))
        {
            return;
        }

        _rebindActorCount = level.ActorCount;
        _boundActors.Clear();
        foreach (Slot s in _slots)
        {
            if (!s.Actor.bDeleteMe)
            {
                _boundActors.Add(s.Actor);
            }
        }

        foreach (Actor a in level.DynamicActors<Actor>())
        {
            if (a.bDeleteMe || a.bNoDelete || _boundActors.Contains(a) || _spawnedActors.Contains(a))
            {
                continue;
            }

            foreach (Slot s in _slots)
            {
                if (s.Actor.bDeleteMe && s.Replaceable && s.Actor.GetType() == a.GetType() && s.IsAtPlacement(a.Location))
                {
                    _nameOfActor.Remove(s.Actor);
                    s.Rebind(a);
                    _nameOfActor[a] = s.NameId;
                    _boundActors.Add(a);
                    Rebinds++;
                    break;
                }
            }
        }
    }

    /// <summary>
    /// Follows the plain props map actors spawn for themselves, which no map record places: a security camera's
    /// wall mount (CameraMountViews), a drop zone's lid (DeviceViews.BindCovers) and a disk's rack. Each gets its
    /// owner's name with a suffix; one is followed per owner (a client has its own spawn and may also get the
    /// server's copy).
    /// </summary>
    private void FollowSpawned(Level level)
    {
        if (level.ActorCount == _spawnScanActorCount)
        {
            return;
        }

        _spawnScanActorCount = level.ActorCount;
        foreach (SBasicGamePlayObject a in level.DynamicActors<SBasicGamePlayObject>())
        {
            if (a.bDeleteMe || a.GetType() != typeof(SBasicGamePlayObject) || a.StaticMesh.IsNone ||
                _spawnedActors.Contains(a) || _nameOfActor.ContainsKey(a) ||
                a.Owner is not { } owner || !_nameOfActor.TryGetValue(owner, out int ownerName))
            {
                continue;
            }

            string suffix = owner switch
            {
                SCamera => "_Mount",
                SFlagDropZone => "_Cover",
                SFlag => "_Server",
                _ => "_Prop",
            };
            int name = Intern(NameOf(ownerName) + suffix);
            if (_followerByName.ContainsKey(name))
            {
                continue;
            }

            var slot = new Slot(NameOf(name), name, a);
            _spawnedActors.Add(a);
            _nameOfActor[a] = name;
            AddFollower(new Follower(this, slot));
        }
    }

    private void Emit(int kind, int nameId, int a, int b, FVector at, FVector direction = default)
    {
        if (_events.Count >= MaxEvents)
        {
            // Nobody is reading: keep the newest.
            _events.RemoveRange(0, MaxEvents / 4);
        }

        _events.Add(new BridgeWorldEvent
        {
            Kind = kind,
            NameId = nameId,
            A = a,
            B = b,
            X = at.X,
            Y = at.Y,
            Z = at.Z,
            DX = direction.X,
            DY = direction.Y,
            DZ = direction.Z,
        });
    }

    // ---- per frame -------------------------------------------------------------------------------------------

    /// <summary>
    /// Writes every followed actor as drawn this frame. Returns how many there are (which can be more than
    /// <paramref name="actors"/> holds: only that many are written). The order is stable while no spawned prop
    /// comes or goes.
    /// </summary>
    public int GetActors(Span<BridgeWorldActor> actors)
    {
        float alpha = Math.Clamp(_session.Alpha, 0f, 1f);
        int count = Math.Min(actors.Length, _followers.Count);
        for (int i = 0; i < count; i++)
        {
            actors[i] = _followers[i].Draw(alpha);
        }

        return _followers.Count;
    }

    /// <summary>The fixed facts of a followed actor; false when <paramref name="nameId"/> is not one.</summary>
    public bool TryGetActorInfo(int nameId, out BridgeWorldActorInfo info)
    {
        if (_followerByName.TryGetValue(nameId, out Follower? follower))
        {
            info = follower.Info;
            return true;
        }

        info = default;
        return false;
    }

    /// <summary>Writes every light of the map as it shines this frame. Returns how many there are.</summary>
    public int GetLights(Span<BridgeWorldLight> lights)
    {
        // The light types run on scene time, carried on from the last tick so a pulse is smooth between ticks.
        float time = _time + (Math.Clamp(_session.Alpha, 0f, 1f) * TickSeconds);
        if (time != _strobeTime)
        {
            // The strobe's shared switch (a static in the original): flipped once per drawn frame (WorldLights.StrobeOff).
            _strobeTime = time;
            _strobeOff = !_strobeOff;
        }

        int count = Math.Min(lights.Length, _lights.Count);
        for (int i = 0; i < count; i++)
        {
            lights[i] = _lights[i].Draw(time, _strobeOff);
        }

        return _lights.Count;
    }

    /// <summary>
    /// Writes the laser beams to draw this frame: every working tripwire's beam and every mine beam with a length.
    /// Returns how many there are. LaserTriggerView.Draw.
    /// </summary>
    public int GetLasers(Span<BridgeWorldLaser> lasers)
    {
        if (_loaded is null)
        {
            return 0;
        }

        Level level = _session.Session.Client.World.Level;
        int count = 0;
        foreach (SLaserTrigger trigger in level.AllActors<SLaserTrigger>())
        {
            if (trigger.bDeleteMe || trigger.bHidden || trigger.Laser is not { bDeleteMe: false, bHidden: false } beam)
            {
                continue;
            }

            // LasDetectBeam is 1 unit long; the game sets DrawScale3D.X to the beam's length.
            Write(lasers, count++, trigger.ActorId, 0, _nameOfActor.GetValueOrDefault(trigger), beam, beam.DrawScale3D.X * beam.DrawScale);
        }

        foreach (SLaserBeam_MineLaser beam in level.AllActors<SLaserBeam_MineLaser>())
        {
            // A beam with no length (a trap with nothing ahead) draws nothing.
            if (!beam.bDeleteMe && !beam.bHidden && beam.fTailleFaisceau > 0f)
            {
                Write(lasers, count++, beam.ActorId, 1, 0, beam, beam.fTailleFaisceau);
            }
        }

        return count;

        static void Write(Span<BridgeWorldLaser> lasers, int index, int id, int kind, int nameId, Actor beam, float length)
        {
            if (index >= lasers.Length)
            {
                return;
            }

            FVector end = beam.Location + (beam.Rotation.Vector() * length);
            lasers[index] = new BridgeWorldLaser
            {
                Id = id,
                Kind = kind,
                NameId = nameId,
                Flags = beam.bHideInNormalVision ? BridgeWorldLaserFlags.HideInNormalVision : 0,
                StartX = beam.Location.X,
                StartY = beam.Location.Y,
                StartZ = beam.Location.Z,
                EndX = end.X,
                EndY = end.Y,
                EndZ = end.Z,
                Heat = beam.HeatIntensity,
            };
        }
    }

    /// <summary>Writes the gameplay markers as of the last tick. Returns how many there are.</summary>
    public int GetMarkers(Span<BridgeWorldMarker> markers)
    {
        int count = Math.Min(markers.Length, _markers.Count);
        for (int i = 0; i < count; i++)
        {
            markers[i] = _markers[i].Shown;
        }

        return _markers.Count;
    }

    /// <summary>Writes the skeletal props as drawn this frame. Returns how many there are.</summary>
    public int GetAnimated(Span<BridgeWorldAnimated> animated)
    {
        float alpha = Math.Clamp(_session.Alpha, 0f, 1f);
        int count = Math.Min(animated.Length, _animated.Count);
        for (int i = 0; i < count; i++)
        {
            animated[i] = _animated[i].Draw(alpha);
        }

        return _animated.Count;
    }

    /// <summary>
    /// Moves the oldest waiting events into <paramref name="events"/> and returns how many were written; call again
    /// while it fills the buffer. An event is handed out once.
    /// </summary>
    public int DrainEvents(Span<BridgeWorldEvent> events)
    {
        int count = Math.Min(events.Length, _events.Count);
        for (int i = 0; i < count; i++)
        {
            events[i] = _events[i];
        }

        _events.RemoveRange(0, count);
        return count;
    }

    // ---- helpers ---------------------------------------------------------------------------------------------

    private static bool Away(FVector d, FRotator rotation, FRotator rotation0) =>
        MathF.Abs(d.X) > PlacementSlack || MathF.Abs(d.Y) > PlacementSlack || MathF.Abs(d.Z) > PlacementSlack ||
        Math.Abs(FRotator.NormalizeAxis(rotation.Pitch - rotation0.Pitch)) > RotationSlack ||
        Math.Abs(FRotator.NormalizeAxis(rotation.Yaw - rotation0.Yaw)) > RotationSlack ||
        Math.Abs(FRotator.NormalizeAxis(rotation.Roll - rotation0.Roll)) > RotationSlack;

    private static FVector Lerp(FVector a, FVector b, float t) =>
        new(a.X + ((b.X - a.X) * t), a.Y + ((b.Y - a.Y) * t), a.Z + ((b.Z - a.Z) * t));

    /// <summary>One axis the shortest way round, in degrees.</summary>
    private static float LerpAxis(int from, int to, float t)
    {
        float degrees = ((FRotator.NormalizeAxis(from) + (FRotator.NormalizeAxis(to - from) * t)) * RotatorToDegrees) % 360f;
        return degrees > 180f ? degrees - 360f : degrees <= -180f ? degrees + 360f : degrees;
    }

    private static float DistanceSquared(FVector a, FVector b)
    {
        float x = a.X - b.X, y = a.Y - b.Y, z = a.Z - b.Z;
        return (x * x) + (y * y) + (z * z);
    }

    /// <summary>
    /// One followed map record: the name C++ knows it by and the simulation actor that stands for it now (the map's
    /// own, or the server's copy after a rebind).
    /// </summary>
    private sealed class Slot
    {
        private readonly FVector _location0;

        public Slot(string id, int nameId, Actor actor)
        {
            Id = id;
            NameId = nameId;
            Actor = actor;
            _location0 = actor.Location;
            Replaceable = !actor.bStatic && !actor.bNoDelete;
        }

        public string Id { get; }

        public int NameId { get; }

        public Actor Actor { get; private set; }

        /// <summary>True for a map actor a client destroys at load and gets back from the server.</summary>
        public bool Replaceable { get; }

        /// <summary>Goes up when another actor takes the slot: what follows it starts again from that actor.</summary>
        public int Generation { get; private set; }

        public bool IsAtPlacement(FVector location)
        {
            FVector d = location - _location0;
            return MathF.Abs(d.X) <= PlacementSlack && MathF.Abs(d.Y) <= PlacementSlack && MathF.Abs(d.Z) <= PlacementSlack;
        }

        public void Rebind(Actor actor)
        {
            Actor = actor;
            Generation++;
        }
    }

    /// <summary>One static mesh actor followed for its place, visibility, mesh and skins (DeviceViews.MeshFollower).</summary>
    private sealed class Follower
    {
        private readonly WorldState _world;
        private readonly FVector _location0;
        private readonly FRotator _rotation0;
        private readonly bool _hidden0;
        private readonly bool _visible0;
        private readonly bool _simDecidesHidden;
        private readonly string? _asset0;
        private readonly string?[] _skins = new string?[MaxSkinSlots];
        private readonly bool _glass;
        private FVector _prevLocation, _location;
        private FRotator _prevRotation, _rotation;
        private string? _shownAsset;
        private int _shownMeshId;
        private int _generation;
        private bool _moved;
        private bool _moving;
        private bool _shown;
        private bool _broken;
        private Name _attachBone;
        private int _attachBoneId;

        /// <summary>A map record's mesh.</summary>
        public Follower(WorldState world, Slot slot, ActorRecord record, RenderInstance? instance)
        {
            _world = world;
            Slot = slot;
            Actor actor = slot.Actor;
            // The map's placement the level actor was built at: an actor its own BeginPlay already moved (a camera
            // set off its wall) is drawn where it is, not where the map put it.
            _location0 = instance?.Location ?? record.Location;
            _rotation0 = instance?.Rotation ?? record.Rotation;
            // A help path is hidden by its class default, which the map does not store.
            _hidden0 = actor is SHelpPath || (instance?.Hidden ?? record.Props.GetBool("bHidden"));
            _visible0 = !_hidden0;
            _simDecidesHidden = s_modeDependent.Contains(record.ClassName);
            _asset0 = actor.StaticMesh.Path;
            _glass = actor is SVitre;
            FVector scale = instance?.Scale ?? (actor.DrawScale3D * actor.DrawScale);
            FVector pivot = instance?.PrePivot ?? actor.PrePivot;
            Info = MakeInfo(world, slot, record.ClassName, _asset0, instance, scale, pivot, _location0, _rotation0,
                actor is Mover ? BridgeWorldActorInfoFlags.Mover : 0);
            Start(actor);
        }

        /// <summary>A prop spawned during play: drawn wherever it is from the start.</summary>
        public Follower(WorldState world, Slot slot)
        {
            _world = world;
            Slot = slot;
            Spawned = true;
            Actor actor = slot.Actor;
            _location0 = actor.Location;
            _rotation0 = actor.Rotation;
            _hidden0 = false;
            _visible0 = true;
            _simDecidesHidden = true;
            _asset0 = actor.StaticMesh.Path;
            Info = MakeInfo(world, slot, nameof(SBasicGamePlayObject), _asset0, null, actor.DrawScale3D * actor.DrawScale,
                actor.PrePivot, _location0, _rotation0, BridgeWorldActorInfoFlags.Spawned);
            Start(actor);
            _moved = true;
        }

        public Slot Slot { get; }

        public bool Spawned { get; }

        public BridgeWorldActorInfo Info { get; }

        private static BridgeWorldActorInfo MakeInfo(WorldState world, Slot slot, string className, string? asset,
            RenderInstance? instance, FVector scale, FVector pivot, FVector location, FRotator rotation, int flags) => new()
        {
            NameId = slot.NameId,
            ClassId = world.Intern(className),
            MeshId = world.Intern(asset),
            RenderMeshId = world.Intern(instance?.Mesh),
            Flags = flags | (instance is { InLevel: true } ? BridgeWorldActorInfoFlags.InLevel : 0) |
                (instance is { Hidden: true } ? BridgeWorldActorInfoFlags.HiddenInMap : 0),
            ScaleX = scale.X,
            ScaleY = scale.Y,
            ScaleZ = scale.Z,
            PrePivotX = pivot.X,
            PrePivotY = pivot.Y,
            PrePivotZ = pivot.Z,
            X = location.X,
            Y = location.Y,
            Z = location.Z,
            Pitch = LerpAxis(rotation.Pitch, rotation.Pitch, 0f),
            Yaw = LerpAxis(rotation.Yaw, rotation.Yaw, 0f),
            Roll = LerpAxis(rotation.Roll, rotation.Roll, 0f),
        };

        /// <summary>Starts following <paramref name="actor"/> from where it is, with no blend from before.</summary>
        private void Start(Actor actor)
        {
            if (_generation == Slot.Generation)
            {
                // The first actor: the map's mesh is what shows until the simulation says otherwise.
                _shownAsset = _asset0;
                _shownMeshId = _world.Intern(_asset0);
            }

            _generation = Slot.Generation;
            _prevLocation = _location = actor.Location;
            _prevRotation = _rotation = actor.Rotation;
            _moved |= Away(_location - _location0, _rotation, _rotation0);
            _moving = false;
        }

        public void OnTick(bool first)
        {
            Actor actor = Slot.Actor;
            if (_generation != Slot.Generation)
            {
                Start(actor);
            }
            else
            {
                _prevLocation = _location;
                _prevRotation = _rotation;
                _location = actor.Location;
                _rotation = actor.Rotation;
                if (!_moved && Away(_location - _location0, _rotation, _rotation0))
                {
                    _moved = true;
                }
            }

            // What it shows: the StaticMesh (a damage stage or a trigger swaps it, a broken pane has none).
            string? asset = actor.bDeleteMe ? _shownAsset : actor.StaticMesh.Path;
            if (!ReferenceEquals(asset, _shownAsset) && !string.Equals(asset, _shownAsset, StringComparison.OrdinalIgnoreCase))
            {
                int before = _shownMeshId;
                _shownAsset = asset;
                _shownMeshId = _world.Intern(asset);
                if (!first)
                {
                    _world.Emit(BridgeWorldEventKind.MeshChanged, Slot.NameId, _shownMeshId, before, _location);
                }
            }

            // The actor's Skins over the mesh (UE2 Actor.GetSkin: Skins[i], when set, replaces section i's material).
            if (!actor.bDeleteMe)
            {
                int count = Math.Min(actor.Skins.Count, MaxSkinSlots);
                for (int i = 0; i < MaxSkinSlots; i++)
                {
                    string? skin = i < count ? actor.Skins[i].Path : null;
                    if (ReferenceEquals(skin, _skins[i]) || string.Equals(skin, _skins[i], StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    _skins[i] = skin;
                    if (!first)
                    {
                        _world.Emit(BridgeWorldEventKind.SkinChanged, Slot.NameId, i, _world.Intern(skin), _location);
                    }
                }
            }

            if (_glass && actor is SVitre pane)
            {
                // GlassBreakViews.IsBroken; the impact is given only when the simulation recorded one.
                bool broken = pane.bAlreadyDestroyed || pane.DrawType == EDrawType.DT_None || pane.StaticMesh.IsNone;
                if (broken && !_broken && !first && !pane.bDeleteMe)
                {
                    bool hit = pane.GlassImpactSerial > 0;
                    _world.Emit(BridgeWorldEventKind.GlassBroken, Slot.NameId, hit ? 1 : 0, 0,
                        hit ? pane.GlassImpactLocation : _location, hit ? pane.GlassImpactDirection : default);
                }

                _broken = broken;
            }

            bool moving = !actor.bDeleteMe && DistanceSquared(_prevLocation, _location) > 1e-6f;
            if (moving != _moving)
            {
                _moving = moving;
                if (!first && actor is Mover mover)
                {
                    _world.Emit(BridgeWorldEventKind.MoverMoving, Slot.NameId, moving ? 1 : 0, mover.KeyNum, _location);
                }
            }

            bool shown = IsShown(actor);
            if (shown != _shown)
            {
                _shown = shown;
                if (!first)
                {
                    _world.Emit(BridgeWorldEventKind.ShownChanged, Slot.NameId, shown ? 1 : 0, 0, _location);
                }
            }
        }

        /// <summary>MeshFollower.Draw's visibility, without the Godot node's own state.</summary>
        private bool IsShown(Actor actor)
        {
            // The map's own visibility until the actor changes bHidden; the simulation's for mode-dependent props.
            bool visible = _simDecidesHidden ? !actor.bHidden : actor.bHidden == _hidden0 ? _visible0 : !actor.bHidden;
            // A stage that removes the mesh (a broken window pane) hides it.
            bool meshGone = _asset0 is not null && actor.StaticMesh.IsNone && actor.DrawType == EDrawType.DT_StaticMesh;
            return visible && !meshGone && !actor.bDeleteMe && actor.DrawType != EDrawType.DT_None;
        }

        public BridgeWorldActor Draw(float alpha)
        {
            Actor actor = Slot.Actor;
            int flags = (_shown ? BridgeWorldActorFlags.Visible : 0) | (_moved ? BridgeWorldActorFlags.Moved : 0) |
                (_moving ? BridgeWorldActorFlags.Moving : 0) | (actor.bDeleteMe ? BridgeWorldActorFlags.Gone : 0);
            var drawn = new BridgeWorldActor { NameId = Slot.NameId, MeshId = _shownMeshId };

            // Carried on a pawn's bone (the Story disk on the thigh): the simulation has no posed skeleton, so the
            // relative attachment goes out and C++ hangs it on the drawn bone.
            if (!actor.bDeleteMe && !actor.AttachmentBone.IsNone && actor.Base is SBasePawn carrier)
            {
                drawn.Flags = flags | BridgeWorldActorFlags.Attached | BridgeWorldActorFlags.Moved;
                drawn.AttachPawnId = carrier.ActorId;
                if (actor.AttachmentBone != _attachBone)
                {
                    _attachBone = actor.AttachmentBone;
                    _attachBoneId = _world.Intern(_attachBone.ToString());
                }

                drawn.AttachBoneId = _attachBoneId;
                drawn.X = actor.RelativeLocation.X;
                drawn.Y = actor.RelativeLocation.Y;
                drawn.Z = actor.RelativeLocation.Z;
                drawn.Pitch = LerpAxis(actor.RelativeRotation.Pitch, actor.RelativeRotation.Pitch, 0f);
                drawn.Yaw = LerpAxis(actor.RelativeRotation.Yaw, actor.RelativeRotation.Yaw, 0f);
                drawn.Roll = LerpAxis(actor.RelativeRotation.Roll, actor.RelativeRotation.Roll, 0f);
                return drawn;
            }

            bool cut = DistanceSquared(_prevLocation, _location) > CutDistanceSquared;
            float a = cut ? 1f : alpha;
            FVector location = Lerp(_prevLocation, _location, a);
            var rotation = new FRotator(
                _prevRotation.Pitch + (int)(FRotator.NormalizeAxis(_rotation.Pitch - _prevRotation.Pitch) * a),
                _prevRotation.Yaw + (int)(FRotator.NormalizeAxis(_rotation.Yaw - _prevRotation.Yaw) * a),
                _prevRotation.Roll + (int)(FRotator.NormalizeAxis(_rotation.Roll - _prevRotation.Roll) * a));

            // The level actor's origin is the mesh's origin: Location less the rotated PrePivot x Scale, as the
            // importer placed it (world = ((local - PrePivot) x Scale) rotated + Location).
            BridgeWorldActorInfo info = Info;
            if (info.PrePivotX != 0f || info.PrePivotY != 0f || info.PrePivotZ != 0f)
            {
                rotation.GetAxes(out FVector x, out FVector y, out FVector z);
                location -= (x * (info.PrePivotX * info.ScaleX)) + (y * (info.PrePivotY * info.ScaleY)) + (z * (info.PrePivotZ * info.ScaleZ));
            }

            drawn.Flags = flags;
            drawn.X = location.X;
            drawn.Y = location.Y;
            drawn.Z = location.Z;
            drawn.Pitch = LerpAxis(_prevRotation.Pitch, _rotation.Pitch, a);
            drawn.Yaw = LerpAxis(_prevRotation.Yaw, _rotation.Yaw, a);
            drawn.Roll = LerpAxis(_prevRotation.Roll, _rotation.Roll, a);
            return drawn;
        }
    }

    /// <summary>
    /// One light of the map. A light named by an STriggerLight follows that actor's LightType (DeviceViews switches
    /// the map's light by the actor's id); the others keep their placed type. The light type's curve is
    /// ActorLightRules.Modulation, as WorldLights.Modulation applies it.
    /// </summary>
    private sealed class LightState
    {
        private readonly RenderLight _light;
        private readonly int _nameId;
        private readonly Slot? _switch;
        private readonly uint _seed;
        private bool _on;
        private int _type;
        private float _period, _phase;
        private float _gain = 1f;
        private float _r, _g, _b;
        private bool _corona;

        public LightState(RenderLight light, int nameId, Slot? @switch)
        {
            _light = light;
            _nameId = nameId;
            _switch = @switch;
            _type = light.LightType;
            _on = light.InitiallyOn && light.LightType != 0;
            _period = light.Period;
            _phase = light.Phase;
            (_r, _g, _b) = (light.R, light.G, light.B);
            uint seed = 2166136261;
            foreach (char c in light.Name)
            {
                seed = (seed ^ c) * 16777619;
            }

            _seed = seed;
        }

        public void OnTick(WorldState world, bool first)
        {
            if (_switch?.Actor is not STriggerLight actor)
            {
                return;
            }

            // DeviceViews: on while the actor lives and its LightType is not LT_None.
            bool on = !actor.bDeleteMe && actor.LightType != ELightType.LT_None;
            int type = (int)actor.LightType;
            // A light put out by a broken or chaffed device flickers for half a second first (STriggerLight.LightOff).
            bool breaking = on && actor.LightType == ELightType.LT_Flicker && actor.InitialLightType != ELightType.LT_Flicker;
            if (!first)
            {
                if (breaking && _type != type)
                {
                    world.Emit(BridgeWorldEventKind.LightBreaking, _nameId, 0, type, actor.Location);
                }

                if (on != _on)
                {
                    world.Emit(BridgeWorldEventKind.LightSwitched, _nameId, on ? 1 : 0, type, actor.Location);
                }
            }

            _on = on;
            _type = type;
            _corona = actor.bCorona;
            if (actor.bDeleteMe)
            {
                return;
            }

            _period = actor.LightPeriod;
            _phase = actor.LightPhase;
            _gain = _light.Brightness > 0f ? actor.LightBrightness / _light.Brightness : 1f;
            if (actor.LightHue != _light.Hue || actor.LightSaturation != _light.Saturation)
            {
                // The converter's colour is FGetHSV at full value; the same from the changed hue and saturation.
                LightRgb full = ActorLightRules.Colour(0, 255, 255f);
                LightRgb c = ActorLightRules.Colour(actor.LightHue, actor.LightSaturation, 255f);
                (_r, _g, _b) = (c.R / full.R, c.G / full.R, c.B / full.R);
            }
            else
            {
                (_r, _g, _b) = (_light.R, _light.G, _light.B);
            }
        }

        public BridgeWorldLight Draw(float time, bool strobeOff)
        {
            float scale = 0f;
            if (_on)
            {
                scale = _gain * ActorLightRules.Modulation(_type, _period, _phase, time, _type == 4 ? FrameRandom(time) : 1f,
                    _type == 5 && strobeOff);
            }

            bool breaking = _switch is not null && _on && _type == 4 && _light.LightType != 4;
            return new BridgeWorldLight
            {
                NameId = _nameId,
                Flags = (_on ? BridgeWorldLightFlags.On : 0) | (_switch is not null ? BridgeWorldLightFlags.Triggered : 0) |
                    (breaking ? BridgeWorldLightFlags.Breaking : 0) | (_corona ? BridgeWorldLightFlags.Corona : 0),
                LightType = _type,
                Scale = scale,
                R = _r,
                G = _g,
                B = _b,
            };
        }

        /// <summary>
        /// The flicker's random value: one per light and frame (a hash of the scene time and the light, not the
        /// original's generator; WorldLights.FrameRandom).
        /// </summary>
        private float FrameRandom(float time)
        {
            uint h = (uint)BitConverter.SingleToInt32Bits(time) ^ _seed;
            h ^= h >> 16;
            h *= 0x7feb352dU;
            h ^= h >> 15;
            h *= 0x846ca68bU;
            h ^= h >> 16;
            return (h >> 8) / 16777216f;
        }
    }

    /// <summary>One gameplay object a client marks in the world, read after each tick.</summary>
    private sealed class Marker
    {
        private readonly Slot _slot;
        private readonly int _kind;
        private BridgeWorldMarker _shown;

        public Marker(Slot slot, int kind)
        {
            _slot = slot;
            _kind = kind;
            // Until the first read of a live actor (a map actor the client destroyed at load and has no copy of yet),
            // the marker stands at its map placement.
            FVector at = slot.Actor.Location;
            _shown = new BridgeWorldMarker { NameId = slot.NameId, Kind = kind, X = at.X, Y = at.Y, Z = at.Z };
        }

        public BridgeWorldMarker Shown => _shown;

        public void OnTick(WorldState world, bool first)
        {
            Actor actor = _slot.Actor;
            if (actor.bDeleteMe)
            {
                // Out of relevancy: the last known values stay, marked.
                _shown.Flags |= BridgeWorldMarkerFlags.Gone;
                return;
            }

            int state = 0;
            int flags = actor.bHidden ? BridgeWorldMarkerFlags.Hidden : 0;
            int owner = 0;
            float progress = 0f, timeLeft = 0f;
            FVector at = actor.Location;
            if (actor is SBasicGamePlayObject { bSecondaryObjective: true })
            {
                flags |= BridgeWorldMarkerFlags.Secondary;
            }

            switch (actor)
            {
                case SObjectiveTrigger trigger:
                    flags |= (trigger.bActive ? BridgeWorldMarkerFlags.Active : 0) |
                        (trigger.bTriggering ? BridgeWorldMarkerFlags.Triggering : 0) |
                        (trigger.bLocked ? BridgeWorldMarkerFlags.Locked : 0) |
                        (trigger.Objective is { ObjectiveTaken: true } ? BridgeWorldMarkerFlags.Taken : 0);
                    if (trigger.bTriggering && trigger.CaptureTime > 0)
                    {
                        // GetTriggerTime: a running capture completes at TriggerTime (its start) + CaptureTime, game time.
                        float left = trigger.GetTriggerTime() - ObjectiveNatives.PreciseElapsedTime(actor.Level);
                        timeLeft = Math.Clamp(left, 0f, trigger.CaptureTime);
                        progress = 1f - (timeLeft / trigger.CaptureTime);
                    }

                    switch (trigger)
                    {
                        case SComputerObjectiveTrigger computer:
                            state = computer.SkinState;
                            break;
                        case SBombTargetObjectiveTrigger bomb:
                            state = bomb.bSkinState ? 1 : 0;
                            break;
                        case SFlag flag:
                            if (flag.FlagCarrier is { bDeleteMe: false } carrier)
                            {
                                owner = carrier.ActorId;
                                at = carrier.Location;
                            }

                            state = owner != 0 || DistanceSquared(flag.Location, flag.InitialLocation) > 1f ? 1 : 0;
                            break;
                    }

                    break;
                case SAlarm alarm:
                    state = alarm.bAlarmOn ? 1 : 0;
                    if (alarm.bAlarmOn)
                    {
                        // SAlarm: StartTime is the game time the countdown started, Duration its length.
                        timeLeft = Math.Clamp(alarm.Duration - (ObjectiveNatives.PreciseElapsedTime(actor.Level) - alarm.StartTime), 0f, alarm.Duration);
                        progress = alarm.Duration > 0 ? 1f - (timeLeft / alarm.Duration) : 0f;
                    }

                    break;
                case SCamera camera:
                    state = camera.LightState;
                    owner = camera.LightState == SCamera.LightAlert && camera.DetectedPawn is { bDeleteMe: false } seen ? seen.ActorId : 0;
                    break;
                case SVolumetricSensor sensor:
                    state = sensor.bLightState;
                    break;
                case SLaserTrigger laser:
                    state = laser.bIsBroken ? 2 : laser.bLaserHidden ? 1 : 0;
                    break;
            }

            bool changed = state != _shown.State || flags != _shown.Flags;
            _shown.State = state;
            _shown.Flags = flags;
            _shown.OwnerPawnId = owner;
            _shown.X = at.X;
            _shown.Y = at.Y;
            _shown.Z = at.Z;
            _shown.Progress = progress;
            _shown.TimeLeft = timeLeft;
            if (changed && !first)
            {
                world.Emit(BridgeWorldEventKind.MarkerChanged, _slot.NameId, state, flags, at);
            }
        }
    }

    /// <summary>
    /// One skeletal prop (DeviceViews.AnimatedMeshView): an SAnimatedMesh shows its animation channel, carried on
    /// from the last tick at the channel's rate; a skeletal fish loops the sequence its script last chose and is
    /// blended between its last two ticks as it swims.
    /// </summary>
    private sealed class Animated
    {
        private readonly WorldState _world;
        private readonly Slot _slot;
        private readonly int _meshId;
        private FVector _prevLocation, _location;
        private FRotator _prevRotation, _rotation;
        private int _generation;
        private int _sequenceId;
        private Name _sequenceName;
        private float _frame, _rate, _end = 1f;
        private bool _loop, _channel, _visible;

        public Animated(WorldState world, Slot slot)
        {
            _world = world;
            _slot = slot;
            _meshId = world.Intern(slot.Actor.Mesh.Path);
            _prevLocation = _location = slot.Actor.Location;
            _prevRotation = _rotation = slot.Actor.Rotation;
        }

        public void OnTick()
        {
            Actor actor = _slot.Actor;
            if (_generation != _slot.Generation)
            {
                _generation = _slot.Generation;
                _location = actor.Location;
                _rotation = actor.Rotation;
            }

            _prevLocation = _location;
            _prevRotation = _rotation;
            _location = actor.Location;
            _rotation = actor.Rotation;
            _visible = !actor.bHidden && !actor.bDeleteMe && actor.DrawType == EDrawType.DT_Mesh;
            Name sequence = default;
            _channel = false;
            switch (actor)
            {
                case SAnimatedMesh mesh:
                    _channel = mesh.AnimSequence is { NumFrames: > 1 };
                    if (mesh.AnimSequence is { } playing)
                    {
                        sequence = playing.Name;
                        // The channel's frame is normalised over the N frames and ends at (N - 1) / N.
                        _end = playing.NumFrames > 1 ? (playing.NumFrames - 1) / (float)playing.NumFrames : 1f;
                    }

                    _frame = mesh.AnimFrame;
                    _rate = mesh.AnimFrameRate;
                    _loop = mesh.AnimLooping;
                    break;
                case SFish fish:
                    sequence = fish.LoopedAnim;
                    _loop = true;
                    break;
                case SFish_Follow follower:
                    sequence = follower.LoopedAnim;
                    _loop = true;
                    break;
            }

            if (sequence != _sequenceName)
            {
                _sequenceName = sequence;
                _sequenceId = sequence.IsNone ? 0 : _world.Intern(sequence.ToString());
            }
        }

        public BridgeWorldAnimated Draw(float alpha)
        {
            Actor actor = _slot.Actor;
            bool cut = DistanceSquared(_prevLocation, _location) > CutDistanceSquared;
            float a = cut ? 1f : alpha;
            FVector location = Lerp(_prevLocation, _location, a);
            FVector scale = actor.DrawScale3D * actor.DrawScale;
            float frame = -1f, rate = 1f;
            if (_channel)
            {
                // AnimatedMeshView.DrawChannel: carried on from the last tick, wrapped when looping.
                float f = _frame + (_rate * alpha * TickSeconds);
                if (_loop && f > _end)
                {
                    f %= _end;
                }

                frame = Math.Clamp(f, 0f, _end) / _end;
                rate = _rate / _end;
            }
            else if (actor is SAnimatedMesh)
            {
                frame = 0f;
                rate = 0f;
            }

            return new BridgeWorldAnimated
            {
                NameId = _slot.NameId,
                MeshId = _meshId,
                SequenceId = _sequenceId,
                Flags = (_visible ? BridgeWorldAnimatedFlags.Visible : 0) | (_loop ? BridgeWorldAnimatedFlags.Looping : 0),
                X = location.X,
                Y = location.Y,
                Z = location.Z,
                Pitch = LerpAxis(_prevRotation.Pitch, _rotation.Pitch, a),
                Yaw = LerpAxis(_prevRotation.Yaw, _rotation.Yaw, a),
                Roll = LerpAxis(_prevRotation.Roll, _rotation.Roll, a),
                ScaleX = scale.X,
                ScaleY = scale.Y,
                ScaleZ = scale.Z,
                Frame = frame,
                Rate = rate,
            };
        }
    }
}
