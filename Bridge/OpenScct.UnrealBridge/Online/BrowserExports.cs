using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>BrowserRow.Flags: the online table's server flags (OnlineServerFlags) plus the browser's own.</summary>
public static class BrowserRowFlags
{
    /// <summary>The server answered a query (its ping, phase and players are its own).</summary>
    public const int Answered = 512;

    /// <summary>The server is one of the player's saved addresses.</summary>
    public const int Saved = 1024;

    /// <summary>The LAN search found the server.</summary>
    public const int OnLan = 2048;
}

/// <summary>One row of the server browser as Unreal reads it (Source/OpenSCCT/Runtime/ScctBrowserApi.h FScctBrowserRow).</summary>
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
public unsafe struct BrowserRowData
{
    public const int KeyCapacity = 96;
    public const int NameCapacity = 64;
    public const int MapCapacity = 32;
    public const int RegionCapacity = 16;
    public const int AddressCapacity = 64;
    public const int ProblemCapacity = 48;

    public int PingMs;
    public int Players;
    public int MaxPlayers;
    public int Mode;
    public int Phase;
    public int Flags;
    public int Source;
    public int Reach;
    public fixed char Key[KeyCapacity];
    public fixed char Name[NameCapacity];
    public fixed char Map[MapCapacity];
    public fixed char Region[RegionCapacity];
    public fixed char Address[AddressCapacity];
    public fixed char Problem[ProblemCapacity];
}

/// <summary>
/// The server browser's entry points Unreal calls (Source/OpenSCCT/Runtime/ScctBrowserApi.h mirrors it): one list of
/// the master's servers, the LAN search and the saved addresses with a measured ping each (Online/ServerBrowser.cs),
/// the selected server's players, and a join that takes a password and a side and goes the way the row is reached.
/// A table of its own, so the online table keeps its layout. Every entry catches its exceptions. Strings are UTF-16,
/// zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BrowserApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> Open;
    public delegate* unmanaged[Cdecl]<void> Close;
    public delegate* unmanaged[Cdecl]<int> GetRevision;
    public delegate* unmanaged[Cdecl]<BrowserRowData*, int, int> GetRows;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetPlayers;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> Join;
}

/// <summary>GetText's kinds.</summary>
public enum BrowserText
{
    /// <summary>What the browser is doing: the master's state, LAN and saved answers.</summary>
    Status = 0,

    /// <summary>Why the last Join was refused, or empty.</summary>
    Refusal = 1,
}

public static unsafe class BrowserExports
{
    public const int ApiVersion = 1;

    private static ServerBrowser Browser => OnlineBridge.Instance.Browser;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(BrowserApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Open = &Open;
        api->Close = &Close;
        api->GetRevision = &GetRevision;
        api->GetRows = &GetRows;
        api->GetText = &GetText;
        api->GetPlayers = &GetPlayers;
        api->Join = &Join;
        return ApiVersion;
    }

