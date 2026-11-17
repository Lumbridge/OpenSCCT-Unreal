using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using DiscordRPC;
using DiscordRPC.IO;
using OpenScct.Net.Discovery;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Social;

// Usage:
//   SocialProbe [states] [contentRoot]   the Discord status of several states, the join secrets, the invite pipe fix
//   SocialProbe forward <server id>      hands a website join to the game that owns the pipe (a second process)
//   SocialProbe list [masterUrl]         the master's servers and hubs with their ids (for a website join to try)
//   SocialProbe account [masterUrl] [inviteKey] [--token T] [--sync] [--claim NAME] [--prestige] [--signout]
//                                        the account service against a master (AccountCheck.cs)
//   SocialProbe demo [file or folder]    the demo-saving choice and the saved demos read back (DemoCheck.cs)
string mode = args.Length > 0 ? args[0] : "states";
return mode switch
{
    "account" => AccountCheck.Run(args[1..]),
    "demo" => DemoCheck.Run(args[1..]),
    "forward" => Forward(args.Length > 1 ? args[1] : string.Empty),
    "list" => List(args.Length > 1 ? args[1] : WebsiteJoin.OfficialMasterUrl),
    _ => States(args.Length > 1 ? args[1] : OpenScct.UnrealBridge.DevPaths.ContentRoot()),
};

