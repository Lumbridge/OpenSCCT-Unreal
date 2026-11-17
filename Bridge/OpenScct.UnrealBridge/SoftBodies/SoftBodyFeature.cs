using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Physics.Collision;
using OpenScct.Provenance;

namespace OpenScct.UnrealBridge.SoftBodies;

/// <summary>
/// The map's soft bodies (ESBPatchActor cloth: flags, awnings, banners; ESBStripDoorActor strip curtains) for one
/// session, as the Godot client's SoftBodyViews runs them: one <see cref="SoftBodySim"/> per instance of
/// render/world.glb, stepped on this machine only with whole fixed steps of 1/60 s, against the pawns of the client's
/// world (collActor), pushed by the shots that cross them (rifle tracers, the local merc's rounds, every gun's muzzle
/// count) and playing their collSound and shotSound. Soft bodies are not in the simulation: nothing here writes the
/// world except the two sounds, which go into the client world's presentation events as a PlaySound with no actor,
/// so the events feature resolves and hands them to Unreal's audio like any other positional sound.
/// </summary>
public sealed class SoftBodyFeature
{
    private readonly BridgeSession _bridge;
    private readonly List<Body> _bodies = [];
    private readonly List<SoftBodyCollider> _colliders = [];
    private readonly HashSet<SFireTrace> _traces = new(ReferenceEqualityComparer.Instance);
    private readonly List<SFireTrace> _gone = [];
    private readonly Dictionary<Actor, int> _muzzles = new(ReferenceEqualityComparer.Instance);
    private readonly List<Actor> _muzzlesGone = [];
    private SWeaponDefense? _gun;
    private int _clip;
    private float _bank;
    private float _clock;

    private SoftBodyFeature(BridgeSession bridge)
    {
        _bridge = bridge;
        long start = Stopwatch.GetTimestamp();
        (List<SoftBodySource> sources, List<string> warnings, JsonDocument? doc) = SoftBodyMap.Load(bridge.Session.MapFolder);
        using (doc)
        {
            Warnings.AddRange(warnings);
            foreach (SoftBodySource source in sources)
            {
                if (Body.Create(source) is { } body)
                {
                    _bodies.Add(body);
                }
                else
                {
                    Warnings.Add(source.Actor + ": the softBody block and the mesh do not match (drawn as the plain mesh)");
                }
            }
        }

        LoadMilliseconds = (float)Stopwatch.GetElapsedTime(start).TotalMilliseconds;
    }

    public static SoftBodyFeature Of(BridgeSession session) => session.Feature(static s => new SoftBodyFeature(s));

    public int Count => _bodies.Count;

    /// <summary>Instances that could not be built, and why.</summary>
    public List<string> Warnings { get; } = [];

    public float LoadMilliseconds { get; }

    /// <summary>Steps run so far (all bodies together).</summary>
    public long Steps { get; private set; }

    public int CollSounds { get; private set; }

    public int ShotSounds { get; private set; }

    /// <summary>Shot lines seen (tracers, own rounds, muzzle steps), whether they crossed a body or not.</summary>
    public int ShotLines { get; private set; }

    /// <summary>Shot lines that crossed a body (each body crossed counts).</summary>
    public int ShotHits { get; private set; }

    /// <summary>Pawn colliders handed to the bodies in the last update.</summary>
    public int LastColliders { get; private set; }

    /// <summary>Bodies stepped in the last update that stepped.</summary>
    public int LastStepped { get; private set; }

    public float LastUpdateMilliseconds { get; private set; }

    internal SoftBodySim Sim(int index) => _bodies[index].Sim;

    public BridgeSoftBodyState State()
    {
        float max = 0f;
        foreach (Body b in _bodies)
        {
            max = MathF.Max(max, b.Displacement);
        }

        return new BridgeSoftBodyState
        {
            Count = _bodies.Count,
            Stepped = LastStepped,
            Colliders = LastColliders,
            CollSounds = CollSounds,
            ShotSounds = ShotSounds,
            ShotLines = ShotLines,
            ShotHits = ShotHits,
            Warnings = Warnings.Count,
            Steps = Steps,
            MaxDisplacement = max,
            UpdateMilliseconds = LastUpdateMilliseconds,
            LoadMilliseconds = LoadMilliseconds,
        };
    }

