using System;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using OpenScct.Core;

namespace OpenScct.UnrealBridge.Social;

/// <summary>What the player is doing, as Unreal sees it (SocialPresence::Activity).</summary>
public enum SocialActivity
{
    Menus = 0,
    Online = 1,
    Hub = 2,
    Offline = 3,
}

/// <summary>The front end's page while in the menus (SocialPresence::Page).</summary>
public enum SocialPage
{
    Other = 0,
    Servers = 1,
    JoinByAddress = 2,
    OfflineSetup = 3,
    Hub = 4,
    Options = 5,
    Results = 6,
    HostSetup = 7,
}

/// <summary>Where an online visit is (SocialPresence::Phase).</summary>
public enum SocialPhase
{
    Joining = 0,
    Lobby = 1,
    Match = 2,
    Results = 3,
}

/// <summary>SocialPresence::Flags.</summary>
[Flags]
public enum SocialPresenceFlags
{
    None = 0,

    /// <summary>This PC hosts the server or the hub.</summary>
    Hosting = 1,

    /// <summary>The round's level is drawn and played (the side and the scores mean something).</summary>
    RoundDrawn = 2,

    /// <summary>The player watches the round.</summary>
    Spectating = 4,

    /// <summary>The online visit is a hub pad's match: friends cannot join it from Discord.</summary>
    HubMatch = 8,

    /// <summary>The server asks for a password (the join secret says so; the password stays here).</summary>
    JoinPassword = 16,

    /// <summary>The player stands in the drawn hub (before that the hub is still loading).</summary>
    HubArrived = 32,
}

/// <summary>
/// The player's state as Unreal builds it each second from its online, hub and session subsystems and the front end
/// (Source/OpenSCCT/Runtime/ScctSocialApi.h, FScctSocialPresence, mirrors it field for field). The texts go beside it:
/// the server's or hub's name, the map's id and the join secret's value.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct SocialPresence
{
    public int Activity;  // SocialActivity
    public int Page;      // SocialPage, in the menus
    public int Phase;     // SocialPhase, online
    public int Flags;     // SocialPresenceFlags
    public int Side;      // 0 spy, 1 merc, -1 not on a side
    public int SpyScore;  // the match score, -1 when not known
    public int MercScore;
    public int Mode;      // EGameMode, -1 when not known
    public int Bots;      // bots in an offline match
    public int PartySize; // players on the server or in the hub
    public int PartyMax;
    public int JoinKind;  // DiscordJoinKind, -1: friends cannot join
}

/// <summary>
/// What the player's Discord status says (the Godot client's game/Scripts/Discord/PresenceSnapshot.cs). Records
/// compare by value, so the presence sends an update only when something shown changed.
/// </summary>
public sealed record PresenceState
{
    /// <summary>The first line ("Story on Steel Squat"). A new line restarts the elapsed timer.</summary>
    public string Details { get; init; } = string.Empty;

    /// <summary>The second line ("Spy · Spies 2 – 1 Mercs").</summary>
    public string State { get; init; } = string.Empty;

    /// <summary>"spy", "merc" or null: the small image in the corner of the logo.</summary>
    public string? Side { get; init; }

    /// <summary>The party (players on the server); null when the player is not on one.</summary>
    public string? PartyId { get; init; }

    public int PartySize { get; init; }

    public int PartyMax { get; init; }

    /// <summary>The join secret for friends, or null when this game cannot be joined from Discord.</summary>
    public DiscordJoinSecret? Join { get; init; }

    /// <summary>One line for logs and the probe: what Discord shows.</summary>
    public string Describe() =>
        $"\"{Details}\" / \"{State}\"" + (Side is null ? string.Empty : $" [{Side}]") +
        (PartyId is null ? string.Empty : $" party {PartySize}/{PartyMax}") +
        (Join is null ? string.Empty : $" join {Join}");
}

/// <summary>
/// Turns Unreal's <see cref="SocialPresence"/> into the <see cref="PresenceState"/> Discord shows, with the Godot
/// client's wording for every state: the menus by page, an online visit (joining, the lobby or hosting, the match with
/// the side and the team scores, the results), the hub, and an offline match against bots.
/// </summary>
public static class PresenceSnapshot
{
    public static PresenceState Build(in SocialPresence p, string? server, string? map, string? joinValue, Func<string, string>? mapTitle = null)
    {
        string title = map is { Length: > 0 } ? mapTitle?.Invoke(map) ?? map : string.Empty;
        server ??= string.Empty;
        var flags = (SocialPresenceFlags)p.Flags;
        switch ((SocialActivity)p.Activity)
        {
            case SocialActivity.Online:
                return Online(p, flags, server, title, Join(p, flags, joinValue));
            case SocialActivity.Hub:
                return Hub(p, flags, server, joinValue);
            case SocialActivity.Offline:
            {
                (string? side, string line) = MatchLine(p, flags);
                return new PresenceState
                {
                    Details = "Offline: " + Game(p.Mode, title),
                    State = line + (p.Bots > 0 ? " · vs bots" : string.Empty),
                    Side = side,
                };
            }

            default:
                return new PresenceState
                {
                    Details = (SocialPage)p.Page switch
                    {
                        SocialPage.Servers => "Browsing servers",
                        SocialPage.HostSetup => "Setting up a game",
                        SocialPage.OfflineSetup => "Setting up an offline match",
                        SocialPage.Hub => "Looking for a hub",
                        SocialPage.Options => "In the options",
                        SocialPage.Results => "Looking at the results",
                        _ => "In the menus",
                    },
                };
        }
    }

