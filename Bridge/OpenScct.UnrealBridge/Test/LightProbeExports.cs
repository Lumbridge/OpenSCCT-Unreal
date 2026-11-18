using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.UnrealBridge.Test;

namespace OpenScct.UnrealBridge;

/// <summary>What the simulation believes about the light at one point (Source/OpenSCCT/Test/ScctLightProbeApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightReading
{
    /// <summary>
    /// The bots' light level for a pawn at the point, lit from every side, animated lights at their mean: the luminance
    /// of the original's pawn light term (0 black, 1 a white texture at full white, at most 2).
    /// </summary>
    public float Level;

    /// <summary>The same on the side of the pawn that faces the viewer, now: what the bots' sight uses.</summary>
    public float LevelFacing;

    /// <summary>Luminance of the zone ambient at the point (part of both levels).</summary>
    public float Ambient;

    /// <summary>
    /// The original's baked light term of the floor under the point (2 x the stored lightmap) and its luminance, in
    /// the same units as the levels. Only when <see cref="HasBaked"/> is 1.
    /// </summary>
    public float BakedR, BakedG, BakedB, Baked;

    /// <summary>Height of the floor found under the point.</summary>
    public float FloorZ;

    /// <summary>The point's zone index in the collision's BSP, -1 unknown.</summary>
    public int Zone;

    /// <summary>1 when the floor under the point is lightmapped level geometry.</summary>
    public int HasBaked;
}

/// <summary>A sample spot for the light parity measurement: the floor point, the level there and why it was chosen.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightSpot
{
    public float X, Y, Z;

    /// <summary>The bots' level for a standing pawn here (<see cref="BridgeLightReading.Level"/>).</summary>
    public float Level;

    /// <summary>0 among the darkest, 1 among the brightest, 2 the spread between, 3 the lit and 4 the dark side of a step in the level.</summary>
    public int Kind;
}

/// <summary>What the last FindSpots sampled (Source/OpenSCCT/Test/ScctLightProbeApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeLightFloor
{
    /// <summary>Walkable nodes of the grid, those on a connected floor a map light hangs over, and those sampled.</summary>
    public int FloorNodes, LitFloorNodes, PlayNodes;

    /// <summary>Starts, objectives, flag zones and switches the play floor was grown from.</summary>
    public int Anchors;

    /// <summary>1 when the play floor was sampled, 0 when any connected floor under a map light was.</summary>
    public int PlayRule;
}

/// <summary>A switched light of the map and the two spots of the switch test (LightProbe.FindSwitchSpots).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeLightSwitch
{
    /// <summary>The light's name in lighting.json (its actor id), UTF-8, zero terminated.</summary>
    public fixed byte Name[48];

    public float LightX, LightY, LightZ;

    /// <summary>How far the light reaches (uu) and its LightBrightness.</summary>
    public float Radius, Brightness;

    /// <summary>The floor point where a standing pawn's level changes most, its level now and with every switched light toggled.</summary>
    public float NearX, NearY, NearZ, NearLevel, NearToggled;

    /// <summary>A floor point beyond the radius where the level does not change (only when <see cref="HasFar"/> is 1), and its level.</summary>
    public float FarX, FarY, FarZ, FarLevel;

    public int HasFar;

    /// <summary>1 when the light is on now.</summary>
    public int On;

    /// <summary>The light's LightEffect (22 a projector).</summary>
    public int Effect;
}

/// <summary>
/// The light parity measurement's entry points (Source/OpenSCCT/Test/ScctLightParitySubsystem.h): how lit the simulation
/// believes a pawn at a point is (the bots' light field, the only light rule the simulation has), the original's baked
/// light under the point, a choice of walkable sample spots that covers the map's range, and a way to stand the local
/// player on one. Everything but <c>PlacePlayer</c> only reads the client world.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct LightProbeApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int> Prepare;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, float, BridgeLightReading*, int> Read;
    public delegate* unmanaged[Cdecl]<int, int, BridgeLightSpot*, int, BridgeLightFloor*, int> FindSpots;
    public delegate* unmanaged[Cdecl]<float, float, float, int> PlacePlayer;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, float, float, float, float*, int> PredictSphere;
    public delegate* unmanaged[Cdecl]<BridgeLightSwitch*, int, int> FindSwitchSpots;
}

public static unsafe class LightProbeExports
{
    // 2: FindSpots takes the floor rule and says what it sampled; PredictSphere and FindSwitchSpots added.
    public const int ApiVersion = 2;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(LightProbeApi* api)
    {
        api->Version = ApiVersion;
        api->Prepare = &Prepare;
        api->Read = &Read;
        api->FindSpots = &FindSpots;
        api->PlacePlayer = &PlacePlayer;
        api->PredictSphere = &PredictSphere;
        api->FindSwitchSpots = &FindSwitchSpots;
        return ApiVersion;
    }

    private static LightProbe? Probe =>
        Exports.Current is { IsDisposed: false } session && session.Session.Client.Map is not null ? session.Feature(s => new LightProbe(s)) : null;

