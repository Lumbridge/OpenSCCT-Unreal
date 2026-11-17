using System;
using OpenScct.Core;
using OpenScct.Physics.Collision;

namespace OpenScct.UnrealBridge.Events;

/// <summary>
/// What lies between the listener and a source (rule 18): the first surface seen from each end, its location, its
/// material's OcclusionType, the zone number just off it and its normal. Godot's OcclusionHits
/// (game/Scripts/Audio/AudioWorld.cs).
/// </summary>
internal readonly record struct OcclusionHits(FVector NearListener, FVector NearSource, int TypeNearListener, int TypeNearSource,
    int ZoneNearListener, int ZoneNearSource, FVector NormalNearListener, FVector NormalNearSource);

/// <summary>The direct and room levels a wall lets through (Godot's TransmissionLevels, AcousticTransmission.cs).</summary>
internal readonly record struct TransmissionLevels(float DirectGain, float DirectHF, float RoomGain, float RoomHF)
{
    public static TransmissionLevels Clear => new(1f, 1f, 1f, 1f);

    /// <summary>Eases toward <paramref name="target"/> with a 60 ms time constant.</summary>
    public TransmissionLevels Approach(TransmissionLevels target, float seconds)
    {
        float mix = 1f - MathF.Exp(-Math.Max(0f, seconds) / 0.06f);
        return new(DirectGain + ((target.DirectGain - DirectGain) * mix),
            DirectHF + ((target.DirectHF - DirectHF) * mix),
            RoomGain + ((target.RoomGain - RoomGain) * mix),
            RoomHF + ((target.RoomHF - RoomHF) * mix));
    }
}

/// <summary>
/// The original's occlusion rule and Godot's material presets for sound through walls, ported from
/// game/Scripts/Audio/SoundRules.cs (Occlusion, AmbientZoneFactor), AudioK.cs (the numbers and their evidence),
/// AcousticTransmission.cs and AudioWorld.cs (TraceOcclusion). Presentation only: nothing here decides what the
/// simulation hears.
/// </summary>
internal static class AcousticRules
{
    /// <summary>
    /// Rule 18: the direct-path factor of each Material.OcclusionType (Generic, None, Full, SingleWindow, DoubleWindow,
    /// ThinDoor, ThickDoor, WoodWall, BrickWall, StoneWall, Curtain), the EAX material presets' occlusion as a linear
    /// factor (UUNIAudioSubsystem constructor game 0x10b029a0).
    /// </summary>
    public static readonly float[] OcclusionMaterialFactor =
        [0.316228f, 1f, 1e-5f, 0.199526f, 0.056234f, 0.354813f, 0.079433f, 0.1f, 0.056234f, 0.031623f, 0.501187f];

    /// <summary>OcclusionType None: a surface sound passes through untouched.</summary>
    public const int OcclusionTypeNone = 1;

    /// <summary>Rule 18 (GetOcclusion 0x10b07100): 2 - thickness / 700, times the material, times 1.5 x 0.95 ^ (ZoneDist^2).</summary>
    private const float OcclusionThicknessScale = 700f;
    private const float OcclusionBase = 2f;
    private const float OcclusionZoneBase = 0.95f;
    private const float OcclusionZoneScale = 1.5f;

    /// <summary>Rule 18: both hits in one zone pull the factor halfway back to 1.</summary>
    private const float OcclusionSameZoneMix = 0.5f;

    /// <summary>Rule 18: the occluded minimum distance is never below this (game 0x10b0bc80).</summary>
    public const float OccludedMinDistance = 0.1f;

    /// <summary>Rule 18: the minimum distance eases to a new occlusion over at most this many seconds (Update 0x10b05f7c).</summary>
    public const float OcclusionSettleSeconds = 1f;

    /// <summary>Rule 20 and HearSound: a blocked sound's radius shrinks by 0.8 ^ (ZoneDist^2).</summary>
    private const float AmbientZoneBase = 0.8f;

    /// <summary>Actor.SoundOcclusion OCCLUSION_None: the actor's sounds are never occluded.</summary>
    public const byte SoundOcclusionNone = 1;

    /// <summary>Movers, the level and its geometry, with the hit material (UE2 0x1286 less the single-result bit).</summary>
    private const ETraceFlags OcclusionTrace = ETraceFlags.TRACE_Material | ETraceFlags.TRACE_StopAtFirstHit |
        ETraceFlags.TRACE_LevelGeometry | ETraceFlags.TRACE_Level | ETraceFlags.TRACE_Movers;

