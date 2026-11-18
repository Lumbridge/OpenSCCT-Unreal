using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using OpenScct.Core;
using OpenScct.Game.Lighting;

namespace OpenScct.UnrealBridge.LightPlan;

/// <summary>What the lighting plan does with a map light (the order of the tests is the plan's; see <see cref="LightPlanMap.ClassOf"/>).</summary>
public static class BridgeLightClass
{
    /// <summary>No static, in-game or dynamic flag and no light type: it lights nothing.</summary>
    public const int None = 0;

    /// <summary>LightEffect 22 (LE_ESpotShadow): a projected, shadowed patch on world and characters.</summary>
    public const int Projector = 1;

    /// <summary>bDynamicLight: a live light on the world, the only world light that goes when switched or shot.</summary>
    public const int Dynamic = 2;

    /// <summary>bApplyToStaticLighting and not dynamic: its light is in the lightmaps and vertex colours already.</summary>
    public const int Baked = 3;

    /// <summary>bApplyToInGameLighting only: it lights characters and other actor-lit things, never the world.</summary>
    public const int CharactersOnly = 4;

    /// <summary>bSpecialLit: it lights special-lit actors only.</summary>
    public const int SpecialLit = 5;
}

/// <summary>Flags of <see cref="BridgeLightInfo.Flags"/>.</summary>
public static class BridgeLightInfoFlags
{
    public const int ApplyStatic = 1;
    public const int ApplyInGame = 2;
    public const int Dynamic = 4;
    public const int OwnZoneOnly = 8;
    public const int SpecialLit = 16;
    public const int ActorShadows = 32;
    public const int Triggered = 64;
    public const int InitiallyOn = 128;

    /// <summary>bHidden: the importer placed no level light for it.</summary>
    public const int Hidden = 256;

    /// <summary>The light actor is bStatic.</summary>
    public const int Static = 512;
    public const int Corona = 1024;

    /// <summary>It is in the actor light set (ActorLightRules.LightsActors): the rig may choose it.</summary>
    public const int LightsActors = 2048;

    /// <summary>UsesLightBeam.</summary>
    public const int Beam = 4096;

    /// <summary>A cone light (LE_StaticSpot or LE_Spotlight).</summary>
    public const int Spot = 8192;

    /// <summary>LE_PointAmbient: it also adds its attenuated colour whatever the normal.</summary>
    public const int PointAmbient = 16384;

    /// <summary>LE_Sunlight.</summary>
    public const int Sun = 32768;
}

/// <summary>One light of render/lighting.json with everything the lighting plan asks of it.</summary>
public sealed class PlanLight
{
    public string? Name { get; init; }
    public int Index { get; init; }
    public bool Hidden { get; init; }
    public bool ApplyStatic { get; init; }
    public bool ApplyInGame { get; init; }
    public bool Dynamic { get; init; }
    public bool OwnZoneOnly { get; init; }
    public bool SpecialLit { get; init; }
    public bool ActorShadows { get; init; }
    public bool Triggered { get; init; }
    public bool InitiallyOn { get; init; }
    public bool Static { get; init; }
    public bool Corona { get; init; }
    public bool Beam { get; init; }
    public int Effect { get; init; }
    public int LightType { get; init; }
    public float Period { get; init; }
    public float Phase { get; init; }
    public float Brightness { get; init; }
    public byte Hue { get; init; }
    public byte Saturation { get; init; }

    /// <summary>LightRadius (the reach is 25 x (LightRadius + 1) uu).</summary>
    public float Radius { get; init; }
    public byte Cone { get; init; }
    public FVector Location { get; init; }
    public FRotator Rotation { get; init; }

    /// <summary>The zone index of the light's zone, -1 unknown.</summary>
    public int Zone { get; init; }
    public SpotShadowFrustum Frustum { get; init; }

    /// <summary>The projected texture of an Echelon light (the render part's file), or null.</summary>
    public string? Cookie { get; init; }

    /// <summary>In the actor light set: kept for run time, with a light type, not a projector (ActorLightRules.LightsActors).</summary>
    public bool InSet { get; init; }

    /// <summary>A projector that gives light (MapAiLight: effect 22 with brightness above 0).</summary>
    public bool IsProjector => Effect == ActorLightRules.LE_ESpotShadow;

    public int Class { get; init; }
}

/// <summary>One zone of render/lighting.json.</summary>
public sealed class PlanZone
{
    public int Index { get; init; }
    public string Name { get; init; } = "";
    public string Place { get; init; } = "";

    /// <summary>The zone ambient pawns take (m_RefAmbientColor / 255, each channel at most 1).</summary>
    public LightRgb Ambient { get; init; }
    public bool Sky { get; init; }
    public bool Fog { get; init; }
    public LightRgb FogColour { get; init; }
    public float FogStart { get; init; }
    public float FogEnd { get; init; }
}

