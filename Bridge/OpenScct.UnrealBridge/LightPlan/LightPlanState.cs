using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.Lighting;
using OpenScct.Game.SBase;
using OpenScct.Physics.Collision;
using OpenScct.UnrealBridge.Devices;

namespace OpenScct.UnrealBridge.LightPlan;

/// <summary>How a rig is asked for (<see cref="LightPlanState.Rig"/>).</summary>
public static class BridgeLightRigOptions
{
    /// <summary>Map lights only: no flare, muzzle flash, flash grenade, flag or torch (as the bots' field without torches).</summary>
    public const int NoRuntimeLights = 1;

    /// <summary>Every map light where the map placed it, not where its actor rides now (as the bots' field).</summary>
    public const int PlacedOnly = 2;

    /// <summary>Do not trace the occluded flag of the chosen lights.</summary>
    public const int NoOcclusion = 4;

    /// <summary>Leave special-lit lights out of the set (the simulation's field does not).</summary>
    public const int NoSpecialLit = 8;

    // The pawn material supplies the ordinary lamps whose world contribution is already baked.
    // Live map lamps, projectors, torches and effect lights have their own shadowed Unreal light.
    public const int MaterialLamps = 16;

    /// <summary>With MaterialLamps, return the light reaching a Spy's surface this frame in Level (0..1).</summary>
    public const int SpySurface = 32;
}

/// <summary>Flags of one light of a rig.</summary>
public static class BridgeRigLightFlags
{
    /// <summary>LE_PointAmbient: the colour is also added whatever the normal.</summary>
    public const int PointAmbient = 1;

    /// <summary>A collision line from the light to the actor's centre is blocked. The original lights the actor all the same.</summary>
    public const int Occluded = 2;

    /// <summary>A sun: X, Y, Z is the unit direction towards the light, not a place.</summary>
    public const int Sun = 4;

    /// <summary>Not a map light: a flare, a muzzle flash, a flash grenade, a flag (the id is minus the actor id).</summary>
    public const int Runtime = 8;

    /// <summary>A cone light; the cone's edge is already in the colour (taken at the actor's centre).</summary>
    public const int Spot = 16;
}

/// <summary>One chosen light of a rig.</summary>
public struct RigLight
{
    /// <summary>Stable over frames: the index of the light in lighting.json for a map light, minus the actor id for a runtime light.</summary>
    public int Id;
    public int Flags;

    /// <summary>Where the light is now (or, for a sun, the unit direction towards it).</summary>
    public FVector Position;

    /// <summary>
    /// The light's colour as it reaches the actor's centre, in the display units of the term: colour x light type x
    /// gain x falloff x cone. The term of a surface with normal N is this x max(0, N.L), plus this again for a
    /// point-ambient light.
    /// </summary>
    public LightRgb Colour;
}

/// <summary>The light set of one actor: what the original's renderer would light it with now.</summary>
public sealed class RigResult
{
    public LightRgb Ambient;
    public int Count;
    public readonly RigLight[] Lights = new RigLight[ActorLightRules.MaxActorLights];

    /// <summary>The summed term of the projectors and torches that reach the centre with an open line (every side facing).</summary>
    public LightRgb Spot;
    public int SpotCount;

    /// <summary>The simulation's level for the actor lit from every side, animated lights at their mean (AiLightField.LevelAround).</summary>
    public float Level;

    /// <summary>The same on the side that faces the viewer, now (AiLightField.LevelAt).</summary>
    public float LevelFacing;
    public int Zone;
}

/// <summary>
/// The lighting plan's view of a running session: the map's lights with their flags and classes, the zone ambients,
/// the actor light rig (which lights the original's renderer would light an actor with, by the simulation's own
/// <see cref="ActorLightRules"/>), the own spy's shadow darkness (<see cref="ActorShadowLighting"/>) and the merc's
/// squint. It reads the client world and never writes it, except that the sticky camera's shadow rule can be asked
/// to follow the darkness (<see cref="FeedStickyCamera"/>; off unless asked).
/// </summary>
public sealed class LightPlanState : IDisposable
{
    private const ETraceFlags ShadowFlags = ETraceFlags.TRACE_World | ETraceFlags.TRACE_StopAtFirstHit;
    private const float RotatorToDegrees = 360f / 65536f;
    private const int AdditionalLightCandidateCapacity = 64;

