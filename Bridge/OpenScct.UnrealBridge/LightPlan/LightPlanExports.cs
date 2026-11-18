using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Lighting;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.LightPlan;

namespace OpenScct.UnrealBridge;

// The structs below cross to C++ as they are. Source/OpenSCCT/Runtime/ScctLightPlanApi.h mirrors them field for
// field; change both together and bump LightPlanExports.ApiVersion. Every field is 4 bytes wide.

/// <summary>The map's light data in short (<c>GetLevel</c>). The class counts leave hidden lights out, as no level light stands for them.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightLevel
{
    /// <summary>1 when the map's render/lighting.json was read.</summary>
    public int HasData;

    /// <summary>Lights in the file, and how many of them are hidden.</summary>
    public int Lights, Hidden;

    /// <summary>Lights that are not hidden, by <see cref="BridgeLightClass"/>.</summary>
    public int Projectors, Dynamic, Baked, CharactersOnly, SpecialLit, Unused;

    public int Zones;

    /// <summary>LevelInfo.GlowThreshold (0 to 255) and whether the map's glow is on.</summary>
    public int GlowThreshold, GlowEnabled;

    /// <summary>The sky zone's index, -1 none.</summary>
    public int SkyZone;

    /// <summary>LevelInfo.ShadowDarknessScale (1.7 unless the map says otherwise).</summary>
    public float ShadowDarknessScale;
}

/// <summary>One light of render/lighting.json: its flags, its raw values and the plan's class (<c>GetLights</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightInfo
{
    /// <summary>The light actor's name in the world table (WorldExports.GetName; the level light's tag is "Scct:" + it), 0 when it has none.</summary>
    public int NameId;

    /// <summary><see cref="BridgeLightClass"/>.</summary>
    public int Class;

    /// <summary><see cref="BridgeLightInfoFlags"/>.</summary>
    public int Flags;

    /// <summary>The original's ELightEffect (8 and 12 cone lights, 19 sun, 20 quadratic, 21 point ambient, 22 projector).</summary>
    public int Effect;

    /// <summary>The placed ELightType (0 none, 1 steady, 2 pulse, 3 blink, 4 flicker, 5 strobe, 7 subtle pulse).</summary>
    public int LightType;

    /// <summary>LightBrightness, LightHue, LightSaturation (255 is white), LightRadius and LightCone as the map has them.</summary>
    public float Brightness;
    public int Hue, Saturation;
    public float LightRadius;
    public int Cone;

    /// <summary>How far the light reaches in uu: 25 x (LightRadius + 1); for a projector its EchelonRange.</summary>
    public float WorldRadius;

    /// <summary>A cone light's full outer angle in degrees (1.15 acos((1 - LightCone / 256)^2)); the inner one is 0.05 of it.</summary>
    public float ConeDegrees;

    /// <summary>The zone index of the light's zone, -1 unknown.</summary>
    public int Zone;

    /// <summary>Where the map placed it, and its rotation in degrees.</summary>
    public float X, Y, Z;
    public float Pitch, Yaw, Roll;

    /// <summary>
    /// The light's colour in the display units of the original's light term, before the gain: FGetHSV x brightness / 255
    /// (a projector: / 510, its brightest channel at most 1). 1 is a white texture at full white.
    /// </summary>
    public float R, G, B;

    /// <summary>What the term multiplies the colour by at the light: 2 (points, cones, projectors), 1.75 (suns).</summary>
    public float Gain;

    /// <summary>
    /// A projector's frustum: it reaches EchelonRange along its axis and lights +-SpotWidth x +-SpotHeight there; the
    /// apex sits ApexDistance behind the light (-1: parallel rays). NormalInfluence 0 lights every surface fully, 1 by
    /// N.L. Attenuation 0 none, 1 linear (1 - depth / range), 2 late (1 until 0.9).
    /// </summary>
    public float EchelonRange, SpotWidth, SpotHeight, NearPlaneRatio, NormalInfluence, ApexDistance;
    public int Attenuation;

    /// <summary>The projected texture's file in the render part (world table name id), 0 none.</summary>
    public int CookieNameId;

    public float Period, Phase;
}

/// <summary>One zone of the map (<c>GetZones</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightZone
{
    public int ZoneIndex;

    /// <summary>The zone actor's name and the place name players see (world table name ids).</summary>
    public int NameId, PlaceNameId;

    /// <summary>1 the sky zone, 2 fogged.</summary>
    public int Flags;

    /// <summary>The zone ambient in display units (m_RefAmbientColor / 255): what every actor in the zone is lit with at least.</summary>
    public float R, G, B;

    public float FogR, FogG, FogB, FogStart, FogEnd;
}