/// <summary>
/// The lights and zones of a map's render/lighting.json as the lighting plan needs them: every light with its flags
/// and raw values, the class the plan gives it, the zone ambients and the look. The values the actor light rig is
/// computed from are read exactly as the simulation's own reader takes them (OpenScct.Server MapAiLight.Parse: the
/// same defaults and the same casts), so the rig's level equals the bots' light field.
/// </summary>
public sealed class LightPlanMap
{
    private readonly List<PlanLight> _lights = [];
    private readonly List<PlanZone> _zones = [];
    private readonly Dictionary<int, LightRgb> _ambient = new();

    public IReadOnlyList<PlanLight> Lights => _lights;
    public IReadOnlyList<PlanZone> Zones => _zones;

    /// <summary>The zone ambient by zone index.</summary>
    public IReadOnlyDictionary<int, LightRgb> Ambient => _ambient;

    /// <summary>LevelInfo.GlowThreshold as the converter wrote it (224 when the map has none).</summary>
    public int GlowThreshold { get; private set; } = 224;
    public bool GlowEnabled { get; private set; } = true;
    public int SkyZone { get; private set; } = -1;

    /// <summary>False when the map has no lighting.json (a server with only the sim part).</summary>
    public bool HasData { get; private set; }

    public string? Error { get; private set; }

    /// <summary>
    /// The plan's class of a light. Special-lit lights come first (they light special-lit actors only, so the plan
    /// hides them in both kinds of map); then the projector, because in the data projectors carry the static flag too;
    /// then dynamic, because every dynamic light of the surveyed maps is also flagged static; then baked; then the
    /// lights kept only for characters.
    /// </summary>
    public static int ClassOf(bool specialLit, int effect, bool dynamic, bool applyStatic, bool applyInGame)
    {
        if (specialLit)
        {
            return BridgeLightClass.SpecialLit;
        }

        if (effect == ActorLightRules.LE_ESpotShadow)
        {
            return BridgeLightClass.Projector;
        }

        if (dynamic)
        {
            return BridgeLightClass.Dynamic;
        }

        if (applyStatic)
        {
            return BridgeLightClass.Baked;
        }

        return applyInGame ? BridgeLightClass.CharactersOnly : BridgeLightClass.None;
    }

    public static LightPlanMap Load(string? mapFolder)
    {
        var map = new LightPlanMap();
        string? path = mapFolder is null ? null : Path.Combine(mapFolder, "render", "lighting.json");
        if (path is null || !File.Exists(path))
        {
            return map;
        }

        try
        {
            using JsonDocument json = JsonDocument.Parse(File.ReadAllBytes(path));
            map.Parse(json.RootElement);
            map.HasData = true;
        }
        catch (Exception e) when (e is IOException or JsonException or InvalidOperationException or KeyNotFoundException or FormatException)
        {
            map.Error = e.Message;
        }

        return map;
    }