    private readonly BridgeSession _session;
    private readonly WorldState _world;
    private readonly PlanLight[] _lights;
    private readonly bool[] _on;
    private readonly float[] _scale;
    private readonly FVector[] _location;
    private readonly FRotator[] _rotation;
    private readonly int[] _worldIndex;
    private readonly (float Score, int Index, bool IsSun)[] _ranked = new (float, int, bool)[ActorLightRules.MaxActorLights];
    private readonly List<Actor> _runtime = [];
    private readonly ActorShadowLighting _shadow = new();
    private readonly List<ActorShadowLight> _shadowLights = [];
    private long _drawnRevision = -1;
    private bool _worldMapped;
    private float _liveTime = float.NaN;
    private float _liveAlpha = float.NaN;
    private float _runtimeTime = float.NaN;
    private SBasePawn? _shadowPawn;
    private FVector _shadowLast;
    private float _shadowTime;
    private bool _shadowView = true;
    private bool _feed;

    private LightPlanState(BridgeSession session)
    {
        _session = session;
        _world = WorldState.Of(session);
        Map = LightPlanMap.Load(session.Session.MapFolder);
        _lights = [.. Map.Lights];
        _on = new bool[_lights.Length];
        _scale = new float[_lights.Length];
        _location = new FVector[_lights.Length];
        _rotation = new FRotator[_lights.Length];
        _worldIndex = new int[_lights.Length];
        for (int i = 0; i < _lights.Length; i++)
        {
            _on[i] = _lights[i].InitiallyOn && _lights[i].LightType != 0;
            _scale[i] = _on[i] ? 1f : 0f;
            _location[i] = _lights[i].Location;
            _rotation[i] = _lights[i].Rotation;
            _worldIndex[i] = -1;
        }

        session.Ticked += OnTick;
    }

    public static LightPlanState Of(BridgeSession session) => session.Feature(s => new LightPlanState(s));

    public LightPlanMap Map { get; }

    public WorldState World => _world;

    private Level Level => _session.Session.Client.World.Level;

    private ICollisionWorld? Collision => GadgetWorld.Collision(Level.Info);

    public void Dispose()
    {
        _session.Ticked -= OnTick;
        if (_feed)
        {
            _session.Session.StickyCamShadowUpdate = null;
        }
    }

    // ---- the lights as they are now ---------------------------------------------------------------------------

    /// <summary>
    /// Takes the map lights' state of this frame from the world state (the one the level's lights are drawn from): on
    /// or off by the trigger lights, the light type's scale, and where a light that rides an actor is.
    /// </summary>
    private void Refresh(bool includeRuntime = true)
    {
        float time = Level.Info.TimeSeconds;
        float alpha = _session.Alpha;
        if (includeRuntime && time != _runtimeTime)
        {
            _runtimeTime = time;
            ScanRuntime();
        }

        if (!_world.Bound || (time == _liveTime && alpha == _liveAlpha))
        {
            return;
        }

        _liveTime = time;
        _liveAlpha = alpha;
        if (!_worldMapped)
        {
            _worldMapped = true;
            var byName = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            IReadOnlyList<RenderLight> render = _world.Render.Lights;
            for (int i = 0; i < render.Count; i++)
            {
                byName.TryAdd(render[i].Name, i);
            }

            for (int i = 0; i < _lights.Length; i++)
            {
                _worldIndex[i] = _lights[i].Name is { } name && byName.TryGetValue(name, out int at) ? at : -1;
            }
        }

        ReadOnlySpan<BridgeWorldLight> lights = _world.PeekLightRows(out long revision);
        if (_drawnRevision == revision)
        {
            return;
        }
        _drawnRevision = revision;

        for (int i = 0; i < _lights.Length; i++)
        {
            int at = _worldIndex[i];
            if (at < 0 || at >= lights.Length)
            {
                continue;
            }

            ref readonly BridgeWorldLight d = ref lights[at];
            _on[i] = (d.Flags & BridgeWorldLightFlags.On) != 0;
            _scale[i] = d.Scale;
            if ((d.Flags & BridgeWorldLightFlags.Moved) != 0 && (d.Flags & BridgeWorldLightFlags.OnBone) == 0)
            {
                _location[i] = new FVector(d.X, d.Y, d.Z);
                _rotation[i] = new FRotator((int)(d.Pitch / RotatorToDegrees), (int)(d.Yaw / RotatorToDegrees), (int)(d.Roll / RotatorToDegrees));
            }
        }
    }