    public bool Info(int index, out BridgeSoftBodyInfo info)
    {
        info = default;
        if ((uint)index >= (uint)_bodies.Count)
        {
            return false;
        }

        Body b = _bodies[index];
        info = new BridgeSoftBodyInfo
        {
            Particles = b.Sim.Count,
            Vertices = b.Sim.Count * 2,
            Indices = b.Indices.Length,
            Kind = b.Sim.IsStrip ? 1 : 0,
            Flags = (b.Sim.CollActor ? BridgeSoftBodyFlags.CollActor : 0) | (b.Sim.CollSound.Length > 0 ? BridgeSoftBodyFlags.CollSound : 0) |
                (b.Sim.ShotSound.Length > 0 ? BridgeSoftBodyFlags.ShotSound : 0),
            CenterX = b.Center.X,
            CenterY = b.Center.Y,
            CenterZ = b.Center.Z,
            Radius = b.Radius,
            Displacement = b.Displacement,
            NormalSign = b.NormalSign,
        };
        return true;
    }

    public string Name(int index) => (uint)index < (uint)_bodies.Count ? _bodies[index].Actor : "";

    /// <summary>The mesh's triangles (indices into 2N vertices, glTF order) and its texture coordinates (two floats a vertex).</summary>
    public bool Mesh(int index, Span<int> indices, Span<float> uvs)
    {
        if ((uint)index >= (uint)_bodies.Count)
        {
            return false;
        }

        Body b = _bodies[index];
        if (indices.Length < b.Indices.Length || uvs.Length < b.Uvs.Length)
        {
            return false;
        }

        b.Indices.CopyTo(indices);
        b.Uvs.CopyTo(uvs);
        return true;
    }

    /// <summary>
    /// The particles now, in the actor's local space: six floats each, the position and the vertex normal from the
    /// front triangles (the back face takes the same normal, as SoftBodyViews.Body.Rebuild gives it). Returns the count.
    /// </summary>
    public int Vertices(int index, Span<float> into)
    {
        if ((uint)index >= (uint)_bodies.Count)
        {
            return 0;
        }

        Body b = _bodies[index];
        int n = b.Sim.Count;
        if (into.Length < n * 6)
        {
            return 0;
        }

        b.WriteVertices(into);
        return n;
    }

    /// <summary>
    /// SoftBodyViews.Update: tracks the shots of the frame, banks <paramref name="dt"/> and steps every body drawn in the
    /// last 5 s with whole fixed steps against the pawns. <paramref name="seen"/> says, a byte a body, which bodies were
    /// drawn this frame (null: all of them). Returns the bodies stepped.
    /// </summary>
    public int Update(float dt, ReadOnlySpan<byte> seen)
    {
        if (_bodies.Count == 0)
        {
            return 0;
        }

        long start = Stopwatch.GetTimestamp();
        _clock += MathF.Max(0f, dt);
        Level level = _bridge.Session.Client.World.Level;
        SBasePawn? localPawn = _bridge.Session.LocalPawn;
        TrackTracers(level);
        TrackOwnShots(localPawn);
        TrackMuzzleShots(level, localPawn);

        _bank = MathF.Min(_bank + MathF.Max(0f, dt), SoftBodyK.MaxStep);
        int steps = (int)(_bank / SoftBodyK.StepInterval);
        if (steps == 0)
        {
            return 0;
        }

        _bank = MathF.Max(0f, _bank - (steps * SoftBodyK.StepInterval));
        int stepped = 0;
        int colliders = 0;
        for (int i = 0; i < _bodies.Count; i++)
        {
            Body body = _bodies[i];
            if (seen.IsEmpty || (i < seen.Length && seen[i] != 0))
            {
                body.LastSeen = _clock;
                body.Seen = true;
            }

            if (!body.Seen || _clock - body.LastSeen > SoftBodyK.DrawnRecently)
            {
                continue;
            }

            Colliders(body, level);
            colliders += _colliders.Count;
            bool touched = false;
            for (int s = 0; s < steps; s++)
            {
                touched |= body.Sim.Step(SoftBodyK.StepInterval, _colliders);
            }

            if (touched && _clock > body.LastCollSound + SoftBodyK.CollSoundInterval)
            {
                body.LastCollSound = _clock;
                if (Play(body.Sim.CollSound, body.Location))
                {
                    CollSounds++;
                }
            }

            body.Measure();
            Steps += steps;
            stepped++;
        }

        LastStepped = stepped;
        LastColliders = colliders;
        LastUpdateMilliseconds = (float)Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        return stepped;
    }

