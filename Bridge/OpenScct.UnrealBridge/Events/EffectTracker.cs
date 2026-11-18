using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.Combat;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScctGame.Play.Session;
using K = OpenScct.Game.Equipment.K;

namespace OpenScct.UnrealBridge.Events;

/// <summary>
/// The effects of the client world a client must draw, found as the Godot client finds them. It reads the world
/// after each tick and never writes to it.
/// <list type="bullet">
/// <item>Effect actors (EffectViews.OnTick, game/Scripts/Fx/EffectViews.cs): every actor spawned during play whose
/// class is an effect class of shared/fx/effects.json: Emitter actors (smoke, chaff, explosions, gas, sparks of
/// devices), thrown and placed gadgets drawn as static meshes, beams, sprinkler puddles, flash lights; and the PS2
/// modem's destruction preset (Ps2ModemEffect), which that table cannot name.</item>
/// <item>Shots (CombatFxView, game/Scripts/Fx/CombatFxView.cs): the muzzle flashes (the SMuzzleSticky and
/// ClientFireMuzzle notifies, and the local merc gun's SpawnSFX count) and the tracers (SFireTrace actors).</item>
/// <item>Impact marks: the level's ImpactTable, read live.</item>
/// </list>
/// </summary>
internal sealed class EffectTracker
{
    private const float RotatorToDegrees = 360f / 65536f;

    /// <summary>A step longer than this in one tick was a placement, not a move: no blend (as the pawns).</summary>
    private const float CutDistanceSquared = 40000f;

    /// <summary>A light reaches 25 x (LightRadius + 1) units (RenderK.LightRadiusUnits in the Godot client).</summary>
    private const float LightRadiusUnits = 25f;

    // Effect classes the fx step lists only since a later converter version (FxLibrary.AddLateClasses).
    private static readonly (string Class, int Kind)[] s_lateClasses =
    [
        ("SBase.SGasMask", BridgeEffectKind.Gadget),
        ("SBase.SBackPack", BridgeEffectKind.Gadget),
        ("SBase.SBackPack_Att", BridgeEffectKind.Gadget),
    ];

    /// <summary>
    /// Effect classes of OpenSCCT's own that shared/fx/effects.json cannot list, found by their runtime type: the PS2
    /// modem's destruction preset is a plain Emitter to the class table (it has no UnrealScript class of its own), so
    /// it would be taken for every other Emitter. EffectViews draws it by type too (the Ps2ModemEffect branch).
    /// </summary>
    private static readonly (Type Type, string Class, int Kind)[] s_runtimeClasses =
    [
        (typeof(Ps2ModemEffect), "SBase.Ps2ModemEffect", BridgeEffectKind.Emitter),
    ];

    private static readonly Name SpawnSFX = new("SpawnSFX");

    private readonly PlaySession _session;
    private readonly DrainQueue<BridgeShot> _shots;
    private readonly Dictionary<string, int> _kinds = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<UcClassInfo, ClassEntry?> _classes = [];
    private readonly Dictionary<Type, ClassEntry> _runtimeEntries = [];
    private readonly Dictionary<Actor, Tracked> _tracked = new(ReferenceEqualityComparer.Instance);
    private readonly List<Tracked> _order = [];
    private readonly HashSet<Actor> _tracers = new(ReferenceEqualityComparer.Instance);
    private readonly List<Actor> _gone = [];
    private readonly HashSet<Actor> _placedPuddles = new(ReferenceEqualityComparer.Instance);
    private readonly List<(Name Kind, Actor? Source)> _pendingFlashes = [];
    private readonly ConditionalWeakTable<SWeaponDefense, MuzzleCursor> _muzzleCursors = new();
    private Level? _level;
    private long _tick;
    private long _ticksSeen;

    public EffectTracker(PlaySession session, string? sharedRoot, DrainQueue<BridgeShot> shots)
    {
        _session = session;
        _shots = shots;
        if (sharedRoot is not null)
        {
            LoadClasses(Path.Combine(sharedRoot, "fx", "effects.json"));
        }

        foreach ((string cls, int kind) in s_lateClasses)
        {
            _kinds.TryAdd(cls, kind);
        }
    }

