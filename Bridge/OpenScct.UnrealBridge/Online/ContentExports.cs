using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Maps;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Downloads from OpenSCCT-Content (Source/OpenSCCT/Runtime/ScctContentApi.h mirrors it field for field): the
/// content index, a map's download with the host's content hash, the lobby's missing-map line with its Download
/// button, the prebuilt bot navigation offered before a start with bots, and the Maps & Content page's catalogue,
/// shared pack downloads and removals (Online/ContentPacks.cs, OnlineBridge.MapFetch.cs, BotNavigationOffer.cs,
/// ContentCatalogue.cs). A table of its own, so the online and lobby tables keep their
/// layout. Every entry catches its exceptions. Strings are UTF-16, zero terminated. New entries go at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ContentApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, int> RefreshIndex;
    public delegate* unmanaged[Cdecl]<int, char*, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<char*, char*, int> StartMapDownload;
    public delegate* unmanaged[Cdecl]<char*, int> CancelDownload;
    public delegate* unmanaged[Cdecl]<char*, ContentDownloadInfo*, int> GetDownload;
    public delegate* unmanaged[Cdecl]<ContentDownloadInfo*, int> GetLobbyFetch;
    public delegate* unmanaged[Cdecl]<int> StartLobbyFetch;
    public delegate* unmanaged[Cdecl]<void> CancelLobbyFetch;
    public delegate* unmanaged[Cdecl]<char*, int> CheckBotNavigation;
    public delegate* unmanaged[Cdecl]<char*, int> StartBotNavigation;
    public delegate* unmanaged[Cdecl]<int> GetGeneration;

    // Version 2: the Maps & Content page (Online/ContentCatalogue.cs).
    public delegate* unmanaged[Cdecl]<char*, int, int> GetCatalogue;
    public delegate* unmanaged[Cdecl]<char*, int> StartSharedDownload;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int> RemoveMap;
}

/// <summary>A download as GetDownload and GetLobbyFetch describe it (32 bytes).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct ContentDownloadInfo
{
    /// <summary><see cref="ContentDownloadState"/>.</summary>
    public int State;

    /// <summary><see cref="ContentPacks.Generation"/> when it was read.</summary>
    public int Generation;

    public long Received;
    public long Total;

    /// <summary>The share of what is downloading now, 0 to 1.</summary>
    public float Fraction;

    /// <summary>GetLobbyFetch: <see cref="ContentLobbyFlags"/>.</summary>
    public int Flags;
}

/// <summary>ContentDownloadInfo.State.</summary>
public enum ContentDownloadState
{
    None = 0,
    Running = 1,
    Done = 2,
    Failed = 3,
    Stopped = 4,
}

/// <summary>ContentDownloadInfo.Flags of the lobby's line.</summary>
[Flags]
public enum ContentLobbyFlags
{
    /// <summary>There is a line to show (the player lacks the setup's map, or a download of it is known).</summary>
    Shown = 1,

    /// <summary>A Download (or Retry) button is offered.</summary>
    CanDownload = 2,

    /// <summary>A download is under way (a Stop button).</summary>
    Running = 4,

    /// <summary>The line is a failure, or a reason nothing can be downloaded.</summary>
    Problem = 8,
}

/// <summary>ContentApi::GetText.</summary>
public enum ContentText
{
    /// <summary>Why the content index could not be read, else empty.</summary>
    IndexError = 0,

    /// <summary>The download of the key: "Downloading Aquarius: 12.3 / 31.0 MB", its error or "... downloaded".</summary>
    Download = 1,

    /// <summary>The lobby's missing-map line.</summary>
    LobbyLine = 2,

    /// <summary>Where the key's download was installed, once done.</summary>
    InstalledFolder = 3,

    /// <summary>The key's (a map's) bot navigation question, or why none is offered.</summary>
    BotNavigation = 4,

    /// <summary>Where the content comes from and where downloads go (two lines).</summary>
    Library = 5,

