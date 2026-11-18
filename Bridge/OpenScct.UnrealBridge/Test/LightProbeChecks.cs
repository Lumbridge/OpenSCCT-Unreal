using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text.Json;
using OpenScct.Bots;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.AI;
using OpenScct.Game.Lighting;
using OpenScct.Game.SBase;
using OpenScct.Physics.Collision;

namespace OpenScct.UnrealBridge.Test;

/// <summary>
/// A switched light of the map (an STriggerLight) with the two spots the switch test measures: one where a standing
/// pawn's level changes most when the switched lights toggle, and one beyond the light's radius where it does not
/// change at all.
/// </summary>
public readonly record struct LightProbeSwitch(
    string Name, FVector Light, float Radius, float Brightness, bool On, int Effect,
    FVector Near, float NearLevel, float NearToggled,
    bool HasFar, FVector Far, float FarLevel);

/// <summary>
/// The parts of the light parity measurement added for the lighting plan: which floor is play floor, what the picture
/// of the grey sphere should be by the simulation's own pawn light term, and the spots of the switch test.
/// </summary>
public sealed partial class LightProbe
{
    private NavGrid? _grid;
    private List<int>? _playNodes;

    /// <summary>Walkable nodes found by the last <see cref="FindSpots"/>, those on a connected floor under a map light, and those sampled.</summary>
    public int FloorNodes { get; private set; }

    public int LitFloorNodes { get; private set; }

    public int PlayNodes { get; private set; }

    /// <summary>Starts, objectives, flag zones and switches the play floor was grown from (0 when the rule could not be used).</summary>
    public int PlayAnchors { get; private set; }

    /// <summary>The last <see cref="FindSpots"/> sampled the play floor (false: any connected floor under a map light).</summary>
    public bool PlayRule { get; private set; }

    // ---- play floor ----------------------------------------------------------------------------------------------

    /// <summary>
    /// The play floor: every node that can be walked or dropped to from a player start, an objective, a flag zone, a
    /// switch or the local player's own place, over the grid's one-way edges (so a roof one can only drop from, the
    /// floor inside a fish tank or a ledge behind glass are not part of it). Places reached only by climbing (pipes,
    /// ladders, ledges) are left out unless one of those actors stands there. Null when the map has none of those
    /// actors or the grid's edges cannot be read: the caller falls back on the floor under the map's lights.
    /// </summary>
    private bool[]? PlayFloor(NavGrid grid, int[] component, int componentCount)
    {
        PlayAnchors = 0;
        var anchors = new List<int>();
        void Anchor(FVector at, float footOffset)
        {
            int node = grid.NearestNode(at, footOffset);
            if (node >= 0)
            {
                anchors.Add(node);
            }
        }

        foreach (Actor actor in _session.Session.Client.World.Level.AllActors<Actor>())
        {
            if (actor.bDeleteMe)
            {
                continue;
            }

            if (actor is PlayerStart)
            {
                Anchor(actor.Location, actor.CollisionHeight);
            }
            else if (actor is SObjectiveTrigger or SFlagDropZone or SInterrupteur)
            {
                Anchor(actor.Location, 0f);
            }
        }

        if (_session.Session.LocalPawn is { } pawn)
        {
            Anchor(pawn.Location, pawn.CollisionHeight);
        }

        if (anchors.Count == 0)
        {
            return null;
        }

        IReadOnlyList<FVector> nodes = grid.Nodes;
        var play = new bool[nodes.Count];
        const BindingFlags Hidden = BindingFlags.Instance | BindingFlags.NonPublic;
        if (typeof(NavGrid).GetField("_edgeStart", Hidden)?.GetValue(grid) is List<int> edgeStart &&
            typeof(NavGrid).GetField("_edges", Hidden)?.GetValue(grid) is List<int> edges && edgeStart.Count == nodes.Count + 1)
        {
            var open = new Stack<int>();
            foreach (int a in anchors)
            {
                if (!play[a])
                {
                    play[a] = true;
                    open.Push(a);
                }
            }

            while (open.Count > 0)
            {
                int n = open.Pop();
                for (int e = edgeStart[n]; e < edgeStart[n + 1]; e++)
                {
                    int to = edges[e];
                    if (!play[to])
                    {
                        play[to] = true;
                        open.Push(to);
                    }
                }
            }
        }
        else
        {
            // The grid keeps its edges to itself in this build: the connected pieces an anchor stands on (drops count both ways).
            var holds = new bool[componentCount];
            foreach (int a in anchors)
            {
                holds[component[a]] = true;
            }

            for (int i = 0; i < play.Length; i++)
            {
                play[i] = holds[component[i]];
            }
        }

        PlayAnchors = anchors.Count;
        return play;
    }