    /// <summary>Effect classes known from the content (0: shared/fx is missing and no effect actor is reported).</summary>
    public int ClassCount => _kinds.Count;

    /// <summary>Effect actors reported since the session started, by class name.</summary>
    public Dictionary<string, int> Seen { get; } = new(StringComparer.Ordinal);

    public long MuzzleFlashes { get; private set; }

    public long Tracers { get; private set; }

    /// <summary>A muzzle notify of the tick (CombatFx.MuzzleSticky or ThirdPersonMuzzle); placed at the end of the tick.</summary>
    public void OnNotify(in SimEvent e)
    {
        if (e.What == CombatFx.MuzzleSticky || e.What == CombatFx.ThirdPersonMuzzle)
        {
            _pendingFlashes.Add((e.What, _session.Client.World.Level.FindActor(e.ActorId)));
        }
    }

    /// <summary>After a client tick: the new effect actors, the ones that went, the tick's flashes and tracers.</summary>
    public void OnTick()
    {
        Level level = _session.Client.World.Level;
        _tick = level.TickCount;
        if (!ReferenceEquals(level, _level))
        {
            _level = level;
            _tracked.Clear();
            _order.Clear();
            _tracers.Clear();

            // The puddles the map places are there from the load and belong to the map's own geometry.
            _placedPuddles.Clear();
            foreach (SFlaque placed in level.AllActors<SFlaque>())
            {
                _placedPuddles.Add(placed);
            }
        }

        TrackRifleFlashes(_session.LocalPawn?.MyWeapon as SWeaponDefense);
        _ticksSeen++;
        foreach ((Name kind, Actor? source) in _pendingFlashes)
        {
            AddFlash(kind, source);
        }

        _pendingFlashes.Clear();
        TrackTracers(level);
        TrackActors(level);
    }

    // ---- effect actors ---------------------------------------------------------------------------------------

    private void TrackActors(Level level)
    {
        foreach (Tracked t in _order)
        {
            if (t.Deleted)
            {
                continue;
            }

            if (t.Actor.bDeleteMe)
            {
                t.Deleted = true;
                t.Killed = t.Actor is Emitter { Killed: true };
                continue;
            }

            t.Previous = t.Current;
            t.Current = PlaceOf(t.Actor);
            t.HasPrevious = true;
        }

        // Indexed, not foreach over the interface (a boxed enumerator every tick).
        IReadOnlyList<Actor> actors = level.Actors;
        for (int i = 0; i < actors.Count; i++)
        {
            Actor a = actors[i];
            if (a is null || a.bDeleteMe || a.bNoDelete || a.bStatic || _tracked.ContainsKey(a) || ClassOf(a) is not { } cls)
            {
                continue;
            }

            // What EffectViews draws of each kind: an Emitter actor's particles, the static mesh of the mesh kinds, a
            // sprinkler's puddle (not the map's own), a light actor's light.
            bool drawn = cls.Kind switch
            {
                BridgeEffectKind.Emitter => a is Emitter,
                BridgeEffectKind.Projectile or BridgeEffectKind.Mine or BridgeEffectKind.Gadget or BridgeEffectKind.Beam =>
                    a.DrawType == EDrawType.DT_StaticMesh,
                BridgeEffectKind.Decal => a is SFlaque && !_placedPuddles.Contains(a) && a.DrawType == EDrawType.DT_StaticMesh,
                BridgeEffectKind.Light => true,
                _ => false,
            };
            if (!drawn)
            {
                continue;
            }

            var t = new Tracked(a, cls)
            {
                FirstTick = _tick,
                Current = PlaceOf(a),
                TrailClassId = TrailFor(a),

                // EffectViews.StartSpawnable: a spawnable emitter runs the particle emitter of the map its Emitters[0] names.
                ParticleId = a is SSpawnableEmitter { Emitters.Count: > 0 } spawnable ? EventNames.Id(spawnable.Emitters[0].Path) : 0,
            };
            _tracked[a] = t;
            _order.Add(t);
            string seen = cls.Name ?? a.GetUcClass().Name;
            Seen[seen] = Seen.GetValueOrDefault(seen) + 1;
        }
    }