    private static PresenceState Online(in SocialPresence p, SocialPresenceFlags flags, string server, string title, DiscordJoinSecret? join)
    {
        string game = Game(p.Mode, title);
        if (flags.HasFlag(SocialPresenceFlags.HubMatch))
        {
            join = null;
        }

        int size = Math.Max(1, p.PartySize);
        var state = new PresenceState
        {
            PartyId = PartyId(join?.ToString() ?? "server:" + server),
            PartySize = size,
            PartyMax = Math.Max(p.PartyMax, size),
            Join = join,
        };

        switch ((SocialPhase)p.Phase)
        {
            case SocialPhase.Lobby:
                return state with
                {
                    Details = (flags.HasFlag(SocialPresenceFlags.Hosting) ? "Hosting " : "In the lobby of ") + server,
                    State = game,
                };
            case SocialPhase.Match when flags.HasFlag(SocialPresenceFlags.RoundDrawn):
            {
                (string? side, string line) = MatchLine(p, flags);
                return state with { Details = game, State = line + " · " + server, Side = side };
            }

            case SocialPhase.Results:
                return state with { Details = "Match over on " + server, State = game };
            default:
                return state with { Details = "Joining " + server, State = game };
        }
    }

    private static PresenceState Hub(in SocialPresence p, SocialPresenceFlags flags, string name, string? address)
    {
        // A hub this PC hosts is not offered: its address is the one only this PC knows it by.
        DiscordJoinSecret? join = p.JoinKind == (int)DiscordJoinKind.Hub && !flags.HasFlag(SocialPresenceFlags.Hosting) &&
            address is { } a && DiscordJoinSecret.Shareable(a)
            ? new DiscordJoinSecret(DiscordJoinKind.Hub, a)
            : null;
        if (name.Length == 0)
        {
            name = address ?? string.Empty;
        }

        int players = Math.Max(1, p.PartySize);
        string? side = p.Side switch
        {
            0 => "spy",
            1 => "merc",
            _ => null,
        };
        bool arrived = flags.HasFlag(SocialPresenceFlags.HubArrived);
        return new PresenceState
        {
            Details = "In the hub: " + name,
            State = !arrived ? "Arriving" : side == "merc" ? "Mercenary" : side == "spy" ? "Spy" : "Looking around",
            Side = arrived ? side : null,
            PartyId = PartyId(join?.ToString() ?? "hub:" + name),
            PartySize = players,
            PartyMax = Math.Max(p.PartyMax, players),
            Join = join,
        };
    }

    /// <summary>The join secret of an online visit as Unreal described it, or null when friends cannot join.</summary>
    private static DiscordJoinSecret? Join(in SocialPresence p, SocialPresenceFlags flags, string? value)
    {
        if (value is not { Length: > 0 } v)
        {
            return null;
        }

        bool password = flags.HasFlag(SocialPresenceFlags.JoinPassword);
        return (DiscordJoinKind)p.JoinKind switch
        {
            DiscordJoinKind.Master => new DiscordJoinSecret(DiscordJoinKind.Master, v, password),
            DiscordJoinKind.Address when DiscordJoinSecret.Shareable(v) => new DiscordJoinSecret(DiscordJoinKind.Address, v, password),
            _ => null,
        };
    }

    /// <summary>The player's side and the team scores in a running match: ("spy", "Spy · Spies 2 – 1 Mercs").</summary>
    private static (string? Side, string Line) MatchLine(in SocialPresence p, SocialPresenceFlags flags)
    {
        if (flags.HasFlag(SocialPresenceFlags.Spectating))
        {
            return (null, "Spectating");
        }

        string side = p.Side == 1 ? "merc" : "spy";
        string line = p.Side == 1 ? "Mercenary" : "Spy";
        if (p.SpyScore >= 0 && p.MercScore >= 0)
        {
            line += string.Create(CultureInfo.InvariantCulture, $" · Spies {p.SpyScore} – {p.MercScore} Mercs");
        }

        return (side, line);
    }

    private static string Game(int mode, string map)
    {
        string name = (EGameMode)(byte)Math.Clamp(mode, 0, 255) switch
        {
            EGameMode.GM_DiskHunt => "Disk Hunt",
            EGameMode.GM_DeathMatch => "Deathmatch",
            EGameMode.GM_MercDeathMatch => "Merc Deathmatch",
            EGameMode.GM_Extraction => "Extraction",
            EGameMode.GM_Sabotage => "Sabotage",
            EGameMode.GM_Neutralization => "Neutralization",
            _ => "Story",
        };
        return map.Length > 0 ? name + " on " + map : name;
    }

    /// <summary>A party id that names the server without showing its address.</summary>
    private static string PartyId(string key) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(key)), 0, 16).ToLowerInvariant();
}