    // ---- the predicted picture ------------------------------------------------------------------------------------

    /// <summary>
    /// What the original's pawn lighting makes of the middle of a matte sphere at <paramref name="centre"/> seen from
    /// <paramref name="viewer"/>: the display colour (0 to 1) of a surface whose texture has the display value
    /// <paramref name="albedoDisplay"/>, lit per pixel by the simulation's term (the zone ambient and the four
    /// strongest map lights by N.L, the projectors, capped at 2), averaged over the disc of the sphere within
    /// <paramref name="discShare"/> of its radius as the camera sees it. This is the picture the lighting plan gives a
    /// body-class surface, so the measured sphere is compared against it.
    /// </summary>
    public AiRgb PredictSphere(FVector centre, float radius, FVector viewer, float albedoDisplay, float discShare)
    {
        _field.Time = _session.Session.Client.World.Level.Info.TimeSeconds;
        _field.FollowSwitches();
        FVector forward = viewer - centre;
        if (!forward.Normalize())
        {
            forward = new FVector(0f, 0f, 1f);
        }

        FVector right = MathF.Abs(forward.Z) > 0.99f ? new FVector(1f, 0f, 0f) : new FVector(0f, 0f, 1f) ^ forward;
        right.Normalize();
        FVector up = forward ^ right;
        const int Rings = 4, Turns = 8;
        float r = 0f, g = 0f, b = 0f;
        for (int ring = 0; ring < Rings; ring++)
        {
            // Rings of equal area.
            float off = Math.Clamp(discShare, 0f, 1f) * MathF.Sqrt((ring + 0.5f) / Rings);
            float along = MathF.Sqrt(MathF.Max(0f, 1f - (off * off)));
            for (int turn = 0; turn < Turns; turn++)
            {
                float angle = MathF.Tau * (turn + (0.5f * (ring & 1))) / Turns;
                FVector normal = (forward * along) + (right * (off * MathF.Cos(angle))) + (up * (off * MathF.Sin(angle)));
                AiRgb term = HasData ? _field.Term(centre, radius, normal) : new AiRgb(AiLightField.UnknownLevel, AiLightField.UnknownLevel, AiLightField.UnknownLevel);
                r += MathF.Min(1f, albedoDisplay * term.R);
                g += MathF.Min(1f, albedoDisplay * term.G);
                b += MathF.Min(1f, albedoDisplay * term.B);
            }
        }

        const float Share = 1f / (Rings * Turns);
        return new AiRgb(r * Share, g * Share, b * Share);
    }

    // ---- the switch test ------------------------------------------------------------------------------------------

    private readonly record struct NamedLight(FVector Location, float Radius, float Brightness, int Effect);