    /// <summary>
    /// The lights that are not the map's: every dynamic actor that shines now and stands behind no map light (a flare,
    /// a muzzle flash, a flash grenade, a flag, a merc's torch, a manned searchlight).
    /// </summary>
    private void ScanRuntime()
    {
        _runtime.Clear();
        foreach (Actor actor in Level.DynamicActors<Actor>())
        {
            if (actor.bDeleteMe || actor.LightType == ELightType.LT_None || actor.LightBrightness <= 0f || _world.IsFollowed(actor))
            {
                continue;
            }

            int effect = (int)actor.LightEffect;
            if (effect == ActorLightRules.LE_ESpotShadow ||
                ActorLightRules.LightsActors(actor.bApplyToInGameLighting, actor.bDynamicLight, (int)actor.LightType, effect))
            {
                _runtime.Add(actor);
            }
        }
    }

    /// <summary>Whether the map light is on now, and the scale its level light is drawn with (0 when off).</summary>
    public bool IsOn(int light) => (uint)light < (uint)_on.Length && _on[light];

    public int RuntimeLightCount
    {
        get
        {
            Refresh();
            return _runtime.Count;
        }
    }

    // ---- the rig ----------------------------------------------------------------------------------------------

    /// <summary>
    /// The light set of an actor whose bounding sphere is (<paramref name="centre"/>, <paramref name="radius"/>), by
    /// the rules of <see cref="ActorLightRules"/> in the order the simulation's AiLightField applies them: the zone
    /// ambient, the four strongest lights of the actor light set that are on (own-zone lights only in their zone;
    /// ranked by LightBrightness x (1 - d^2 / (R + r)^2), suns first), and every projector and torch whose frustum
    /// holds the centre and whose line to it is open. <paramref name="self"/> is the actor itself: a light it owns (a
    /// merc's own torch) is left out.
    /// </summary>
    public void Rig(FVector centre, float radius, FVector viewer, int options, Actor? self, RigResult rig)
    {
        bool materialLamps = (options & BridgeLightRigOptions.MaterialLamps) != 0;
        bool spySurface = materialLamps && (options & BridgeLightRigOptions.SpySurface) != 0;
        bool runtime = (!materialLamps || spySurface) && (options & BridgeLightRigOptions.NoRuntimeLights) == 0;
        Refresh(runtime);
        ICollisionWorld? collision = Collision;
        ICollisionWorld? shadowWorld = (options & BridgeLightRigOptions.NoOcclusion) == 0 ? collision : null;
        bool placed = (options & BridgeLightRigOptions.PlacedOnly) != 0;
        bool noSpecial = (options & BridgeLightRigOptions.NoSpecialLit) != 0;
        float time = Level.Info.TimeSeconds;
        int zone = collision?.GetZoneIndex(centre) ?? -1;
        FVector toViewer = viewer - centre;
        float length = toViewer.Size();
        FVector normal = length > 1e-3f ? toViewer * (1f / length) : new FVector(0f, 0f, 1f);

        LightRgb ambient = zone >= 0 && Map.Ambient.TryGetValue(zone, out LightRgb a) ? a : default;
        LightRgb surfaceLight = ambient;
        rig.Ambient = ambient;
        rig.Zone = zone;
        rig.Count = 0;
        rig.SpotCount = 0;

        // Runtime lights are numbered after the map's. Count all candidates for the runtime scan limit.
        int candidates = 0;
        for (int i = 0; i < _lights.Length; i++)
        {
            PlanLight l = _lights[i];
            if (!l.InSet || !_on[i] || (noSpecial && l.SpecialLit) || (l.OwnZoneOnly && zone >= 0 && l.Zone >= 0 && l.Zone != zone))
            {
                continue;
            }
            if (materialLamps && (l.Hidden || l.Class is not (BridgeLightClass.Baked or BridgeLightClass.CharactersOnly)))
            {
                if (spySurface && !l.Hidden && l.Class == BridgeLightClass.Dynamic)
                {
                    surfaceLight += LiveSurfaceLight(l.Effect, _location[i], _rotation[i], l.Hue, l.Saturation,
                        l.Brightness * _scale[i], l.Radius, l.Cone, centre, shadowWorld);
                }
                continue;
            }

            float score = ActorLightRules.Score(l.Effect, placed ? l.Location : _location[i], placed ? l.Rotation : _rotation[i], l.Brightness,
                l.Radius, l.Cone, centre, radius);
            if (score > 0f)
            {
                candidates = RankLight(candidates, score, i, l.Effect);
            }
        }

        if (runtime)
        {
            int candidateLimit = _lights.Length + AdditionalLightCandidateCapacity;
            for (int k = 0; k < _runtime.Count && candidates < candidateLimit; k++)
            {
                Actor actor = _runtime[k];
                int effect = (int)actor.LightEffect;
                if (effect == ActorLightRules.LE_ESpotShadow || actor.bDeleteMe || ReferenceEquals(actor, self))
                {
                    continue;
                }

                if (actor.bAffectOwnZoneOnly && zone >= 0)
                {
                    int lightZone = collision?.GetZoneIndex(actor.Location) ?? actor.Region.ZoneNumber;
                    if (lightZone >= 0 && lightZone != zone)
                    {
                        continue;
                    }
                }

                float lightRadius = FlareLighting.RenderRadius(actor);
                if (spySurface)
                {
                    float modulation = FieldModulation((int)actor.LightType, actor.LightPeriod, actor.LightPhase, time);
                    surfaceLight += LiveSurfaceLight(effect, actor.Location, actor.Rotation, actor.LightHue, actor.LightSaturation,
                        actor.LightBrightness * modulation, lightRadius, actor.LightCone, centre, shadowWorld);
                    continue;
                }
                float score = ActorLightRules.Score(effect, actor.Location, actor.Rotation, actor.LightBrightness, lightRadius, actor.LightCone,
                    centre, radius);
                if (score > 0f)
                {
                    candidates = RankLight(candidates, score, _lights.Length + k, effect);
                }
            }
        }

        LightRgb around = ambient;
        LightRgb facing = ambient;
        int count = Math.Min(_ranked.Length, candidates);
        for (int i = 0; i < count; i++)
        {
            int index = _ranked[i].Index;
            int effect, type, id, flags = 0;
            FVector location;
            FRotator rotation;
            LightRgb colour;
            float lightRadius, period, phase, drawn;
            byte cone;
            if (index < _lights.Length)
            {
                PlanLight l = _lights[index];
                effect = l.Effect;
                type = l.LightType;
                id = l.Index;
                location = placed ? l.Location : _location[index];
                rotation = placed ? l.Rotation : _rotation[index];
                colour = ActorLightRules.Colour(l.Hue, l.Saturation, l.Brightness);
                lightRadius = l.Radius;
                cone = l.Cone;
                period = l.Period;
                phase = l.Phase;
                drawn = _scale[index];
            }
            else
            {
                Actor actor = _runtime[index - _lights.Length];
                effect = (int)actor.LightEffect;
                type = (int)actor.LightType;
                id = -actor.ActorId;
                flags |= BridgeRigLightFlags.Runtime;
                location = actor.Location;
                rotation = actor.Rotation;
                colour = ActorLightRules.Colour(actor.LightHue, actor.LightSaturation, actor.LightBrightness);
                lightRadius = FlareLighting.RenderRadius(actor);
                cone = actor.LightCone;
                period = actor.LightPeriod;
                phase = actor.LightPhase;
                drawn = FieldModulation(type, period, phase, time);
            }

            // Sum light like AiLightField.Evaluate: the mean of the light type for "around", its
            // value now (a flicker at its mean) on the side that faces the viewer.
            around += ActorLightRules.Term(effect, location, rotation, colour, lightRadius, cone, centre, null) * ActorLightRules.MeanModulation(type);
            facing += ActorLightRules.Term(effect, location, rotation, colour, lightRadius, cone, centre, normal) *
                FieldModulation(type, period, phase, time);

            // The drawn light: the scale its level light has this frame, every side facing; a point-ambient light's
            // undirected half is the same colour again, so only the directed half is kept.
            LightRgb reach = ActorLightRules.Term(effect, location, rotation, colour * drawn, lightRadius, cone, centre, null);
            if (effect == ActorLightRules.LE_PointAmbient)
            {
                reach *= 0.5f;
                flags |= BridgeRigLightFlags.PointAmbient;
            }

            if (ActorLightRules.IsSpot(effect))
            {
                flags |= BridgeRigLightFlags.Spot;
            }

            if (effect == ActorLightRules.LE_Sunlight)
            {
                flags |= BridgeRigLightFlags.Sun;
                location = -rotation.Vector();
            }
            if ((effect != ActorLightRules.LE_Sunlight || materialLamps) && shadowWorld is not null)
            {
                FVector source = effect == ActorLightRules.LE_Sunlight ? centre + (location * 65536f) : location;
                if (shadowWorld.LineCheck(out _, null, centre, source, default, ShadowFlags))
                {
                    flags |= BridgeRigLightFlags.Occluded;
                }
            }

            if (spySurface && (flags & BridgeRigLightFlags.Occluded) == 0)
            {
                surfaceLight += effect == ActorLightRules.LE_PointAmbient ? reach * 2f : reach;
            }
            rig.Lights[i] = new RigLight { Id = id, Flags = flags, Position = location, Colour = reach };
        }

        rig.Count = count;

        if (materialLamps && !spySurface)
        {
            rig.Spot = default;
            rig.Level = ActorLightRules.Combine(around, default).Luminance;
            rig.LevelFacing = ActorLightRules.Combine(facing, default).Luminance;
            return;
        }

        // Projectors and torches (AiLightField: every spot shadow light that gives light, shadowed by a line).
        LightRgb spotAround = default, spotFacing = default;
        for (int i = 0; i < _lights.Length; i++)
        {
            PlanLight l = _lights[i];
            if (!l.IsProjector || l.Brightness <= 0f || !_on[i] || (noSpecial && l.SpecialLit) ||
                (spySurface && (l.Hidden || l.Class != BridgeLightClass.Projector)))
            {
                continue;
            }

            FVector location = placed ? l.Location : _location[i];
            FRotator rotation = placed ? l.Rotation : _rotation[i];
            AddSpot(l.Frustum, location, rotation, l.Hue, l.Saturation, l.Brightness, l.LightType, l.Period, l.Phase, time, centre, normal,
                spySurface ? shadowWorld : collision, ref spotAround, ref spotFacing, rig, spySurface);
        }

        if (runtime)
        {
            foreach (Actor actor in _runtime)
            {
                if ((int)actor.LightEffect != ActorLightRules.LE_ESpotShadow || actor.bDeleteMe || ReferenceEquals(actor, self) ||
                    (self is not null && (ReferenceEquals(actor.Owner, self) || ReferenceEquals(actor.Instigator, self))))
                {
                    continue;
                }

                var frustum = new SpotShadowFrustum(
                    actor.EchelonRange > 0f ? actor.EchelonRange : 1000f,
                    actor.SpotWidth > 0f ? actor.SpotWidth : 250f,
                    actor.SpotHeight > 0f ? actor.SpotHeight : 250f,
                    actor.NearPlaneRatio,
                    actor is SGadTorche ? 0f : actor.EchelonNormalInfluence,
                    1);
                AddSpot(frustum, actor.Location, actor.Rotation, actor.LightHue, actor.LightSaturation, actor.LightBrightness, (int)actor.LightType,
                    actor.LightPeriod, actor.LightPhase, time, centre, normal, spySurface ? shadowWorld : collision,
                    ref spotAround, ref spotFacing, rig, spySurface);
            }
        }

        rig.Spot = spotAround;
        rig.Level = spySurface ? Math.Clamp((surfaceLight + spotAround).Luminance, 0f, 1f) : ActorLightRules.Combine(around, spotAround).Luminance;
        rig.LevelFacing = ActorLightRules.Combine(facing, spotFacing).Luminance;
    }

