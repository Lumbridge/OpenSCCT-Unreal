using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Server;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Where this client finds its maps (Source/OpenSCCT/Runtime/ScctMapsApi.h mirrors it): Unreal names the map folders
/// of its command line and its executable (<see cref="MapFolders"/>), asks which folder a map is in, and lists the
/// maps it can draw from their render files. A table of its own, so the online table keeps its layout. Every entry
/// catches its exceptions. Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MapsApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, char*, int> SetFolders;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int> GetMapFolder;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
}

/// <summary>MapsApi::GetText.</summary>
public enum MapsText
{
    /// <summary>Every folder searched for maps, in order, one per line.</summary>
    Roots = 0,

    /// <summary>The maps with render files (render/world.glb) a level can be built from, one per line, in the catalog's order.</summary>
    RenderableMaps = 1,
}

public static unsafe class MapsExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(MapsApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->SetFolders = &SetFolders;
        api->GetMapFolder = &GetMapFolder;
        api->GetText = &GetText;
        return ApiVersion;
    }

    /// <summary>
    /// Sets the folders searched before the defaults (<paramref name="first"/>: -ScctMaps) and after them
    /// (<paramref name="last"/>: maps next to the executable), each a list separated by newlines or semicolons.
    /// Returns how many of them exist, -1 on failure.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetFolders(char* first, char* last)
    {
        try
        {
            return MapFolders.Set(Split(first), Split(last));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    /// <summary>Copies the folder of <paramref name="map"/> into the buffer. Returns its full length, 0 when no folder has it.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMapFolder(char* map, char* buffer, int capacity)
    {
        try
        {
            string name = map == null ? string.Empty : new string(map);
            string? folder = name.Length == 0 ? null : FolderOf(name);
            return OnlineTextCopy.Buffer(buffer, capacity, folder ?? string.Empty);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, Text((MapsText)kind));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>The folder of a map in the play catalog (the managed side of <c>GetMapFolder</c>; the probe calls it).</summary>
    public static string? FolderOf(string map) => OnlineBridge.Instance.Catalog.Maps.FindFolder(map);

    /// <summary>The managed side of <c>GetText</c>.</summary>
    public static string Text(MapsText kind)
    {
        PlayCatalog catalog = OnlineBridge.Instance.Catalog;
        var lines = new List<string>();
        switch (kind)
        {
            case MapsText.Roots:
                lines.AddRange(catalog.Maps.Roots);
                break;
            case MapsText.RenderableMaps:
                foreach (MapListEntry entry in catalog.Maps.ListMaps())
                {
                    if (entry.Folder is { } folder && MapFolders.HasRender(folder))
                    {
                        lines.Add(entry.Id);
                    }
                }

                break;
        }

        return string.Join('\n', lines);
    }

    private static IEnumerable<string> Split(char* text)
    {
        if (text == null)
        {
            return [];
        }

        return new string(text).Split(['\n', '\r', ';'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
    }
}
