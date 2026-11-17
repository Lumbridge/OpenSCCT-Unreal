using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The HUD's table of entry points. Unreal asks for <see cref="HudExports.GetApi"/> once
/// (<c>Host.GetFunction("OpenScct.UnrealBridge.HudExports", "GetApi")</c>) and keeps the table;
/// Source/OpenSCCT/Runtime/ScctHudApi.h mirrors it field for field. Every entry returns 0 (or writes nothing) while
/// no session runs.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<HudState*, int> GetState;
    public delegate* unmanaged[Cdecl]<HudRound*, int> GetRound;
    public delegate* unmanaged[Cdecl]<HudGadget*, int, int> GetGadgets;
    public delegate* unmanaged[Cdecl]<HudInteraction*, int, int> GetInteractions;
    public delegate* unmanaged[Cdecl]<HudMenuLine*, int, int> GetMenu;
    public delegate* unmanaged[Cdecl]<HudObjective*, int, int> GetObjectives;
    public delegate* unmanaged[Cdecl]<HudObjective*, int, int> GetBinocularTargets;
    public delegate* unmanaged[Cdecl]<HudObjectiveLine*, int, int> GetObjectiveLines;
    public delegate* unmanaged[Cdecl]<HudBlip*, int, int> GetRadar;
    public delegate* unmanaged[Cdecl]<HudShownMessage*, int, int> GetShownMessages;
    public delegate* unmanaged[Cdecl]<HudScoreRow*, int, int> GetScoreboard;
    public delegate* unmanaged[Cdecl]<HudMessage*, int, int> DrainMessages;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetText;
    public delegate* unmanaged[Cdecl]<char*, char*, char*, char*, int, int> Localize;
    public delegate* unmanaged[Cdecl]<int, int, void> SetViewport;
    public delegate* unmanaged[Cdecl]<int, int, void> SetOption;
}

/// <summary><see cref="HudApi.SetOption"/>'s options.</summary>
public static class HudOption
{
    /// <summary>Marker distances in feet (1) or metres (0): the profile's bInFeet.</summary>
    public const int DistanceInFeet = 0;

    /// <summary>The player's "show connected players" toggle (SHud.bShowConnectedPlayers).</summary>
    public const int ShowPlayers = 1;
}

public static unsafe class HudExports
{
    /// <summary>Fills <paramref name="api"/>. Returns the HUD API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudApi* api)
    {
        api->Version = HudFeature.ApiVersion;
        api->GetState = &GetState;
        api->GetRound = &GetRound;
        api->GetGadgets = &GetGadgets;
        api->GetInteractions = &GetInteractions;
        api->GetMenu = &GetMenu;
        api->GetObjectives = &GetObjectives;
        api->GetBinocularTargets = &GetBinocularTargets;
        api->GetObjectiveLines = &GetObjectiveLines;
        api->GetRadar = &GetRadar;
        api->GetShownMessages = &GetShownMessages;
        api->GetScoreboard = &GetScoreboard;
        api->DrainMessages = &DrainMessages;
        api->GetText = &GetText;
        api->Localize = &Localize;
        api->SetViewport = &SetViewport;
        api->SetOption = &SetOption;
        return HudFeature.ApiVersion;
    }

    /// <summary>The running session's HUD, or null when no session runs.</summary>
    private static HudFeature? Hud => Exports.Current is { } session ? HudFeature.Of(session) : null;

