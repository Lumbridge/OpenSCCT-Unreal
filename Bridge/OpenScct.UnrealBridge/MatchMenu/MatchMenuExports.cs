using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Objectives;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Online;
using OpenScctGame.UI;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Who the player is when the in-match menu opens, and the buttons it then shows: the Godot client's
/// <see cref="InGameMenuContext"/> (PlayMenu.Context) and <see cref="InGameMenuModel.Layout"/> (linked from
/// game/Scripts/UI), so Unreal shows exactly the entries the Godot client shows.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeMatchMenuContext
{
    public const int MaxEntries = 12;

    /// <summary>1: the player runs the round (offline always; online the lobby's host).</summary>
    public int IAmServer;

    /// <summary>1: a round on a server (NET_LAN); 0: a local match.</summary>
    public int Online;
    public int Tutorial;
    public int MatchBegun;
    public int GameEnded;
    public int Spectator;
    public int Lives;

    /// <summary>1: practice rules (a practice lobby online, unlimited gear offline).</summary>
    public int Practice;
    public int MercDeathMatch;
    public int CanSwitchTeam;
    public int CanResign;

    /// <summary>The local player's team: 0 spies, 1 mercs, -1 none yet.</summary>
    public int LocalTeam;

    /// <summary>1: the local player plays a merc (his pawn's kind, else the side the session started for).</summary>
    public int Merc;

    /// <summary>The round's EGameMode (1 Extraction ... 5 Story, 6 Deathmatch, 7 Merc Deathmatch), 0 unknown.</summary>
    public int GameMode;

    /// <summary>The shown buttons, top to bottom, as InGameMenuButton values (0 Back ... 7 QuitSession).</summary>
    public int EntryCount;
    public fixed int Entries[MaxEntries];
}

/// <summary>
/// The in-match menu's table (Source/OpenSCCT/Runtime/ScctMatchMenuApi.h mirrors it field for field): the context and
/// its entries, and the actions the Godot client's PlayMenu.Act carries out that need the simulation or the online
/// visit: switch team, resign, return to lobby. The help page's goals for the round come from here too.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MatchMenuApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<BridgeMatchMenuContext*, int> GetContext;
    public delegate* unmanaged[Cdecl]<int> SwitchTeam;
    public delegate* unmanaged[Cdecl]<int> Resign;
    public delegate* unmanaged[Cdecl]<int> ReturnToLobby;
    public delegate* unmanaged[Cdecl]<char*, int, int> GetGoals;
    public delegate* unmanaged[Cdecl]<char*, int, int> LastError;
}

/// <summary>The rules behind the table, callable from managed code (the probe uses them).</summary>
public static class MatchMenu
{
    /// <summary>
    /// PlayMenu.Context: offline the player runs the server; online the lobby says whether he hosts. A tutorial is a
    /// TME or TSP map, as GadgetWorld.CurLevelIsTutorial.
    /// </summary>
    public static InGameMenuContext Context()
    {
        BridgeSession? current = Exports.Current;
        SPlayerController? pc = current?.Session.LocalController;
        string map = current?.Map ?? "";
        bool tutorial = map.StartsWith("TME", StringComparison.OrdinalIgnoreCase) || map.StartsWith("TSP", StringComparison.OrdinalIgnoreCase);
        bool matchBegun = pc?.GameReplicationInfo?.bMatchHasBegun ?? false;
        bool gameEnded = pc?.bGameEnded ?? true;
        bool spectator = pc?.PlayerReplicationInfo?.bOnlySpectator ?? true;
        int lives = pc?.PlayerReplicationInfo?.NumLives ?? 0;
        if (current is { IsRemote: true } && OnlineBridge.Existing?.Client is { } client)
        {
            bool host = (client.Lobby.Local?.IsHost ?? false) || (pc?.PlayerReplicationInfo?.bIAmServer ?? false);
            return new InGameMenuContext(host, InGameMenuNet.Lan, tutorial, matchBegun, gameEnded, spectator, lives,
                client.Lobby.Setup.Practice, client.Lobby.Setup.Mode == EGameMode.GM_MercDeathMatch);
        }

        return new InGameMenuContext(IAmServer: true, InGameMenuNet.None, tutorial, matchBegun, gameEnded, spectator, lives,
            pc is not null && OpenScct.Game.Modes.PracticeRules.IsOn(pc), pc?.IsMercDeathMatch ?? false);
    }