    /// <summary>
    /// GetOcclusion's two line checks (game 0x10b07100): from the listener to the source and back. False when the line
    /// from the listener is clear.
    /// </summary>
    public static bool TraceOcclusion(ICollisionWorld collision, FVector listener, FVector source, out OcclusionHits hits)
    {
        hits = default;
        if (!collision.LineCheck(out FCheckResult near, null, source, listener, default, OcclusionTrace) || !near.bHit)
        {
            return false;
        }

        // The way back from the source: the far side of what is in between (the source itself when it is inside).
        FCheckResult far = collision.LineCheck(out FCheckResult back, null, listener, source, default, OcclusionTrace) && back.bHit
            ? back
            : FCheckResult.NoHit(source);
        var world = collision as CollisionWorld;

        // The zones are looked up just off each surface, on the side the line came from (a point on the surface
        // itself can fall in solid space, zone 0).
        hits = new OcclusionHits(near.Location, far.Location, world?.OcclusionTypeOf(near.Material) ?? 0,
            far.bHit ? world?.OcclusionTypeOf(far.Material) ?? 0 : 0, collision.GetZoneIndex(near.Location + near.Normal),
            collision.GetZoneIndex(far.bHit ? far.Location + far.Normal : far.Location), near.Normal, far.Normal);
        return true;
    }

    /// <summary>
    /// Rule 18 (GetOcclusion): the occlusion factor of a source behind something, 1 unoccluded down to 0, from the
    /// thickness between the two hits, their materials (the one that lets less through counts), their zones and the
    /// portal distance between those zones. Null when the second hit's zone is 0 (the original then leaves the sound
    /// unoccluded).
    /// </summary>
    public static float? Occlusion(float thickness, int typeNearListener, int typeNearSource, int zoneNearListener,
        int zoneNearSource, int zoneDistance)
    {
        if (zoneNearSource == 0)
        {
            return null;
        }

        float m = Math.Min(MaterialFactor(typeNearListener), MaterialFactor(typeNearSource));
        float f = (OcclusionBase - (thickness / OcclusionThicknessScale)) * m;
        float zone = MathF.Pow(OcclusionZoneBase, zoneDistance * zoneDistance);
        f = Math.Min(zone * f * OcclusionZoneScale, 1f);
        if (zoneNearListener == zoneNearSource)
        {
            f += (1f - f) * OcclusionSameZoneMix;
        }

        return Math.Max(f, 0f);
    }

    /// <summary>Rule 20: the range factor for a portal distance between two zones.</summary>
    public static float ZoneReachFactor(int zoneDistance) => MathF.Pow(AmbientZoneBase, zoneDistance * zoneDistance);

    /// <summary>
    /// Godot's AcousticTransmission.Through: what a wall of <paramref name="thickness"/> between two materials lets
    /// through. Low frequencies keep a recognisable cue while the high ones are cut harder; the room field follows a
    /// gentler path.
    /// </summary>
    public static TransmissionLevels Through(float thickness, int nearMaterial, int farMaterial)
    {
        if (!float.IsFinite(thickness))
        {
            thickness = 0f;
        }

        float material = Math.Min(MaterialFactor(nearMaterial), MaterialFactor(farMaterial));
        float factor = Math.Clamp(MathF.Pow(material, 1f + (Math.Clamp(thickness, 0f, 700f) / 700f)), 0.00001f, 1f);
        return new(Math.Max(0.25f, MathF.Pow(factor, 0.25f)), Math.Max(0.001f, MathF.Pow(factor, 0.75f)),
            Math.Max(0.4f, MathF.Pow(factor, 0.15f)), Math.Max(0.03f, MathF.Pow(factor, 0.35f)));
    }

    /// <summary>
    /// The cut-off of a one-pole low-pass whose gain at <paramref name="referenceHz"/> is <paramref name="gainHF"/>:
    /// how the EAX-style high-frequency gain Godot sets on a high shelf at 5 kHz becomes the single low-pass an Unreal
    /// voice has. Clamped to 20 kHz (no cut) at the top and 200 Hz at the bottom.
    /// </summary>
    public static float LowPassHz(float gainHF, float referenceHz = 5000f)
    {
        float g = Math.Clamp(gainHF, 0.001f, 1f);
        if (g >= 0.999f)
        {
            return 20000f;
        }

        return Math.Clamp(referenceHz * g / MathF.Sqrt(1f - (g * g)), 200f, 20000f);
    }

    private static float MaterialFactor(int type) =>
        type >= 0 && type < OcclusionMaterialFactor.Length ? OcclusionMaterialFactor[type] : OcclusionMaterialFactor[0];
}
