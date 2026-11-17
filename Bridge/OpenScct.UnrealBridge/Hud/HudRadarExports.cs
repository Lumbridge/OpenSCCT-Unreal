using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The heart sensor's cone as the radar draws it (<see cref="HudRadarApi.GetHeartSensor"/>).
/// Source/OpenSCCT/Runtime/ScctHudRadarApi.h mirrors it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudHeartSensorState
{
    /// <summary>As <c>HudState.HeartCone</c>: 0 hidden, 1 idle, 2 scanning, 3 detected.</summary>
    public int Cone;

    /// <summary>The shown cone's clip frame (0 up to <see cref="LoopFrames"/>, 10 a second) and its loop length; 0 when none.</summary>
    public float Frame;

    public int LoopFrames;

    /// <summary>Scan pings and heartbeats the sensor asked for this session.</summary>
    public int CheckSounds;

    public int DetectedSounds;

    /// <summary>Of those, the calls raised as sound events (a sensor whose sound is None raises none).</summary>
    public int SoundsRaised;
}

/// <summary>
/// The radar's table of entry points, beside the HUD's own (<see cref="HudExports"/>, whose GetRadar gives the blips):
/// the heart sensor's cone with its clip frame, and an unattended check's way to select the sensor. Unreal asks for
/// <see cref="GetApi"/> once (<c>Host.GetFunction("OpenScct.UnrealBridge.HudRadarExports", "GetApi")</c>).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudRadarApi
{
    public int Version;

    /// <summary>Fills the heart sensor's state after the last tick; 1 when a cone is shown, else 0.</summary>
    public delegate* unmanaged[Cdecl]<HudHeartSensorState*, int> GetHeartSensor;

    /// <summary>Unattended checks only: selects the local pawn's heart beat sensor with the gadget keys; 1 when it is selected.</summary>
    public delegate* unmanaged[Cdecl]<int> SelectHeartSensor;
}

public static unsafe class HudRadarExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the radar API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudRadarApi* api)
    {
        api->Version = ApiVersion;
        api->GetHeartSensor = &GetHeartSensor;
        api->SelectHeartSensor = &SelectHeartSensor;
        return ApiVersion;
    }

    /// <summary>What <see cref="GetHeartSensor"/> fills, from the session's sensor driver (the probe calls this).</summary>
    public static HudHeartSensorState Read(HudHeartSensor sensor) => new()
    {
        Cone = (int)sensor.Cone,
        Frame = sensor.Frame,
        LoopFrames = sensor.LoopFrames,
        CheckSounds = sensor.CheckSounds,
        DetectedSounds = sensor.DetectedSounds,
        SoundsRaised = sensor.SoundsRaised,
    };

    /// <summary>What <see cref="SelectHeartSensor"/> does: the gadget keys' own selection, slot by slot, until the sensor is selected.</summary>
    public static bool Select(SPlayerController? pc)
    {
        if (pc?.SBPawn is not { } pawn)
        {
            return false;
        }

        for (int slot = 0; slot < 8 && pawn.SelectedGadget is not SGadHeartBeatSensor; slot++)
        {
            pc.SelectGadget(slot);
        }

        return pawn.SelectedGadget is SGadHeartBeatSensor;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetHeartSensor(HudHeartSensorState* state)
    {
        try
        {
            HudHeartSensorState read = Exports.Current is { IsDisposed: false } session ? Read(HudHeartSensor.Of(session)) : default;
            if (state != null)
            {
                *state = read;
            }

            return read.Cone != (int)HeartCone.Hidden ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SelectHeartSensor()
    {
        try
        {
            return Select(Exports.Current?.Session.LocalController is { bDeleteMe: false } pc ? pc : null) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
