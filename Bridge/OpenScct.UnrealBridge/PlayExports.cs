using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Server;
using OpenScct.UnrealBridge.Controls;
using OpenScct.UnrealBridge.Devices;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The table of the player's own entry points that belong to no other feature: the gear he brings to a match, the
/// chat line he types, the text of the loading page and the map name of an actor a mark lies on. Unreal asks for
/// <see cref="PlayExports.GetApi"/> once and keeps the table; Source/OpenSCCT/Runtime/ScctPlayApi.h mirrors it field
/// for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct PlayApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int, void> SetSidearm;
    public delegate* unmanaged[Cdecl]<char*, int, int> SendChat;
    public delegate* unmanaged[Cdecl]<int, int, char*, int, int> GetTip;
    public delegate* unmanaged[Cdecl]<int, char*, char*, int, int> ExpandKeys;
    public delegate* unmanaged[Cdecl]<char*, char*, int, int> GetMapTitle;
    public delegate* unmanaged[Cdecl]<int, char*, int, int> GetActorName;
    public delegate* unmanaged[Cdecl]<char*, int, char*, int, int> GetLoadingImage;
}

public static unsafe class PlayExports
{
    public const int ApiVersion = 1;

    /// <summary>
    /// The key tokens of the game's texts (KeyTokens.Tokens of the Godot client) and the action of the controls
    /// (<see cref="ControlActions"/>) each stands for; the two that differ by side name the spy's action first.
    /// Longer tokens come before their own beginnings (MOVEF before nothing shorter, GADGET1 before nothing shorter).
    /// </summary>
    private static readonly (string Token, string Spy, string Merc)[] s_tokens =
    [
        ("MOVEF", "forward", "forward"), ("MOVEB", "back", "back"), ("MOVEL", "left", "left"), ("MOVER", "right", "right"),
        ("USE", "use", "use"), ("PRIMARYFIRE", "fire", "fire"), ("SECONDARYFIRE", "altfire", "altfire"),
        ("JUMP", "jump", "jump"), ("CROUCH", "crouch", "crouch"), ("SPEEDMODE", "speed", "speed"),
        ("HACKCOMM", "hackcomms", "hackcomms"), ("SWITCHVISION", "visioncycle", "visioncycle"),
        ("VISION1", "vision1", "vision1"), ("VISION2", "vision2", "vision2"),
        ("GADGET1", "slot1", "slot1"), ("GADGET2", "slot2", "slot2"), ("GADGET3", "slot3", "slot3"), ("GADGET4", "slot4", "slot4"),
        ("NEXTGADGET", "next", "next"), ("PREVIOUSGADGET", "previous", "previous"),
        ("ACTION1", "roll", "laser"), ("ACTION2", "ready", "torch"), ("COOP", "coop", "coop"), ("BTW", "wall", "wall"),
        ("BINOCULARS", "scope", "scope"), ("RELOAD", "reload", "reload"), ("FIRERATE", "rof", "rof"),
        ("LASER", "weaponlaser", "weaponlaser"), ("CAMERA", "sticky", "sticky"), ("ZOOMIN", "zoomin", "zoomin"),
        ("ZOOMOUT", "zoomout", "zoomout"), ("ER", "er", "er"), ("INGAMEMENU", "menu", "menu"),
    ];

    /// <summary>
    /// Fills <paramref name="api"/>. Returns the API version. Asking for the table also makes sure the player's gear
    /// (Loadout/LoadoutServices: the saved kit, which the sidearm stepper and the Equipment page both write) is on
    /// every level the play loaders load.
    /// </summary>
    [UnmanagedCallersOnly]
    public static int GetApi(PlayApi* api)
    {
        Loadout.LoadoutServices.Hook();
        if (api == null)
        {
            return ApiVersion;
        }

        api->Version = ApiVersion;
        api->SetSidearm = &SetSidearm;
        api->SendChat = &SendChat;
        api->GetTip = &GetTip;
        api->ExpandKeys = &ExpandKeys;
        api->GetMapTitle = &GetMapTitle;
        api->GetActorName = &GetActorName;
        api->GetLoadingImage = &GetLoadingImage;
        return ApiVersion;
    }