    /// <summary>
    /// Writes the effect actors as drawn this frame. Returns the count written. An actor that went is written once
    /// more with the Deleted flag and then forgotten. The ones that went are written first, so a buffer that is too
    /// small for everything never loses an ending (a live actor it has no room for is simply not drawn this frame).
    /// </summary>
    public int Write(Span<BridgeEffectActor> into, float alpha)
    {
        int count = 0;
        for (int i = 0; i < _order.Count && count < into.Length; i++)
        {
            Tracked t = _order[i];
            if (t.Deleted)
            {
                into[count++] = RecordOf(t, alpha);
                t.Reported = true;
            }
        }

        for (int i = 0; i < _order.Count && count < into.Length; i++)
        {
            Tracked t = _order[i];
            if (!t.Deleted)
            {
                into[count++] = RecordOf(t, alpha);
            }
        }

        // The ones that went and have now been told are forgotten.
        for (int i = _order.Count - 1; i >= 0; i--)
        {
            if (_order[i].Reported)
            {
                _tracked.Remove(_order[i].Actor);
                _order.RemoveAt(i);
            }
        }

        return count;
    }

    private BridgeEffectActor RecordOf(Tracked t, float alpha)
    {
        Actor a = t.Actor;
        bool blend = t.HasPrevious && !t.Deleted && FVector.DistSquared(t.Previous, t.Current) <= CutDistanceSquared;
        FVector at = blend ? t.Previous + ((t.Current - t.Previous) * alpha) : t.Current;
        FVector scale = a.DrawScale3D * a.DrawScale;
        Actor? attachedTo = a.Base ?? a.Owner;

        // EffectViews.PlaceOf and BoneMeshFrame: only a pawn has a drawn skeleton to follow. An effect on a gun's bone
        // was already placed at the simulation's estimate of that bone (PlaceOf below).
        bool onBone = !a.AttachmentBone.IsNone && attachedTo is SBasePawn;
        return new BridgeEffectActor
        {
            ActorId = a.ActorId,
            ClassId = t.Class.ClassId,
            Kind = t.Class.Kind,
            Flags = (a.bHidden || WaitsForWearer(a) ? BridgeEffectFlags.Hidden : 0) | (t.Deleted ? BridgeEffectFlags.Deleted : 0) |
                (t.Killed ? BridgeEffectFlags.Killed : 0) | (a.bDynamicLight ? BridgeEffectFlags.DynamicLight : 0) |
                (onBone ? BridgeEffectFlags.OnBone : 0) | (a.bUnlit || a is STazerEmitter ? BridgeEffectFlags.Unlit : 0) |
                (DrawnByOwnView(a) ? BridgeEffectFlags.OwnView : 0) | (blend ? 0 : BridgeEffectFlags.Snapped) |
                (a.bHideInNormalVision ? BridgeEffectFlags.VisionOnly : 0),
            MeshId = a.DrawType == EDrawType.DT_StaticMesh ? MeshId(t, a) : 0,
            TrailClassId = t.TrailClassId,
            BaseActorId = attachedTo?.ActorId ?? 0,
            BoneId = EventNames.Id(a.AttachmentBone),
            InstigatorId = a.Instigator?.ActorId ?? 0,
            ParticleId = t.ParticleId,
            X = at.X,
            Y = at.Y,
            Z = at.Z,
            Pitch = a.Rotation.Pitch * RotatorToDegrees,
            Yaw = a.Rotation.Yaw * RotatorToDegrees,
            Roll = a.Rotation.Roll * RotatorToDegrees,
            ScaleX = scale.X,
            ScaleY = scale.Y,
            ScaleZ = scale.Z,
            PrePivotX = a.PrePivot.X,
            PrePivotY = a.PrePivot.Y,
            PrePivotZ = a.PrePivot.Z,
            VelX = a.Velocity.X,
            VelY = a.Velocity.Y,
            VelZ = a.Velocity.Z,
            RelX = a.RelativeLocation.X,
            RelY = a.RelativeLocation.Y,
            RelZ = a.RelativeLocation.Z,
            RelPitch = a.RelativeRotation.Pitch * RotatorToDegrees,
            RelYaw = a.RelativeRotation.Yaw * RotatorToDegrees,
            RelRoll = a.RelativeRotation.Roll * RotatorToDegrees,
            Age = (_tick - t.FirstTick + alpha) * Sim.DeltaTime,
            LifeSpan = a.LifeSpan,
            LightBrightness = LightBrightnessOf(a),
            LightHue = a.LightHue,
            LightSaturation = a.LightSaturation,
            LightRadius = LightRadiusUnits * (a.LightRadius + 1f),
        };
    }

