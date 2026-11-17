using System;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Net.Scoreboards;
using OpenScct.UnrealBridge.Demo;
using OpenScct.UnrealBridge.Online;
using OnlineDemo = OpenScct.Net.Discovery.DemoEntry;

namespace OpenScct.UnrealBridge;

/// <summary>GetPlayInfo's answer (Source/OpenSCCT/Runtime/ScctDemoPlayApi.h FScctDemoPlayInfo).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct DemoPlayInfoBlock
{
    public int State, Flags, SessionId, Camera, SpeedIndex, TickRate;
    public int FirstTick, LastTick, CurrentTick, SeekTarget;
    public int TargetIndex, TargetCount, MarkerCount, Revision;
}

/// <summary>A watchable player (FScctDemoTarget).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct DemoTargetBlock
{
    public const int NameCapacity = 32;
    public int PlayerId, Team;
    public fixed char Name[NameCapacity];
}

/// <summary>A timeline marker (FScctDemoMarker).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct DemoMarkerBlock
{
    public const int TextCapacity = 64;
    public int Tick, Kind;
    public fixed char Text[TextCapacity];
}

/// <summary>A row of the Demos page (FScctDemoEntry).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct DemoEntryBlock
{
    public int Flags, Mode, DurationSeconds, PlayerCount;
    public long StartUnixMs, Bytes, Id, ExpiresUnixMs;
}

/// <summary>GetLibraryInfo's answer (FScctDemoLibraryInfo).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct DemoLibraryInfoBlock
{
    public int Flags, LocalCount, OnlineState, OnlinePage, OnlinePages, OnlineTotal, OnlineCount, DownloadPercent, Revision, Reserved;
}

/// <summary>A scoreboard row (FScctDemoScoreRow).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct DemoScoreRowBlock
{
    public const int NameCapacity = 32;
    public int Team, Score, Kills, Deaths, Objectives, Flags;
    public fixed char Name[NameCapacity];
}

/// <summary>
/// The demo playback and library table (Source/OpenSCCT/Runtime/ScctDemoPlayApi.h mirrors it): <see cref="DemoBridge"/>
/// and <see cref="DemoLibrary"/>. Unreal asks for <see cref="GetApi"/> once and keeps the table. Every entry catches its
/// exceptions (none may cross into native code); an unexpected one is kept for the main table's LastError and the call
/// returns its empty answer. New entries are added at the end.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct DemoPlayApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<char*, int, char*, int, int> Open;
    public delegate* unmanaged[Cdecl]<int> Start;
    public delegate* unmanaged[Cdecl]<float, void> Frame;
    public delegate* unmanaged[Cdecl]<void> Stop;
    public delegate* unmanaged[Cdecl]<DemoPlayInfoBlock*, int> GetPlayInfo;
    public delegate* unmanaged[Cdecl]<int, int, void> Control;
    public delegate* unmanaged[Cdecl]<float, float, void> Turn;
    public delegate* unmanaged[Cdecl]<float, float, float, int, float, void> Fly;
    public delegate* unmanaged[Cdecl]<DemoTargetBlock*, int, int> GetTargets;
    public delegate* unmanaged[Cdecl]<DemoMarkerBlock*, int, int> GetMarkers;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetPlayText;
    public delegate* unmanaged[Cdecl]<int> RefreshLocal;
    public delegate* unmanaged[Cdecl]<DemoLibraryInfoBlock*, int> GetLibraryInfo;
    public delegate* unmanaged[Cdecl]<int, DemoEntryBlock*, int, int> GetEntries;
    public delegate* unmanaged[Cdecl]<int, int, int, char*, int, int> GetEntryText;
    public delegate* unmanaged[Cdecl]<int, int> Keep;
    public delegate* unmanaged[Cdecl]<int, int> Delete;
    public delegate* unmanaged[Cdecl]<int, void> RequestOnline;
    public delegate* unmanaged[Cdecl]<int, int, int> Download;
    public delegate* unmanaged[Cdecl]<char*, int, int> TakeWatchPath;
    public delegate* unmanaged[Cdecl]<int, DemoScoreRowBlock*, int, int> GetScoreboard;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetLibraryText;
}