    // Dynamic lamps and effects already light the model in Unreal. Their contribution here controls reflections only.
    private static LightRgb LiveSurfaceLight(int effect, FVector location, FRotator rotation, byte hue, byte saturation,
        float brightness, float radius, byte cone, FVector centre, ICollisionWorld? collision)
    {
        LightRgb reach = ActorLightRules.Term(effect, location, rotation, ActorLightRules.Colour(hue, saturation, brightness),
            radius, cone, centre, null);
        if (reach.Max <= 0f || collision is null)
        {
            return reach;
        }
        FVector source = effect == ActorLightRules.LE_Sunlight ? centre - rotation.Vector() * 65536f : location;
        return collision.LineCheck(out _, null, centre, source, default, ShadowFlags) ? default : reach;
    }

    private static void AddSpot(in SpotShadowFrustum frustum, FVector location, FRotator rotation, byte hue, byte saturation, float brightness,
        int type, float period, float phase, float time, FVector centre, FVector normal, ICollisionWorld? collision,
        ref LightRgb around, ref LightRgb facing, RigResult rig, bool instantaneous)
    {
        bool? open = null;
        bool Open() => open ??= collision is null || !collision.LineCheck(out _, null, centre, location, default, ShadowFlags);

        float now = FieldModulation(type, period, phase, time);
        float mean = instantaneous ? now : ActorLightRules.MeanModulation(type);
        if (mean > 0f)
        {
            LightRgb c = ActorLightRules.SpotShadowColour(hue, saturation, brightness) * mean;
            LightRgb term = ActorLightRules.SpotShadowTerm(frustum, location, rotation, c, centre, null);
            if (term.Max > 0f && Open())
            {
                around += term;
                rig.SpotCount++;
            }
        }

        if (now > 0f)
        {
            LightRgb c = ActorLightRules.SpotShadowColour(hue, saturation, brightness) * now;
            LightRgb term = ActorLightRules.SpotShadowTerm(frustum, location, rotation, c, centre, normal);
            if (term.Max > 0f && Open())
            {
                facing += term;
            }
        }
    }