    /// <summary>ShockFxView.BlinkPeriod: the original's LT_Blink period is not decoded.</summary>
    private const float BlinkPeriod = 0.2f;

    /// <summary>
    /// EffectViews.PlaceLight: no light for LT_None; the flash of a frag fades from 255 to 0 over fTempsDisparition
    /// (the port leaves LightBrightness alone and the views work the fade out).
    /// </summary>
    private static float LightBrightnessOf(Actor a)
    {
        if (a.LightType == ELightType.LT_None)
        {
            return 0f;
        }

        // LT_Blink (the shock sparks' light): fully on and off, every 0.2 s as ShockFxView blinks it.
        if (a.LightType == ELightType.LT_Blink && (int)(a.Level.TimeSeconds / BlinkPeriod) % 2 != 0)
        {
            return 0f;
        }

        float brightness = a.LightBrightness;
        if (a is SFlashLight_FragGrenade { fTempsDisparition: > 0f } frag)
        {
            brightness = 255f - ((a.Level.TimeSeconds - frag.fDateDepart) / frag.fTempsDisparition * 255f);
        }

        return Math.Clamp(brightness, 0f, 255f);
    }

    /// <summary>
    /// EffectViews.Draw: the gas mask and the backpack are shown only once they sit on the bone of the pawn that
    /// wears them (the attachment of a remote wearer arrives after the actor), and the mask only while that pawn lives.
    /// </summary>
    private static bool WaitsForWearer(Actor a) => a switch
    {
        SGasMask => !(a.AttachmentBone == K.GasMaskBone && a.Instigator is SBasePawn { Health: > 0, bDeleteMe: false } wearer &&
            ReferenceEquals(a.Base, wearer)),
        SBackPack => !(a.AttachmentBone == K.BackPackBone && a.Instigator is SBasePawn { bDeleteMe: false } carrier &&
            ReferenceEquals(a.Base, carrier)),
        _ => false,
    };

    /// <summary>The mesh the actor draws now (script swaps some at run time), interned once per path.</summary>
    private static int MeshId(Tracked t, Actor a)
    {
        string? path = a.StaticMesh.Path;
        if (!ReferenceEquals(path, t.MeshPath))
        {
            t.MeshPath = path;
            t.MeshId = EventNames.Id(path);
        }

        return t.MeshId;
    }

    /// <summary>
    /// EffectViews.PlaceOf, as far as the simulation knows: an effect on a merc gun's bone (the barrel smoke on FX)
    /// sits at the simulation's place of that bone; one on a pawn's bone is reported with its bone (the drawn
    /// skeleton is Unreal's); others are at their own Location.
    /// </summary>
    private static FVector PlaceOf(Actor a)
    {
        if (!a.AttachmentBone.IsNone && a.Base is SWeaponDefense { bDeleteMe: false } weapon)
        {
            FCoords c = CombatServices.Ports(weapon).BoneCoords(weapon, a.AttachmentBone);
            FVector r = a.RelativeLocation;
            return c.Origin + (c.XAxis * r.X) + (c.YAxis * r.Y) + (c.ZAxis * r.Z);
        }

        return a.Location;
    }

