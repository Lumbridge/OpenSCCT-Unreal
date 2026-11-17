using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.UnrealBridge.Devices;

namespace OpenScct.UnrealBridge;

/// <summary>What <see cref="MarkSurfaceExports"/> knows of an impact mark's surface (Source/OpenSCCT/Runtime/ScctMarkSurfaceApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeMarkSurface
{
    /// <summary>The world table's name id of the map actor that stands for the surface (WorldExports.GetName), 0 when the world state does not follow it.</summary>
    public int NameId;

    /// <summary><see cref="BridgeMarkSurfaceFlags"/>.</summary>
    public int Flags;

    /// <summary>Where the surface is in the simulation now, and its turn in degrees: for a surface Unreal has no actor for.</summary>
    public float X, Y, Z, Pitch, Yaw, Roll;
}

public static class BridgeMarkSurfaceFlags
{
    /// <summary>The surface is an actor of the client's level that has not been destroyed.</summary>
    public const int Exists = 1;

    /// <summary>ImpactMark.IsDrawn: the surface is drawn now, so a mark on it is too (not hidden, not DT_None).</summary>
    public const int Drawn = 2;
}

/// <summary>
/// Entry points for drawing an impact mark on the actor it hit (a door, a lift, a pane of glass): the bridge's events
/// hand each mark's surface as a simulation actor id (BridgeImpactMark.SurfaceActorId), and this says which followed
/// map actor that is and whether it is drawn now, as the Godot client's CombatFxView.AttachDecal and
/// UpdateDecalAttachment read it. It reads the client world and never writes it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MarkSurfaceApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, BridgeMarkSurface*, int> GetSurface;
}

public static unsafe class MarkSurfaceExports
{
    public const int ApiVersion = 1;

    private const float RotatorToDegrees = 360f / 65536f;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(MarkSurfaceApi* api)
    {
        api->Version = ApiVersion;
        api->GetSurface = &GetSurface;
        return ApiVersion;
    }

    /// <summary>
    /// Describes the surface with simulation id <paramref name="actorId"/> into <paramref name="surface"/>. Returns
    /// its flags: 0 when there is no session, the id is 0 (the level's own geometry) or the actor is gone.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetSurface(int actorId, BridgeMarkSurface* surface)
    {
        try
        {
            if (surface == null)
            {
                return 0;
            }

            *surface = default;
            if (actorId == 0 || Exports.Current is not { IsDisposed: false } session ||
                session.Session.Client.World.Level.FindActor(actorId) is not { } actor || actor.bDeleteMe)
            {
                return 0;
            }

            surface->NameId = WorldState.Of(session).NameIdOf(actor);
            surface->Flags = BridgeMarkSurfaceFlags.Exists | (ImpactMark.IsDrawn(actor) ? BridgeMarkSurfaceFlags.Drawn : 0);
            surface->X = actor.Location.X;
            surface->Y = actor.Location.Y;
            surface->Z = actor.Location.Z;
            surface->Pitch = actor.Rotation.Pitch * RotatorToDegrees;
            surface->Yaw = actor.Rotation.Yaw * RotatorToDegrees;
            surface->Roll = actor.Rotation.Roll * RotatorToDegrees;
            return surface->Flags;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
