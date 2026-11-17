using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Game.Equipment;
using OpenScct.Game.GameFlow;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Loadout;
using OpenScct.UnrealBridge.Tutorials;
using GK = OpenScct.Game.GameFlow.K;

namespace OpenScct.UnrealBridge;

/// <summary>The saved tutorial progress (<see cref="TutorialApi.GetProgress"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeTutorialProgress
{
    /// <summary>Spy tutorials done, bit i for <see cref="TutorialMaps.Spy"/>[i] (the exam is the last).</summary>
    public int SpyDone;

    /// <summary>Merc tutorials done, bit i for <see cref="TutorialMaps.Merc"/>[i].</summary>
    public int MercDone;

    public int ExamDone;

    /// <summary>The exam was passed for the first time since the game started (not saved).</summary>
    public int ExamJustCompleted;

    public int SpyCount;
    public int MercCount;
    public int ExamIndex;
}

/// <summary>The running round as a tutorial play watches it (<see cref="TutorialApi.GetRound"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeTutorialRound
{
    /// <summary>1 once the round has a winner (GRI.WinningTeam is not K.NoResult).</summary>
    public int Ended;

    /// <summary>0 spies, 1 mercs, 255 not decided.</summary>
    public int WinningTeam;

    /// <summary>The local player's side (SPawnType: 0 spy, 1 merc); -1 before he has a controller.</summary>
    public int LocalTeam;

    /// <summary>1 when the session's map is one of the tutorial maps (SPersistentData.CurLevelIsTutorial).</summary>
    public int Tutorial;

    /// <summary>Its index in its side's list and that side (0 spies, 1 mercs); -1 when not a tutorial.</summary>
    public int NumTut;
    public int NumTeam;

    /// <summary>1 for a round on a remote server (a tutorial play is always local).</summary>
    public int Remote;
}

/// <summary>One gadget of a side for Gadget Details (<see cref="TutorialApi.GetGadget"/>).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeTutorialGadget
{
    public const int PathCapacity = 64;
    public const int FileCapacity = 260;

    /// <summary>The class path (TeamInfo.ClassInventory[i]).</summary>
    public fixed char Path[PathCapacity];

    /// <summary>The full path of its picture (the HUD's gadget atlas, a .png of the player's content), or empty.</summary>
    public fixed char Icon[FileCapacity];

    /// <summary>Its tile of the picture in texels (IconU, IconV, 64 x 64); Size 0 for the whole picture.</summary>
    public int U;
    public int V;
    public int Size;

    /// <summary>1 for the spy's retired cloak gadget (the suit has the cloak; the pages leave it out).</summary>
    public int Retired;
}

/// <summary>
/// The tutorials' table (Bridge/OpenScct.UnrealBridge/Tutorials): the tutorial maps in their order, the saved progress,
/// the round watch of a tutorial play and the gadgets of Gadget Details. Unreal asks for
/// <see cref="TutorialExports.GetApi"/> once and keeps the table; Source/OpenSCCT/Runtime/ScctTutorialApi.h mirrors it
/// field for field. Every text is UTF-16, zero terminated, cut to fit; the entries that write one return its length.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct TutorialApi
{
    public int Version;

    /// <summary>(path): reads the progress file into use. 1 read, 0 missing (no progress), -1 unreadable.</summary>
    public delegate* unmanaged[Cdecl]<char*, int> Load;

    /// <summary>Fills the progress; returns 1.</summary>
    public delegate* unmanaged[Cdecl]<BridgeTutorialProgress*, int> GetProgress;

    /// <summary>(team, index, buffer, capacity): the map code of a side's tutorial ("TSP20"); 0 past the end.</summary>
    public delegate* unmanaged[Cdecl]<int, int, char*, int, int> GetMap;

    /// <summary>(team, buffer, capacity): the side's first tutorial not done; 0 when every one is.</summary>
    public delegate* unmanaged[Cdecl]<int, char*, int, int> NextTutorial;

    /// <summary>Fills the running round's state; returns 1 while a session with a local controller runs.</summary>
    public delegate* unmanaged[Cdecl]<BridgeTutorialRound*, int> GetRound;

    /// <summary>(map, won): marks a won tutorial map done and saves. 1 when it was marked.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, int> FinishRound;

    /// <summary>(map, visit, buffer, capacity): the tutorial to play after this round; 0 for the Tutorials page.</summary>
    public delegate* unmanaged[Cdecl]<char*, int, char*, int, int> NextAfter;

    /// <summary>
    /// (team): ends the local match's round with that side winning (negative: the local player's side), as the map's
    /// own story end does (SGameInfo.ReplicateWinningReason, sEndGame) on the host's next tick. 1 when asked.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, int> ForceWin;

    /// <summary>(team, index, gadget): one gadget of the side's ClassInventory; returns how many the side has.</summary>
    public delegate* unmanaged[Cdecl]<int, int, BridgeTutorialGadget*, int> GetGadget;

    /// <summary>Forgets all progress and saves (for unattended checks).</summary>
    public delegate* unmanaged[Cdecl]<void> Reset;
}

