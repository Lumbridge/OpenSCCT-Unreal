using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game;

namespace OpenScct.UnrealBridge;

/// <summary>
/// What the listening player's view does to the match's sound this frame (Source/OpenSCCT/Runtime/ScctAudioViewApi.h
/// FScctAudioView): the scope's or binoculars' audio zoom and the view FOV it is measured against, as the Godot
/// client's PlayAudio feeds SoundSystem.SetAudioZoom, and the actors whose own sounds are bound to the listener.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeAudioView
{
    /// <summary>The local controller's AudioZoomFactor: 0 outside a scope or binoculars, else 0.33, 0.66 or 1 by zoom step.</summary>
    public float Zoom;

    /// <summary>The local controller's FovAngle in degrees (horizontal over a 4:3 picture, the zoom in it).</summary>
    public float Fov;

    /// <summary>The actor the player views (its ViewTarget, else its pawn), 0 for none: its ambient loop moves with the listener.</summary>
    public int ViewActorId;

    /// <summary>The pawn the player controls, 0 for none.</summary>
    public int LocalPawnId;

    /// <summary>1 when a local controller exists this frame, 0 otherwise (the other fields are then 0 and 90).</summary>
    public int Valid;
}

/// <summary>
/// The listening player's audio view. Unreal asks for <see cref="AudioViewExports.GetApi"/> (type
/// "OpenScct.UnrealBridge.AudioViewExports, OpenScct.UnrealBridge", method "GetApi").
/// Source/OpenSCCT/Runtime/ScctAudioViewApi.h mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct AudioViewApi
{
    public int Version;

    /// <summary>Fills the audio view of this frame. Returns 1, or 0 when no session runs (the view is then cleared).</summary>
    public delegate* unmanaged[Cdecl]<BridgeAudioView*, int> GetView;
}

public static unsafe class AudioViewExports
{
    public const int ApiVersion = 1;

    /// <summary>The FOV of a view the zoom is not measured against (no controller): the original's default.</summary>
    private const float DefaultFov = 90f;

    [UnmanagedCallersOnly]
    public static int GetApi(AudioViewApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->GetView = &GetView;
        return ApiVersion;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetView(BridgeAudioView* view)
    {
        if (view == null)
        {
            return 0;
        }

        *view = new BridgeAudioView { Fov = DefaultFov };
        if (Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            // PlayAudio.cs: SetAudioZoom(pc.AudioZoomFactor, pc.FovAngle), and the view actor and local pawn the
            // sound system keeps for the listener-bound sounds.
            if (session.Session.LocalController is { bDeleteMe: false } pc)
            {
                view->Zoom = Math.Max(pc.AudioZoomFactor, 0f);
                view->Fov = pc.FovAngle > 0f ? pc.FovAngle : DefaultFov;
                view->ViewActorId = (pc.ViewTarget ?? pc.Pawn)?.ActorId ?? 0;
                view->LocalPawnId = pc.Pawn?.ActorId ?? 0;
                view->Valid = 1;
            }

            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