public static unsafe class DemoPlayExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(DemoPlayApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Open = &Open;
        api->Start = &Start;
        api->Frame = &Frame;
        api->Stop = &Stop;
        api->GetPlayInfo = &GetPlayInfo;
        api->Control = &Control;
        api->Turn = &Turn;
        api->Fly = &Fly;
        api->GetTargets = &GetTargets;
        api->GetMarkers = &GetMarkers;
        api->GetPlayText = &GetPlayText;
        api->RefreshLocal = &RefreshLocal;
        api->GetLibraryInfo = &GetLibraryInfo;
        api->GetEntries = &GetEntries;
        api->GetEntryText = &GetEntryText;
        api->Keep = &Keep;
        api->Delete = &Delete;
        api->RequestOnline = &RequestOnline;
        api->Download = &Download;
        api->TakeWatchPath = &TakeWatchPath;
        api->GetScoreboard = &GetScoreboard;
        api->GetLibraryText = &GetLibraryText;
        return ApiVersion;
    }

    // ---- playback -----------------------------------------------------------------------------------------------

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Open(char* path, int seekTick, char* map, int mapCapacity)
    {
        try
        {
            string? opened = path == null ? null : DemoBridge.Open(new string(path), (uint)Math.Max(0, seekTick));
            OnlineTextCopy.Buffer(map, mapCapacity, opened);
            return opened is null ? 0 : 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Start()
    {
        try
        {
            return DemoBridge.Start();
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Frame(float seconds)
    {
        try
        {
            DemoBridge.Frame(seconds);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Stop()
    {
        try
        {
            DemoBridge.Stop();
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPlayInfo(DemoPlayInfoBlock* info)
    {
        if (info == null)
        {
            return 0;
        }

        try
        {
            *info = default;
            info->Revision = DemoBridge.Revision;
            info->SpeedIndex = DemoBridge.SpeedIndex;
            info->TargetIndex = -1;
            if (DemoBridge.Player is not { } player)
            {
                return 1;
            }

            BridgeSession? adopted = DemoBridge.Adopted;
            info->State = adopted is not null ? 2 : 1;
            info->SessionId = adopted?.Id ?? 0;
            info->Flags = (player.Clock.Paused ? 1 : 0) | (player.IsSeeking ? 2 : 0) | (player.AtEnd ? 4 : 0) |
                (player.UsingCheckpoint ? 8 : 0) | (player.Looking ? 16 : 0) | (player.CanLook ? 32 : 0);
            info->Camera = (int)player.Mode;
            info->TickRate = Math.Max(1, player.Header.TickRate);
            info->FirstTick = (int)player.FirstTick;
            info->LastTick = (int)player.LastTick;
            info->CurrentTick = (int)Math.Clamp(player.CurrentTick, player.FirstTick, player.LastTick);
            info->SeekTarget = (int)player.SeekTarget;
            info->TargetCount = player.Targets.Count;
            for (int i = 0; i < player.Targets.Count; i++)
            {
                if (ReferenceEquals(player.Targets[i], player.Target))
                {
                    info->TargetIndex = i;
                    break;
                }
            }

            info->MarkerCount = player.Recording.Index.Markers.Count;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Control(int command, int arg)
    {
        try
        {
            DemoPlayback? player = DemoBridge.Player;
            switch (command)
            {
                case 0: DemoBridge.TogglePause(); break;
                case 1: DemoBridge.SetPaused(arg != 0); break;
                case 2: DemoBridge.StepSpeed(arg); break;
                case 3: DemoBridge.SetSpeedIndex(arg); break;
                case 4: DemoBridge.Skip(arg); break;
                case 5: DemoBridge.Seek((uint)Math.Max(0, arg)); break;
                case 6: DemoBridge.Restart(); break;
                case 7: player?.CycleMode(); break;
                case 8:
                    if (player is not null && arg is >= 0 and <= 3)
                    {
                        player.SetLooking(false);
                        player.Mode = (DemoCameraMode)arg;
                    }

                    break;
                case 9: player?.CycleTarget(arg); break;
                case 10: player?.WatchIndex(arg); break;
                case 11: player?.WatchPlayer(arg); break;
                case 12: player?.SetLooking(arg != 0); break;
                case 13: if (player is not null) DemoBridge.Seek(player.LastTick); break;
                case 14: DemoBridge.SeekMarker(arg); break;
            }

            DemoBridge.Touch();
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Turn(float dx, float dy)
    {
        try
        {
            DemoBridge.Player?.Turn(dx, dy);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void Fly(float forward, float right, float up, int fast, float seconds)
    {
        try
        {
            DemoBridge.Player?.Fly(forward, right, up, fast != 0, seconds);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetTargets(DemoTargetBlock* rows, int capacity)
    {
        try
        {
            if (DemoBridge.Player is not { } player || rows == null)
            {
                return 0;
            }

            int count = Math.Min(capacity, player.Targets.Count);
            for (int i = 0; i < count; i++)
            {
                SBasePawn pawn = player.Targets[i];
                rows[i].PlayerId = pawn.PlayerReplicationInfo?.PlayerID ?? -1;
                rows[i].Team = pawn.PlayerReplicationInfo?.Team?.TeamIndex ?? (pawn is SPAWNDEFENSE ? 1 : 0);
                OnlineTextCopy.Fixed(rows[i].Name, DemoTargetBlock.NameCapacity, pawn.PlayerReplicationInfo?.PlayerName ?? "Player");
            }

            return count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMarkers(DemoMarkerBlock* rows, int capacity)
    {
        try
        {
            if (DemoBridge.Player is not { } player || rows == null)
            {
                return 0;
            }

            var markers = player.Recording.Index.Markers;
            int count = Math.Min(capacity, markers.Count);
            for (int i = 0; i < count; i++)
            {
                rows[i].Tick = (int)markers[i].Tick;
                rows[i].Kind = (int)markers[i].Kind;
                OnlineTextCopy.Fixed(rows[i].Text, DemoMarkerBlock.TextCapacity, markers[i].Text);
            }

            return count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetPlayText(int kind, char* buffer, int capacity)
    {
        try
        {
            DemoPlayback? player = DemoBridge.Player;
            string text = kind switch
            {
                0 => player?.Header.Map ?? string.Empty,
                1 => player is null ? string.Empty : DemoLibrary.LocalModeName(player.Header),
                2 => player?.Header.ServerName ?? string.Empty,
                3 => DemoBridge.Error,
                4 => player is null ? string.Empty : player.Mode == DemoCameraMode.Free ? "Free camera" : player.Target?.PlayerReplicationInfo?.PlayerName ?? "Waiting for a player",
                5 => player?.Path ?? string.Empty,
                6 => player?.Mode switch
                {
                    DemoCameraMode.Free => "Hold right mouse to look · WASD fly · Q / E down / up · Shift faster",
                    DemoCameraMode.ThirdPerson => "Hold right mouse to orbit · Release to follow behind · C change camera · [ / ] change player",
                    DemoCameraMode.AsPlayed => "Recorded player camera · C change camera · [ / ] change player",
                    null => string.Empty,
                    _ => "Player's eye view · C change camera · [ / ] change player · H hide controls",
                },
                7 => player is null ? string.Empty : player.IsSeeking ? "SEEKING" : player.AtEnd ? "FINISHED" : player.Clock.Paused ? "PAUSED" : "PLAYING",
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

    // ---- the library --------------------------------------------------------------------------------------------

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RefreshLocal()
    {
        try
        {
            DemoLibrary.RefreshLocal();
            return DemoLibrary.Local.Count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLibraryInfo(DemoLibraryInfoBlock* info)
    {
        if (info == null)
        {
            return 0;
        }

        try
        {
            DemoLibrary.Pump();
            *info = new DemoLibraryInfoBlock
            {
                Flags = (DemoLibrary.StatusIsError ? 1 : 0) | (DemoLibrary.Downloading ? 2 : 0) | (DemoLibrary.WatchReady ? 4 : 0),
                LocalCount = DemoLibrary.Local.Count,
                OnlineState = (int)DemoLibrary.OnlineState,
                OnlinePage = DemoLibrary.OnlinePage,
                OnlinePages = DemoLibrary.OnlinePages,
                OnlineTotal = DemoLibrary.OnlineTotal,
                OnlineCount = DemoLibrary.Online.Count,
                DownloadPercent = DemoLibrary.DownloadPercent,
                Revision = DemoLibrary.Revision,
            };
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            *info = default;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetEntries(int collection, DemoEntryBlock* rows, int capacity)
    {
        try
        {
            if (rows == null)
            {
                return 0;
            }

            if (collection == 0)
            {
                int count = Math.Min(capacity, DemoLibrary.Local.Count);
                for (int i = 0; i < count; i++)
                {
                    LocalDemo demo = DemoLibrary.Local[i];
                    rows[i] = new DemoEntryBlock
                    {
                        Flags = (demo.Cached ? 1 : 0) | (demo.Playable ? 2 : 0) | (demo.Index.Scoreboard is not null ? 4 : 0),
                        Mode = DemoPlayback.ModeId(demo.Header.GameOptions),
                        DurationSeconds = demo.DurationSeconds,
                        PlayerCount = demo.Index.Players.Count,
                        StartUnixMs = demo.Header.StartUnixMs,
                        Bytes = demo.Bytes,
                    };
                }

                return count;
            }

            int online = Math.Min(capacity, DemoLibrary.Online.Count);
            for (int i = 0; i < online; i++)
            {
                OnlineDemo demo = DemoLibrary.Online[i];
                rows[i] = new DemoEntryBlock
                {
                    Flags = (DemoLibrary.OnlinePlayable(demo) ? 2 : 0) | (demo.Expires <= DateTimeOffset.UtcNow ? 8 : 0),
                    Mode = demo.Mode,
                    DurationSeconds = demo.DurationSeconds,
                    PlayerCount = demo.Players.Count,
                    StartUnixMs = demo.Time.ToUnixTimeMilliseconds(),
                    Bytes = demo.Size,
                    Id = demo.Id,
                    ExpiresUnixMs = demo.Expires.ToUnixTimeMilliseconds(),
                };
            }

            return online;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetEntryText(int collection, int index, int kind, char* buffer, int capacity)
    {
        try
        {
            string text = string.Empty;
            if (collection == 0 && index >= 0 && index < DemoLibrary.Local.Count)
            {
                LocalDemo demo = DemoLibrary.Local[index];
                text = kind switch
                {
                    0 => demo.Path,
                    1 => demo.FileName,
                    2 => demo.Header.Map,
                    3 => demo.Header.ServerName,
                    4 => string.Join(", ", demo.Index.Players.Select(p => p.Name)),
                    5 => demo.Header.GameVersion,
                    6 => DemoLibrary.LocalModeName(demo.Header),
                    7 => demo.Index.Scoreboard is { } board ? DemoLibrary.Outcome(board) : string.Empty,
                    _ => string.Empty,
                };
            }
            else if (collection == 1 && index >= 0 && index < DemoLibrary.Online.Count)
            {
                OnlineDemo demo = DemoLibrary.Online[index];
                text = kind switch
                {
                    2 => demo.Map,
                    3 => demo.ServerName,
                    4 => string.Join(", ", demo.Players.Select(p => p.Name)),
                    5 => demo.GameVersion,
                    6 => DemoLibrary.ModeName(demo.Mode),
                    7 => demo.WinningTeam == 0 ? "Spies win" : demo.WinningTeam == 1 ? "Mercenaries win" : string.Empty,
                    _ => string.Empty,
                };
            }

            return OnlineTextCopy.Buffer(buffer, capacity, text);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return OnlineTextCopy.Buffer(buffer, capacity, string.Empty);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Keep(int index)
    {
        try
        {
            return DemoLibrary.Keep(index) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Delete(int index)
    {
        try
        {
            return DemoLibrary.Delete(index) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void RequestOnline(int page)
    {
        try
        {
            DemoLibrary.RequestOnline(page);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Download(int index, int keep)
    {
        try
        {
            return DemoLibrary.Download(index, keep != 0) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int TakeWatchPath(char* buffer, int capacity)
    {
        try
        {
            string? path = DemoLibrary.TakeWatchPath();
            return path is null ? 0 : OnlineTextCopy.Buffer(buffer, capacity, path);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetScoreboard(int index, DemoScoreRowBlock* rows, int capacity)
    {
        try
        {
            if (DemoLibrary.Scoreboard(index) is not RoundScoreboard board)
            {
                return -1;
            }

            int count = rows == null ? 0 : Math.Min(capacity, board.Rows.Count);
            for (int i = 0; i < count; i++)
            {
                ScoreboardPlayer row = board.Rows[i];
                rows[i].Team = row.Team;
                rows[i].Score = row.Score;
                rows[i].Kills = row.Kills;
                rows[i].Deaths = row.Deaths;
                rows[i].Objectives = row.Objectives;
                rows[i].Flags = (row.Bot ? 1 : 0) | (row.LeftEarly ? 2 : 0);
                OnlineTextCopy.Fixed(rows[i].Name, DemoScoreRowBlock.NameCapacity, row.Name);
            }

            return count;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLibraryText(int kind, char* buffer, int capacity)
    {
        try
        {
            string text = kind switch
            {
                0 => DemoLibrary.Status,
                1 => DemoLibrary.ListingError,
                2 => DemoLibrary.SavedFolder ?? string.Empty,
                3 => DemoLibrary.CacheFolder ?? string.Empty,
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
}