    // AiLightField.Modulation: a flicker counts at its mean, every other light type at its value now.
    private static float FieldModulation(int type, float period, float phase, float time) =>
        type == 4 ? ActorLightRules.MeanModulation(4) : ActorLightRules.Modulation(type, period, phase, time);

    private int RankLight(int candidates, float score, int index, int effect)
    {
        // Keep only the lights the rig uses. Equal scores keep the order they were visited.
        var light = (Score: score, Index: index, IsSun: effect == ActorLightRules.LE_Sunlight);
        int last = _ranked.Length - 1;
        if (candidates > last && !Before(light, _ranked[last]))
        {
            return candidates + 1;
        }

        int slot = Math.Min(candidates, last);
        while (slot > 0 && Before(light, _ranked[slot - 1]))
        {
            _ranked[slot] = _ranked[slot - 1];
            slot--;
        }
        _ranked[slot] = light;
        return candidates + 1;
    }

    private static bool Before((float Score, int Index, bool IsSun) a, (float Score, int Index, bool IsSun) b) =>
        a.IsSun != b.IsSun ? a.IsSun : a.Score > b.Score;

    /// <summary>The rig of the pawn with this actor id, seen from <paramref name="viewer"/>. False when there is no such pawn.</summary>
    public bool RigOfPawn(int actorId, FVector viewer, int options, RigResult rig)
    {
        foreach (Pawn pawn in Level.AllActors<Pawn>())
        {
            if (pawn.ActorId == actorId && !pawn.bDeleteMe)
            {
                Rig(pawn.Location, PawnRadius(pawn), viewer, options, pawn, rig);
                return true;
            }
        }

        return false;
    }

