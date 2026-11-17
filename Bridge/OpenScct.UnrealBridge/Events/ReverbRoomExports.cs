using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Physics.Collision;
using OpenScct.UnrealBridge.Events;

namespace OpenScct.UnrealBridge;

/// <summary>Where a point is for the reverb (Source/OpenSCCT/Runtime/ScctReverbRoomApi.h FScctReverbRoom).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeReverbRoom
{
    /// <summary>The point's BSP zone number (0: the default zone, solid space, or no collision yet).</summary>
    public int ZoneIndex;

    /// <summary>1: the zone has a ZoneInfo (its place is in ZoneX..Z); 2: the acoustic scene has a way to it from the listener.</summary>
    public int Flags;

    /// <summary>The place of the zone's ZoneInfo actor, which is how the map's audio table names the zone's reverb.</summary>
    public float ZoneX, ZoneY, ZoneZ;

    /// <summary>The way from the listener to the zone through the map's openings: its length, level and high-frequency gain.</summary>
    public float PathDistance, PathGain, PathHF;

    /// <summary>Openings on that way (0: the listener's own zone).</summary>
    public int Hops;

    /// <summary>The last opening on the way, where the way enters the zone (the listener's place for its own zone).</summary>
    public float OpeningX, OpeningY, OpeningZ;

    /// <summary>Godot's AcousticScene.ReflectedGain of the way: what the zone's reverb keeps on its way to the listener.</summary>
    public float ReflectedGain;
}

/// <summary>The measured acoustic volume around a point (FScctReverbGeometry, Godot's ReverbGeometry).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeReverbGeometry
{
    /// <summary>1 once a whole sweep gave a usable estimate (the other fields are then set).</summary>
    public int Valid;

    /// <summary>Cube root of the volume in metres, 1 to 100.</summary>
    public float Size;

    /// <summary>Volume in cubic metres and surface in square metres of the sampled space.</summary>
    public float Volume, Area;

    /// <summary>The share of rays that found nothing within range, 0 to 1.</summary>
    public float Openness;

    /// <summary>Sabine-style decay time in seconds and its high and low frequency ratios.</summary>
    public float Decay, HFDecayRatio, LFDecayRatio;

    /// <summary>Early and late reflection delays in seconds.</summary>
    public float EarlyDelay, LateDelay;

    public float Diffusion;

    /// <summary>How much of a reverb a bounded room leaves (low in the open), 0.001 to 1.</summary>
    public float WetGain;

    /// <summary>Rays traced and sweeps completed since the slot was last reset.</summary>
    public int Rays, Sweeps;
}

/// <summary>
/// The reverb rooms of the map for Unreal's source-room and adaptive reverb (Play/ScctAudioRooms.cpp): which zone a
/// point is in and the way to it (Godot's IAudioWorld.ZoneIdAt / ZoneIndexAt and AcousticScene.PathTo, as
/// ReverbBuses.RoomForSource asks them), and the bounded room-size probe of ReverbBuses.Geometry and
/// ReverbGeometryProbe. Unreal asks for <see cref="ReverbRoomExports.GetApi"/> (type
/// "OpenScct.UnrealBridge.ReverbRoomExports, OpenScct.UnrealBridge", method "GetApi").
/// Source/OpenSCCT/Runtime/ScctReverbRoomApi.h mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ReverbRoomApi
{
    public int Version;

    /// <summary>
    /// Points is Count x 3 floats; writes Count rooms in their order. Listener (3 floats) and UpdateScene: with
    /// UpdateScene 1 the acoustic scene's ways are brought up to date from the listener first (when the acoustic table
    /// did not, its "Sound through walls" not being Material). Returns Count, or 0 when no session runs.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float*, float, int, float*, int, BridgeReverbRoom*, int> Locate;

    /// <summary>
    /// Traces up to MaxRays more rays of slot Slot's sweep around Origin (3 floats) in zone ZoneIndex (a new sweep
    /// starts there when the last one is done) and writes the slot's latest estimate. Returns the rays traced, or -1
    /// when no session runs.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, float*, int, int, BridgeReverbGeometry*, int> Probe;

    /// <summary>Forgets slot Slot's sweep and estimate (-1: every slot).</summary>
    public delegate* unmanaged[Cdecl]<int, void> Reset;
}

public static unsafe class ReverbRoomExports
{
    public const int ApiVersion = 1;

    [UnmanagedCallersOnly]
    public static int GetApi(ReverbRoomApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->Locate = &Locate;
        api->Probe = &Probe;
        api->Reset = &Reset;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Locate(float* listener, float deltaSeconds, int updateScene, float* points, int count, BridgeReverbRoom* rooms)
    {
        if (listener == null || (count > 0 && (points == null || rooms == null)) || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            ReverbRooms state = session.Feature(static s => new ReverbRooms(EventsFeature.Of(s).Acoustics));
            state.Locate(new FVector(listener[0], listener[1], listener[2]), deltaSeconds, updateScene != 0,
                new ReadOnlySpan<float>(points, Math.Max(count, 0) * 3), new Span<BridgeReverbRoom>(rooms, Math.Max(count, 0)));
            return Math.Max(count, 0);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Probe(int slot, float* origin, int zoneIndex, int maxRays, BridgeReverbGeometry* geometry)
    {
        if (origin == null || geometry == null || Exports.Current is not { } session)
        {
            return -1;
        }

        try
        {
            ReverbRooms state = session.Feature(static s => new ReverbRooms(EventsFeature.Of(s).Acoustics));
            return state.Probe(slot, new FVector(origin[0], origin[1], origin[2]), zoneIndex, maxRays, out *geometry);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Reset(int slot)
    {
        if (Exports.Current is not { } session)
        {
            return;
        }

        try
        {
            session.Feature(static s => new ReverbRooms(EventsFeature.Of(s).Acoustics)).Reset(slot);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }
}
