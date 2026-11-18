using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Game.Camera;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Where the local player's first-person view stands against the head bone and the cylinder's eye, for the check of
/// the camera's bone service (<see cref="PoseCameraBones"/>). Mirrored by FScctCameraBoneState; every field is 4 bytes.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeCameraBoneState
{
    /// <summary>1 while the local camera is attached to a bone (SPlayerCam.m_bAttachToBone).</summary>
    public int Attached;

    /// <summary>1 when the local pawn's pose has a 'B Head' bone and the three places below are filled.</summary>
    public int HasHead;

    /// <summary>The view of the last tick (units).</summary>
    public float ViewX, ViewY, ViewZ;

    /// <summary>'B Head' as last drawn.</summary>
    public float HeadX, HeadY, HeadZ;

    /// <summary>The eye of the pawn's cylinder: its location plus the eye height and the walk bob.</summary>
    public float EyeX, EyeY, EyeZ;

    /// <summary>Bone lookups the service has answered since the game began.</summary>
    public int Answered;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct CameraBoneApi
{
    public int Version;

    /// <summary>Writes the local player's state. Returns 1, or 0 without a session, a local pawn or a view.</summary>
    public delegate* unmanaged[Cdecl]<BridgeCameraBoneState*, int> GetState;

    /// <summary>
    /// A check only (the game's -ScctKnockCheck switch): knocks the local player down backwards as a hit does
    /// (s_FallDown, label DIE_Backward), which attaches the first-person camera to 'B Head'. Returns 1 when done.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int> KnockCheck;
}

/// <summary>
/// The check of the camera's bone service from Unreal (Source/OpenSCCT/Runtime/ScctCameraBoneApi.h mirrors the table).
/// The service itself needs no table: the simulation asks it during its own tick and the view Unreal draws already
/// carries the answer.
/// </summary>
public static unsafe class CameraBoneExports
{
    public const int ApiVersion = 1;

    private static readonly Name s_head = new("B Head");
    private static readonly Name s_fallDown = new("s_FallDown");
    private static readonly Name s_backward = new("DIE_Backward");

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(CameraBoneApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->GetState = &GetState;
        api->KnockCheck = &KnockCheck;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(BridgeCameraBoneState* state)
    {
        try
        {
            if (state == null || Exports.Current is not { IsDisposed: false } session)
            {
                return 0;
            }

            return Read(session, out *state) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The managed side of the GetState entry.</summary>
    public static bool Read(BridgeSession session, out BridgeCameraBoneState state)
    {
        state = default;
        var view = session.Session.View;
        if (session.Session.LocalController is not { } pc || session.Session.LocalPawn is not { } pawn || !view.Valid)
        {
            return false;
        }

        FVector eye = pawn.Location + pawn.WalkBob + new FVector(0f, 0f, pawn.EyeHeight);
        state.Attached = PlayerCamera.Of(pc)?.m_bAttachToBone == true ? 1 : 0;
        state.ViewX = view.Location.X;
        state.ViewY = view.Location.Y;
        state.ViewZ = view.Location.Z;
        state.EyeX = eye.X;
        state.EyeY = eye.Y;
        state.EyeZ = eye.Z;
        state.Answered = (int)Math.Min(PoseCameraBones.Answered, int.MaxValue);
        if (session.PoserOf(pawn.ActorId) is { } poser && ReferenceEquals(poser.Pawn, pawn) && poser.TryGetBone(s_head, out FVector head, out _))
        {
            state.HasHead = 1;
            state.HeadX = head.X;
            state.HeadY = head.Y;
            state.HeadZ = head.Z;
        }

        return true;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int KnockCheck()
    {
        try
        {
            return Exports.Current is { IsDisposed: false } session && Knock(session) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The managed side of the KnockCheck entry (the probe calls it): the owner changes state and the server follows.</summary>
    public static bool Knock(BridgeSession session)
    {
        if (session.Session.LocalController is not { } pc || session.Session.LocalPawn is null)
        {
            return false;
        }

        pc.eventChangeState(s_fallDown, s_backward);
        return true;
    }
}
