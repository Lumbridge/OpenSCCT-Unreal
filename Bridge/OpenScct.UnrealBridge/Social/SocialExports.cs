using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Online;
using OpenScct.UnrealBridge.Social;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The social table (Source/OpenSCCT/Runtime/ScctSocialApi.h mirrors it): the Discord status and a friend's Join, the
/// website join and the master's presence lease, all of <see cref="SocialBridge"/>. Unreal asks for
/// <see cref="GetApi"/> once and keeps the table. Every entry catches its exceptions (none may cross into native code);
/// an unexpected one goes to the main table's LastError and the call returns its empty answer. Strings are UTF-16, zero
/// terminated. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct SocialApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int, void> Start;
    public delegate* unmanaged[Cdecl]<int, char*, char*, void> SetDiscord;
    public delegate* unmanaged[Cdecl]<SocialPresence*, char*, char*, char*, int> SetPresence;
    public delegate* unmanaged[Cdecl]<float, int, int> Update;
    public delegate* unmanaged[Cdecl]<SocialStatus*, int> GetStatus;
    public delegate* unmanaged[Cdecl]<SocialRequest*, char*, int, char*, int, int> TakeRequest;
    public delegate* unmanaged[Cdecl]<char*, int, int> TakeLog;
    public delegate* unmanaged[Cdecl]<SocialJoin*, char*, int, int> GetVisitJoin;
    public delegate* unmanaged[Cdecl]<char*, char*, char*, int, int, int> JoinMaster;
    public delegate* unmanaged[Cdecl]<char*, char*, char*, int, int, int> JoinHubListed;
    public delegate* unmanaged[Cdecl]<char*, int> SimulateDiscordJoin;
    public delegate* unmanaged[Cdecl]<char*, int> QueueWebJoin;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetPresenceText;
}

public static unsafe class SocialExports
{
    public const int ApiVersion = 1;

    /// <summary>SocialApi.Start's flags.</summary>
    public const int StartLease = 1;

    private static SocialBridge Social => SocialBridge.Instance;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(SocialApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Start = &Start;
        api->SetDiscord = &SetDiscord;
        api->SetPresence = &SetPresence;
        api->Update = &Update;
        api->GetStatus = &GetStatus;
        api->TakeRequest = &TakeRequest;
        api->TakeLog = &TakeLog;
        api->GetVisitJoin = &GetVisitJoin;
        api->JoinMaster = &JoinMaster;
        api->JoinHubListed = &JoinHubListed;
        api->SimulateDiscordJoin = &SimulateDiscordJoin;
        api->QueueWebJoin = &QueueWebJoin;
        api->GetPresenceText = &GetPresenceText;
        return ApiVersion;
    }

    /// <summary>Starts the website join (with the command line's server id, or null) and, with <see cref="StartLease"/>, the presence lease. Once.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Start(char* webJoinId, int flags) =>
        Guarded(() =>
        {
            Social.Start(Text(webJoinId) is { Length: > 0 } id ? id : null, (flags & StartLease) != 0);
            return 0;
        });

    /// <summary>Turns the Discord status on (1) or off (0); the exe and the command register the game for a friend's Join (empty: no Join).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetDiscord(int enabled, char* launchExe, char* launchCommand) =>
        Guarded(() =>
        {
            Social.SetDiscord(enabled != 0, Text(launchExe), Text(launchCommand));
            return 0;
        });

    /// <summary>The player's state now, with the server's or hub's name, the map's id and the join value. Returns 1.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetPresence(SocialPresence* presence, char* server, char* map, char* joinValue)
    {
        if (presence == null)
        {
            return 0;
        }

        SocialPresence copy = *presence;
        return Guarded(() =>
        {
            Social.SetPresence(copy, Text(server), Text(map), Text(joinValue));
            return 1;
        });
    }

    /// <summary>Once a frame: <paramref name="seconds"/> since the last call, readiness <see cref="SocialReadiness"/>. Returns the requests waiting.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Update(float seconds, int ready) =>
        Guarded(() =>
        {
            Social.Update(Math.Max(0f, seconds), (SocialReadiness)ready);
            return Social.RequestsWaiting;
        });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetStatus(SocialStatus* status)
    {
        if (status == null)
        {
            return 0;
        }

        try
        {
            *status = Social.Status;
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            *status = default;
            return 0;
        }
    }