    /// <summary>
    /// For unattended checks: a pawn-sized cylinder (radius 30, half height 80, walking pace) crosses the body through
    /// its middle, front to back, over the next <paramref name="seconds"/>, as a pawn walking through it would.
    /// </summary>
    public bool Walk(int index, float seconds)
    {
        if ((uint)index >= (uint)_bodies.Count)
        {
            return false;
        }

        Body b = _bodies[index];
        b.WalkStart = _clock;
        b.WalkSeconds = MathF.Max(0.5f, seconds);
        return true;
    }

    /// <summary>For unattended checks: one shot line through the body's middle, front to back, as a round through it.</summary>
    public bool Shoot(int index)
    {
        if ((uint)index >= (uint)_bodies.Count)
        {
            return false;
        }

        Body b = _bodies[index];
        FVector mid = b.LocalMiddle;
        ShotLine(b.ToWorld(mid + new FVector(0f, -300f, 0f)), b.ToWorld(mid + new FVector(0f, 300f, 0f)));
        return true;
    }

    /// <summary>The pawns near <paramref name="body"/> in its local space (slot 0x94, 0x1099a850), and its check walker.</summary>
    [Provenance(ProvenanceKind.Decoded, "0x1099a850: actors overlapping the body's bounds, tested at Location + Velocity * 0.08 with their CollisionRadius and CollisionHeight")]
    [Provenance(ProvenanceKind.Estimated, "Only pawns with bCollideActors are taken (the original takes any actor whose two collision flags at +0x2f0 are set)")]
    private void Colliders(Body body, Level level)
    {
        _colliders.Clear();
        if (!body.Sim.CollActor)
        {
            return;
        }

        foreach (Pawn p in level.AllActors<Pawn>())
        {
            if (p.bDeleteMe || !p.bCollideActors)
            {
                continue;
            }

            FVector local = body.ToLocal(p.Location + (p.Velocity * SoftBodyK.ColliderLookAhead));
            float reach = p.CollisionRadius + p.CollisionHeight;
            if (local.X < body.Min.X - reach || local.X > body.Max.X + reach || local.Y < body.Min.Y - reach ||
                local.Y > body.Max.Y + reach || local.Z < body.Min.Z - reach || local.Z > body.Max.Z + reach)
            {
                continue;
            }

            _colliders.Add(new SoftBodyCollider(local, body.DirectionToLocal(p.Velocity), p.CollisionRadius, p.CollisionHeight,
                p.Velocity.SizeSquared() > 0f));
        }

        if (body.WalkSeconds > 0f)
        {
            float t = (_clock - body.WalkStart) / body.WalkSeconds;
            if (t > 1f)
            {
                body.WalkSeconds = 0f;
            }
            else
            {
                const float Span = 360f;
                FVector mid = body.LocalMiddle;
                var at = new FVector(mid.X, mid.Y - (Span * 0.5f) + (Span * t), mid.Z);
                _colliders.Add(new SoftBodyCollider(at, new FVector(0f, Span / body.WalkSeconds, 0f), 30f, 80f, true));
            }
        }
    }

    /// <summary>
    /// SBBulletHit / BulletWentTru for the rifle tracers of the client world that no player fired (the players' shots
    /// are taken from their aim below): a tracer's line from the muzzle to its impact that crosses a body pushes it.
    /// </summary>
    private void TrackTracers(Level level)
    {
        _gone.Clear();
        foreach (SFireTrace t in _traces)
        {
            if (t.bDeleteMe)
            {
                _gone.Add(t);
            }
        }

        foreach (SFireTrace t in _gone)
        {
            _traces.Remove(t);
        }

        foreach (SFireTrace t in level.AllActors<SFireTrace>())
        {
            if (t.bDeleteMe || t.ImpactLoc.IsZero() || !_traces.Add(t) || t.Instigator is SBasePawn)
            {
                continue;
            }

            ShotLine(t.Location, t.ImpactLoc);
        }
    }

