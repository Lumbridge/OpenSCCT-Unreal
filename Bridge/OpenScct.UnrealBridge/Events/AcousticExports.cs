using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.UnrealBridge.Events;

namespace OpenScct.UnrealBridge;

/// <summary>One 3D voice Unreal asks about this frame (Source/OpenSCCT/Runtime/ScctAcousticApi.h FScctAcousticSource).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeAcousticSource
{
    /// <summary>Unreal's own handle for the voice; its state is kept by it between frames.</summary>
    public int Key;

    /// <summary>The actor the sound belongs to (its SoundOcclusion None exempts it), 0 for none.</summary>
    public int ActorId;

    /// <summary>1: never occluded (a sound at the listener).</summary>
    public int Flags;

    /// <summary>Where the voice is, in world units.</summary>
    public float X, Y, Z;

    public float MinDistance;

    /// <summary>The sound's radius: nothing is heard past 100 x it (0: no limit).</summary>
    public float Radius;
}

/// <summary>What a voice should sound like this frame (FScctAcousticResult).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeAcousticResult
{
    public int Key;

    /// <summary>1 blocked from the listener, 2 heard by a way round (openings or a corner), 4 that way is a corner, 8 traced this frame.</summary>
    public int Flags;

    /// <summary>Rule 18: 1 - the occlusion factor of the line from the listener (0 in the open).</summary>
    public float Occlusion;

    /// <summary>Rule 18: the minimum distance the occlusion leaves, eased over 1 s (the source's own when open).</summary>
    public float OccludedMinDistance;

    /// <summary>The level and high-frequency gain (at 5 kHz) that reach the listener, after wall and way.</summary>
    public float DirectGain, DirectHF;

    /// <summary>DirectHF as the cut-off of the voice's low-pass filter, 20,000 for none.</summary>
    public float LowPassHz;

    /// <summary>The volume multiplier on top of the voice's own: what it should get over what Unreal's curve gives at X, Y, Z.</summary>
    public float VolumeScale;

    /// <summary>Where the voice should be heard from: its place, or the opening it is heard through.</summary>
    public float X, Y, Z;

    /// <summary>The length of the way the sound takes (the straight distance in the open).</summary>
    public float Distance;

    /// <summary>0 straight, 1 fully by the way round (eased).</summary>
    public float PropagationMix;

    /// <summary>Openings on the way it is heard by (0 straight).</summary>
    public int Hops;

    /// <summary>The source's BSP zone number.</summary>
    public int SourceZone;
}

/// <summary>The acoustic scene of the session (FScctAcousticInfo).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeAcousticInfo
{
    public int Portals;
    public int Zones;
    public int ListenerZone;
    public int TrackedVoices;
    public long LineChecks;
    public long PortalProbes;

    /// <summary>Attenuated calls whose reach the occlusion shrank, and the ones that it put out of reach.</summary>
    public long ReachShrunk;
    public long ReachDropped;
}

/// <summary>
/// Sound through walls and openings for Unreal's 3D voices (Events/AcousticVoices, AcousticScene). Unreal asks for
/// <see cref="AcousticExports.GetApi"/> (type "OpenScct.UnrealBridge.AcousticExports, OpenScct.UnrealBridge", method
/// "GetApi"). Source/OpenSCCT/Runtime/ScctAcousticApi.h mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct AcousticApi
{
    public int Version;

    /// <summary>
    /// One frame of every 3D voice: Listener is 3 floats, Mode 0 material transmission, 1 the original's rule 18, 2 off.
    /// Writes Count results in the sources' order. Returns Count, or 0 when no session runs.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float*, float, int, BridgeAcousticSource*, int, BridgeAcousticResult*, int> Update;

    /// <summary>Fills the scene's counts. Returns 1, or 0 when no session runs.</summary>
    public delegate* unmanaged[Cdecl]<BridgeAcousticInfo*, int> GetInfo;
}

public static unsafe class AcousticExports
{
    public const int ApiVersion = 1;

    [UnmanagedCallersOnly]
    public static int GetApi(AcousticApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->Update = &Update;
        api->GetInfo = &GetInfo;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Update(float* listener, float deltaSeconds, int mode, BridgeAcousticSource* sources, int count,
        BridgeAcousticResult* results)
    {
        if (listener == null || (count > 0 && (sources == null || results == null)) || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            AcousticVoices voices = EventsFeature.Of(session).AcousticVoices;
            voices.Update(new FVector(listener[0], listener[1], listener[2]), deltaSeconds,
                (AcousticMode)Math.Clamp(mode, 0, 2), new ReadOnlySpan<BridgeAcousticSource>(sources, Math.Max(count, 0)),
                new Span<BridgeAcousticResult>(results, Math.Max(count, 0)));
            return Math.Max(count, 0);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInfo(BridgeAcousticInfo* info)
    {
        if (info == null || Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            EventsFeature feature = EventsFeature.Of(session);
            AcousticMap map = feature.Acoustics;
            *info = new BridgeAcousticInfo
            {
                Portals = map.Scene.PortalCount,
                Zones = map.ZoneCount,
                ListenerZone = map.Scene.ListenerZone,
                TrackedVoices = feature.AcousticVoices.Tracked,
                LineChecks = map.Queries,
                PortalProbes = map.Scene.PortalQueries,
                ReachShrunk = feature.SoundCallsReachShrunk,
                ReachDropped = feature.SoundCallsReachDropped,
            };
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