/// <summary>One light of an actor's rig (<see cref="BridgeRigLightFlags"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeRigLight
{
    /// <summary>Stable over frames: the light's index in <c>GetLights</c> for a map light, minus the actor id for a runtime light.</summary>
    public int Id;
    public int Flags;

    /// <summary>Where the light is now; for a sun the unit direction towards it.</summary>
    public float X, Y, Z;

    /// <summary>
    /// The light's colour as it reaches the actor's centre, in the display units of the term (gain, light type, falloff
    /// and cone included). A surface of the actor with normal N gets this x max(0, N.L), plus this once more whatever
    /// the normal when the light is point ambient.
    /// </summary>
    public float R, G, B;
}

/// <summary>
/// What the original's renderer lights one actor with now (<c>GetActorRig</c>, <c>GetPointRig</c>): the display term
/// of a surface is min(2, min(2, ambient + sum of the lights) + projectors), and a texture of display value t shows
/// min(1, t x term).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightRig
{
    /// <summary>The zone ambient at the actor.</summary>
    public float AmbientR, AmbientG, AmbientB;

    /// <summary>The summed term of the projectors and torches that reach the actor's centre with an open line, every side facing.</summary>
    public float SpotR, SpotG, SpotB;

    /// <summary>The simulation's level for the actor lit from every side, animated lights at their mean (0 black, at most 2).</summary>
    public float Level;

    /// <summary>The same on the side that faces the viewer, now: what the bots' sight uses.</summary>
    public float LevelFacing;

    public int Zone;

    /// <summary>How many of the four lights are filled, and how many projectors and torches are in the Spot term.</summary>
    public int Count, SpotCount;

    /// <summary>How many of the filled lights have a blocked line to the centre.</summary>
    public int Occluded;

    public BridgeRigLight Light0, Light1, Light2, Light3;
}

/// <summary>The own spy's projected shadow as the original's light cache has it (<c>GetShadowLighting</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeShadowLighting
{
    /// <summary>1 while the local player's pawn is a living spy.</summary>
    public int Valid;

    /// <summary>The shadow's darkness, 0 (no shadow) to 255.</summary>
    public int Darkness;

    /// <summary>1 when the sticky camera's second view solve runs this tick by the original's rule (the darkness before was not 0).</summary>
    public int ViewUpdate;

    /// <summary>1 when the session's sticky camera follows that rule (SetStickyCamShadow).</summary>
    public int Feeds;

    /// <summary>The unit direction from the shadow's origin towards the weighted place of the lights, and the origin.</summary>
    public float DirX, DirY, DirZ;
    public float OriginX, OriginY, OriginZ;
}

/// <summary>The local merc's squint (<c>GetSquint</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightSquint
{
    /// <summary>1 with a local player controller.</summary>
    public int Valid;

    /// <summary>SquintingFactor, 0 to 0.4.</summary>
    public float Factor;

    /// <summary>What the original adds to every light term in normal view for it: 2 x (int(factor x 28) >> 1) / 255, at most 10 / 255.</summary>
    public float Term;
}

/// <summary>One living pawn's light by the simulation (<c>GetAudit</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightAudit
{
    public int ActorId;

    /// <summary>0 spy, 1 merc.</summary>
    public int Kind;

    /// <summary>1 the local player's pawn.</summary>
    public int Flags;

    public int Zone;
    public float X, Y, Z;
    public float Level, LevelFacing;

    /// <summary>Luminance of the zone ambient and of the projector and torch term.</summary>
    public float Ambient, Spot;

    /// <summary>Lights in the rig, and how many of them have a blocked line.</summary>
    public int Lights, Occluded;
}

/// <summary>
/// The lighting plan's entry points (Source/OpenSCCT/Runtime/ScctLightPlanApi.h mirrors the table): the facts of the
/// map's lights and zones the plan classifies and dresses the level by, the actor light rig, the own spy's shadow
/// darkness and the merc's squint. Everything reads the running session and returns 0 when there is none.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct LightPlanApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<BridgeLightLevel*, int> GetLevel;
    public delegate* unmanaged[Cdecl]<BridgeLightInfo*, int, int> GetLights;
    public delegate* unmanaged[Cdecl]<BridgeLightZone*, int, int> GetZones;
    public delegate* unmanaged[Cdecl]<float, float, float, int> ZoneAt;
    public delegate* unmanaged[Cdecl]<int, float, float, float, int, BridgeLightRig*, int> GetActorRig;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, float, int, BridgeLightRig*, int> GetPointRig;
    public delegate* unmanaged[Cdecl]<BridgeShadowLighting*, int> GetShadowLighting;
    public delegate* unmanaged[Cdecl]<BridgeLightSquint*, int> GetSquint;
    public delegate* unmanaged[Cdecl]<float, float, float, BridgeLightAudit*, int, int> GetAudit;
    public delegate* unmanaged[Cdecl]<int, int> SetStickyCamShadow;
}