    /// <summary>The Content page's status line: loading, the index's failure, or the count of maps.</summary>
    Summary = 6,

    /// <summary>The folder downloads go into.</summary>
    PacksFolder = 7,
}

public static unsafe class ContentExports
{
    public const int ApiVersion = 2;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(ContentApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->RefreshIndex = &RefreshIndex;
        api->GetText = &GetText;
        api->StartMapDownload = &StartMapDownload;
        api->CancelDownload = &CancelDownload;
        api->GetDownload = &GetDownload;
        api->GetLobbyFetch = &GetLobbyFetch;
        api->StartLobbyFetch = &StartLobbyFetch;
        api->CancelLobbyFetch = &CancelLobbyFetch;
        api->CheckBotNavigation = &CheckBotNavigation;
        api->StartBotNavigation = &StartBotNavigation;
        api->GetGeneration = &GetGeneration;
        api->GetCatalogue = &GetCatalogue;
        api->StartSharedDownload = &StartSharedDownload;
        api->RemoveMap = &RemoveMap;
        return ApiVersion;
    }

    /// <summary>Starts reading the index if due (force: now). Returns 0 while loading, 1 read, 2 failed.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RefreshIndex(int force)
    {
        try
        {
            return IndexState(force != 0);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 2;
        }
    }

    /// <summary>The managed side of <c>RefreshIndex</c>.</summary>
    public static int IndexState(bool force)
    {
        ContentPacks.RefreshIndex(force);
        return ContentPacks.Index is not null ? 1 : ContentPacks.IndexError is not null ? 2 : 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* key, char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, Text((ContentText)kind, key == null ? string.Empty : new string(key)));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>The managed side of <c>GetText</c>.</summary>
    public static string Text(ContentText kind, string key) => kind switch
    {
        ContentText.IndexError => ContentPacks.IndexError ?? string.Empty,
        ContentText.Download => ContentPacks.Job(key)?.Describe() ?? string.Empty,
        ContentText.LobbyLine => OnlineBridge.Instance.MapFetch?.Line ?? string.Empty,
        ContentText.InstalledFolder => ContentPacks.Job(key)?.InstalledFolder ?? string.Empty,
        ContentText.BotNavigation => BotNavigationText(key),
        ContentText.Library => ContentCatalogue.Library(),
        ContentText.Summary => ContentCatalogue.Summary(),
        ContentText.PacksFolder => OpenScct.Server.ContentPaths.PacksFolder,
        _ => string.Empty,
    };

    private static string BotNavigationText(string map)
    {
        BotNavigationOffer.Check(map, out string text);
        return text;
    }

    /// <summary>
    /// Downloads <paramref name="map"/> from OpenSCCT-Content: the index's copy with content hash <paramref name="hash"/>
    /// (16 hex digits) when one is given. Returns 1 started (or already running), 0 the index has no such map, -1
    /// the index is not read yet, -2 on failure.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartMapDownload(char* map, char* hash)
    {
        try
        {
            return Start(map == null ? string.Empty : new string(map), hash == null ? null : new string(hash));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -2;
        }
    }

    /// <summary>The managed side of <c>StartMapDownload</c>.</summary>
    public static int Start(string map, string? hash)
    {
        if (ContentPacks.Index is not { } index)
        {
            return -1;
        }

        ContentMap? found = index.FindMap(map, string.IsNullOrWhiteSpace(hash) ? null : hash);
        if (found is null || (!string.IsNullOrWhiteSpace(hash) && !string.Equals(found.ContentHash, hash, StringComparison.OrdinalIgnoreCase)))
        {
            return 0;
        }

        ContentPacks.Download(found);
        return 1;
    }

