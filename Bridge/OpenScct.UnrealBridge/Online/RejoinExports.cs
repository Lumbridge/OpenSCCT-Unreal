using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Rejoining a dropped round (Online/RejoinService.cs), as Unreal calls it: Source/OpenSCCT/Runtime/ScctRejoinApi.h
/// mirrors it field for field. A table of its own, so the online table keeps its layout. Every entry catches its
/// exceptions. Strings are UTF-16, zero terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct RejoinApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, void> SetFile;
    public delegate* unmanaged[Cdecl]<void> Check;
    public delegate* unmanaged[Cdecl]<RejoinChoiceData*, int> GetRejoin;
    public delegate* unmanaged[Cdecl]<int> Rejoin;
    public delegate* unmanaged[Cdecl]<void> Dismiss;
    public delegate* unmanaged[Cdecl]<char*, int, int> TakeLine;
}

/// <summary>The round offered for a rejoin, as GetRejoin fills it.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct RejoinChoiceData
{
    public const int NameCapacity = 64;
    public const int MapCapacity = 32;
    public const int WhereCapacity = 64;

    /// <summary>Goes up whenever the offer changes.</summary>
    public int Revision;

    /// <summary>1 when a round is offered.</summary>
    public int Offered;

    /// <summary>1 when the master offered it for the signed-in account (a round dropped on any PC).</summary>
    public int FromAccount;

    /// <summary>1 when the server's password was kept with it.</summary>
    public int HasPassword;

    /// <summary>1 while the master or the server is being asked whether the remembered round is still played.</summary>
    public int Checking;

    public fixed char ServerName[NameCapacity];
    public fixed char Map[MapCapacity];

    /// <summary>The address, or "master id ..." for a server reached through the master.</summary>
    public fixed char Where[WhereCapacity];
}

public static unsafe class RejoinExports
{
    public const int ApiVersion = 1;

    private static RejoinService Service => OnlineBridge.Instance.Rejoin;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(RejoinApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->SetFile = &SetFile;
        api->Check = &Check;
        api->GetRejoin = &GetRejoin;
        api->Rejoin = &Rejoin;
        api->Dismiss = &Dismiss;
        api->TakeLine = &TakeLine;
        return ApiVersion;
    }

    /// <summary>Where the record is kept (Saved/Online/rejoin.json); null or empty turns rejoining off (test runs).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetFile(char* path)
    {
        string? file = path == null ? null : new string(path);
        Guard(() => Service.SetFile(file));
    }

    /// <summary>The main menu is up: checks the remembered round (once per drop) and the account's offer.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Check() => Guard(() => Service.Check());

    /// <summary>Fills <paramref name="choice"/>. Returns 1 when a round is offered.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRejoin(RejoinChoiceData* choice)
    {
        try
        {
            return Fill(Service, choice);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The managed side of <c>GetRejoin</c> (the probe calls it too).</summary>
    public static int Fill(RejoinService service, RejoinChoiceData* choice)
    {
        ArgumentNullException.ThrowIfNull(service);
        RejoinChoice? offer = service.Offer;
        if (choice != null)
        {
            choice->Revision = service.Revision;
            choice->Offered = offer is null ? 0 : 1;
            choice->FromAccount = offer?.FromAccount == true ? 1 : 0;
            choice->HasPassword = offer?.Password is { Length: > 0 } ? 1 : 0;
            choice->Checking = service.Checking ? 1 : 0;
            OnlineTextCopy.Fixed(choice->ServerName, RejoinChoiceData.NameCapacity, offer?.ServerName);
            OnlineTextCopy.Fixed(choice->Map, RejoinChoiceData.MapCapacity, offer?.Map);
            OnlineTextCopy.Fixed(choice->Where, RejoinChoiceData.WhereCapacity,
                offer is null ? null : offer.Address ?? (offer.MasterId is { } id ? "master id " + id : null));
        }

        return offer is null ? 0 : 1;
    }

    /// <summary>Rejoins the offered round (the online table follows the visit as any other). Returns 1 when the join started.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Rejoin()
    {
        try
        {
            return Service.Rejoin() ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Not now: the round is not offered again.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Dismiss() => Guard(() => Service.Dismiss());

    /// <summary>Writes the service's next log line, cut to fit. Returns its full length, -1 when none waits.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeLine(char* buffer, int capacity)
    {
        try
        {
            return Service.TakeLine(out string line) ? OnlineTextCopy.Buffer(buffer, capacity, line) : -1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    private static void Guard(Action action)
    {
        try
        {
            action();
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }
}