    /// <summary>
    /// The sidearm the local merc brings to matches that allow sidearms: 0 suppressed, 1 automatic, 2 .44 Magnum,
    /// 3 Desert Eagle, 4 unsuppressed (<see cref="SidearmKind"/>); anything else for none. It is read when the
    /// server asks for the kit, so set it before a match starts. It goes into the gear in use (the Equipment page shows
    /// it), not into the saved file; whether the match carries sidearms at all is the match's own Sidearms switch.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetSidearm(int kind)
    {
        try
        {
            Loadout.LoadoutServices.Hook();
            if (kind < 0 || kind >= Sidearms.All.Count)
            {
                return;
            }

            Loadout.LoadoutChoice choice = Loadout.LoadoutStore.Current.Clone();
            choice.MercSidearm = Sidearms.Get((SidearmKind)kind).CarryId;
            Loadout.LoadoutStore.Set(choice);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
        }
    }

    /// <summary>
    /// Sends a chat line the player typed, to everyone or to his team, in a local match or an online round. The
    /// server relays it back, so it arrives in the HUD's messages like anyone else's. Returns 1 when it was sent.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SendChat(char* text, int teamOnly)
    {
        try
        {
            if (text == null || Exports.Current is not { } session)
            {
                return 0;
            }

            var clean = new StringBuilder();
            foreach (char c in new string(text))
            {
                if (clean.Length >= ChatK.MaxChars)
                {
                    break;
                }

                if (!char.IsControl(c))
                {
                    clean.Append(c);
                }
            }

            string line = clean.ToString().Trim();
            if (line.Length == 0)
            {
                return 0;
            }

            session.Session.Client.SendChat(teamOnly != 0 ? EChatScope.TeamSay : EChatScope.Say, line);
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    /// <summary>
    /// One of the side's tips (the team class's aTipStrings, which the original shows on the death page and the
    /// Godot client also on its loading page), with its key tokens replaced by the player's keys. <paramref name="pick"/>
    /// chooses it (any number, wrapped); negative for a random one. Returns the full length, 0 when the content has no
    /// tips. Works without a session, once the content folder is set.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetTip(int merc, int pick, char* buffer, int capacity)
    {
        try
        {
            STeamInfo team = merc != 0 ? new STeamDefenseur() : new STeamAttaquant();
            List<string> tips = team.aTipStrings.FindAll(static tip => !string.IsNullOrWhiteSpace(tip));
            if (tips.Count == 0)
            {
                return Write("", buffer, capacity);
            }

            int index = pick < 0 ? Random.Shared.Next(tips.Count) : pick % tips.Count;
            return Write(Expand(tips[index], merc != 0), buffer, capacity);
        }
        catch (Exception)
        {
            // No converted text: the page simply has no tip.
            return Write("", buffer, capacity);
        }
    }

    /// <summary>Replaces the key tokens of a game text (<c>\USE</c>, <c>\GADGET1</c>) by the side's keys. Returns the full length.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ExpandKeys(int merc, char* text, char* buffer, int capacity)
    {
        try
        {
            return Write(text == null ? "" : Expand(new string(text), merc != 0), buffer, capacity);
        }
        catch (Exception)
        {
            return Write(text == null ? "" : new string(text), buffer, capacity);
        }
    }

    /// <summary>
    /// The name every page shows for a map (the catalog's name of the original's map list, "ENHANCED AQUARIUS"), or
    /// the map's id when the catalog does not list it. Returns the full length.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetMapTitle(char* map, char* buffer, int capacity)
    {
        string id = map == null ? "" : new string(map);
        try
        {
            s_catalog ??= MapCatalog.CreateDefault();
            return Write(s_catalog.GetCatalogEntry(id)?.DisplayName is { Length: > 0 } name ? name : id, buffer, capacity);
        }
        catch (Exception)
        {
            return Write(id, buffer, capacity);
        }
    }

    private static MapCatalog? s_catalog;

    /// <summary>
    /// The file of the map's loading picture for the side (the other side's when it has none, then the menu's
    /// picture of the map), as a full path; the Godot client's MapImages.Loading. A map with several gives a random
    /// one. Returns the full length, 0 when the content has no picture of the map.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLoadingImage(char* map, int merc, char* buffer, int capacity)
    {
        try
        {
            s_catalog ??= MapCatalog.CreateDefault();
            MapCatalogEntry? entry = map == null ? null : s_catalog.GetCatalogEntry(new string(map));
            if (entry is null)
            {
                return Write("", buffer, capacity);
            }

            IReadOnlyList<string> own = merc != 0 ? entry.LoadingMercImages : entry.LoadingSpyImages;
            IReadOnlyList<string> other = merc != 0 ? entry.LoadingSpyImages : entry.LoadingMercImages;
            string? file = Pick(own) ?? Pick(other) ?? entry.PreviewImage;
            return Write(file is not null && System.IO.File.Exists(file) ? file : "", buffer, capacity);
        }
        catch (Exception)
        {
            return Write("", buffer, capacity);
        }

        static string? Pick(IReadOnlyList<string> files) => files.Count == 0 ? null : files[Random.Shared.Next(files.Count)];
    }

    /// <summary>
    /// The map name of the followed actor with this simulation ActorId (a mover a bullet mark lies on), as the world
    /// table names it, so Unreal finds the level actor it draws. Returns the full length, 0 when the actor is not a
    /// followed map actor.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetActorName(int actorId, char* buffer, int capacity)
    {
        try
        {
            string name = Exports.Current is { } session && actorId != 0 ? WorldState.Of(session).MapNameOfActor(actorId) : "";
            return Write(name, buffer, capacity);
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return Write("", buffer, capacity);
        }
    }

    /// <summary>The token pass: every <c>\TOKEN</c> becomes the name of the key the side has its action on.</summary>
    internal static string Expand(string text, bool merc)
    {
        if (!text.Contains('\\', StringComparison.Ordinal))
        {
            return text;
        }

        KeyLayout layout = ControlsStore.Bindings.Of(merc).Keys;
        var result = new StringBuilder(text.Length + 16);
        int at = 0;
        while (at < text.Length)
        {
            if (text[at] != '\\')
            {
                result.Append(text[at++]);
                continue;
            }

            // The converted text can carry the backslash doubled.
            int start = at + 1;
            while (start < text.Length && text[start] == '\\')
            {
                start++;
            }

            bool replaced = false;
            foreach ((string token, string spy, string mercAction) in s_tokens)
            {
                if (string.Compare(text, start, token, 0, token.Length, StringComparison.OrdinalIgnoreCase) != 0)
                {
                    continue;
                }

                // GADGET1 must not be read as a shorter token followed by letters: the token ends at a non-letter.
                int end = start + token.Length;
                if (end < text.Length && char.IsLetterOrDigit(text[end]))
                {
                    continue;
                }

                result.Append(KeyOf(layout, merc ? mercAction : spy) ?? "[unbound]");
                at = end;
                replaced = true;
                break;
            }

            if (!replaced)
            {
                result.Append(text[at++]);
            }
        }

        return result.ToString();
    }

    /// <summary>The key the layout has a controls action on (a plain key before a chord), or null.</summary>
    private static string? KeyOf(KeyLayout layout, string actionId)
    {
        ControlAction? action = null;
        foreach (ControlAction candidate in ControlActions.All)
        {
            if (candidate.Id == actionId)
            {
                action = candidate;
                break;
            }
        }

        if (action is null)
        {
            return null;
        }

        List<KeyAction> wanted = KeyLayout.Resolve(action.Command);
        if (wanted.Count == 0)
        {
            return null;
        }

        string? chord = null;
        foreach (string key in layout.Bindings.Keys)
        {
            foreach (KeyAction bound in layout.Actions(key))
            {
                if (bound.Kind != wanted[0].Kind || bound.Arg != wanted[0].Arg ||
                    !string.Equals(bound.Name, wanted[0].Name, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                if (!key.Contains('+', StringComparison.Ordinal))
                {
                    return key;
                }

                chord ??= key;
            }
        }

        return chord;
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
