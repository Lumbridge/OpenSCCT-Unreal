using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.UnrealBridge.Events;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The entry points of the events feature. Unreal asks for <see cref="EventsExports.GetApi"/> once
/// (type "OpenScct.UnrealBridge.EventsExports, OpenScct.UnrealBridge", method "GetApi") and keeps the table.
/// Source/OpenSCCT/Runtime/ScctEventsApi.h mirrors it field for field. Every entry returns 0 when no session runs
/// and catches its exceptions (a failure stops the session with the text as the bridge's last error).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct EventsApi
{
    public int Version;

    /// <summary>
    /// Starts the feature for the running session (it starts on the first call of any entry; call this right after
    /// StartLocal so no event is missed). Returns 1 when a session runs.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int> Begin;

    /// <summary>Takes waiting raw events, oldest first. Returns the count written; call again while it equals the capacity.</summary>
    public delegate* unmanaged[Cdecl]<BridgeSimEvent*, int, int> DrainEvents;

    /// <summary>Takes waiting sound records, oldest first.</summary>
    public delegate* unmanaged[Cdecl]<BridgeSound*, int, int> DrainSounds;

    /// <summary>Takes waiting muzzle flashes and tracers, oldest first.</summary>
    public delegate* unmanaged[Cdecl]<BridgeShot*, int, int> DrainShots;

    /// <summary>Writes the effect actors as drawn this frame. Returns the count written.</summary>
    public delegate* unmanaged[Cdecl]<BridgeEffectActor*, int, int> GetEffectActors;

    /// <summary>Writes the live impact marks (at most 50). Returns the count written.</summary>
    public delegate* unmanaged[Cdecl]<BridgeImpactMark*, int, int> GetImpactMarks;

    /// <summary>Writes the ambient sound sources of this frame. Returns the count written.</summary>
    public delegate* unmanaged[Cdecl]<BridgeAmbientSound*, int, int> GetAmbientSounds;

    /// <summary>
    /// Copies the text of a name id (UTF-16, zero terminated) into the buffer. Returns its full length in
    /// characters, 0 for an unknown id. Works without a session; ids never change within a process.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetName;

    /// <summary>Writes an actor's current location (3 floats). Returns 1, or 0 when the actor is gone.</summary>
    public delegate* unmanaged[Cdecl]<int, float*, int> GetActorLocation;
}

public static unsafe class EventsExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(EventsApi* api)
    {
        // A null table would throw here, and nothing may be thrown into native code: 0 is "no API".
        if (api == null)
        {
            return 0;
        }

        api->Version = ApiVersion;
        api->Begin = &Begin;
        api->DrainEvents = &DrainEvents;
        api->DrainSounds = &DrainSounds;
        api->DrainShots = &DrainShots;
        api->GetEffectActors = &GetEffectActors;
        api->GetImpactMarks = &GetImpactMarks;
        api->GetAmbientSounds = &GetAmbientSounds;
        api->GetName = &GetName;
        api->GetActorLocation = &GetActorLocation;
        return ApiVersion;
    }

    /// <summary>The feature of the running session, null when none runs or it could not start.</summary>
    private static EventsFeature? Feature()
    {
        if (Exports.Current is not { } session)
        {
            return null;
        }

        try
        {
            return EventsFeature.Of(session);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return null;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Begin() => Feature() is null ? 0 : 1;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int DrainEvents(BridgeSimEvent* events, int capacity)
    {
        if (events == null || capacity <= 0 || Feature() is not { } feature)
        {
            return 0;
        }

        try
        {
            return feature.DrainEvents(new Span<BridgeSimEvent>(events, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int DrainSounds(BridgeSound* sounds, int capacity)
    {
        if (sounds == null || capacity <= 0 || Feature() is not { } feature)
        {
            return 0;
        }

        try
        {
            return feature.DrainSounds(new Span<BridgeSound>(sounds, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int DrainShots(BridgeShot* shots, int capacity)
    {
        if (shots == null || capacity <= 0 || Feature() is not { } feature)
        {
            return 0;
        }

        try
        {
            return feature.DrainShots(new Span<BridgeShot>(shots, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetEffectActors(BridgeEffectActor* actors, int capacity)
    {
        if (actors == null || capacity <= 0 || Feature() is not { } feature)
        {
            return 0;
        }

        try
        {
            return feature.GetEffectActors(new Span<BridgeEffectActor>(actors, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetImpactMarks(BridgeImpactMark* marks, int capacity)
    {
        if (marks == null || capacity <= 0 || Feature() is not { } feature)
        {
            return 0;
        }

        try
        {
            return feature.GetImpactMarks(new Span<BridgeImpactMark>(marks, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAmbientSounds(BridgeAmbientSound* sounds, int capacity)
    {
        if (sounds == null || capacity <= 0 || Feature() is not { } feature)
        {
            return 0;
        }

        try
        {
            return feature.GetAmbientSounds(new Span<BridgeAmbientSound>(sounds, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetName(int id, char* buffer, int capacity)
    {
        try
        {
            string text = EventNames.Text(id);
            if (buffer != null && capacity > 0)
            {
                int count = Math.Min(text.Length, capacity - 1);
                text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
                buffer[count] = '\0';
            }

            return text.Length;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActorLocation(int actorId, float* location)
    {
        if (location == null || Feature() is not { } feature)
        {
            return 0;
        }

        try
        {
            if (!feature.TryGetActorLocation(actorId, out FVector at))
            {
                return 0;
            }

            location[0] = at.X;
            location[1] = at.Y;
            location[2] = at.Z;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
