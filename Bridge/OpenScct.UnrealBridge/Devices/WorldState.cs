using System;
using System.Collections.Generic;
using OpenScct.Content;
using OpenScct.Content.Loading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Lighting;
using OpenScct.Game.Objectives;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Devices;

/// <summary>
/// The dynamic state of the level for Unreal: every map actor that moves, hides or changes what it shows, the map's
/// lights as they shine now, the laser beams, the gameplay objects a client marks, and the state changes between
/// frames as events. It follows the client world's actors after each tick and hands them out blended between the
/// last two ticks. It reads the simulation and never writes it.
/// <para>A port of the Godot client's device drawing without the drawing: game/Scripts/Views/DeviceViews.cs
/// (MeshFollower, AnimatedMeshView, the trigger light switches, Rebind, the drop zone lids),
/// Views/CameraMountViews.cs, Views/GlassBreakViews.cs (when a pane breaks), Fx/LaserTriggerView.cs and
/// Render/WorldLights.cs (Modulation) and, for lights only, Views/AttachedEffects.cs (a light that rides a mover or
/// hangs on a prop's bone). Not ported: conveyors (Views/ConveyorViews.cs, an OpenSCCT addition the stock maps do
/// not use), the emitters that ride a mover or a bone (AttachedEffects), the bHardAttach frame of a rider (no
/// Enhanced map sets it), the PS2 objective computers' own animation, and the animation studio's poses.</para>
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
    private readonly Dictionary<string, Slot> _slotById = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, (int Prop, int Bone)> _renderBoneLights = new(StringComparer.OrdinalIgnoreCase);
    private int _rebindActorCount = -1;
    private int _spawnScanActorCount = -1;
    private float _time;
    private float _strobeTime = float.NaN;
    private bool _strobeOff;

    private WorldState(BridgeSession session)
    {
        _session = session;
        _render = MapRenderIndex.Load(session.Session.MapFolder);
        // A session that already ticked has its map: bind now so the first read is not empty. The tick hook is
        // added last, so a bind that throws leaves no handler of a dead object on the session.
        TryBind();
        session.Ticked += OnTick;
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

    /// <summary>True when <paramref name="actor"/> stands behind a name this state hands out (for the probe).</summary>
    public bool IsFollowed(Actor actor) => _nameOfActor.ContainsKey(actor);

    /// <summary>The name id this state hands out for <paramref name="actor"/>, 0 when it does not follow it.</summary>
    public int NameIdOf(Actor actor) => _nameOfActor.GetValueOrDefault(actor);

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

        // _loaded is set only once everything is bound, so a bind that threw part of the way starts clean here.
        _slots.Clear();
        _slotById.Clear();
        _followers.Clear();
        _followerByName.Clear();
        _lights.Clear();
        _markers.Clear();
        _animated.Clear();
        _nameOfActor.Clear();
        _spawnedActors.Clear();
        _renderBoneLights.Clear();
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
            // A light of lighting.json with a simulation actor: an STriggerLight (switched), or any actor that
            // carries the light with it (a lift door's lamp, a light riding a mover).
            bool light = actor is STriggerLight || _render.FindLight(record.Id) is not null;
            if (!mesh && !skeletal && markerKind == 0 && !light)
            {
                continue;
            }

            var slot = new Slot(record.Id, Intern(record.Id), actor);
            _slots.Add(slot);
            _slotById.TryAdd(record.Id, slot);
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

        BindRenderBoneLights(loaded);
        foreach (RenderLight light in _render.Lights)
        {
            _renderBoneLights.TryGetValue(light.Name, out (int Prop, int Bone) bone);
            _lights.Add(new LightState(light, Intern(light.Name), _slotById.GetValueOrDefault(light.Name), bone.Prop, bone.Bone));
        }

        // The first state is the map's, not a change: take it without events.
        _loaded = loaded;
        AfterTick(loaded.Level, first: true);
    }

    /// <summary>
    /// The lights a skeletal prop's ActorsToAttach hangs on its bones that have no simulation actor: the converter
    /// keeps plain Light actors in the render part only, so the map names them as "Map.LightName" assets (SteeE's
    /// crane flares). AttachedEffects' BoneLight. Lights with a simulation actor are read from the actor each tick.
    /// </summary>
    private void BindRenderBoneLights(MapLoadResult loaded)
    {
        string prefix = loaded.Gameplay.Map + ".";
        foreach (ActorRecord record in loaded.Gameplay.Actors)
        {
            if (loaded.ActorOf(record) is not SAnimatedMesh || !_slotById.TryGetValue(record.Id, out Slot? prop) ||
                !record.Props.TryGetValue("ActorsToAttach", out PropValue attachments) || attachments.Kind != PropKind.Array)
            {
                continue;
            }

            foreach (PropValue attachment in attachments.AsArray())
            {
                if (attachment.Kind != PropKind.Struct)
                {
                    continue;
                }

                PropBag entry = attachment.AsStruct();
                if (!entry.TryGetValue("ActorName", out PropValue target) || target.Kind != PropKind.Asset ||
                    !entry.TryGetValue("BoneName", out PropValue bone) || bone.Kind != PropKind.String)
                {
                    continue;
                }

                string path = target.AsAsset().Path;
                if (path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) && _render.FindLight(path[prefix.Length..]) is { } light)
                {
                    _renderBoneLights.TryAdd(light.Name, (prop.NameId, Intern(bone.AsString())));
                }
            }
        }
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
                // C++ is told, so it can remove what it spawned for it without comparing lists.
                Emit(BridgeWorldEventKind.ShownChanged, f.Slot.NameId, 0, 1, f.Slot.Actor.Location);
                _spawnScanActorCount = -1;
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

        if (!first)
        {
            TriggeredEmitters(level);
        }
    }

    /// <summary>
    /// The map Emitters the event graph triggered this tick (their own notify, or the server's relayed to this
    /// client), as the Godot client finds them (PlayRoot.TriggerMapEmitters): one event each, by the map name.
    /// </summary>
    private void TriggeredEmitters(Level level)
    {
        if (_loaded is not { } loaded)
        {
            return;
        }

        foreach (ref readonly SimEvent e in _session.Session.Client.World.Events.Current)
        {
            if (e.Kind == SimEventKind.Notify && e.What == Emitter.NAME_ReplicateTrigger &&
                level.FindActor(e.ActorId) is Emitter actor && loaded.RecordOf(actor)?.Id is { } id)
            {
                Emit(BridgeWorldEventKind.EmitterTriggered, Intern(id), 0, 0, actor.Location);
            }
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
    /// owner's name with a suffix; one is followed per owner (a client has its own spawn of a mount and may also
    /// get the server's copy).
    /// <para>The lid and the rack are spawned by the server only and their Owner is sent only to the connection that
    /// owns them, so a client can see them without one. Those are matched by mesh and by where their owner spawns
    /// them, as DeviceViews.BindCovers does for the lid.</para>
    /// </summary>
    private void FollowSpawned(Level level)
    {
        // The count check misses a prop that came in the tick another actor went; the slow rescan picks it up.
        if (level.ActorCount == _spawnScanActorCount && level.TickCount % RebindRescanTicks != 0)
        {
            return;
        }

        _spawnScanActorCount = level.ActorCount;
        foreach (SBasicGamePlayObject a in level.DynamicActors<SBasicGamePlayObject>())
        {
            if (a.bDeleteMe || a.GetType() != typeof(SBasicGamePlayObject) || a.StaticMesh.IsNone ||
                _spawnedActors.Contains(a) || _nameOfActor.ContainsKey(a))
            {
                continue;
            }

            int ownerName;
            string suffix;
            if (a.Owner is { } owner)
            {
                if (!_nameOfActor.TryGetValue(owner, out ownerName))
                {
                    continue;
                }

                suffix = owner switch
                {
                    SCamera => "_Mount",
                    SFlagDropZone => "_Cover",
                    SFlag => "_Server",
                    _ => "_Prop",
                };
            }
            else if (!TryFindSpawner(a, out ownerName, out suffix))
            {
                continue;
            }

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

    /// <summary>How far a replicated prop may stand from where its owner spawned it and still be taken as that owner's (DeviceViews.BindCovers: 3 units).</summary>
    private const float SpawnPlaceSlackSquared = 9f;

    /// <summary>SFlagDropZone.PostBeginPlay: the lid is spawned at Location + (0, 0, 10) - 10 along the zone's forward axis.</summary>
    private const float LidOffsetUp = 10f, LidOffsetBack = 10f;

    /// <summary>
    /// The map actor that spawned the ownerless plain prop <paramref name="prop"/>: the drop zone whose lid it is
    /// (the lid mesh, at the zone's lid place) or the disk whose rack it is (the disk's own map mesh, at the disk's
    /// home, where SFlag.PostBeginPlay spawns it).
    /// </summary>
    private bool TryFindSpawner(SBasicGamePlayObject prop, out int ownerName, out string suffix)
    {
        foreach (Marker m in _markers)
        {
            Actor owner = m.Slot.Actor;
            if (owner.bDeleteMe)
            {
                continue;
            }

            if (owner is SFlagDropZone zone)
            {
                FVector lid = zone.Location + new FVector(0f, 0f, LidOffsetUp) - (zone.Rotation.Vector() * LidOffsetBack);
                if (prop.StaticMesh == SFlagDropZone.LidMesh && DistanceSquared(prop.Location, lid) <= SpawnPlaceSlackSquared)
                {
                    ownerName = m.Slot.NameId;
                    suffix = "_Cover";
                    return true;
                }
            }
            else if (owner is SFlag && m.Slot.IsAtPlacement(prop.Location) &&
                string.Equals(prop.StaticMesh.Path, m.Slot.MapMesh, StringComparison.OrdinalIgnoreCase))
            {
                ownerName = m.Slot.NameId;
                suffix = "_Server";
                return true;
            }
        }

        ownerName = 0;
        suffix = "";
        return false;
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

    /// <summary>How many material slots <see cref="GetActorSkins"/> reports.</summary>
    public const int SkinSlots = MaxSkinSlots;

    /// <summary>
    /// Writes the skins a followed actor wears now, one name id per material slot (0: the mesh's own material), as
    /// UE2's Actor.Skins: Skins[i], when set, replaces the material of the mesh's section i. Returns how many slots
    /// there are (<see cref="SkinSlots"/>; only what fits is written), 0 when <paramref name="nameId"/> is not a
    /// followed actor. This is the state the SkinChanged events change: read it when an actor is first seen.
    /// </summary>
    public int GetActorSkins(int nameId, Span<int> skins)
    {
        if (!_followerByName.TryGetValue(nameId, out Follower? follower))
        {
            return 0;
        }

        follower.SkinIds[..Math.Min(skins.Length, MaxSkinSlots)].CopyTo(skins);
        return MaxSkinSlots;
    }

    /// <summary>Writes every light of the map as it shines and stands this frame. Returns how many there are.</summary>
    public int GetLights(Span<BridgeWorldLight> lights)
    {
        float alpha = Math.Clamp(_session.Alpha, 0f, 1f);
        // The light types run on scene time, carried on from the last tick so a pulse is smooth between ticks.
        float time = _time + (alpha * TickSeconds);
        if (time != _strobeTime)
        {
            // The strobe's shared switch (a static in the original): flipped once per drawn frame (WorldLights.StrobeOff).
            _strobeTime = time;
            _strobeOff = !_strobeOff;
        }

        int count = Math.Min(lights.Length, _lights.Count);
        for (int i = 0; i < count; i++)
        {
            lights[i] = _lights[i].Draw(time, _strobeOff, alpha);
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

    /// <summary>True when two rotations point the same way (their axes wrapped).</summary>
    private static bool SameTurn(FRotator a, FRotator b) =>
        FRotator.NormalizeAxis(a.Pitch - b.Pitch) == 0 && FRotator.NormalizeAxis(a.Yaw - b.Yaw) == 0 &&
        FRotator.NormalizeAxis(a.Roll - b.Roll) == 0;

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
            MapMesh = actor.StaticMesh.Path;
        }

        public string Id { get; }

        /// <summary>The static mesh the actor had when the slot was made (the map's), or null.</summary>
        public string? MapMesh { get; }

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
        private readonly int[] _skinIds = new int[MaxSkinSlots];
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

        /// <summary>The skin worn in each material slot as a name id, 0 for the mesh's own material.</summary>
        public ReadOnlySpan<int> SkinIds => _skinIds;

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
                    _skinIds[i] = _world.Intern(skin);
                    if (!first)
                    {
                        _world.Emit(BridgeWorldEventKind.SkinChanged, Slot.NameId, i, _skinIds[i], _location);
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

            // A door that swings only turns: the rotation counts as much as the location.
            bool moving = !actor.bDeleteMe &&
                (DistanceSquared(_prevLocation, _location) > 1e-6f || !SameTurn(_prevRotation, _rotation));
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
    /// ActorLightRules.Modulation, as WorldLights.Modulation applies it. A light with any simulation actor also
    /// follows where that actor is (a lift door's lamp, a light riding a mover) or names the bone it hangs on
    /// (AttachedEffects).
    /// </summary>
    private sealed class LightState
    {
        private readonly RenderLight _light;
        private readonly int _nameId;
        private readonly Slot? _slot;
        private readonly int _renderProp, _renderBone;
        private readonly uint _seed;
        private bool _on;
        private int _type;
        private float _period, _phase;
        private float _gain = 1f;
        private float _r, _g, _b;
        private bool _corona;
        private FVector _prevLocation, _location;
        private FRotator _prevRotation, _rotation;
        private bool _moved;
        private int _generation;
        private int _boneProp, _boneId;
        private Name _boneName;

        /// <param name="renderProp">The skeletal prop a render-only light hangs on (name id), 0 when none.</param>
        /// <param name="renderBone">That prop's bone (name id).</param>
        public LightState(RenderLight light, int nameId, Slot? slot, int renderProp, int renderBone)
        {
            _light = light;
            _nameId = nameId;
            _slot = slot;
            _renderProp = renderProp;
            _renderBone = renderBone;
            _type = light.LightType;
            _on = light.InitiallyOn && light.LightType != 0;
            _period = light.Period;
            _phase = light.Phase;
            (_r, _g, _b) = (light.R, light.G, light.B);
            _prevLocation = _location = light.Location;
            _prevRotation = _rotation = light.Rotation;
            uint seed = 2166136261;
            foreach (char c in light.Name)
            {
                seed = (seed ^ c) * 16777619;
            }

            _seed = seed;
        }

        public void OnTick(WorldState world, bool first)
        {
            if (_slot?.Actor is not { } actor)
            {
                return;
            }

            if (!actor.bDeleteMe)
            {
                Place(world, actor);
            }

            if (actor is STriggerLight trigger)
            {
                Switch(world, trigger, first);
            }
        }

        /// <summary>Where the light's actor is, and the bone it hangs on (AttachedEffects.Follower and Place).</summary>
        private void Place(WorldState world, Actor actor)
        {
            if (_generation != _slot!.Generation)
            {
                // Another actor took the slot: no blend from where the last one was.
                _generation = _slot.Generation;
                _location = actor.Location;
                _rotation = actor.Rotation;
            }

            _prevLocation = _location;
            _prevRotation = _rotation;
            _location = actor.Location;
            _rotation = actor.Rotation;
            if (!_moved && Away(_location - _light.Location, _rotation, _light.Rotation))
            {
                _moved = true;
            }

            // SAnimatedMesh.AttachActorToBone: based on the prop with the bone's name; the views pose it.
            if (!actor.AttachmentBone.IsNone && actor.Base is SAnimatedMesh { bDeleteMe: false } prop &&
                world._nameOfActor.TryGetValue(prop, out int propName))
            {
                _boneProp = propName;
                if (actor.AttachmentBone != _boneName)
                {
                    _boneName = actor.AttachmentBone;
                    _boneId = world.Intern(_boneName.ToString());
                }
            }
            else
            {
                _boneProp = 0;
            }
        }

        private void Switch(WorldState world, STriggerLight actor, bool first)
        {
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
                // The converter's colour is FGetHSV at full value over its full brightness (0.8213, what white
                // gives); the same from the changed hue and saturation.
                float full = ActorLightRules.Colour(0, 255, 255f).R;
                LightRgb c = ActorLightRules.Colour(actor.LightHue, actor.LightSaturation, 255f);
                (_r, _g, _b) = (c.R / full, c.G / full, c.B / full);
            }
            else
            {
                (_r, _g, _b) = (_light.R, _light.G, _light.B);
            }
        }

        public BridgeWorldLight Draw(float time, bool strobeOff, float alpha)
        {
            float scale = 0f;
            if (_on)
            {
                scale = _gain * ActorLightRules.Modulation(_type, _period, _phase, time, _type == 4 ? FrameRandom(time) : 1f,
                    _type == 5 && strobeOff);
            }

            bool triggered = _slot?.Actor is STriggerLight;
            bool breaking = triggered && _on && _type == 4 && _light.LightType != 4;
            var drawn = new BridgeWorldLight
            {
                NameId = _nameId,
                Flags = (_on ? BridgeWorldLightFlags.On : 0) | (triggered ? BridgeWorldLightFlags.Triggered : 0) |
                    (breaking ? BridgeWorldLightFlags.Breaking : 0) | (_corona ? BridgeWorldLightFlags.Corona : 0) |
                    (_moved ? BridgeWorldLightFlags.Moved : 0),
                LightType = _type,
                Scale = scale,
                R = _r,
                G = _g,
                B = _b,
            };

            int prop = _boneProp != 0 ? _boneProp : _renderProp;
            if (prop != 0)
            {
                // The simulation has no posed skeleton: the bone and the relative attachment go out and C++ hangs the
                // light on the drawn bone. The map's attachments sit on the bone (both relative values zero).
                Actor? actor = _boneProp != 0 ? _slot!.Actor : null;
                FVector at = actor?.RelativeLocation ?? default;
                FRotator turn = actor?.RelativeRotation ?? default;
                drawn.Flags |= BridgeWorldLightFlags.OnBone;
                drawn.AttachNameId = prop;
                drawn.AttachBoneId = _boneProp != 0 ? _boneId : _renderBone;
                drawn.X = at.X;
                drawn.Y = at.Y;
                drawn.Z = at.Z;
                drawn.Pitch = LerpAxis(turn.Pitch, turn.Pitch, 0f);
                drawn.Yaw = LerpAxis(turn.Yaw, turn.Yaw, 0f);
                drawn.Roll = LerpAxis(turn.Roll, turn.Roll, 0f);
                return drawn;
            }

            float a = DistanceSquared(_prevLocation, _location) > CutDistanceSquared ? 1f : alpha;
            FVector location = Lerp(_prevLocation, _location, a);
            drawn.X = location.X;
            drawn.Y = location.Y;
            drawn.Z = location.Z;
            drawn.Pitch = LerpAxis(_prevRotation.Pitch, _rotation.Pitch, a);
            drawn.Yaw = LerpAxis(_prevRotation.Yaw, _rotation.Yaw, a);
            drawn.Roll = LerpAxis(_prevRotation.Roll, _rotation.Roll, a);
            return drawn;
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

        public Slot Slot => _slot;

        public void OnTick(WorldState world, bool first)
        {
            Actor actor = _slot.Actor;
            if (actor.bDeleteMe)
            {
                // Out of relevancy: the last known values stay, marked. Said once, like any other change of flags.
                if ((_shown.Flags & BridgeWorldMarkerFlags.Gone) == 0)
                {
                    _shown.Flags |= BridgeWorldMarkerFlags.Gone;
                    if (!first)
                    {
                        world.Emit(BridgeWorldEventKind.MarkerChanged, _slot.NameId, _shown.State, _shown.Flags,
                            new FVector(_shown.X, _shown.Y, _shown.Z));
                    }
                }

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