    /// <summary>
    /// EffectViews.TrailFor: the launch trail script spawns for a thrown grenade on every drawing machine (not for a
    /// quick throw): SProjSmoke, SProjChaffe, SProjFlashBang and SProjPhospho.PostBeginPlay spawn an
    /// SSmokeLaunchEmitter, SProjFrag an SFragLaunchEmitter.
    /// </summary>
    private static int TrailFor(Actor a) => a switch
    {
        SProjFrag => EventNames.Id("SBase.SFragLaunchEmitter"),
        SProjGrenade { QuickLaunch: true } => 0,
        SProjSmoke or SProjChaffe or SProjFlashBang or SProjPhospho => EventNames.Id("SBase.SSmokeLaunchEmitter"),
        _ => 0,
    };

    /// <summary>EffectViews.DrawnByOwnView: beams the Godot client draws from the drawn hand or the trigger instead.</summary>
    private static bool DrawnByOwnView(Actor a) =>
        a is SLaserBeam_MineLaser || (a is SLaserBeam && a.Owner is SWeaponAttaque or SGadTorcheLaser or SLaserTrigger);

    private ClassEntry? ClassOf(Actor a)
    {
        foreach ((Type type, string cls, int kind) in s_runtimeClasses)
        {
            if (type.IsInstanceOfType(a))
            {
                return _runtimeEntries.TryGetValue(type, out ClassEntry? known) ? known
                    : _runtimeEntries[type] = new ClassEntry(EventNames.Id(cls), kind, cls[(cls.LastIndexOf('.') + 1)..]);
            }
        }

        UcClassInfo info = a.GetUcClass();
        if (!_classes.TryGetValue(info, out ClassEntry? entry))
        {
            entry = _kinds.TryGetValue(info.Path, out int kind) && kind != 0 ? new ClassEntry(EventNames.Id(info.Path), kind) : null;
            _classes[info] = entry;
        }

        return entry;
    }

    private void LoadClasses(string path)
    {
        if (!File.Exists(path))
        {
            return;
        }

        using JsonDocument doc = JsonDocument.Parse(File.ReadAllBytes(path));
        foreach (JsonProperty c in doc.RootElement.GetProperty("classes").EnumerateObject())
        {
            // The other kinds (muzzle, impact, tracer, projector) are not found as actors: shots and marks carry them.
            _kinds[c.Name] = c.Value.GetProperty("kind").GetString() switch
            {
                "emitter" => BridgeEffectKind.Emitter,
                "projectile" => BridgeEffectKind.Projectile,
                "mine" => BridgeEffectKind.Mine,
                "gadget" => BridgeEffectKind.Gadget,
                "beam" => BridgeEffectKind.Beam,
                "decal" => BridgeEffectKind.Decal,
                "light" => BridgeEffectKind.Light,
                _ => 0,
            };
        }
    }

    // ---- shots -----------------------------------------------------------------------------------------------

    /// <summary>
    /// CombatFxView.TrackRifleFlashes: the local merc gun's SpawnSFX runs before the tick clears the event buffer;
    /// its count on the weapon survives that, so the flashes are taken from the count.
    /// </summary>
    private void TrackRifleFlashes(SWeaponDefense? rifle)
    {
        if (rifle is null)
        {
            return;
        }

        // A gun the player already held when the feature started does not replay the shots fired before that. A gun
        // first seen later starts from 0 as in the Godot client, so a shot of its first tick is not lost.
        if (!_muzzleCursors.TryGetValue(rifle, out MuzzleCursor? cursor))
        {
            cursor = new MuzzleCursor { Count = _ticksSeen == 0 ? rifle.MuzzleFlashCount : 0 };
            _muzzleCursors.Add(rifle, cursor);
        }

        while (cursor.Count < rifle.MuzzleFlashCount)
        {
            AddFlash(SpawnSFX, rifle);
            cursor.Count++;
        }
    }