public static unsafe class TutorialExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the tutorials API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(TutorialApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->Load = &Load;
        api->GetProgress = &GetProgress;
        api->GetMap = &GetMap;
        api->NextTutorial = &NextTutorial;
        api->GetRound = &GetRound;
        api->FinishRound = &FinishRound;
        api->NextAfter = &NextAfter;
        api->ForceWin = &ForceWin;
        api->GetGadget = &GetGadget;
        api->Reset = &Reset;
        return ApiVersion;
    }

    private static int Copy(string text, char* buffer, int capacity)
    {
        if (buffer != null && capacity > 0)
        {
            int count = Math.Min(text.Length, capacity - 1);
            text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
            buffer[count] = '\0';
        }

        return text.Length;
    }

    private static void CopyFixed(string text, char* buffer, int capacity)
    {
        int count = Math.Min(text.Length, capacity - 1);
        text.AsSpan(0, count).CopyTo(new Span<char>(buffer, count));
        buffer[count] = '\0';
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Load(char* path)
    {
        try
        {
            return path != null && TutorialStore.Load(new string(path)) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure("Tutorial progress could not be read: " + e.Message);
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetProgress(BridgeTutorialProgress* progress)
    {
        if (progress == null)
        {
            return 0;
        }

        TutorialProgress p = TutorialStore.Current;
        *progress = new BridgeTutorialProgress
        {
            SpyDone = p.SpyDone,
            MercDone = p.MercDone,
            ExamDone = p.ExamDone ? 1 : 0,
            ExamJustCompleted = p.ExamJustCompleted ? 1 : 0,
            SpyCount = TutorialMaps.Spy.Count,
            MercCount = TutorialMaps.Merc.Count,
            ExamIndex = TutorialMaps.ExamIndex,
        };
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMap(int team, int index, char* buffer, int capacity)
    {
        var maps = TutorialMaps.Of(team);
        return index >= 0 && index < maps.Count ? Copy(maps[index], buffer, capacity) : 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int NextTutorial(int team, char* buffer, int capacity)
    {
        TutorialProgress progress = TutorialStore.Current;
        return TutorialMaps.NextTutorial(team, i => progress.GetMapTutorialDone(i, team)) is { } next ? Copy(next, buffer, capacity) : 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRound(BridgeTutorialRound* round)
    {
        try
        {
            var read = new BridgeTutorialRound { WinningTeam = GK.NoResult, LocalTeam = -1, NumTut = -1, NumTeam = -1 };
            BridgeSession? session = Exports.Current is { IsDisposed: false } s ? s : null;
            SPlayerController? pc = session?.Session.LocalController;
            if (session is not null)
            {
                read.Remote = session.IsRemote ? 1 : 0;
                read.Tutorial = TutorialMaps.CurLevelIsTutorial(session.Map, out int numTut, out int numTeam) ? 1 : 0;
                read.NumTut = numTut;
                read.NumTeam = numTeam;
            }

            if (pc is not null)
            {
                read.LocalTeam = (int)pc.SPawnType;
                if (pc.GameReplicationInfo is { } gri)
                {
                    read.WinningTeam = gri.WinningTeam;
                    read.Ended = gri.WinningTeam != GK.NoResult ? 1 : 0;
                }
            }

            if (round != null)
            {
                *round = read;
            }

            return pc is null ? 0 : 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FinishRound(char* map, int won)
    {
        try
        {
            return map != null && TutorialStore.FinishRound(new string(map), won != 0) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure("Tutorial progress could not be saved: " + e.Message);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int NextAfter(char* map, int visit, char* buffer, int capacity)
    {
        return map != null && TutorialStore.NextAfter(new string(map), visit != 0) is { } next ? Copy(next, buffer, capacity) : 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ForceWin(int team)
    {
        try
        {
            if (Exports.Current is not { IsDisposed: false, IsRemote: false } session)
            {
                return 0;
            }

            int winner = team is 0 or 1 ? team : session.Session.LocalController is { } pc ? (int)pc.SPawnType : session.LocalMerc ? 1 : 0;
            var play = session.Session;
            bool done = false;
            play.Host.BeforeWorldTick += _ =>
            {
                if (done || play.Loader.Game is not SGameInfo game)
                {
                    return;
                }

                done = true;
                game.ReplicateWinningReason((byte)winner, EEndReason.END_STORY);
                game.sEndGame();
            };
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetGadget(int team, int index, BridgeTutorialGadget* gadget)
    {
        try
        {
            STeamInfo info = team == 0 ? new STeamAttaquant() : new STeamDefenseur();
            int count = info.ClassInventory.Count;
            if (gadget != null)
            {
                *gadget = default;
                if (index >= 0 && index < count)
                {
                    string path = info.ClassInventory[index].Path ?? "";
                    GearIcon icon = GearIntel.Icon(path);
                    CopyFixed(path, gadget->Path, BridgeTutorialGadget.PathCapacity);
                    CopyFixed(icon.File, gadget->Icon, BridgeTutorialGadget.FileCapacity);
                    gadget->U = icon.U;
                    gadget->V = icon.V;
                    gadget->Size = icon.Size;
                    gadget->Retired = team == 0 && SpyCloak.IsLegacyGadget(path) ? 1 : 0;
                }
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
    private static void Reset()
    {
        try
        {
            TutorialStore.Reset();
        }
        catch (Exception e)
        {
            Exports.ReportFailure("Tutorial progress could not be reset: " + e.Message);
        }
    }
}
