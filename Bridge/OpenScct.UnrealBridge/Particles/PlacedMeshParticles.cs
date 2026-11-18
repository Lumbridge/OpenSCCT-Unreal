using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Game.Equipment;
using OpenScct.Physics.Collision;
using OpenScct.UnrealBridge.Devices;
using OpenScctGame.Play;
using OpenScctGame.Play.Session;
using OpenScctGame.Render.Data;
using OpenScctGame.Render.Particles;

namespace OpenScct.UnrealBridge;

internal sealed class PlacedMeshParticles
{
    private sealed class Owner(FxEmitter actor, ParticleSystemSim sim)
    {
        public readonly ParticleSystemSim Sim = sim;
        public FVector Location = actor.Location;
        public FRotator Rotation = actor.Rotation;
    }

    private sealed record Layer(Owner Owner, ParticleEmitterSim Emitter, BridgeMeshParticleLayer Info);

    private readonly BridgeSession _session;
    private readonly Dictionary<int, Owner> _owners = [];
    private readonly List<Layer> _layers = [];
    private readonly List<MeshInstance> _placements = [];
    private readonly DrawnTime _clock = new();
    private ICollisionWorld? _collision;
    private float _time;

    private PlacedMeshParticles(BridgeSession session)
    {
        _session = session;
        string folder = session.Session.MapFolder;
        string shared = SharedContent.FindRoot(folder) ?? Path.GetFullPath(Path.Combine(folder, "..", "..", "shared"));
        FxDoc fx = FxDoc.Load(Path.Combine(folder, "render", "fx.json"), shared);
        WorldState names = WorldState.Of(session);
        foreach (FxEmitter actor in fx.Emitters)
        {
            if (!actor.Emitters.Any(s => ParticleParams.KindOf(s.Class) == ParticleKind.Mesh)) continue;

            // Keep the siblings (and their indices): a particle can take another emitter's location or velocity,
            // or cause it to spawn on collision. Only the mesh layers are handed to Unreal for drawing.
            var children = new List<ParticleParams>();
            foreach (FxSubEmitter sub in actor.Emitters)
            {
                ParticleParams p = ParticleParams.From(sub.Class, sub.ObjectPath, sub.Props, sub.Defaults, sub.Texture);
                if (p.MaxParticles > 0) children.Add(p);
            }
            if (!children.Exists(p => p.Kind == ParticleKind.Mesh)) continue;

            JsonElement props = actor.Props;
            bool Flag(string key, bool fallback = false) =>
                props.ValueKind == JsonValueKind.Object && props.TryGetProperty(key, out JsonElement v)
                    ? v.ValueKind == JsonValueKind.True : fallback;
            var settings = new ParticleSystemParams(Flag("AutoDestroy"), Flag("AutoReset"), default, default);
            var sim = new ParticleSystemSim(children, settings, FxRandom.Seed(actor.Id)) { Trace = Trace };
            var owner = new Owner(actor, sim);
            int id = names.Intern(actor.Id);
            _owners.Add(id, owner);
            foreach (ParticleEmitterSim child in sim.Emitters)
            {
                ParticleParams p = child.Params;
                if (p.Kind != ParticleKind.Mesh) continue;
                _layers.Add(new Layer(owner, child, new BridgeMeshParticleLayer
                {
                    OwnerId = id,
                    MeshId = names.Intern(p.StaticMesh),
                    Capacity = p.MaxParticles,
                    DrawStyle = p.UseMeshBlendMode ? -1 : p.DrawStyle,
                    TextureId = p.UseMeshBlendMode ? 0 : names.Intern(p.Texture),
                    Unlit = Flag("bUnlit", true) ? 1 : 0,
                    AlphaRef = p.AlphaTest ? p.AlphaRef / 255f : -1f
                }));
            }
        }
        if (_layers.Count > 0)
        {
            OnTick();
            _time = _clock.Current;
            session.Ticked += OnTick;
        }
    }

    internal static PlacedMeshParticles Of(BridgeSession session) => session.Feature(static s => new PlacedMeshParticles(s));

    private void OnTick() => _clock.OnTick(_session.Session.Client.World.Level.Info.TimeSeconds);

    internal int Layers(Span<BridgeMeshParticleLayer> output)
    {
        for (int i = 0; i < Math.Min(output.Length, _layers.Count); ++i) output[i] = _layers[i].Info;
        return _layers.Count;
    }

    internal void Trigger(int id)
    {
        if (_owners.TryGetValue(id, out Owner? owner)) owner.Sim.Trigger();
    }

    internal void Place(int id, FVector location, FRotator rotation)
    {
        if (!_owners.TryGetValue(id, out Owner? owner)) return;
        owner.Location = location;
        owner.Rotation = rotation;
    }

    private bool Trace(FVector start, FVector end, out FVector hit, out FVector normal)
    {
        if (_collision is { } world && world.LineCheck(out FCheckResult result, null, end, start, FVector.Zero,
                (ETraceFlags)ParticleK.TraceFlags) && result.bHit)
        {
            hit = result.Location;
            normal = result.Normal;
            return true;
        }
        hit = normal = default;
        return false;
    }

    internal int Frame(Span<BridgeMeshParticle> output)
    {
        // As in Emitters.SetTime, advance on the interpolated level clock. Pausing holds the particles and a
        // slow render frame still runs collision and spawning in at most one-tick steps.
        float now = _clock.At(_session.Alpha);
        _collision = GadgetWorld.Collision(_session.Session.Client.World.Level.Info);
        float elapsed = now - _time;
        if (elapsed < 0)
        {
            foreach (Owner owner in _owners.Values) owner.Sim.Reset();
            elapsed = 0;
        }
        int steps = (int)MathF.Floor(elapsed / Sim.DeltaTime);
        float rest = elapsed - steps * Sim.DeltaTime;
        foreach (Owner owner in _owners.Values)
        {
            for (int i = 0; i < steps; ++i) owner.Sim.Step(Sim.DeltaTime, owner.Location, owner.Rotation, default);
            if (rest > 0.00001f) owner.Sim.Step(rest, owner.Location, owner.Rotation, default);
        }
        _time = rest > 0.00001f ? now : now - rest;

        int count = 0;
        for (int i = 0; i < _layers.Count; ++i)
        {
            Layer layer = _layers[i];
            if (layer.Emitter.Disabled) continue;
            Owner o = layer.Owner;
            _placements.Clear();
            ParticleGeometry.Meshes(layer.Emitter, new ParticleOwner(o.Location, o.Rotation, default, o.Sim.GlobalOffset), _placements);
            foreach (MeshInstance m in _placements)
            {
                if (count < output.Length) output[count] = new BridgeMeshParticle(i, m);
                count++;
            }
        }
        return count;
    }
}