    /// <summary>
    /// The local merc's own rounds (each one leaving the clip): the line from the gun's fire start along his aim to
    /// the first world hit within ShootingRange (SoftBodyViews.TrackOwnShots).
    /// </summary>
    [Provenance(ProvenanceKind.Estimated, "Soft bodies are not in the simulation, so the server's pass-through trace is stood in for by this machine's aim line (no spread)")]
    private void TrackOwnShots(SBasePawn? localPawn)
    {
        if (localPawn?.MyWeapon is not SWeaponDefense gun || localPawn.Controller is not PlayerController pc)
        {
            _gun = null;
            return;
        }

        if (ReferenceEquals(gun, _gun) && gun.ClipAmmo < _clip)
        {
            FVector start = gun.GetFireStart();
            FVector end = start + (pc.Rotation.Vector() * gun.ShootingRange);
            if (OpenScct.Game.Equipment.GadgetWorld.Line(localPawn, end, start, ETraceFlags.TRACE_World, out FCheckResult hit))
            {
                end = hit.Location;
            }

            for (int i = gun.ClipAmmo; i < _clip; i++)
            {
                ShotLine(start, end);
            }
        }

        _gun = gun;
        _clip = gun.ClipAmmo;
    }

    /// <summary>
    /// The other players' rifle rounds and every spy's pistol shots: each step of the replicated muzzle count is a
    /// line from the shooter's eye along his replicated aim to the first world hit (SoftBodyViews.TrackMuzzleShots).
    /// </summary>
    [Provenance(ProvenanceKind.Estimated, "One line per muzzle count step at the replicated aim, without spread, from the eye point")]
    private void TrackMuzzleShots(Level level, SBasePawn? localPawn)
    {
        _muzzlesGone.Clear();
        foreach (Actor a in _muzzles.Keys)
        {
            if (a.bDeleteMe)
            {
                _muzzlesGone.Add(a);
            }
        }

        foreach (Actor a in _muzzlesGone)
        {
            _muzzles.Remove(a);
        }

        foreach (SWeaponDefenseDummy dummy in level.AllActors<SWeaponDefenseDummy>())
        {
            bool own = localPawn is not null && ReferenceEquals(dummy.Instigator, localPawn);
            MuzzleShots(dummy, dummy.MuzzleCount, own ? null : dummy.Instigator as SBasePawn, localPawn, SoftBodyK.MercShootingRange);
        }

        foreach (SWeaponAttaque pistol in level.AllActors<SWeaponAttaque>())
        {
            MuzzleShots(pistol, pistol.MuzzleCount, pistol.Instigator as SBasePawn, localPawn, pistol.ShootingRange);
        }
    }

    private void MuzzleShots(Actor gun, int count, SBasePawn? shooter, SBasePawn? localPawn, float range)
    {
        if (gun.bDeleteMe)
        {
            return;
        }

        bool seen = _muzzles.TryGetValue(gun, out int last);
        _muzzles[gun] = count;
        if (!seen || count <= last || shooter is null)
        {
            return;
        }

        FRotator aim = ReferenceEquals(shooter, localPawn) && shooter.Controller is PlayerController pc
            ? pc.Rotation
            : PackedAim(shooter.SPawnType == PawnType.SPAWNDEFENSE ? shooter.iDesiredYawPitch : shooter.iYawPitchFromView);
        FVector start = shooter.Location + new FVector(0f, 0f, shooter.EyeHeight);
        FVector end = start + (aim.Vector() * range);
        if (OpenScct.Game.Equipment.GadgetWorld.Line(shooter, end, start, ETraceFlags.TRACE_World, out FCheckResult hit))
        {
            end = hit.Location;
        }

        for (int i = last; i < count; i++)
        {
            ShotLine(start, end);
        }
    }

    private static FRotator PackedAim(int packed) => new((packed >> 16) & 0xffff, packed & 0xffff, 0);

