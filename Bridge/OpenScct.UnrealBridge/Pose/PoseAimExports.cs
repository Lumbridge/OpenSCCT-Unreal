using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Game.Animation;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Equipment;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge;

/// <summary>What the last pose of a pawn aimed at (Source/OpenSCCT/Runtime/ScctPoseAimApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeAimProbe
{
    /// <summary>The body's yaw as drawn, degrees.</summary>
    public float BodyYaw;

    /// <summary>
    /// Where the drawn upper body points, degrees: the body's yaw less the AAFULL yaw over its share of the view
    /// (MeshAimIK.FullAimShare), the view yaw that pair stands for.
    /// </summary>
    public float PosedViewYaw;

    /// <summary>The same for the pair the ticks alone give (the aim interpolated between the last two ticks).</summary>
    public float TickViewYaw;

    /// <summary>The drawn and the tick AAFULL pitch, degrees.</summary>
    public float PosedPitch, TickPitch;

    /// <summary>1 when the pose aimed at the drawn view handed over this frame.</summary>
    public int FromView;
}

/// <summary>One view model channel's clip and the frame rate it ran at in the last draw.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeClipRate
{
    public int Channel;
    public float Fps;

    /// <summary>1 when an animation project's gameplay clip set the rate, 0 for the view models' stock 15 fps.</summary>
    public int FromProject;

    /// <summary>The clip's name, UTF-16, zero-terminated.</summary>
    public fixed char Name[32];
}

/// <summary>
/// The pose's tie to the frame's camera (Source/OpenSCCT/Runtime/ScctPoseAimApi.h mirrors it): the local spy's
/// third-person body aims at the view drawn this frame, the spy's eye view says which of its sockets a wall hides,
/// and the merc's view model says the frame rate of each clip. Port of the Godot client's PawnViews.Draw handing
/// SimPawnView.DrawnView over, SpyFirstPersonView.TryGetSocket and ViewModelPose's NativeFrameRate.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct PoseAimApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, float, float, float, int> SetDrawnView;
    public delegate* unmanaged[Cdecl]<int, BridgeAimProbe*, int> GetAimProbe;
    public delegate* unmanaged[Cdecl]<float*, int> GetFirstPersonSockets;
    public delegate* unmanaged[Cdecl]<BridgeClipRate*, int, int> GetViewModelClipRates;
}

public static unsafe class PoseAimExports
{
    public const int ApiVersion = 1;

    private const float DegreesToUnits = 65536f / 360f;
    private const float UnitsToDegrees = 360f / 65536f;
    private const float RadiansToDegrees = 180f / MathF.PI;

    private static PawnPoser? s_viewed;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(PoseAimApi* api)
    {
        api->Version = ApiVersion;
        api->SetDrawnView = &SetDrawnView;
        api->GetAimProbe = &GetAimProbe;
        api->GetFirstPersonSockets = &GetFirstPersonSockets;
        api->GetViewModelClipRates = &GetViewModelClipRates;
        return ApiVersion;
    }

    /// <summary>
    /// Before this frame's poses: the view drawn this frame (degrees; the controls' AimPitch/AimYaw) for the local spy
    /// <paramref name="actorId"/>; 0 hands none over. Only the local player's own spy takes it (PlayRoot's aimer: not a
    /// merc, controlled by the local controller); every other pawn is posed from the ticks. Returns 1 when taken.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetDrawnView(int actorId, float pitch, float yaw, float roll)
    {
        try
        {
            if (s_viewed is not null)
            {
                s_viewed.DrawnView = null;
                s_viewed = null;
            }

            if (actorId == 0 || Exports.Current is not { IsDisposed: false } session ||
                session.PoserOf(actorId) is not { } poser || poser.Pawn is SPAWNDEFENSE ||
                session.Session.LocalController is not { } pc || !ReferenceEquals(poser.Pawn.Controller, pc))
            {
                return 0;
            }

            poser.DrawnView = new FRotator((int)MathF.Round(pitch * DegreesToUnits), (int)MathF.Round(yaw * DegreesToUnits),
                (int)MathF.Round(roll * DegreesToUnits));
            s_viewed = poser;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>What the last pose of <paramref name="actorId"/> aimed at, into <paramref name="probe"/>. Returns 1, or 0 without a pose.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAimProbe(int actorId, BridgeAimProbe* probe)
    {
        try
        {
            if (probe == null)
            {
                return 0;
            }

            *probe = default;
            if (Exports.Current is not { IsDisposed: false } session || session.PoserOf(actorId) is not { Placed: true } poser)
            {
                return 0;
            }

            float body = FRotator.NormalizeAxis(poser.DrawnYaw) * UnitsToDegrees;
            probe->BodyYaw = body;
            probe->PosedViewYaw = body - (poser.PosedAim.X / MeshAimIK.FullAimShare * RadiansToDegrees);
            probe->TickViewYaw = body - (poser.TickAim.X / MeshAimIK.FullAimShare * RadiansToDegrees);
            probe->PosedPitch = poser.PosedAim.Y * RadiansToDegrees;
            probe->TickPitch = poser.TickAim.Y * RadiansToDegrees;
            probe->FromView = poser.PosedFromView ? 1 : 0;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// After GetFirstPerson: which sockets of this frame's spy eye view a wall hides from the eye (1 the muzzle, 2 the
    /// laser), and how far the muzzle socket is from the eye in <paramref name="muzzleDistance"/> (units, may be null).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetFirstPersonSockets(float* muzzleDistance)
    {
        try
        {
            if (muzzleDistance != null)
            {
                *muzzleDistance = 0f;
            }

            if (Exports.Current is not { IsDisposed: false } session)
            {
                return 0;
            }

            EquipmentFeature equipment = EquipmentFeature.For(session);
            if (muzzleDistance != null)
            {
                *muzzleDistance = equipment.FirstPersonMuzzleDistance;
            }

            return equipment.FirstPersonSocketsHidden;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// After GetFirstPerson: the clips the merc's view model drew this frame with their frame rates, into
    /// <paramref name="rates"/>. Returns how many were written.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetViewModelClipRates(BridgeClipRate* rates, int capacity)
    {
        try
        {
            if (rates == null || capacity <= 0 || Exports.Current is not { IsDisposed: false } session)
            {
                return 0;
            }

            ViewModelPose pose = EquipmentFeature.For(session).FirstPersonPose;
            ReadOnlySpan<ChannelPose> channels = pose.Channels;
            int count = 0;
            for (int c = 0; c < channels.Length && count < capacity; c++)
            {
                if (channels[c].Clip is null)
                {
                    continue;
                }

                BridgeClipRate* rate = rates + count;
                *rate = default;
                rate->Channel = c;
                rate->Fps = channels[c].Fps;
                rate->FromProject = pose.RateFromProject(c) ? 1 : 0;
                string name = channels[c].Sequence ?? "";
                int length = Math.Min(name.Length, 31);
                for (int i = 0; i < length; i++)
                {
                    rate->Name[i] = name[i];
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
}
