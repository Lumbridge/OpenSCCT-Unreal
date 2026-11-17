using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.SoftBodies;

namespace OpenScct.UnrealBridge;

/// <summary>The soft bodies of the session as a whole (Source/OpenSCCT/Runtime/ScctSoftBodyApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeSoftBodyState
{
    /// <summary>Bodies built from the map's render data.</summary>
    public int Count;

    /// <summary>Bodies stepped by the last update that stepped (the ones drawn in the last 5 s).</summary>
    public int Stepped;

    /// <summary>Pawn colliders the stepped bodies were handed in that update (a pawn near two bodies counts twice).</summary>
    public int Colliders;

    /// <summary>collSound and shotSound calls made since the session started.</summary>
    public int CollSounds, ShotSounds;

    /// <summary>Shot lines looked at, and how many times one crossed a body.</summary>
    public int ShotLines, ShotHits;

    /// <summary>Soft body instances of the map that could not be built (they stay plain meshes).</summary>
    public int Warnings;

    /// <summary>Fixed steps run so far, all bodies together.</summary>
    public long Steps;

    /// <summary>The farthest any particle of any body is from its rest place now, uu.</summary>
    public float MaxDisplacement;

    /// <summary>Milliseconds the last update took, and the loading of the bodies.</summary>
    public float UpdateMilliseconds, LoadMilliseconds;

    public int Reserved;
}

/// <summary><see cref="BridgeSoftBodyInfo.Flags"/>.</summary>
public static class BridgeSoftBodyFlags
{
    /// <summary>Pawns push it (collActor).</summary>
    public const int CollActor = 1;

    public const int CollSound = 2;
    public const int ShotSound = 4;
}

/// <summary>One soft body (Source/OpenSCCT/Runtime/ScctSoftBodyApi.h mirrors it).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeSoftBodyInfo
{
    /// <summary>Particles (N); the mesh has 2N vertices, vertex i and i + N (the back face) on particle i.</summary>
    public int Particles;

    public int Vertices;

    /// <summary>Triangle indices of the mesh (3 a triangle).</summary>
    public int Indices;

    /// <summary>0 a cloth patch (ESBPatchActor), 1 a strip curtain (ESBStripDoorActor).</summary>
    public int Kind;

    /// <summary><see cref="BridgeSoftBodyFlags"/>.</summary>
    public int Flags;

    /// <summary>The middle of its rest bounds in the world, and the radius around it, uu.</summary>
    public float CenterX, CenterY, CenterZ, Radius;

    /// <summary>The farthest a particle is from its rest place now, uu.</summary>
    public float Displacement;

    /// <summary>+1 or -1: how the normals were turned to agree with the library mesh's.</summary>
    public float NormalSign;
}

/// <summary>
/// The entry points for the map's soft bodies (cloth patches and strip curtains), which the bridge simulates on this
/// machine as the Godot client's SoftBodyViews does: Unreal lists the bodies, builds a mesh for each from its
/// triangles and texture coordinates, calls Update once a frame with which bodies it drew, and rewrites each drawn
/// body's vertices from GetVertices. The collision and shot sounds go out through the events feature's sounds.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct SoftBodyApi
{
    public int Version;

    /// <summary>Fills the state. Returns 1, or 0 without a session.</summary>
    public delegate* unmanaged[Cdecl]<BridgeSoftBodyState*, int> GetState;

    /// <summary>Fills one body's description. Returns 1, or 0 for an index out of range.</summary>
    public delegate* unmanaged[Cdecl]<int, BridgeSoftBodyInfo*, int> GetInfo;

    /// <summary>Writes the body's map actor name (UTF-16, no terminator). Returns its length, or 0.</summary>
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetName;

    /// <summary>Writes the triangle indices (Info.Indices of them) and the texture coordinates (2 floats a vertex). Returns 1 when both fit.</summary>
    public delegate* unmanaged[Cdecl]<int, int*, int, float*, int, int> GetMesh;

    /// <summary>
    /// Once a frame: the frame's seconds and, a byte a body (Count of them; null for all), whether it was drawn this
    /// frame. Takes the frame's shots and steps the bodies drawn in the last 5 s. Returns the bodies stepped.
    /// </summary>
    public delegate* unmanaged[Cdecl]<float, byte*, int, int> Update;

    /// <summary>
    /// Writes the body's particles in its actor's local space, six floats each (position, normal). Returns the
    /// particles written, 0 when the buffer is short.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, float*, int, int> GetVertices;

    /// <summary>Unattended checks: a pawn-sized cylinder walks through the body over the seconds given. Returns 1.</summary>
    public delegate* unmanaged[Cdecl]<int, float, int> Walk;

    /// <summary>Unattended checks: a shot line through the body's middle. Returns 1.</summary>
    public delegate* unmanaged[Cdecl]<int, int> Shoot;
}

public static unsafe class SoftBodyExports
{
    public const int ApiVersion = 1;

    [UnmanagedCallersOnly]
    public static int GetApi(SoftBodyApi* api)
    {
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->GetState = &GetState;
        api->GetInfo = &GetInfo;
        api->GetName = &GetName;
        api->GetMesh = &GetMesh;
        api->Update = &Update;
        api->GetVertices = &GetVertices;
        api->Walk = &Walk;
        api->Shoot = &Shoot;
        return ApiVersion;
    }

    private static SoftBodyFeature? Feature() =>
        Exports.Current is { IsDisposed: false } session ? SoftBodyFeature.Of(session) : null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(BridgeSoftBodyState* state)
    {
        try
        {
            if (state == null || Feature() is not { } f)
            {
                return 0;
            }

            *state = f.State();
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInfo(int index, BridgeSoftBodyInfo* info)
    {
        try
        {
            if (info == null || Feature() is not { } f || !f.Info(index, out BridgeSoftBodyInfo found))
            {
                return 0;
            }

            *info = found;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetName(int index, char* into, int capacity)
    {
        try
        {
            if (into == null || Feature() is not { } f)
            {
                return 0;
            }

            string name = f.Name(index);
            int length = Math.Min(name.Length, capacity);
            name.AsSpan(0, length).CopyTo(new Span<char>(into, capacity));
            return length;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMesh(int index, int* indices, int indexCapacity, float* uvs, int uvCapacity)
    {
        try
        {
            if (indices == null || uvs == null || Feature() is not { } f)
            {
                return 0;
            }

            return f.Mesh(index, new Span<int>(indices, indexCapacity), new Span<float>(uvs, uvCapacity)) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Update(float dt, byte* seen, int count)
    {
        try
        {
            if (Feature() is not { } f)
            {
                return 0;
            }

            return f.Update(dt, seen == null || count <= 0 ? ReadOnlySpan<byte>.Empty : new ReadOnlySpan<byte>(seen, count));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetVertices(int index, float* into, int capacity)
    {
        try
        {
            if (into == null || Feature() is not { } f)
            {
                return 0;
            }

            return f.Vertices(index, new Span<float>(into, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Walk(int index, float seconds)
    {
        try
        {
            return Feature() is { } f && f.Walk(index, seconds) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Shoot(int index)
    {
        try
        {
            return Feature() is { } f && f.Shoot(index) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
