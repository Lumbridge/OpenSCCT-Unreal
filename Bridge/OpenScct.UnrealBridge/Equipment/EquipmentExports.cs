using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Server;
using OpenScct.UnrealBridge.Equipment;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The table of equipment entry points Unreal calls: what pawns hold and wear, what hangs on their bones, and the
/// local player's first-person view model. Unreal asks for <see cref="EquipmentExports.GetApi"/> once
/// (Host.GetFunction("OpenScct.UnrealBridge.EquipmentExports, OpenScct.UnrealBridge", "GetApi")) and keeps the
/// table. Source/OpenSCCT/Runtime/ScctEquipmentApi.h mirrors it field for field.
/// <para>Per frame, after the bridge's Update: GetEquipment, GetAttachments (then GetAttachmentPose for the animated
/// ones), GetFirstPerson (then GetFirstPersonPose for each part). All of them describe the same drawn moment as
/// GetPawns and GetPawnPose. Names cross as ids; GetName gives an id's text once and Unreal keeps it.</para>
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct EquipmentApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetName;
    public delegate* unmanaged[Cdecl]<BridgeEquipment*, int, int> GetEquipment;
    public delegate* unmanaged[Cdecl]<BridgeAttachment*, int, int> GetAttachments;
    public delegate* unmanaged[Cdecl]<int, int, BridgeBone*, int, int> GetAttachmentPose;
    public delegate* unmanaged[Cdecl]<BridgeFirstPerson*, int> GetFirstPerson;
    public delegate* unmanaged[Cdecl]<int, BridgeBone*, int, int> GetFirstPersonPose;
    public delegate* unmanaged[Cdecl]<BridgeEquipmentMesh*, int, int> GetCatalog;
    public delegate* unmanaged[Cdecl]<int, BridgeBoneInfo*, int, int> GetMeshSkeleton;
}

public static unsafe class EquipmentExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(EquipmentApi* api)
    {
        api->Version = ApiVersion;
        api->GetName = &GetName;
        api->GetEquipment = &GetEquipment;
        api->GetAttachments = &GetAttachments;
        api->GetAttachmentPose = &GetAttachmentPose;
        api->GetFirstPerson = &GetFirstPerson;
        api->GetFirstPersonPose = &GetFirstPersonPose;
        api->GetCatalog = &GetCatalog;
        api->GetMeshSkeleton = &GetMeshSkeleton;
        return ApiVersion;
    }

    /// <summary>
    /// Copies the text of a name id into <paramref name="buffer"/> (UTF-16, zero terminated, cut to fit). Returns its
    /// full length in characters; 0 for id 0 or an id never given out. Works without a session.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetName(int id, char* buffer, int capacity)
    {
        try
        {
            string text = EquipmentNames.Text(id);
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

    /// <summary>Writes what every pawn holds and wears this frame. Returns the count written.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetEquipment(BridgeEquipment* equipment, int capacity)
    {
        if (Exports.Current is not { } session || equipment == null || capacity <= 0)
        {
            return 0;
        }

        try
        {
            return EquipmentFeature.For(session).GetEquipment(new Span<BridgeEquipment>(equipment, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes everything hanging on the pawns' bones this frame. Returns the count written.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAttachments(BridgeAttachment* attachments, int capacity)
    {
        if (Exports.Current is not { } session || attachments == null || capacity <= 0)
        {
            return 0;
        }

        try
        {
            return EquipmentFeature.For(session).GetAttachments(new Span<BridgeAttachment>(attachments, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes the own bone pose of an attachment flagged Animated by the last GetAttachments (each bone relative to
    /// its parent, in GetMeshSkeleton's order). Returns the bone count (only <paramref name="capacity"/> are
    /// written), 0 when the attachment is not animated.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetAttachmentPose(int actorId, int slot, BridgeBone* bones, int capacity)
    {
        if (Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            return EquipmentFeature.For(session).GetAttachmentPose(actorId, slot, bones == null || capacity <= 0 ? default : new Span<BridgeBone>(bones, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the first-person view model of this frame. Returns 1 when one is drawn, else 0 (and a cleared struct).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetFirstPerson(BridgeFirstPerson* view)
    {
        if (view == null)
        {
            return 0;
        }

        *view = default;
        if (Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            bool shown = EquipmentFeature.For(session).GetFirstPerson(out BridgeFirstPerson drawn);
            *view = drawn;
            return shown ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes the pose of a part of this frame's first-person view (each bone relative to its parent, in
    /// GetMeshSkeleton's order). Returns the bone count (only <paramref name="capacity"/> are written), 0 when there
    /// is no such part.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetFirstPersonPose(int part, BridgeBone* bones, int capacity)
    {
        if (Exports.Current is not { } session)
        {
            return 0;
        }

        try
        {
            return EquipmentFeature.For(session).GetFirstPersonPose(part, bones == null || capacity <= 0 ? default : new Span<BridgeBone>(bones, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes the weapon and gadget mesh files of the content. Returns the entry count (only
    /// <paramref name="capacity"/> are written). Works before a session starts, once the content is configured.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetCatalog(BridgeEquipmentMesh* meshes, int capacity)
    {
        try
        {
            return EquipmentCatalog.Fill(SharedRoot(), meshes == null || capacity <= 0 ? default : new Span<BridgeEquipmentMesh>(meshes, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Writes the skeleton of an equipment mesh by the name id of its asset ("SWeaponFirst.1DEF_01"), in the order
    /// its poses are written. Returns the bone count (only <paramref name="capacity"/> are written), 0 when the
    /// content has no such mesh. Works before a session starts, once the content is configured.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMeshSkeleton(int mesh, BridgeBoneInfo* bones, int capacity)
    {
        try
        {
            return EquipmentModel.Load(EquipmentNames.Text(mesh), SharedRoot()) is { } model
                ? BridgeSession.GetSkeleton(model.Character, bones == null || capacity <= 0 ? default : new Span<BridgeBoneInfo>(bones, capacity))
                : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The shared content of the running session, else of the configured content.</summary>
    private static string? SharedRoot() =>
        Exports.Current is { } session ? EquipmentFeature.For(session).SharedRoot : ContentPaths.Shared;
}