    /// <summary>The bounding radius the bots' sight gives a pawn (AiWorld.LightOn).</summary>
    public static float PawnRadius(Pawn pawn) => pawn.CollisionRadius + (pawn.CollisionHeight * 0.5f);

    /// <summary>The pawns of the client world that are alive, for the audit.</summary>
    public IEnumerable<Pawn> Pawns()
    {
        foreach (Pawn pawn in Level.AllActors<Pawn>())
        {
            if (!pawn.bDeleteMe && pawn.Health > 0 && pawn is SBasePawn)
            {
                yield return pawn;
            }
        }
    }

    public int ZoneAt(FVector point) => Collision?.GetZoneIndex(point) ?? -1;

    // ---- the own spy's shadow ---------------------------------------------------------------------------------

    /// <summary>The darkness byte of the local spy's projected shadow (0 none) as of the last tick, and where its light comes from.</summary>
    public byte ShadowDarkness => _shadow.Darkness;

    public FVector ShadowDirection => _shadow.Direction;

    public FVector ShadowOrigin { get; private set; }

    /// <summary>True while the local player's pawn is a living spy (the darkness is its).</summary>
    public bool ShadowValid => _shadowPawn is not null;

    /// <summary>What the sticky camera's shadow rule reads: the darkness before the last update was not 0 and the spy is drawn.</summary>
    public bool ShadowViewUpdate => _shadowView;