public static unsafe class LightPlanExports
{
    public const int ApiVersion = 2;

    private const float RotatorToDegrees = 360f / 65536f;

    [ThreadStatic]
    private static RigResult? s_rig;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(LightPlanApi* api)
    {
        api->Version = ApiVersion;
        api->GetLevel = &GetLevel;
        api->GetLights = &GetLights;
        api->GetZones = &GetZones;
        api->ZoneAt = &ZoneAt;
        api->GetActorRig = &GetActorRig;
        api->GetPointRig = &GetPointRig;
        api->GetShadowLighting = &GetShadowLighting;
        api->GetSquint = &GetSquint;
        api->GetAudit = &GetAudit;
        api->SetStickyCamShadow = &SetStickyCamShadow;
        return ApiVersion;
    }

    private static LightPlanState? State =>
        Exports.Current is { IsDisposed: false } session && session.Session.Client.Map is not null ? LightPlanState.Of(session) : null;

    /// <summary>Writes the map's light data in short. Returns 1 when written.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLevel(BridgeLightLevel* level)
    {
        try
        {
            if (level == null || State is not { } state)
            {
                return 0;
            }

            LightPlanMap map = state.Map;
            var row = new BridgeLightLevel
            {
                HasData = map.HasData ? 1 : 0,
                Lights = map.Lights.Count,
                Zones = map.Zones.Count,
                GlowThreshold = map.GlowThreshold,
                GlowEnabled = map.GlowEnabled ? 1 : 0,
                SkyZone = map.SkyZone,
                ShadowDarknessScale = Exports.Current!.Session.Client.World.Level.Info.ShadowDarknessScale,
            };
            foreach (PlanLight l in map.Lights)
            {
                if (l.Hidden)
                {
                    row.Hidden++;
                    continue;
                }

                switch (l.Class)
                {
                    case BridgeLightClass.Projector: row.Projectors++; break;
                    case BridgeLightClass.Dynamic: row.Dynamic++; break;
                    case BridgeLightClass.Baked: row.Baked++; break;
                    case BridgeLightClass.CharactersOnly: row.CharactersOnly++; break;
                    case BridgeLightClass.SpecialLit: row.SpecialLit++; break;
                    default: row.Unused++; break;
                }
            }

            *level = row;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes the map's lights in the file's order (a rig's light id is the index here). Returns how many there are
    /// (which can be more than <paramref name="capacity"/>: only that many are written; a null buffer asks for the count).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLights(BridgeLightInfo* lights, int capacity)
    {
        try
        {
            if (State is not { } state)
            {
                return 0;
            }

            IReadOnlyList<PlanLight> all = state.Map.Lights;
            int count = lights == null ? 0 : Math.Min(Math.Max(capacity, 0), all.Count);
            for (int i = 0; i < count; i++)
            {
                PlanLight l = all[i];
                bool projector = l.IsProjector;
                LightRgb colour = projector
                    ? ActorLightRules.SpotShadowColour(l.Hue, l.Saturation, l.Brightness)
                    : ActorLightRules.Colour(l.Hue, l.Saturation, l.Brightness);
                SpotShadowFrustum f = l.Frustum;
                lights[i] = new BridgeLightInfo
                {
                    NameId = l.Name is { Length: > 0 } name ? state.World.Intern(name) : 0,
                    Class = l.Class,
                    Flags = (l.ApplyStatic ? BridgeLightInfoFlags.ApplyStatic : 0) | (l.ApplyInGame ? BridgeLightInfoFlags.ApplyInGame : 0) |
                        (l.Dynamic ? BridgeLightInfoFlags.Dynamic : 0) | (l.OwnZoneOnly ? BridgeLightInfoFlags.OwnZoneOnly : 0) |
                        (l.SpecialLit ? BridgeLightInfoFlags.SpecialLit : 0) | (l.ActorShadows ? BridgeLightInfoFlags.ActorShadows : 0) |
                        (l.Triggered ? BridgeLightInfoFlags.Triggered : 0) | (l.InitiallyOn ? BridgeLightInfoFlags.InitiallyOn : 0) |
                        (l.Hidden ? BridgeLightInfoFlags.Hidden : 0) | (l.Static ? BridgeLightInfoFlags.Static : 0) |
                        (l.Corona ? BridgeLightInfoFlags.Corona : 0) | (l.InSet ? BridgeLightInfoFlags.LightsActors : 0) |
                        (l.Beam ? BridgeLightInfoFlags.Beam : 0) | (ActorLightRules.IsSpot(l.Effect) ? BridgeLightInfoFlags.Spot : 0) |
                        (l.Effect == ActorLightRules.LE_PointAmbient ? BridgeLightInfoFlags.PointAmbient : 0) |
                        (l.Effect == ActorLightRules.LE_Sunlight ? BridgeLightInfoFlags.Sun : 0),
                    Effect = l.Effect,
                    LightType = l.LightType,
                    Brightness = l.Brightness,
                    Hue = l.Hue,
                    Saturation = l.Saturation,
                    LightRadius = l.Radius,
                    Cone = l.Cone,
                    WorldRadius = projector ? f.Range : ActorLightRules.WorldRadius(l.Radius),
                    ConeDegrees = ActorLightRules.SpotPhi(l.Cone) * (180f / MathF.PI),
                    Zone = l.Zone,
                    X = l.Location.X,
                    Y = l.Location.Y,
                    Z = l.Location.Z,
                    Pitch = FRotator.NormalizeAxis(l.Rotation.Pitch) * RotatorToDegrees,
                    Yaw = FRotator.NormalizeAxis(l.Rotation.Yaw) * RotatorToDegrees,
                    Roll = FRotator.NormalizeAxis(l.Rotation.Roll) * RotatorToDegrees,
                    R = colour.R,
                    G = colour.G,
                    B = colour.B,
                    Gain = l.Effect == ActorLightRules.LE_Sunlight ? ActorLightRules.SunLightGain
                        : projector ? ActorLightRules.SpotShadowGain : ActorLightRules.PointLightGain,
                    EchelonRange = f.Range,
                    SpotWidth = f.SpotWidth,
                    SpotHeight = f.SpotHeight,
                    NearPlaneRatio = f.NearPlaneRatio,
                    NormalInfluence = f.NormalInfluence,
                    ApexDistance = ActorLightRules.ApexDistance(in f) ?? -1f,
                    Attenuation = f.Attenuation,
                    CookieNameId = l.Cookie is { Length: > 0 } cookie ? state.World.Intern(cookie) : 0,
                    Period = l.Period,
                    Phase = l.Phase,
                };
            }

            return all.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the map's zones. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetZones(BridgeLightZone* zones, int capacity)
    {
        try
        {
            if (State is not { } state)
            {
                return 0;
            }

            IReadOnlyList<PlanZone> all = state.Map.Zones;
            int count = zones == null ? 0 : Math.Min(Math.Max(capacity, 0), all.Count);
            for (int i = 0; i < count; i++)
            {
                PlanZone z = all[i];
                zones[i] = new BridgeLightZone
                {
                    ZoneIndex = z.Index,
                    NameId = z.Name.Length > 0 ? state.World.Intern(z.Name) : 0,
                    PlaceNameId = z.Place.Length > 0 ? state.World.Intern(z.Place) : 0,
                    Flags = (z.Sky ? 1 : 0) | (z.Fog ? 2 : 0),
                    R = z.Ambient.R,
                    G = z.Ambient.G,
                    B = z.Ambient.B,
                    FogR = z.FogColour.R,
                    FogG = z.FogColour.G,
                    FogB = z.FogColour.B,
                    FogStart = z.FogStart,
                    FogEnd = z.FogEnd,
                };
            }

            return all.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The zone index of a point by the collision's BSP, -1 unknown.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ZoneAt(float x, float y, float z)
    {
        try
        {
            return State?.ZoneAt(new FVector(x, y, z)) ?? -1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    /// <summary>
    /// The rig of the pawn with the simulation actor id <paramref name="actorId"/> (BridgePawn.ActorId), seen from
    /// (vx, vy, vz); <paramref name="options"/> are <see cref="BridgeLightRigOptions"/>. Returns 1 when written, 0 when
    /// there is no such pawn.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActorRig(int actorId, float vx, float vy, float vz, int options, BridgeLightRig* rig)
    {
        try
        {
            if (rig == null || State is not { } state)
            {
                return 0;
            }

            RigResult result = s_rig ??= new RigResult();
            if (!state.RigOfPawn(actorId, new FVector(vx, vy, vz), options, result))
            {
                return 0;
            }

            *rig = Pack(result);
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// The rig of anything actor-lit whose bounding sphere is ((x, y, z), <paramref name="radius"/>): a view model, a
    /// mover, a prop, a probe. Returns 1 when written.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPointRig(float x, float y, float z, float radius, float vx, float vy, float vz, int options, BridgeLightRig* rig)
    {
        try
        {
            if (rig == null || State is not { } state)
            {
                return 0;
            }

            RigResult result = s_rig ??= new RigResult();
            state.Rig(new FVector(x, y, z), radius, new FVector(vx, vy, vz), options, null, result);
            *rig = Pack(result);
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    private static BridgeLightRig Pack(RigResult r)
    {
        var rig = new BridgeLightRig
        {
            AmbientR = r.Ambient.R,
            AmbientG = r.Ambient.G,
            AmbientB = r.Ambient.B,
            SpotR = r.Spot.R,
            SpotG = r.Spot.G,
            SpotB = r.Spot.B,
            Level = r.Level,
            LevelFacing = r.LevelFacing,
            Zone = r.Zone,
            Count = r.Count,
            SpotCount = r.SpotCount,
        };
        BridgeRigLight* lights = &rig.Light0;
        for (int i = 0; i < r.Count; i++)
        {
            RigLight l = r.Lights[i];
            lights[i] = new BridgeRigLight
            {
                Id = l.Id, Flags = l.Flags, X = l.Position.X, Y = l.Position.Y, Z = l.Position.Z, R = l.Colour.R, G = l.Colour.G, B = l.Colour.B,
            };
            rig.Occluded += (l.Flags & BridgeRigLightFlags.Occluded) != 0 ? 1 : 0;
        }

        return rig;
    }

    /// <summary>Writes the own spy's shadow darkness as of the last tick. Returns 1 when written.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetShadowLighting(BridgeShadowLighting* shadow)
    {
        try
        {
            if (shadow == null || State is not { } state)
            {
                return 0;
            }

            FVector d = state.ShadowDirection, o = state.ShadowOrigin;
            *shadow = new BridgeShadowLighting
            {
                Valid = state.ShadowValid ? 1 : 0,
                Darkness = state.ShadowDarkness,
                ViewUpdate = state.ShadowViewUpdate ? 1 : 0,
                Feeds = state.FeedsStickyCamera ? 1 : 0,
                DirX = d.X, DirY = d.Y, DirZ = d.Z,
                OriginX = o.X, OriginY = o.Y, OriginZ = o.Z,
            };
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the local merc's squint. Returns 1 when written.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSquint(BridgeLightSquint* squint)
    {
        try
        {
            if (squint == null || State is not { } state)
            {
                return 0;
            }

            bool valid = state.Squint(out float factor, out float term);
            *squint = new BridgeLightSquint { Valid = valid ? 1 : 0, Factor = factor, Term = term };
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes one row per living pawn: its level by the simulation, and on the side that faces (vx, vy, vz) (the
    /// camera). Returns how many pawns there are.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAudit(float vx, float vy, float vz, BridgeLightAudit* rows, int capacity)
    {
        try
        {
            if (State is not { } state)
            {
                return 0;
            }

            RigResult rig = s_rig ??= new RigResult();
            var viewer = new FVector(vx, vy, vz);
            Pawn? local = Exports.Current!.Session.LocalPawn;
            int count = 0;
            foreach (Pawn pawn in state.Pawns())
            {
                if (rows != null && count < capacity)
                {
                    state.Rig(pawn.Location, LightPlanState.PawnRadius(pawn), viewer, 0, pawn, rig);
                    int occluded = 0;
                    for (int i = 0; i < rig.Count; i++)
                    {
                        occluded += (rig.Lights[i].Flags & BridgeRigLightFlags.Occluded) != 0 ? 1 : 0;
                    }

                    rows[count] = new BridgeLightAudit
                    {
                        ActorId = pawn.ActorId,
                        Kind = pawn is SPAWNDEFENSE ? 1 : 0,
                        Flags = ReferenceEquals(pawn, local) ? 1 : 0,
                        Zone = rig.Zone,
                        X = pawn.Location.X, Y = pawn.Location.Y, Z = pawn.Location.Z,
                        Level = rig.Level,
                        LevelFacing = rig.LevelFacing,
                        Ambient = rig.Ambient.Luminance,
                        Spot = rig.Spot.Luminance,
                        Lights = rig.Count,
                        Occluded = occluded,
                    };
                }

                count++;
            }

            return count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// 1: the sticky camera's fly-in follows the original's shadow rule (solved twice a tick only while the own spy's
    /// shadow is drawn). 0: the session's default (always twice). Returns what is set now.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetStickyCamShadow(int on)
    {
        try
        {
            if (State is not { } state)
            {
                return 0;
            }

            state.FeedStickyCamera(on != 0);
            return state.FeedsStickyCamera ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