    /// <summary>CombatFxView.AddFlash and AddMuzzleMeshes, without the drawing: who fired what, from where, which way.</summary>
    private void AddFlash(Name kind, Actor? source)
    {
        bool sticky = kind == CombatFx.MuzzleSticky;
        bool firstPerson = kind == SpawnSFX;
        SPlayerController? viewer = _session.LocalController;

        // CombatFxView.AddFlash: the flash of the own gun is not drawn while the player looks through the binoculars.
        if (firstPerson && viewer is { bBinocularVision: true })
        {
            return;
        }

        Pawn? pawn = source?.Instigator ?? source as Pawn;
        FVector at;
        FVector forward;
        if (firstPerson && _session.View.Valid)
        {
            // The local merc's first-person gun: ahead of the camera, right and down, until the drawn gun's FX bone says better.
            _session.View.Rotation.GetAxes(out FVector x, out FVector y, out FVector z);
            at = _session.View.Location + (x * 60f) + (y * 9f) - (z * 9f);
            forward = x;
        }
        else
        {
            if (pawn is null)
            {
                return;
            }

            FRotator aim = pawn.Controller is SPlayerController pc ? pc.MyDesiredRotation :
                pawn is SPAWNDEFENSE remote ? new FRotator((short)(remote.iDesiredYawPitch >> 16), (short)remote.iDesiredYawPitch, 0) : pawn.Rotation;
            if (pawn.Controller is SPlayerController own && ReferenceEquals(own, viewer))
            {
                aim = own.Rotation;
            }

            aim.GetAxes(out forward, out _, out _);
            at = pawn.Location + new FVector(0f, 0f, pawn.EyeHeight * 0.6f) + (forward * 40f);
        }

        bool pistol = source is MercSidearm or MercSidearmDummy;
        bool suppressed = source is MercSidearm { Kind: 0 } or MercSidearmDummy { Kind: 0 } or
            OpenScct.Game.Svm.M4A1S or OpenScct.Game.Svm.M4A1SDummy;
        bool revolver = source is MercSidearm { Kind: 2 } or MercSidearmDummy { Kind: 2 };
        bool shotgun = source is SWeaponDefense_Shotgun or SWeaponDefenseDummy_Shotgun;
        bool uzi = source is SWeaponDefense_Uzi;

        // The first-person Uzi and shotgun have no barrel flash (spec/equipment/merc-guns.md).
        bool barrel = !sticky && !pistol && !suppressed && !(firstPerson && (uzi || source is SWeaponDefense_Shotgun));
        _shots.Add(new BridgeShot
        {
            Kind = sticky ? BridgeShotKind.MuzzleSticky : firstPerson ? BridgeShotKind.FirstPersonMuzzle : BridgeShotKind.ThirdPersonMuzzle,
            Tick = (int)_tick,
            SourceActorId = source?.ActorId ?? 0,
            ShooterId = pawn?.ActorId ?? 0,
            WeaponClassId = source is null ? 0 : EventNames.Id(source.GetUcClass().Path),
            Flags = (pawn is not null && ReferenceEquals(pawn, viewer?.Pawn) ? BridgeShotFlags.Local : 0) |
                (suppressed ? BridgeShotFlags.Suppressed : 0) | (pistol ? BridgeShotFlags.Pistol : 0) |
                (revolver ? BridgeShotFlags.Revolver : 0) | (shotgun ? BridgeShotFlags.Shotgun : 0) | (uzi ? BridgeShotFlags.Uzi : 0) |
                (!sticky && !revolver ? BridgeShotFlags.Casing : 0) | (barrel ? BridgeShotFlags.BarrelFlash : 0),
            X = at.X,
            Y = at.Y,
            Z = at.Z,
            DirX = forward.X,
            DirY = forward.Y,
            DirZ = forward.Z,
            Life = 0.1f,
        });
        MuzzleFlashes++;
    }