    /// <summary>
    /// Has the sticky camera follow the shadow rule of the original (the fly-in is solved twice a tick only while the
    /// spy's shadow is drawn), or gives the rule back to the session's default (always twice).
    /// </summary>
    public void FeedStickyCamera(bool on)
    {
        if (on == _feed)
        {
            return;
        }

        _feed = on;
        _session.Session.StickyCamShadowUpdate = on ? _ => _shadowView : null;
    }

    public bool FeedsStickyCamera => _feed;

    private void OnTick()
    {
        if (_session.Session.LocalController?.Pawn is not SPawnAttaque pawn || pawn.bDeleteMe || pawn.Health <= 0)
        {
            _shadowPawn = null;
            _shadow.Reset();
            _shadowView = true;
            return;
        }

        Refresh();
        float time = pawn.XLevel.Info.TimeSeconds;
        if (!ReferenceEquals(_shadowPawn, pawn) || time < _shadowTime || FVector.DistSquared(_shadowLast, pawn.Location) > 256f * 256f)
        {
            _shadow.Reset();
        }

        _shadowPawn = pawn;
        _shadowLast = pawn.Location;
        _shadowTime = time;

        // The projector's tick uses the darkness of the render before; this update then refreshes it (SpyShadowLighting.Update).
        _shadowView = _shadow.Darkness != 0 && !pawn.bHidden;

        // No drawn mesh on this side: the pawn's cylinder and the native root fallback, Location + (0, 0, 5).
        FVector centre = pawn.Location;
        float radius = MathF.Sqrt((pawn.CollisionRadius * pawn.CollisionRadius) + (pawn.CollisionHeight * pawn.CollisionHeight));
        ShadowOrigin = pawn.Location + new FVector(0f, 0f, 5f);
        ICollisionWorld? collision = Collision;
        int zone = collision?.GetZoneIndex(centre) ?? -1;
        _shadowLights.Clear();
        for (int i = 0; i < _lights.Length; i++)
        {
            PlanLight l = _lights[i];
            bool present = l.InSet && _on[i] && (!l.OwnZoneOnly || zone < 0 || l.Zone < 0 || zone == l.Zone) && pawn.bSpecialLit == l.SpecialLit &&
                (pawn.bUseDynamicLights || !l.Dynamic) && Intersects(l.Effect, _location[i], l.Radius, centre, radius);
            _shadowLights.Add(new ActorShadowLight(l, _location[i], _rotation[i], l.Brightness, l.Radius, l.Cone, l.Effect, l.ActorShadows, l.Static,
                l.Dynamic, present));
        }

        foreach (Actor actor in _runtime)
        {
            int lightZone = collision?.GetZoneIndex(actor.Location) ?? actor.Region.ZoneNumber;
            float lightRadius = FlareLighting.RenderRadius(actor);
            bool present = !actor.bDeleteMe &&
                ActorLightRules.LightsActors(actor.bApplyToInGameLighting, actor.bDynamicLight, (int)actor.LightType, (int)actor.LightEffect) &&
                (!actor.bAffectOwnZoneOnly || zone < 0 || lightZone < 0 || zone == lightZone) && pawn.bSpecialLit == actor.bSpecialLit &&
                (pawn.bUseDynamicLights || !actor.bDynamicLight) && !pawn.IgnoredEchelonLights.Contains(actor) &&
                Intersects((int)actor.LightEffect, actor.Location, lightRadius, centre, radius);
            _shadowLights.Add(new ActorShadowLight(actor, actor.Location, actor.Rotation, actor.LightBrightness, lightRadius, actor.LightCone,
                (int)actor.LightEffect, actor.bActorShadows, actor.bStatic, actor.bDynamicLight, present, !actor.bDeleteMe));
        }

        _shadow.Update(_shadowLights, centre, radius, ShadowOrigin, time, pawn.XLevel.Info.ShadowDarknessScale, pawn.bLightingVisibility,
            pawn.GlassEffect, (light, from) => Visible(collision, pawn, light, from));
    }