    /// <summary>Takes the oldest request: its kind and flags, its value and text (cut to fit). Returns 1, or 0 when none waits.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeRequest(SocialRequest* request, char* value, int valueCapacity, char* text, int textCapacity)
    {
        try
        {
            if (!Social.TakeRequest(out SocialRequest next, out string v, out string t))
            {
                return 0;
            }

            if (request != null)
            {
                *request = next;
            }

            OnlineTextCopy.Buffer(value, valueCapacity, v);
            OnlineTextCopy.Buffer(text, textCapacity, t);
            return 1;
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    /// <summary>Takes the oldest log line (cut to fit). Returns its full length, or -1 when none waits.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeLog(char* buffer, int capacity)
    {
        try
        {
            return Social.TakeLog(out string line) ? OnlineTextCopy.Buffer(buffer, capacity, line) : -1;
        }
        catch (Exception e)
        {
            Failed(e);
            return -1;
        }
    }

    /// <summary>The online visit's join secret: its kind (-1 none) and password flag, the value copied. Returns the value's length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetVisitJoin(SocialJoin* join, char* value, int capacity)
    {
        try
        {
            DiscordJoinSecret? secret = SocialBridge.VisitJoin();
            if (join != null)
            {
                join->Kind = secret is null ? -1 : (int)secret.Kind;
                join->Password = secret?.NeedsPassword == true ? 1 : 0;
            }

            return OnlineTextCopy.Buffer(value, capacity, secret?.Value);
        }
        catch (Exception e)
        {
            Failed(e);
            if (join != null)
            {
                join->Kind = -1;
                join->Password = 0;
            }

            return OnlineTextCopy.Buffer(value, capacity, string.Empty);
        }
    }

    /// <summary>Joins the server the master lists by id (team 0 spies, 1 mercs, 255 watch, -1 the smaller side). Returns 1 when the master is asked.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int JoinMaster(char* id, char* name, char* password, int team, int requireAccount)
    {
        string serverId = Text(id), label = Text(name), pass = Text(password);
        return Guarded(() => serverId.Length > 0 && Social.JoinMaster(serverId, label, pass, team, requireAccount != 0) ? 1 : 0);
    }

    /// <summary>Joins the hub the master lists by id as the player's name on a side. Returns 1 when the master is asked.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int JoinHubListed(char* id, char* hubName, char* playerName, int team, int requireAccount)
    {
        string hubId = Text(id), hub = Text(hubName), player = Text(playerName);
        return Guarded(() => Social.JoinHubListed(hubId, hub, player, team, requireAccount != 0) ? 1 : 0);
    }

    /// <summary>A friend's Join with this secret, as Discord would hand it (unattended checks). Returns 1 when it is understood.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SimulateDiscordJoin(char* secret)
    {
        string s = Text(secret);
        return Guarded(() => Social.SimulateDiscordJoin(s) ? 1 : 0);
    }

    /// <summary>A website join of this server id, as if it came over the pipe. Returns 1 when it is a server id.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int QueueWebJoin(char* id)
    {
        string s = Text(id);
        return Guarded(() =>
        {
            Social.Web.Queue(s);
            return WebsiteJoin.IsServerId(s) ? 1 : 0;
        });
    }

    /// <summary>What Discord shows for the last state Unreal described, as one line. Returns its full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPresenceText(char* buffer, int capacity)
    {
        try
        {
            return OnlineTextCopy.Buffer(buffer, capacity, Social.LastState?.Describe());
        }
        catch (Exception e)
        {
            Failed(e);
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    // ---- guards -------------------------------------------------------------------------------------------------

    private static string Text(char* text) => text == null ? string.Empty : new string(text);

    private static int Guarded(Func<int> call)
    {
        try
        {
            return call();
        }
        catch (Exception e)
        {
            Failed(e);
            return 0;
        }
    }

    private static void Failed(Exception e) => Exports.ReportFailure(e.ToString());
}