    /// <summary>Stops the download of a key (a map code, or "nav:" and a map). Returns 1 when one was running.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CancelDownload(char* key)
    {
        try
        {
            if (key != null && ContentPacks.Job(new string(key)) is { Running: true } job)
            {
                job.Cancel();
                return 1;
            }

            return 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Describes the download of a key. Returns its <see cref="ContentDownloadState"/>.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetDownload(char* key, ContentDownloadInfo* info)
    {
        try
        {
            ContentDownloadInfo read = Describe(key == null ? null : ContentPacks.Job(new string(key)));
            if (info != null)
            {
                *info = read;
            }

            return read.State;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>A job as the table describes it (the probe reads it too).</summary>
    public static ContentDownloadInfo Describe(ContentPacks.DownloadJob? job) => new()
    {
        State = (int)(job is null ? ContentDownloadState.None
            : job.Done ? ContentDownloadState.Done
            : job.Error == "stopped" ? ContentDownloadState.Stopped
            : job.Error is not null ? ContentDownloadState.Failed
            : ContentDownloadState.Running),
        Generation = ContentPacks.Generation,
        Received = job?.Received ?? 0,
        Total = job?.Total ?? 0,
        Fraction = (float)(job?.Fraction ?? 0),
    };

    /// <summary>The lobby's missing-map line (its text: GetText LobbyLine). Returns its <see cref="ContentLobbyFlags"/>.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLobbyFetch(ContentDownloadInfo* info)
    {
        try
        {
            OnlineBridge online = OnlineBridge.Instance;
            LobbyMapFetch? fetch = online.MapFetch;
            ContentDownloadInfo read = Describe(online.MapFetchJob);
            ContentLobbyFlags flags = 0;
            if (fetch is { } f)
            {
                flags |= ContentLobbyFlags.Shown;
                flags |= f.CanDownload ? ContentLobbyFlags.CanDownload : 0;
                flags |= f.Running ? ContentLobbyFlags.Running : 0;
                flags |= f.Problem ? ContentLobbyFlags.Problem : 0;
                read.Fraction = (float)f.Fraction;
            }

            read.Flags = (int)flags;
            if (info != null)
            {
                *info = read;
            }

            return read.Flags;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The lobby's Download: fetches the setup's map. Returns 1 started, 0 nothing to download.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartLobbyFetch()
    {
        try
        {
            return OnlineBridge.Instance.StartMapFetch() ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void CancelLobbyFetch()
    {
        try
        {
            OnlineBridge.Instance.CancelMapFetch();
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    /// <summary>Whether to offer a map's prebuilt bot navigation. Returns its <see cref="BotNavigationState"/>.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CheckBotNavigation(char* map)
    {
        try
        {
            return map == null ? 0 : (int)BotNavigationOffer.Check(new string(map), out _);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Downloads a map's prebuilt bot navigation (its key: "nav:" and the map). Returns 1 started, 0 none to download.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartBotNavigation(char* map)
    {
        try
        {
            return map != null && BotNavigationOffer.Start(new string(map)) is not null ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetGeneration() => ContentPacks.Generation;

    /// <summary>The Content page's rows (<see cref="ContentCatalogue.Lines"/>). Returns the text's full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetCatalogue(char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, ContentCatalogue.Lines());
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>Downloads a shared pack (key "shared:" and its id). Returns 1 started, 0 no such pack, -1 index not read yet, -2 failure.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StartSharedDownload(char* id)
    {
        try
        {
            return id == null ? 0 : ContentCatalogue.StartShared(new string(id));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -2;
        }
    }

    /// <summary>
    /// Removes a downloaded map, writing why it could not into <paramref name="error"/>. Returns 1 removed, 0 not
    /// downloaded, -1 its download is running, -2 failed.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RemoveMap(char* code, char* error, int capacity)
    {
        try
        {
            int result = ContentCatalogue.Remove(code == null ? string.Empty : new string(code), out string why);
            OnlineTextCopy.Buffer(error, capacity, why);
            return result;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            OnlineTextCopy.Buffer(error, capacity, e.Message);
            return -2;
        }
    }
}