    private static bool Intersects(int effect, FVector location, float lightRadius, FVector centre, float radius)
    {
        if (effect == ActorLightRules.LE_Sunlight)
        {
            return true;
        }

        float reach = ActorLightRules.WorldRadius(lightRadius) + radius;
        return reach > 0f && FVector.DistSquared(location, centre) <= reach * reach;
    }

    // Actor light visibility, game 0x10a363d0: point mask 0x386, sunlight BSP 0x104 and geometry 0x382 (SpyShadowLighting.Visible).
    private static bool Visible(ICollisionWorld? collision, Actor pawn, ActorShadowLight light, FVector centre)
    {
        if (collision is null)
        {
            return true;
        }

        if (light.Effect != ActorLightRules.LE_Sunlight)
        {
            return !collision.LineCheck(out _, pawn, light.Location, centre, default, (ETraceFlags)0x386);
        }

        FVector end = centre - (light.Rotation.Vector() * 65536f);
        return !collision.LineCheck(out _, pawn, end, centre, default, (ETraceFlags)0x104) &&
            !collision.LineCheck(out _, pawn, end, centre, default, (ETraceFlags)0x382);
    }

    // ---- the merc's squint ------------------------------------------------------------------------------------

    /// <summary>
    /// The local player's SquintingFactor (0 to 0.4; a merc standing still in the dark with the torch off) and what the
    /// original's renderer adds to the light term in normal view for it: 2 x (int(factor x 28) >> 1) / 255, at most
    /// 10 / 255. False without a local player controller.
    /// </summary>
    public bool Squint(out float factor, out float term)
    {
        factor = 0f;
        term = 0f;
        if (_session.Session.LocalController is not { } controller)
        {
            return false;
        }

        factor = controller.SquintingFactor;
        term = 2f * ((int)(factor * 28f) >> 1) / 255f;
        return true;
    }
}