    /// <summary>
    /// A new search: the LAN on <paramref name="lanPort"/>, the saved addresses (separated by ';'), and the master's list
    /// when one is set. With <paramref name="loopbackOnly"/> 1 the LAN search asks this machine only (test runs). Returns
    /// 1, or 0 when no UDP socket could be opened (the master's servers still come; the status says why).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Open(int lanPort, char* saved, int loopbackOnly)
    {
        try
        {
            return Browser.Open(lanPort, saved == null ? null : new string(saved), loopbackOnly != 0) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Stops searching (the page closed). The rows stay until the next Open.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Close()
    {
        try
        {
            Browser.Close();
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    /// <summary>Goes up whenever a row or the status changed (the online table's Update runs the search).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRevision()
    {
        try
        {
            return Browser.Revision;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    /// <summary>Writes up to <paramref name="capacity"/> rows, in the browser's order. Returns the number of rows there are.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRows(BrowserRowData* rows, int capacity)
    {
        try
        {
            return Rows(Browser, rows == null ? Span<BrowserRowData>.Empty : new Span<BrowserRowData>(rows, Math.Max(capacity, 0)));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>A text of the browser (BrowserText). Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int kind, char* buffer, int capacity)
    {
        try
        {
            string text = (BrowserText)kind switch
            {
                BrowserText.Status => Browser.Status,
                BrowserText.Refusal => Browser.Refusal,
                _ => string.Empty,
            };
            return OnlineTextCopy.Buffer(buffer, capacity, text);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>The row's players by side ("Spies: a, b     Mercenaries: c"). Returns its full length in characters.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPlayers(int index, char* buffer, int capacity)
    {
        try
        {
            var rows = Browser.Rows;
            return OnlineTextCopy.Buffer(buffer, capacity, index >= 0 && index < rows.Count ? ServerBrowser.PlayersText(rows[index]) : string.Empty);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    /// <summary>
    /// Joins the row at <paramref name="index"/> with a password (null for none) and a side (0 spies, 1 mercs, 255 watch,
    /// -1 the smaller side). Returns 1 when the join started (the online table's state follows it), 0 when refused
    /// (GetText Refusal says why).
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Join(int index, char* password, int preferredTeam)
    {
        try
        {
            return Browser.Join(index, password == null ? null : new string(password), preferredTeam) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the browser's rows as <c>GetRows</c> does (the probe calls it too). Returns the number of rows.</summary>
    public static int Rows(ServerBrowser browser, Span<BrowserRowData> rows)
    {
        var list = browser.Rows;
        for (int i = 0; i < list.Count && i < rows.Length; i++)
        {
            BrowserRow r = list[i];
            string? problem = browser.Problem(r);
            ref BrowserRowData target = ref rows[i];
            target.PingMs = r.PingMs;
            target.Players = r.Players;
            target.MaxPlayers = r.MaxPlayers;
            target.Mode = r.Mode;
            target.Phase = (int)r.Phase;
            target.Source = (int)r.Source;
            target.Reach = (int)r.Reach;
            target.Flags =
                (r.Password ? OnlineServerFlags.Password : 0) |
                (r.Dedicated ? OnlineServerFlags.Dedicated : 0) |
                (r.RequireAccounts ? OnlineServerFlags.RequireAccounts : 0) |
                (r.Trusted ? OnlineServerFlags.Trusted : 0) |
                (r.Practice ? OnlineServerFlags.Practice : 0) |
                (problem != "another build" ? OnlineServerFlags.SameBuild : 0) |
                (problem is null || !problem.StartsWith("missing map", StringComparison.Ordinal) ? OnlineServerFlags.HasMap : 0) |
                (problem is null || !problem.StartsWith("different version", StringComparison.Ordinal) ? OnlineServerFlags.SameMapVersion : 0) |
                (r.MaxPlayers > 0 && r.Players >= r.MaxPlayers ? OnlineServerFlags.Full : 0) |
                (r.Seen is not null ? BrowserRowFlags.Answered : 0) |
                (r.SavedAddress.Length > 0 ? BrowserRowFlags.Saved : 0) |
                (r.OnLan ? BrowserRowFlags.OnLan : 0);
            fixed (char* key = target.Key, name = target.Name, map = target.Map, region = target.Region, address = target.Address, why = target.Problem)
            {
                OnlineTextCopy.Fixed(key, BrowserRowData.KeyCapacity, r.Key);
                OnlineTextCopy.Fixed(name, BrowserRowData.NameCapacity, r.Name.Length > 0 ? r.Name : r.Key);
                OnlineTextCopy.Fixed(map, BrowserRowData.MapCapacity, r.Map);
                OnlineTextCopy.Fixed(region, BrowserRowData.RegionCapacity, r.Region);
                OnlineTextCopy.Fixed(address, BrowserRowData.AddressCapacity, ServerBrowser.AddressOf(r));
                OnlineTextCopy.Fixed(why, BrowserRowData.ProblemCapacity, problem ?? string.Empty);
            }
        }

        return list.Count;
    }
}