static int States(string content)
{
    int failures = 0;
    void Check(bool ok, string what)
    {
        if (!ok)
        {
            failures++;
            Console.WriteLine("FAILED: " + what);
        }
    }

    // The record sizes Source/OpenSCCT/Runtime/ScctSocialApi.h asserts for its mirrors.
    unsafe
    {
        (string Name, int Size, int Expected)[] sizes =
        [
            (nameof(SocialPresence), sizeof(SocialPresence), 48), (nameof(SocialRequest), sizeof(SocialRequest), 8),
            (nameof(SocialStatus), sizeof(SocialStatus), 28), (nameof(SocialJoin), sizeof(SocialJoin), 8),
        ];
        Console.WriteLine("record sizes: " + string.Join(", ", sizes.Select(s => $"{s.Name} {s.Size}")));
        foreach (var s in sizes)
        {
            Check(s.Size == s.Expected, $"{s.Name} is {s.Size} bytes, the C++ header says {s.Expected}");
        }
    }

    BridgeSession.Configure(content);

    // Each state as Unreal describes it, and what Discord then shows.
    static SocialPresence P(SocialActivity activity) => new()
    {
        Activity = (int)activity, Side = -1, SpyScore = -1, MercScore = -1, Mode = -1, JoinKind = -1,
    };

    var cases = new List<(string Name, SocialPresence P, string Server, string Map, string Join, string Expect)>
    {
        ("home", P(SocialActivity.Menus), "", "", "", "\"In the menus\""),
        ("server list", P(SocialActivity.Menus) with { Page = (int)SocialPage.Servers }, "", "", "", "\"Browsing servers\""),
        ("offline setup", P(SocialActivity.Menus) with { Page = (int)SocialPage.OfflineSetup }, "", "", "", "\"Setting up an offline match\""),
        ("hub page", P(SocialActivity.Menus) with { Page = (int)SocialPage.Hub }, "", "", "", "\"Looking for a hub\""),
        ("joining", P(SocialActivity.Online) with { Phase = (int)SocialPhase.Joining, Mode = 5, PartySize = 1, PartyMax = 8, JoinKind = 0 },
            "Lumbridge EU", "AquaE", "srv-123", "\"Joining Lumbridge EU\""),
        ("lobby", P(SocialActivity.Online) with { Phase = (int)SocialPhase.Lobby, Mode = 4, PartySize = 3, PartyMax = 8, JoinKind = 0 },
            "Lumbridge EU", "AquaE", "srv-123", "\"In the lobby of Lumbridge EU\""),
        ("hosting", P(SocialActivity.Online) with { Phase = (int)SocialPhase.Lobby, Mode = 6, Flags = (int)SocialPresenceFlags.Hosting, PartySize = 2, PartyMax = 4, JoinKind = 1 },
            "Ryan's game", "ClubE", "203.0.113.7:28888", "\"Hosting Ryan's game\""),
        ("match as merc", P(SocialActivity.Online) with
            {
                Phase = (int)SocialPhase.Match, Mode = 5, Flags = (int)SocialPresenceFlags.RoundDrawn, Side = 1, SpyScore = 2, MercScore = 1,
                PartySize = 4, PartyMax = 4, JoinKind = 0,
            },
            "Lumbridge EU", "AquaE", "srv-123", "Mercenary · Spies 2 – 1 Mercs · Lumbridge EU"),
        ("match spectating", P(SocialActivity.Online) with
            {
                Phase = (int)SocialPhase.Match, Mode = 5, Flags = (int)(SocialPresenceFlags.RoundDrawn | SocialPresenceFlags.Spectating),
                PartySize = 4, PartyMax = 8, JoinKind = 0,
            },
            "Lumbridge EU", "AquaE", "srv-123", "\"Spectating · Lumbridge EU\""),
        ("password lobby", P(SocialActivity.Online) with { Phase = (int)SocialPhase.Lobby, Mode = 5, Flags = (int)SocialPresenceFlags.JoinPassword, PartySize = 1, PartyMax = 8, JoinKind = 0 },
            "Private", "AquaE", "srv-9", "join oscct1|mp|srv-9"),
        ("results", P(SocialActivity.Online) with { Phase = (int)SocialPhase.Results, Mode = 7, PartySize = 5, PartyMax = 8, JoinKind = 0 },
            "Lumbridge EU", "ClubE", "srv-123", "\"Match over on Lumbridge EU\""),
        ("hub pad match", P(SocialActivity.Online) with
            {
                Phase = (int)SocialPhase.Match, Mode = 5, Flags = (int)(SocialPresenceFlags.RoundDrawn | SocialPresenceFlags.HubMatch), Side = 0, SpyScore = 0, MercScore = 0,
                PartySize = 2, PartyMax = 4, JoinKind = 1,
            },
            "Pad 1", "AquaE", "127.0.0.1:28890", "Spy · Spies 0 – 0 Mercs · Pad 1"),
        ("hub arriving", P(SocialActivity.Hub) with { PartySize = 1, PartyMax = 32, JoinKind = 2 },
            "OpenSCCT Hub", "", "hub.example.org:28886", "\"Arriving\""),
        ("hub as spy", P(SocialActivity.Hub) with { Side = 0, Flags = (int)SocialPresenceFlags.HubArrived, PartySize = 6, PartyMax = 32, JoinKind = 2 },
            "OpenSCCT Hub", "", "hub.example.org:28886", "join oscct1|h|hub.example.org:28886"),
        ("hosted hub", P(SocialActivity.Hub) with { Side = 1, Flags = (int)(SocialPresenceFlags.HubArrived | SocialPresenceFlags.Hosting), PartySize = 1, PartyMax = 32, JoinKind = 2 },
            "", "", "127.0.0.1:28886", "\"In the hub: 127.0.0.1:28886\""),
        ("offline vs bots", P(SocialActivity.Offline) with { Mode = 4, Side = 0, SpyScore = 1, MercScore = 3, Bots = 3, Flags = (int)SocialPresenceFlags.RoundDrawn },
            "", "AquaE", "", " · vs bots"),
        ("offline alone", P(SocialActivity.Offline) with { Mode = 6, Side = 1, SpyScore = 0, MercScore = 0, Flags = (int)SocialPresenceFlags.RoundDrawn },
            "", "ClubE", "", "\"Mercenary · Spies 0 – 0 Mercs\""),
    };

    SocialBridge social = SocialBridge.Instance;
    foreach (var c in cases)
    {
        PresenceState state = social.SetPresence(c.P, c.Server, c.Map, c.Join);
        string line = state.Describe();
        Console.WriteLine($"{c.Name,-17} {line}");
        Check(line.Contains(c.Expect, StringComparison.Ordinal), $"{c.Name} should show {c.Expect}");
    }

    // Cases the wording alone does not show.
    PresenceState hosted = social.SetPresence(cases.First(c => c.Name == "hosted hub").P, "", "", "127.0.0.1:28886");
    Check(hosted.Join is null, "a hub this PC hosts must not offer a join");
    PresenceState pad = social.SetPresence(cases.First(c => c.Name == "hub pad match").P, "Pad 1", "AquaE", "127.0.0.1:28890");
    Check(pad.Join is null, "a hub pad's match must not offer a join");
    PresenceState offline = social.SetPresence(cases.First(c => c.Name == "offline vs bots").P, "", "AquaE", "");
    Check(offline.PartyId is null && offline.Join is null, "an offline match has no party and no join");
    Check(offline.Details.Contains("AQUARIUS", StringComparison.Ordinal), "the map's id should read as its catalog name (AquaE)");

    // The join secrets: each kind out and back, the password kinds, Discord's limit, what an older build sends.
    Console.WriteLine();
    DiscordJoinSecret[] secrets =
    [
        new(DiscordJoinKind.Master, "0f3a9c1e2b"), new(DiscordJoinKind.Master, "0f3a9c1e2b", true),
        new(DiscordJoinKind.Address, "203.0.113.7:28888"), new(DiscordJoinKind.Address, "[2001:db8::1]:28888", true),
        new(DiscordJoinKind.Hub, "hub.example.org:28886"), new(DiscordJoinKind.Hub, "hub.example.org:28886", true),
    ];
    foreach (DiscordJoinSecret secret in secrets)
    {
        string text = secret.Format()!;
        DiscordJoinSecret? back = DiscordJoinSecret.Parse(text);
        // A hub never carries the password flag (hubs have no password).
        DiscordJoinSecret expected = secret.Kind == DiscordJoinKind.Hub ? secret with { NeedsPassword = false } : secret;
        Console.WriteLine($"secret {text,-36} -> {back?.Kind} {back?.Value} password={back?.NeedsPassword}");
        Check(back == expected, "round trip of " + text);
    }

    Check(new DiscordJoinSecret(DiscordJoinKind.Master, new string('x', 130)).Format() is null, "a secret over 128 characters is not shown");
    foreach (string bad in new[] { "", "oscct2|m|x", "oscct1|z|x", "oscct1|m|  ", "oscct1|m" })
    {
        Check(DiscordJoinSecret.Parse(bad) is null, "'" + bad + "' must not parse");
    }

    Check(!DiscordJoinSecret.Shareable("127.0.0.1:28888") && !DiscordJoinSecret.Shareable("localhost:1") && !DiscordJoinSecret.Shareable("[::1]:1") &&
        DiscordJoinSecret.Shareable("203.0.113.7:28888"), "loopback addresses are not shareable");
    Console.WriteLine("secrets: round trips, the 128-character limit, unknown kinds and loopback addresses checked");

    // The invite pipe fix: the activity the library serialises carries its obsolete MatchSecret; the frame sent
    // without it keeps the join secret and the rest.
    Console.WriteLine();
    PresenceState joinable = social.SetPresence(cases.First(c => c.Name == "lobby").P, "Lumbridge EU", "AquaE", "srv-123");
    RichPresence rich = DiscordPresence.ToRichPresence(joinable, DateTime.UtcNow);
    var frame = new PipeFrame(Opcode.Frame, new { cmd = "SET_ACTIVITY", nonce = "1", args = new { pid = 1, activity = rich } });
    string before = frame.Message;
    string after = DiscordInvitePipe.Strip(frame).Message;
    Console.WriteLine("activity sent:   " + after);
    Check(before.Contains("MatchSecret", StringComparison.Ordinal) || before.Contains("\"match\"", StringComparison.Ordinal),
        "the library's activity was expected to carry the retired match secret (the fix may no longer be needed)");
    Check(!after.Contains("MatchSecret", StringComparison.Ordinal) && !after.Contains("\"match\"", StringComparison.Ordinal), "the match secret must be stripped");
    Check(after.Contains("oscct1|m|srv-123", StringComparison.Ordinal), "the join secret must survive the strip");

    // The website join's ids and its master check.
    Check(WebsiteJoin.IsServerId("abc-DEF_123") && !WebsiteJoin.IsServerId("a b") && !WebsiteJoin.IsServerId("../x") && !WebsiteJoin.IsServerId(new string('a', 129)),
        "website server ids");
    Check(WebsiteJoin.UsesOfficialMaster("https://master.lumbridge.co.uk") && WebsiteJoin.UsesOfficialMaster("https://master.lumbridge.co.uk/") &&
        !WebsiteJoin.UsesOfficialMaster("http://master.lumbridge.co.uk") && !WebsiteJoin.UsesOfficialMaster("https://master.lumbridge.co.uk:8443") &&
        !WebsiteJoin.UsesOfficialMaster("https://evil.example/") && !WebsiteJoin.UsesOfficialMaster(null), "the official master check");

    Console.WriteLine();
    Console.WriteLine(failures == 0 ? "PROBE OK" : $"PROBE FAILED ({failures})");
    return failures == 0 ? 0 : 1;
}

static int Forward(string id)
{
    if (!WebsiteJoin.IsServerId(id))
    {
        Console.WriteLine("usage: SocialProbe forward <server id>");
        return 2;
    }

    bool ok = WebsiteJoin.ForwardAsync(id, CancellationToken.None).GetAwaiter().GetResult();
    Console.WriteLine(ok ? $"forwarded {id}: the running game answered OK" : $"forwarding {id} failed: no game owns {WebsiteJoin.PipeName}, or it did not answer");
    return ok ? 0 : 1;
}

static int List(string url)
{
    using var client = new MasterClient(url, "b7cee2a094feca62d43bdac7f80be755a98c62a85c5977a0");
    ServerListResponse list = client.ListAsync().GetAwaiter().GetResult();
    Console.WriteLine($"{list.Servers.Count} listed on {url}");
    foreach (ServerEntry e in list.Servers)
    {
        Console.WriteLine($"  {e.Id,-40} {e.Kind,-6} {e.Name} ({e.Players}/{e.MaxPlayers}, {e.Map}){(e.Password ? " password" : string.Empty)}{(e.RequireAccounts ? " accounts" : string.Empty)}");
    }

    return 0;
}
