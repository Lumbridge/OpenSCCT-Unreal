using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Devices;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The table of world entry points Unreal calls (Source/OpenSCCT/Runtime/ScctWorldApi.h mirrors it). Unreal asks for
/// <see cref="WorldExports.GetApi"/> once and keeps the table. Every entry works on the running session and returns
/// 0 (or nothing) when there is none. Strings are UTF-16, zero terminated.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct WorldApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int> GetNameCount;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<char*, int> FindName;
    public delegate* unmanaged[Cdecl]<BridgeWorldActor*, int, int> GetActors;
    public delegate* unmanaged[Cdecl]<int, BridgeWorldActorInfo*, int> GetActorInfo;
    public delegate* unmanaged[Cdecl]<BridgeWorldLight*, int, int> GetLights;
    public delegate* unmanaged[Cdecl]<BridgeWorldLaser*, int, int> GetLasers;
    public delegate* unmanaged[Cdecl]<BridgeWorldMarker*, int, int> GetMarkers;
    public delegate* unmanaged[Cdecl]<BridgeWorldAnimated*, int, int> GetAnimated;
    public delegate* unmanaged[Cdecl]<BridgeWorldEvent*, int, int> GetEvents;
    public delegate* unmanaged[Cdecl]<int, int*, int, int> GetActorSkins;
}

public static unsafe class WorldExports
{
    /// <summary>2: lights carry their place and bone, GetActorSkins added.</summary>
    public const int ApiVersion = 2;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(WorldApi* api)
    {
        api->Version = ApiVersion;
        api->GetNameCount = &GetNameCount;
        api->GetName = &GetName;
        api->FindName = &FindName;
        api->GetActors = &GetActors;
        api->GetActorInfo = &GetActorInfo;
        api->GetLights = &GetLights;
        api->GetLasers = &GetLasers;
        api->GetMarkers = &GetMarkers;
        api->GetAnimated = &GetAnimated;
        api->GetEvents = &GetEvents;
        api->GetActorSkins = &GetActorSkins;
        return ApiVersion;
    }

    /// <summary>The running session's world state (made, and hooked to the ticks, on the first call), or null.</summary>
    private static WorldState? State => Exports.Current is { } session ? WorldState.Of(session) : null;

    /// <summary>How many names the table holds, id 0 (none) included. It only grows while a session runs.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetNameCount()
    {
        try
        {
            return State?.NameCount ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Copies the name of <paramref name="id"/> into <paramref name="buffer"/> (cut to fit, always terminated).
    /// Returns its full length in characters; 0 for id 0 or an unknown id.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetName(int id, char* buffer, int capacity)
    {
        try
        {
            string name = State?.NameOf(id) ?? "";
            if (buffer != null && capacity > 0)
            {
                int count = Math.Min(name.Length, capacity - 1);
                name.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
                buffer[count] = '\0';
            }

            return name.Length;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The id of a name already in the table (exact case), 0 when it is not there.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FindName(char* name)
    {
        try
        {
            return name == null ? 0 : State?.FindName(new string(name)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes the followed map actors as drawn this frame. Returns how many there are (which can be more than
    /// <paramref name="capacity"/>: only that many are written; call with a null buffer to ask for the count).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActors(BridgeWorldActor* actors, int capacity)
    {
        try
        {
            return State?.GetActors(SpanOf(actors, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the fixed facts of the followed actor named <paramref name="nameId"/>. Returns 1, or 0 when it is not one.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActorInfo(int nameId, BridgeWorldActorInfo* info)
    {
        try
        {
            if (info == null || State is not { } state || !state.TryGetActorInfo(nameId, out BridgeWorldActorInfo found))
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

    /// <summary>Writes the map's lights as they shine and stand this frame. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLights(BridgeWorldLight* lights, int capacity)
    {
        try
        {
            return State?.GetLights(SpanOf(lights, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the laser beams to draw this frame. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLasers(BridgeWorldLaser* lasers, int capacity)
    {
        try
        {
            return State?.GetLasers(SpanOf(lasers, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the gameplay markers as of the last tick. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMarkers(BridgeWorldMarker* markers, int capacity)
    {
        try
        {
            return State?.GetMarkers(SpanOf(markers, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the skeletal map props as drawn this frame. Returns how many there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAnimated(BridgeWorldAnimated* animated, int capacity)
    {
        try
        {
            return State?.GetAnimated(SpanOf(animated, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Moves the oldest waiting events into <paramref name="events"/>; returns how many were written (call again
    /// while it returns <paramref name="capacity"/>). Each event is handed out once.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetEvents(BridgeWorldEvent* events, int capacity)
    {
        try
        {
            return State?.DrainEvents(SpanOf(events, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes the skins the followed actor named <paramref name="nameId"/> wears now: one name id per material slot,
    /// 0 for the mesh's own material. Returns the slot count (8; only <paramref name="capacity"/> are written), or 0
    /// when it is not a followed actor. Read it when an actor is first seen; SkinChanged events carry the changes.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActorSkins(int nameId, int* skins, int capacity)
    {
        try
        {
            return State?.GetActorSkins(nameId, SpanOf(skins, capacity)) ?? 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    private static Span<T> SpanOf<T>(T* buffer, int capacity)
        where T : unmanaged =>
        buffer == null || capacity <= 0 ? default : new Span<T>(buffer, capacity);
}