    private void Parse(JsonElement root)
    {
        var zoneByName = new Dictionary<string, int>(StringComparer.Ordinal);
        if (root.TryGetProperty("zones", out JsonElement zones) && zones.ValueKind == JsonValueKind.Array)
        {
            foreach (JsonElement z in zones.EnumerateArray())
            {
                int index = (int)Num(z, "zoneIndex", -1f);
                if (index < 0)
                {
                    continue;
                }

                string? name = Str(z, "name");
                if (name is not null)
                {
                    zoneByName[name] = index;
                }

                LightRgb ambient = default;
                if (z.TryGetProperty("ambient", out JsonElement a) && a.ValueKind == JsonValueKind.Array && a.GetArrayLength() >= 3)
                {
                    ambient = new LightRgb(SvmMath.Min(1f, a[0].GetSingle() / 255f), SvmMath.Min(1f, a[1].GetSingle() / 255f),
                        SvmMath.Min(1f, a[2].GetSingle() / 255f));
                    _ambient[index] = ambient;
                }

                LightRgb fog = default;
                if (z.TryGetProperty("fog_color", out JsonElement f) && f.ValueKind == JsonValueKind.Array && f.GetArrayLength() >= 3)
                {
                    fog = new LightRgb(f[0].GetSingle() / 255f, f[1].GetSingle() / 255f, f[2].GetSingle() / 255f);
                }

                _zones.Add(new PlanZone
                {
                    Index = index,
                    Name = name ?? "",
                    Place = Str(z, "location_name") ?? "",
                    Ambient = ambient,
                    Sky = Bool(z, "sky"),
                    Fog = Bool(z, "fog"),
                    FogColour = fog,
                    FogStart = Num(z, "fog_start"),
                    FogEnd = Num(z, "fog_end"),
                });
            }
        }

        if (root.TryGetProperty("look", out JsonElement look) && look.ValueKind == JsonValueKind.Object)
        {
            GlowThreshold = (int)Num(look, "glow_threshold", 224f);
            GlowEnabled = !(look.TryGetProperty("glow_enabled", out JsonElement ge) && ge.ValueKind == JsonValueKind.False);
        }

        if (root.TryGetProperty("sky", out JsonElement sky) && sky.ValueKind == JsonValueKind.Object)
        {
            SkyZone = (int)Num(sky, "zone", -1f);
        }

        if (!root.TryGetProperty("lights", out JsonElement list) || list.ValueKind != JsonValueKind.Array)
        {
            return;
        }

        foreach (JsonElement l in list.EnumerateArray())
        {
            if (l.ValueKind != JsonValueKind.Object)
            {
                continue;
            }

            int effect = (int)Num(l, "effect");
            int type = (int)Num(l, "ltype", 1f);
            bool inGame = Bool(l, "apply_ingame");
            bool dynamic = Bool(l, "dynamic");

            // The static flag is true where the map file does not save it (the class default).
            bool applyStatic = !(l.TryGetProperty("apply_static", out JsonElement st) && st.ValueKind == JsonValueKind.False);
            bool special = Bool(l, "special_lit");
            float brightness = Num(l, "brightness", 64f);
            int zone = -1;
            JsonElement props = default;
            bool hasProps = l.TryGetProperty("props", out props) && props.ValueKind == JsonValueKind.Object;
            if (Str(l, "zone") is { } zoneName && zoneByName.TryGetValue(zoneName, out int zi))
            {
                zone = zi;
            }
            else if (hasProps && props.TryGetProperty("Region", out JsonElement region) && region.ValueKind == JsonValueKind.Object &&
                     region.TryGetProperty("ZoneNumber", out JsonElement zn) && zn.ValueKind == JsonValueKind.Number)
            {
                zone = zn.GetInt32();
            }

            float range = Num(l, "echelon_range");
            float width = Num(l, "spot_width");
            float height = Num(l, "spot_height");
            var frustum = new SpotShadowFrustum(
                range > 0f ? range : 1000f,
                width > 0f ? width : 250f,
                height > 0f ? height : 250f,
                Num(l, "near_plane_ratio", 0.1f),
                hasProps ? Num(props, "EchelonNormalInfluence", 1f) : 1f,
                (int)Num(l, "echelon_attenuation", 1f));
            bool projector = effect == ActorLightRules.LE_ESpotShadow;
            _lights.Add(new PlanLight
            {
                Name = Str(l, "name"),
                Index = _lights.Count,
                Hidden = Bool(l, "hidden"),
                ApplyStatic = applyStatic,
                ApplyInGame = inGame,
                Dynamic = dynamic,
                OwnZoneOnly = Bool(l, "own_zone_only"),
                SpecialLit = special,
                ActorShadows = !(l.TryGetProperty("actor_shadows", out JsonElement sh) && sh.ValueKind == JsonValueKind.False),
                Triggered = Bool(l, "triggered"),
                InitiallyOn = !(l.TryGetProperty("initially_on", out JsonElement io) && io.ValueKind == JsonValueKind.False),
                Static = Bool(l, "static"),
                Corona = Bool(l, "corona"),
                Beam = hasProps && Bool(props, "UsesLightBeam"),
                Effect = effect,
                LightType = type,
                Period = Num(l, "light_period", 32f),
                Phase = Num(l, "light_phase"),
                Brightness = brightness,
                Hue = (byte)Num(l, "hue"),
                Saturation = (byte)Num(l, "saturation", 255f),
                Radius = Num(l, "radius", 64f),
                Cone = (byte)Num(l, "cone", 128f),
                Location = Vec(l, "location"),
                Rotation = Rot(l, "rotation"),
                Zone = zone,
                Frustum = frustum,
                Cookie = Str(l, "echelon_texture_uri"),
                InSet = !projector && ActorLightRules.LightsActors(inGame, dynamic, type, effect),
                Class = ClassOf(special, effect, dynamic, applyStatic, inGame),
            });
        }
    }

    private static float Num(JsonElement e, string name, float fallback = 0f) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? v.GetSingle() : fallback;

    private static bool Bool(JsonElement e, string name) => e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.True;

    private static string? Str(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;

    private static FVector Vec(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Array && v.GetArrayLength() >= 3
            ? new FVector(v[0].GetSingle(), v[1].GetSingle(), v[2].GetSingle())
            : default;

    private static FRotator Rot(JsonElement e, string name) =>
        e.TryGetProperty(name, out JsonElement v) && v.ValueKind == JsonValueKind.Array && v.GetArrayLength() >= 3
            ? new FRotator(v[0].GetInt32(), v[1].GetInt32(), v[2].GetInt32())
            : default;
}