    /// <summary>SBBulletHit for one shot line: every body it crosses gets the crossed cell pushed and plays shotSound.</summary>
    private void ShotLine(FVector start, FVector end)
    {
        ShotLines++;
        Span<int> corners = stackalloc int[4];
        FVector dir = end - start;
        foreach (Body body in _bodies)
        {
            FVector a = body.ToLocal(start);
            FVector b = body.ToLocal(end);
            if (body.Sim.LineCheck(a, b, body.Front, corners, out _))
            {
                ShotHits++;
                body.Sim.BulletHit(body.DirectionToLocal(dir), corners);
                // A body not drawn lately is woken by the shot, so the push is seen when it comes into view.
                body.LastSeen = _clock;
                body.Seen = true;
                if (Play(body.Sim.ShotSound, body.Location))
                {
                    ShotSounds++;
                }
            }
        }
    }

    /// <summary>
    /// PlaySound(sound, SLOT_None, 1.0, , 128.0) at the body's place, as a presentation event of the client world with
    /// no actor: the events feature picks it up with the events raised between ticks and resolves it as any call.
    /// </summary>
    private bool Play(string path, FVector location)
    {
        if (path.Length == 0)
        {
            return false;
        }

        _bridge.Session.Client.World.Events.Add(new SimEvent
        {
            Kind = SimEventKind.PlaySound,
            What = path,
            Location = location,
            Slot = 0,
            Volume = 1f,
            Radius = SoftBodyK.SoundRadius,
            Pitch = 1f,
        });
        return true;
    }

    private sealed class Body
    {
        // The actor's frame: world = Location + Axes * ((local - PrePivot) * Scale), the axes of the placed rotation
        // as Unreal builds them (FRotationMatrix), so the local space is the one the imported level actor draws in.
        private readonly FVector _x, _y, _z, _scale, _prePivot;
        private readonly FVector[] _rest;
        private readonly FVector[] _normals;

        private Body(SoftBodySource source, SoftBodySim sim)
        {
            Actor = source.Actor;
            Sim = sim;
            Location = source.Location;
            _scale = source.Scale;
            _prePivot = source.PrePivot;
            Axes(source.Rotation, out _x, out _y, out _z);
            Indices = source.Indices;
            Uvs = source.Uvs;
            int n = sim.Count;
            _rest = new FVector[n];
            _normals = new FVector[n];
            for (int t = 0; t + 2 < Indices.Length; t += 3)
            {
                if (Indices[t] < n && Indices[t + 1] < n && Indices[t + 2] < n)
                {
                    Front.Add((Indices[t], Indices[t + 1], Indices[t + 2]));
                }
            }

            var min = new FVector(float.MaxValue, float.MaxValue, float.MaxValue);
            var max = new FVector(float.MinValue, float.MinValue, float.MinValue);
            for (int i = 0; i < n; i++)
            {
                FVector p = sim.Position(i);
                _rest[i] = p;
                min = new FVector(MathF.Min(min.X, p.X), MathF.Min(min.Y, p.Y), MathF.Min(min.Z, p.Z));
                max = new FVector(MathF.Max(max.X, p.X), MathF.Max(max.Y, p.Y), MathF.Max(max.Z, p.Z));
            }

            LocalMiddle = (min + max) * 0.5f;
            Center = ToWorld(LocalMiddle);
            Radius = (max - min).Size() * 0.5f * MathF.Max(_scale.X, MathF.Max(_scale.Y, _scale.Z));

            // The rest bounds grow by the strip length: a free edge swings that far at most.
            float swing = MathF.Max(max.X - min.X, max.Z - min.Z);
            Min = min - new FVector(0f, swing, 0f);
            Max = max + new FVector(0f, swing, 0f);

            // The normals come out of the front triangles' cross product; which way that points depends on the
            // winding, so it is turned to agree with the library mesh's own front normals at rest.
            ComputeNormals(1f);
            if (source.FrontNormals.Length == n)
            {
                float agree = 0f;
                for (int i = 0; i < n; i++)
                {
                    agree += _normals[i].Dot(source.FrontNormals[i]);
                }

                NormalSign = agree < 0f ? -1f : 1f;
            }
        }

        public string Actor { get; }

        public SoftBodySim Sim { get; }

        public FVector Location { get; }

        public int[] Indices { get; }

        public float[] Uvs { get; }

        public List<(int A, int B, int C)> Front { get; } = [];

        public FVector Min { get; }

        public FVector Max { get; }

        /// <summary>The middle of the rest bounds, local and world.</summary>
        public FVector LocalMiddle { get; }

        public FVector Center { get; }