    /// <summary>
    /// Writes the local player's HUD state as of the last simulation tick. Returns 1 when there is a HUD to draw (the
    /// same as its Valid field), else 0 with the struct zeroed. The first call of a session also starts the HUD's
    /// message receiving, so call it once before the session's first Update.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetState(HudState* state)
    {
        *state = default;
        try
        {
            return Hud is { } hud && hud.GetState(out *state) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>Writes the match state (mode, clock, scores, lives, the spectator's fields). Returns 1 once it is known.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRound(HudRound* round)
    {
        *round = default;
        try
        {
            return Hud is { } hud && hud.GetRound(out *round) ? 1 : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    // The list entries below all work the same way: up to capacity rows are written, and the return value is the
    // full count, which can be more than capacity (call again with a larger buffer to get the rest).

    /// <summary>The pawn's gadgets in the order the player cycles them.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetGadgets(HudGadget* rows, int capacity) => Copy(static f => f.Gadgets, rows, capacity);

    /// <summary>The usable entries of the interaction menu.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInteractions(HudInteraction* rows, int capacity) => Copy(static f => f.Interactions, rows, capacity);

    /// <summary>The lines of the open sticky cam or mine menu; none when no menu is open.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMenu(HudMenuLine* rows, int capacity) => Copy(static f => f.Menu, rows, capacity);

    /// <summary>The objective markers in the world (the spy's Enhanced Reality markers).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetObjectives(HudObjective* rows, int capacity) => Copy(static f => f.Objectives, rows, capacity);

    /// <summary>The objective triggers the spy's binoculars mark; none outside the spy's binocular view.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetBinocularTargets(HudObjective* rows, int capacity) => Copy(static f => f.BinocularTargets, rows, capacity);

    /// <summary>The mission's objectives for the player's side, as the mission status page lists them.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetObjectiveLines(HudObjectiveLine* rows, int capacity) => Copy(static f => f.ObjectiveLines, rows, capacity);

    /// <summary>The radar's blips, as world locations.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetRadar(HudBlip* rows, int capacity) => Copy(static f => f.Radar, rows, capacity);

    /// <summary>The messages the original HUD shows right now (its stacks, the game message, the console lines).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetShownMessages(HudShownMessage* rows, int capacity) => Copy(static f => f.Shown, rows, capacity);

    /// <summary>The scoreboard: spies, then mercs, then spectators.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetScoreboard(HudScoreRow* rows, int capacity) => Copy(static f => f.Score, rows, capacity);

    /// <summary>
    /// Moves the new messages (kill feed, objective and game messages, chat) into <paramref name="messages"/>, oldest
    /// first, each once. Returns how many were written; when that equals <paramref name="capacity"/>, call again.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int DrainMessages(HudMessage* messages, int capacity)
    {
        try
        {
            return Hud is { } hud && messages != null && capacity > 0 ? hud.DrainMessages(new Span<HudMessage>(messages, capacity)) : 0;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Copies the text of an ...Id field into <paramref name="buffer"/> (cut to fit, zero terminated). Returns its
    /// full length in characters. A text keeps its id for the whole session, so Unreal can cache it by id.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetText(int id, char* buffer, int capacity)
    {
        try
        {
            return Write(Hud?.Texts.Text(id) ?? "", buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// Copies a string of the game's interface text (the player's content: System/&lt;file&gt;.int, [section], key) into
    /// <paramref name="buffer"/>, for labels the HUD wants in the original's words. Returns its full length, 0 when
    /// the content has no such string.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Localize(char* file, char* section, char* key, char* buffer, int capacity)
    {
        try
        {
            return file == null || section == null || key == null || Hud is not { } hud
                ? Write("", buffer, capacity)
                : Write(hud.Strings.Get(new string(file), new string(section), new string(key)), buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>The view's size in pixels (only its aspect is used: the merc's aim point is projected with it).</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetViewport(int width, int height)
    {
        try
        {
            Hud?.SetViewport(width, height);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    /// <summary>Sets one of <see cref="HudOption"/>.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetOption(int option, int value)
    {
        try
        {
            if (Hud is not { } hud)
            {
                return;
            }

            switch (option)
            {
                case HudOption.DistanceInFeet:
                    hud.SetDistanceInFeet(value != 0);
                    break;
                case HudOption.ShowPlayers:
                    hud.SetShowPlayers(value != 0);
                    break;
            }
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    private static int Copy<T>(Func<HudFrame, HudRows<T>> pick, T* rows, int capacity)
        where T : unmanaged
    {
        try
        {
            if (Hud is not { } hud)
            {
                return 0;
            }

            return pick(hud.Frame).CopyTo(rows == null || capacity <= 0 ? default : new Span<T>(rows, capacity));
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    private static int Write(string text, char* buffer, int capacity)
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