    /// <summary>
    /// PlayMenu.Act(SwitchTeam): online the lobby's SetTeam to the other side, offline the play session's practice
    /// side change. Throws with the reason when the context does not allow it.
    /// </summary>
    public static string SwitchTeam()
    {
        InGameMenuContext context = Context();
        if (!context.CanSwitchTeam)
        {
            throw new InvalidOperationException("Switch Team is only for a practice round that has begun, not a tutorial, a merc deathmatch or a spectator.");
        }

        if (context.Net == InGameMenuNet.Lan && OnlineBridge.Existing is { Client: { } client } online)
        {
            int team = client.Lobby.Local?.Team == 0 ? 1 : 0;
            online.SetTeam(team);
            return team == 0 ? "asked the lobby for the spies" : "asked the lobby for the mercenaries";
        }

        BridgeSession session = Exports.Current ?? throw new InvalidOperationException("No round is being played.");
        session.Session.RequestPracticeTeamSwap();
        return "asked the local round for the other side";
    }

    /// <summary>PlayMenu.Act(Resign): online the "!resign" chat command, which gives up the player's remaining lives.</summary>
    public static void Resign()
    {
        if (!Context().CanResign || OnlineBridge.Existing is not { Client: not null } online)
        {
            throw new InvalidOperationException("Resign is only for a player with lives left in an online round that has begun.");
        }

        online.Chat("!resign", false);
    }

    /// <summary>PlayMenu.Act(ReturnToLobby): the host sends everyone back to the lobby.</summary>
    public static void ReturnToLobby()
    {
        InGameMenuContext context = Context();
        if (!context.IAmServer || context.Net != InGameMenuNet.Lan || OnlineBridge.Existing is not { Client: not null } online)
        {
            throw new InvalidOperationException("Only the host of an online round can send everyone back to the lobby.");
        }

        online.BackToLobby();
    }

    /// <summary>
    /// The round's goals for the help page, one per line: the mode and what it asks of each side (the Godot lobby's
    /// LobbyPresentation.Objective words), the lives left, then each objective of the mode's mission as the player's
    /// side reads it, "[done]" before those taken (the mission status list's rule, HudReader.ReadObjectiveLines).
    /// </summary>
    public static string Goals()
    {
        BridgeSession? current = Exports.Current;
        SPlayerController? pc = current?.Session.LocalController;
        GameReplicationInfo? gri = pc?.GameReplicationInfo;
        if (current is null || pc is null || gri is null)
        {
            return "";
        }

        var text = new StringBuilder();
        text.Append("Mode: ").Append(ModeName(gri.GameMode)).Append('\n');
        text.Append(Objective(gri.GameMode)).Append('\n');
        int lives = pc.PlayerReplicationInfo?.NumLives ?? 0;
        text.Append(lives >= 100 ? "Lives: unlimited" : "Lives left: " + lives).Append('\n');

        bool spy = (pc.PlayerReplicationInfo?.Team?.TeamIndex ?? (current.LocalMerc ? 1 : 0)) == 0;
        foreach (SMission mission in current.Session.Client.World.Level.AllActors<SMission>())
        {
            if (mission.GameMode != gri.GameMode)
            {
                continue;
            }

            bool oneNotTaken = false;
            foreach (SObjective? o in mission.Objectives)
            {
                if (o is null)
                {
                    continue;
                }

                bool open = !o.ObjectiveTaken || (mission.bChained && oneNotTaken);
                oneNotTaken |= open;
                string line = ((spy ? o.Description : o.DescriptionDEF) ?? "").Replace('|', ' ').Trim();
                if (line.Length > 0)
                {
                    text.Append(open ? "- " : "[done] ").Append(line).Append('\n');
                }
            }
        }

        return text.ToString();
    }

    /// <summary>The mode's name as the Godot client's menus write it (DemosScreen.ModeName).</summary>
    public static string ModeName(EGameMode mode) => mode switch
    {
        EGameMode.GM_Extraction => "Extraction",
        EGameMode.GM_Sabotage => "Sabotage",
        EGameMode.GM_Neutralization => "Neutralization",
        EGameMode.GM_DiskHunt => "Disk Hunt",
        EGameMode.GM_DeathMatch => "Deathmatch",
        EGameMode.GM_MercDeathMatch => "Merc Deathmatch",
        _ => "Story",
    };