        public float Radius { get; }

        /// <summary>+1 or -1: the cross product of the front triangles times this points along the mesh's front normals.</summary>
        public float NormalSign { get; } = 1f;

        /// <summary>The farthest any particle is from its rest place now, uu (after the last step).</summary>
        public float Displacement { get; private set; }

        public float LastSeen { get; set; }

        public bool Seen { get; set; }

        public float LastCollSound { get; set; } = float.MinValue;

        public float WalkStart { get; set; }

        public float WalkSeconds { get; set; }

        public static Body? Create(SoftBodySource source)
        {
            if (SoftBodySim.Read(source.Block) is not { } sim || source.Uvs.Length != sim.Count * 4)
            {
                return null;
            }

            foreach (int i in source.Indices)
            {
                if (i < 0 || i >= sim.Count * 2)
                {
                    return null;
                }
            }

            var body = new Body(source, sim);
            return body.Front.Count == 0 ? null : body;
        }

        public FVector ToLocal(FVector world)
        {
            FVector d = world - Location;
            return new FVector(Div(d.Dot(_x), _scale.X), Div(d.Dot(_y), _scale.Y), Div(d.Dot(_z), _scale.Z)) + _prePivot;
        }

        public FVector ToWorld(FVector local)
        {
            FVector s = local - _prePivot;
            s = new FVector(s.X * _scale.X, s.Y * _scale.Y, s.Z * _scale.Z);
            return Location + (_x * s.X) + (_y * s.Y) + (_z * s.Z);
        }

        public FVector DirectionToLocal(FVector world) =>
            new(Div(world.Dot(_x), _scale.X), Div(world.Dot(_y), _scale.Y), Div(world.Dot(_z), _scale.Z));

        public void Measure()
        {
            float max = 0f;
            for (int i = 0; i < _rest.Length; i++)
            {
                max = MathF.Max(max, (Sim.Position(i) - _rest[i]).SizeSquared());
            }

            Displacement = MathF.Sqrt(max);
        }

        public void WriteVertices(Span<float> into)
        {
            ComputeNormals(NormalSign);
            for (int i = 0; i < _rest.Length; i++)
            {
                FVector p = Sim.Position(i);
                FVector n = _normals[i];
                int o = i * 6;
                into[o] = p.X;
                into[o + 1] = p.Y;
                into[o + 2] = p.Z;
                into[o + 3] = n.X;
                into[o + 4] = n.Y;
                into[o + 5] = n.Z;
            }
        }

        /// <summary>Vertex normals from the front triangles (SoftBodyViews.Body.Rebuild), times <paramref name="sign"/>.</summary>
        private void ComputeNormals(float sign)
        {
            Array.Clear(_normals);
            foreach ((int a, int b, int c) in Front)
            {
                FVector pa = Sim.Position(a);
                FVector n = (Sim.Position(b) - pa).Cross(Sim.Position(c) - pa);
                _normals[a] += n;
                _normals[b] += n;
                _normals[c] += n;
            }

            for (int i = 0; i < _normals.Length; i++)
            {
                FVector n = _normals[i];
                _normals[i] = n.SizeSquared() > 1e-12f ? n.SafeNormal() * sign : new FVector(0f, 0f, 1f);
            }
        }

        private static float Div(float a, float b) => b != 0f ? a / b : a;

        /// <summary>Unreal's FRotationMatrix axes of a rotator in the original's 65536-per-turn units.</summary>
        private static void Axes(FRotator r, out FVector x, out FVector y, out FVector z)
        {
            const double Turn = Math.PI * 2.0 / 65536.0;
            double p = r.Pitch * Turn, w = r.Yaw * Turn, l = r.Roll * Turn;
            float sp = (float)Math.Sin(p), cp = (float)Math.Cos(p);
            float sy = (float)Math.Sin(w), cy = (float)Math.Cos(w);
            float sr = (float)Math.Sin(l), cr = (float)Math.Cos(l);
            x = new FVector(cp * cy, cp * sy, sp);
            y = new FVector((sr * sp * cy) - (cr * sy), (sr * sp * sy) + (cr * cy), -sr * cp);
            z = new FVector(-((cr * sp * cy) + (sr * sy)), (cy * sr) - (cr * sp * sy), cr * cp);
        }
    }
}