    /// <summary>
    /// Reads the running map's lights and baked light. Returns how many lights the field holds, 0 when the map has no
    /// light data (every level would read the field's "unknown" 0.6) and -1 without a session or a loaded map.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Prepare()
    {
        try
        {
            return Probe is { } probe ? (probe.HasData ? probe.LightCount : 0) : -1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    /// <summary>
    /// The reading for a pawn of bounding radius <paramref name="radius"/> whose centre is at (x, y, z), seen from
    /// (vx, vy, vz). Returns 1 when written.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Read(float x, float y, float z, float radius, float vx, float vy, float vz, BridgeLightReading* reading)
    {
        try
        {
            if (reading == null || Probe is not { } probe)
            {
                return 0;
            }

            LightProbeReading r = probe.Read(new FVector(x, y, z), radius, new FVector(vx, vy, vz));
            *reading = new BridgeLightReading
            {
                Level = r.Level,
                LevelFacing = r.LevelFacing,
                Ambient = r.Ambient,
                BakedR = r.BakedR,
                BakedG = r.BakedG,
                BakedB = r.BakedB,
                Baked = r.Baked,
                FloorZ = r.FloorZ,
                Zone = r.Zone,
                HasBaked = r.HasBaked ? 1 : 0,
            };
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Chooses the sample spots (<paramref name="perKind"/> dark, bright and spread, and as many again across steps)
    /// and writes up to <paramref name="capacity"/> of them. Returns how many were written. It samples the map's floor
    /// first, which takes some seconds on a large map. Only the play floor is sampled, unless <paramref name="allFloor"/>
    /// is not 0 (any connected floor a map light hangs over, the first version's rule); <paramref name="floor"/>, when
    /// given, says what was sampled.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FindSpots(int perKind, int allFloor, BridgeLightSpot* spots, int capacity, BridgeLightFloor* floor)
    {
        try
        {
            if (spots == null || capacity <= 0 || Probe is not { } probe)
            {
                return 0;
            }

            List<LightProbeSpot> found = probe.FindSpots(perKind, allFloor != 0);
            if (floor != null)
            {
                *floor = new BridgeLightFloor
                {
                    FloorNodes = probe.FloorNodes,
                    LitFloorNodes = probe.LitFloorNodes,
                    PlayNodes = probe.PlayNodes,
                    Anchors = probe.PlayAnchors,
                    PlayRule = probe.PlayRule ? 1 : 0,
                };
            }

            int count = Math.Min(found.Count, capacity);
            for (int i = 0; i < count; i++)
            {
                spots[i] = new BridgeLightSpot { X = found[i].Floor.X, Y = found[i].Floor.Y, Z = found[i].Floor.Z, Level = found[i].Level, Kind = found[i].Kind };
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
    /// The display colour (0 to 1, three floats into <paramref name="rgb"/>) the original's pawn lighting gives the
    /// middle of a matte sphere centred at (x, y, z) seen from (vx, vy, vz): a texture of display value
    /// <paramref name="albedoDisplay"/> under the simulation's term, over the disc within <paramref name="discShare"/>
    /// of the sphere's radius (LightProbe.PredictSphere). Returns 1 when written.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PredictSphere(float x, float y, float z, float radius, float vx, float vy, float vz, float albedoDisplay, float discShare, float* rgb)
    {
        try
        {
            if (rgb == null || Probe is not { } probe)
            {
                return 0;
            }

            var colour = probe.PredictSphere(new FVector(x, y, z), radius, new FVector(vx, vy, vz), albedoDisplay, discShare);
            rgb[0] = colour.R;
            rgb[1] = colour.G;
            rgb[2] = colour.B;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// The switched lights of the map with the spots of the switch test, on the floor the last FindSpots sampled (call
    /// that first). Writes up to <paramref name="capacity"/> and returns how many.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FindSwitchSpots(BridgeLightSwitch* switches, int capacity)
    {
        try
        {
            if (switches == null || capacity <= 0 || Probe is not { } probe)
            {
                return 0;
            }

            List<LightProbeSwitch> found = probe.FindSwitchSpots(capacity);
            for (int i = 0; i < found.Count; i++)
            {
                LightProbeSwitch s = found[i];
                var entry = new BridgeLightSwitch
                {
                    LightX = s.Light.X, LightY = s.Light.Y, LightZ = s.Light.Z, Radius = s.Radius, Brightness = s.Brightness,
                    NearX = s.Near.X, NearY = s.Near.Y, NearZ = s.Near.Z, NearLevel = s.NearLevel, NearToggled = s.NearToggled,
                    FarX = s.Far.X, FarY = s.Far.Y, FarZ = s.Far.Z, FarLevel = s.FarLevel,
                    HasFar = s.HasFar ? 1 : 0, On = s.On ? 1 : 0, Effect = s.Effect,
                };
                for (int c = 0; c < s.Name.Length && c < 47; c++)
                {
                    entry.Name[c] = s.Name[c] < 128 ? (byte)s.Name[c] : (byte)'?';
                }

                switches[i] = entry;
            }

            return found.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Stands the local player's pawn with its centre at (x, y, z), still, on the server's next world step (a local
    /// match only). Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PlacePlayer(float x, float y, float z)
    {
        var at = new FVector(x, y, z);
        FVector local;
        try
        {
            local = Exports.Current is { IsDisposed: false } session ? session.Session.LocalPawn?.Location ?? default : default;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }

        return WorldDebugExports.OnServer(host =>
        {
            Pawn? player = null;
            foreach (Pawn p in host.World.Level.AllActors<Pawn>())
            {
                if (!p.bDeleteMe && p.Controller is PlayerController &&
                    (player is null || FVector.DistSquared(p.Location, local) < FVector.DistSquared(player.Location, local)))
                {
                    player = p;
                }
            }

            if (player is null)
            {
                return;
            }

            player.SetLocation(at);
            player.Velocity = default;
        });
    }
}