    /// <summary>What the mode asks, in the Godot lobby's words (game/Scripts/UI/LobbyPresentation.Objective).</summary>
    public static string Objective(EGameMode mode) => mode switch
    {
        EGameMode.GM_MercDeathMatch or EGameMode.GM_DeathMatch => "Eliminate the opposing team.",
        EGameMode.GM_DiskHunt => "Collect disks to reach the winning score.",
        EGameMode.GM_Extraction => "Spies extract disks; mercenaries prevent extraction.",
        EGameMode.GM_Sabotage => "Spies plant bombs; mercenaries defend the targets.",
        EGameMode.GM_Neutralization => "Spies neutralize objectives; mercenaries defend them.",
        _ => "Spies pursue the map objectives; mercenaries defend.",
    };

    public static unsafe BridgeMatchMenuContext State()
    {
        InGameMenuContext context = Context();
        BridgeSession? current = Exports.Current;
        SPlayerController? pc = current?.Session.LocalController;
        var s = new BridgeMatchMenuContext
        {
            IAmServer = context.IAmServer ? 1 : 0,
            Online = context.Net == InGameMenuNet.Lan ? 1 : 0,
            Tutorial = context.Tutorial ? 1 : 0,
            MatchBegun = context.MatchBegun ? 1 : 0,
            GameEnded = context.GameEnded ? 1 : 0,
            Spectator = context.Spectator ? 1 : 0,
            Lives = context.Lives,
            Practice = context.Practice ? 1 : 0,
            MercDeathMatch = context.MercDeathmatch ? 1 : 0,
            CanSwitchTeam = context.CanSwitchTeam ? 1 : 0,
            CanResign = context.CanResign ? 1 : 0,
            LocalTeam = pc?.PlayerReplicationInfo?.Team?.TeamIndex ?? -1,
            Merc = current?.LocalMerc == true ? 1 : 0,
            GameMode = (int)(pc?.GameReplicationInfo?.GameMode ?? EGameMode.GM_Undefined),
        };
        IReadOnlyList<InGameMenuEntry> entries = InGameMenuModel.Layout(context);
        for (int i = 0; i < entries.Count && i < BridgeMatchMenuContext.MaxEntries; i++)
        {
            s.Entries[i] = (int)entries[i].Button;
            s.EntryCount++;
        }

        return s;
    }
}

public static unsafe class MatchMenuExports
{
    public const int ApiVersion = 1;

    private static string s_lastError = "";

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(MatchMenuApi* api)
    {
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->GetContext = &GetContext;
        api->SwitchTeam = &SwitchTeam;
        api->Resign = &Resign;
        api->ReturnToLobby = &ReturnToLobby;
        api->GetGoals = &GetGoals;
        api->LastError = &LastError;
        return ApiVersion;
    }

    /// <summary>Writes the context and its entries. Returns 1 while a round is being played, else 0.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetContext(BridgeMatchMenuContext* context) => Guard(() =>
    {
        if (context == null)
        {
            return 0;
        }

        *context = MatchMenu.State();
        return Exports.Current is null ? 0 : 1;
    });

    /// <summary>Asks for the other side. Returns 1, or 0 with the reason in LastError (which also says what was asked on success).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SwitchTeam() => Guard(() =>
    {
        s_lastError = MatchMenu.SwitchTeam();
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Resign() => Guard(() =>
    {
        MatchMenu.Resign();
        return 1;
    });

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ReturnToLobby() => Guard(() =>
    {
        MatchMenu.ReturnToLobby();
        return 1;
    });

    /// <summary>The round's goals, lines broken with '\n'. Returns the full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetGoals(char* buffer, int capacity) => Guard(() => Copy(MatchMenu.Goals(), buffer, capacity));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int LastError(char* buffer, int capacity)
    {
        try
        {
            return Copy(s_lastError, buffer, capacity);
        }
        catch (Exception)
        {
            return 0;
        }
    }

    private static int Guard(Func<int> body)
    {
        try
        {
            return body();
        }
        catch (Exception e)
        {
            // Nothing may cross into native code; a fault here never ends a match.
            s_lastError = e is InvalidOperationException ? e.Message : e.ToString();
            return 0;
        }
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
}