    /// <summary>
    /// CombatFxView.TrackTracers: each SFireTrace actor once its impact point has arrived. The simulation starts it at
    /// its estimate of the gun's FX bone (SFireTrace.PostNetBeginPlay); the drawn muzzle is better.
    /// </summary>
    private void TrackTracers(Level level)
    {
        _gone.Clear();
        foreach (Actor t in _tracers)
        {
            if (t.bDeleteMe)
            {
                _gone.Add(t);
            }
        }

        foreach (Actor t in _gone)
        {
            _tracers.Remove(t);
        }

        foreach (SFireTrace t in level.AllActors<SFireTrace>())
        {
            if (t.bDeleteMe || t.ImpactLoc.IsZero() || !_tracers.Add(t))
            {
                continue;
            }

            FVector to = t.ImpactLoc - t.Location;
            float length = to.Size();
            if (length < 1f)
            {
                continue;
            }

            FVector dir = to * (1f / length);
            _shots.Add(new BridgeShot
            {
                Kind = BridgeShotKind.Tracer,
                Tick = (int)_tick,
                SourceActorId = t.ActorId,
                ShooterId = t.Instigator?.ActorId ?? 0,
                Flags = t.Instigator is not null && ReferenceEquals(t.Instigator, _session.LocalPawn) ? BridgeShotFlags.Local : 0,
                X = t.Location.X,
                Y = t.Location.Y,
                Z = t.Location.Z,
                DirX = dir.X,
                DirY = dir.Y,
                DirZ = dir.Z,
                ImpactX = t.ImpactLoc.X,
                ImpactY = t.ImpactLoc.Y,
                ImpactZ = t.ImpactLoc.Z,
                Speed = t.FTSpeed > 0f ? t.FTSpeed : 7000f,
                Life = 2f,
                HideDistance = K.FireTraceHideDistance,
            });
            Tracers++;
        }
    }

    // ---- impact marks ----------------------------------------------------------------------------------------

    /// <summary>Writes the live marks of the level's impact manager (CombatFxView.TrackMarks reads the same slots).</summary>
    public int WriteMarks(Span<BridgeImpactMark> into)
    {
        if (_session.Client.World.Level.GetService<ImpactTable>() is not { } table)
        {
            return 0;
        }

        IReadOnlyList<ImpactMark> slots = table.Slots;
        int count = 0;
        for (int i = 0; i < slots.Count && count < into.Length; i++)
        {
            ImpactMark m = slots[i];
            if (!m.Live)
            {
                continue;
            }

            int type = EventNames.Id(m.Type);
            into[count++] = new BridgeImpactMark
            {
                Slot = i,
                Serial = m.Serial,
                TypeId = type,
                MaterialId = EventNames.Id(m.Material.Path),
                SparkClassId = EventNames.Id(m.Spark.Path),
                MeshId = EventNames.Id(m.Mesh.Path),
                SoundId = EventNames.Id(m.Sound.Path),
                SurfaceActorId = m.Surface?.ActorId ?? 0,
                Flags = (m.DecalShown ? BridgeImpactFlags.DecalShown : 0) | (m.Unlit ? BridgeImpactFlags.Unlit : 0) |
                    (EventNames.Text(type).Contains("Shocker", StringComparison.Ordinal) ? BridgeImpactFlags.Shocker : 0),
                X = m.Location.X,
                Y = m.Location.Y,
                Z = m.Location.Z,
                Pitch = m.Rotation.Pitch * RotatorToDegrees,
                Yaw = m.Rotation.Yaw * RotatorToDegrees,
                Roll = m.Rotation.Roll * RotatorToDegrees,
                DecalX = m.DecalLocation.X,
                DecalY = m.DecalLocation.Y,
                DecalZ = m.DecalLocation.Z,
                DrawScale = m.DrawScale,
            };
        }

        return count;
    }

    /// <param name="Name">The name it is counted under in <see cref="Seen"/>; null for the actor's own class name.</param>
    private sealed record ClassEntry(int ClassId, int Kind, string? Name = null);

    private sealed class MuzzleCursor
    {
        public long Count;
    }

    private sealed class Tracked(Actor actor, ClassEntry cls)
    {
        public Actor Actor { get; } = actor;

        public ClassEntry Class { get; } = cls;

        public FVector Previous, Current;
        public bool HasPrevious, Deleted, Killed, Reported;
        public long FirstTick;
        public int TrailClassId, MeshId, ParticleId;
        public string? MeshPath;
    }
}