    /// <summary>
    /// The switched lights of the map (at most <paramref name="most"/>, the ones with the largest change first) and
    /// their two spots on the floor the last <see cref="FindSpots"/> sampled. "Toggled" is the state after every
    /// switched light has been fired once (WorldDebugExports.FireLights): the ones that were on are off and the others
    /// on. A light whose toggling changes no walkable spot's level by 0.05 or more is left out.
    /// </summary>
    public List<LightProbeSwitch> FindSwitchSpots(int most)
    {
        var found = new List<LightProbeSwitch>();
        if (_grid is null || _playNodes is null || _collision is null || _session.Session.Client.Map?.Loaded is not { } loaded)
        {
            return found;
        }

        Dictionary<string, NamedLight> byName = ReadNamedLights(_session.Session.MapFolder);
        var switches = new List<(string Name, NamedLight Light, bool On)>();
        foreach (var record in loaded.Gameplay.Actors)
        {
            if (loaded.ActorOf(record) is STriggerLight actor && byName.TryGetValue(record.Id, out NamedLight light))
            {
                switches.Add((record.Id, light, !actor.bDeleteMe && actor.LightType != ELightType.LT_None));
            }
        }

        if (switches.Count == 0)
        {
            return found;
        }

        // The level of every sampled node near a switched light, as it is and with every switched light toggled.
        IReadOnlyList<FVector> nodes = _grid.Nodes;
        var up = new FVector(0f, 0f, CentreHeight);
        var near = new List<int>();
        foreach (int n in _playNodes)
        {
            foreach ((string _, NamedLight light, bool _) in switches)
            {
                float reach = light.Radius + PawnRadius + 400f;
                if (FVector.DistSquared(nodes[n] + up, light.Location) < reach * reach)
                {
                    near.Add(n);
                    break;
                }
            }
        }

        _field.Time = _session.Session.Client.World.Level.Info.TimeSeconds;
        _field.FollowSwitches();
        var now = new Dictionary<int, float>(near.Count);
        var toggled = new Dictionary<int, float>(near.Count);
        foreach (int n in near)
        {
            now[n] = _field.LevelAround(nodes[n] + up, PawnRadius);
        }

        try
        {
            foreach ((string name, NamedLight _, bool on) in switches)
            {
                _field.SetOn(name, !on);
            }

            foreach (int n in near)
            {
                toggled[n] = _field.LevelAround(nodes[n] + up, PawnRadius);
            }
        }
        finally
        {
            foreach ((string name, NamedLight _, bool on) in switches)
            {
                _field.SetOn(name, on);
            }
        }

        var taken = new List<FVector>();
        bool Free(FVector p)
        {
            foreach (FVector t in taken)
            {
                if (FVector.DistSquared(t, p) < 150f * 150f)
                {
                    return false;
                }
            }

            return true;
        }

        var ranked = new List<(float Change, int Switch, int Node)>();
        for (int s = 0; s < switches.Count; s++)
        {
            NamedLight light = switches[s].Light;
            float reach = light.Radius + PawnRadius;
            int best = -1;
            float bestChange = 0.05f;
            foreach (int n in near)
            {
                float change = MathF.Abs(now[n] - toggled[n]);
                if (change > bestChange && FVector.DistSquared(nodes[n] + up, light.Location) < reach * reach)
                {
                    bestChange = change;
                    best = n;
                }
            }

            if (best >= 0)
            {
                ranked.Add((bestChange, s, best));
            }
        }

        ranked.Sort((a, b) => b.Change.CompareTo(a.Change));
        foreach ((float _, int s, int node) in ranked)
        {
            if (found.Count >= most)
            {
                break;
            }

            if (!Free(nodes[node]))
            {
                continue;
            }

            (string name, NamedLight light, bool on) = switches[s];
            taken.Add(nodes[node]);

            // Beyond the radius: no change by the simulation, as near 1.5 m outside as the floor allows, in sight of the light if any is.
            int far = -1;
            float farScore = float.MaxValue;
            foreach (int n in near)
            {
                float distance = FVector.Dist(nodes[n] + up, light.Location) - (light.Radius + PawnRadius);
                if (distance < 60f || distance > 400f || MathF.Abs(now[n] - toggled[n]) > 0.004f || !Free(nodes[n]))
                {
                    continue;
                }

                float score = MathF.Abs(distance - 150f);
                if (score >= farScore)
                {
                    continue;
                }

                bool hidden = _collision.LineCheck(out _, null, nodes[n] + up, light.Location, default, ETraceFlags.TRACE_World | ETraceFlags.TRACE_StopAtFirstHit);
                score += hidden ? 1000f : 0f;
                if (score < farScore)
                {
                    farScore = score;
                    far = n;
                }
            }

            if (far >= 0)
            {
                taken.Add(nodes[far]);
            }

            found.Add(new LightProbeSwitch(name, light.Location, light.Radius, light.Brightness, on, light.Effect,
                nodes[node], now[node], toggled[node], far >= 0, far >= 0 ? nodes[far] : default, far >= 0 ? now[far] : 0f));
        }

        return found;
    }

    /// <summary>The named lights of render/lighting.json: where each is and how far it reaches (a projector's range, else 25 x (LightRadius + 1)).</summary>
    private static Dictionary<string, NamedLight> ReadNamedLights(string folder)
    {
        var lights = new Dictionary<string, NamedLight>(StringComparer.OrdinalIgnoreCase);
        string path = Path.Combine(folder, "render", "lighting.json");
        if (!File.Exists(path))
        {
            return lights;
        }

        using JsonDocument json = JsonDocument.Parse(File.ReadAllBytes(path));
        if (!json.RootElement.TryGetProperty("lights", out JsonElement list) || list.ValueKind != JsonValueKind.Array)
        {
            return lights;
        }

        static float Num(JsonElement e, string name, float fallback) =>
            e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? v.GetSingle() : fallback;

        foreach (JsonElement l in list.EnumerateArray())
        {
            if (!l.TryGetProperty("name", out JsonElement name) || name.ValueKind != JsonValueKind.String || name.GetString() is not { Length: > 0 } id ||
                !l.TryGetProperty("location", out JsonElement v) || v.ValueKind != JsonValueKind.Array || v.GetArrayLength() < 3)
            {
                continue;
            }

            int effect = (int)Num(l, "effect", 0f);
            float range = Num(l, "echelon_range", 0f);
            float radius = effect == ActorLightRules.LE_ESpotShadow ? (range > 0f ? range : 1000f) : ActorLightRules.WorldRadius(Num(l, "radius", 64f));
            lights[id] = new NamedLight(new FVector(v[0].GetSingle(), v[1].GetSingle(), v[2].GetSingle()), radius, Num(l, "brightness", 64f), effect);
        }

        return lights;
    }
}
