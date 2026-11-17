using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;
using OpenScct.UnrealBridge.Hub;

// The hub HUD's table, called through its pointers as Unreal calls it: a hosted hub (loopback only) with three bot
// visitors, then the roster with pings and positions, the name tags round the local eye with their sides, the chat
// with its channels (a line said to everyone and one to the side come back), the world signs, the key hints from the
// default bindings (the hub keys are on J, K, N, P), a side change taking effect and a return to the plaza.
internal static unsafe partial class Probe
{
    private static HubSocialApi s_social;

    private static bool Social(int port, double inHubSeconds)
    {
        HubSocialApi api = default;
        delegate* unmanaged<HubSocialApi*, int> getApi = &HubSocialExports.GetApi;
        int version = getApi(&api);
        s_social = api;
        Console.WriteLine($"social table: version {version}, {sizeof(HubSocialApi)} bytes; state {sizeof(HubSocialState)}, player {sizeof(HubSocialPlayer)}, tag {sizeof(HubSocialTag)}, chat line {sizeof(HubSocialChatLine)}, sign {sizeof(HubSocialSign)}, destination {sizeof(HubSocialDestination)} bytes");
        if (sizeof(HubSocialApi) != 8 + 14 * sizeof(nint) || sizeof(HubSocialState) != 40 || sizeof(HubSocialPlayer) != 92 || sizeof(HubSocialTag) != 348
            || sizeof(HubSocialChatLine) != 592 || sizeof(HubSocialSign) != 1160 || sizeof(HubSocialDestination) != 196)
        {
            Console.WriteLine("FAIL: the layout does not match Runtime/ScctHubSocialApi.h");
            return false;
        }

        Console.WriteLine($"pads table: {sizeof(HubPadsApi)} bytes; state {sizeof(HubPadsState)}, pad {sizeof(HubPadRow)}, laptop row {sizeof(HubLaptopRow)}, laptop map {sizeof(HubLaptopMap)} bytes");
        if (sizeof(HubPadsApi) != 8 + 12 * sizeof(nint) || sizeof(HubPadsState) != 32 || sizeof(HubPadRow) != 928 || sizeof(HubLaptopRow) != 220
            || sizeof(HubLaptopMap) != 780)
        {
            Console.WriteLine("FAIL: the layout does not match Runtime/ScctHubPadsApi.h");
            return false;
        }

        // The default bindings carry the hub's keys, in the action editor's "Hub" category too.
        KeyLayout keys = ControlBindings.DefaultKeys();
        string hints = string.Join(", ", new[] { "HubSpy", "HubMerc", "HubPlaza", "HubPlayers", "HubEmotes", "HubEmote1" }.Select(c => c + "=" + HubBindings.Hint(keys, c)));
        int hubActions = ControlActions.All.Count(a => a.Category == HubBindings.Category);
        Console.WriteLine($"default keys: {hints}; K is '{keys.Bindings["K"]}'; {hubActions} hub actions in the action list");
        if (HubBindings.Hint(keys, "HubSpy") != "J" || HubBindings.Hint(keys, "HubPlaza") != "N" || hubActions != HubBindings.All.Count)
        {
            Console.WriteLine("FAIL: the hub keys are not in the default bindings");
            return false;
        }

        // A file saved before the hub keys gets them; one saved with them keeps an unbound hub key unbound.
        ControlBindings old = ControlBindings.FromJson("{\"version\":1,\"spy\":{\"keys\":{\"W\":\"Button BT_Up\"}}}");
        var unbound = new ControlBindings();
        unbound.Spy.Keys.Bind("J", "");
        ControlBindings kept = ControlBindings.FromJson(unbound.ToJson());
        Console.WriteLine($"old file: spy J '{old.Spy.Keys.Bindings.GetValueOrDefault("J")}'; saved with J unbound: spy J '{kept.Spy.Keys.Bindings.GetValueOrDefault("J")}'");
        if (HubBindings.Hint(old.Spy.Keys, "HubSpy") != "J" || kept.Spy.Keys.Bindings.ContainsKey("J"))
        {
            Console.WriteLine("FAIL: the bindings file upgrade is wrong");
            return false;
        }

        Console.WriteLine($"-- hosting a hub on UDP port {port} (loopback only), joining it as a spy, and sending 3 bots in");
        fixed (char* name = "HubProbe")
        {
            if (s_api.HostAndJoin(port, name, 0, 1) != 1)
            {
                Drain();
                Console.WriteLine($"FAIL: could not host: {Text(HubText.Error)}");
                return false;
            }
        }

        if (s_social.StartBots(3) != 1)
        {
            Console.WriteLine("FAIL: the bots did not start");
            return false;
        }

        if (Run(TimeSpan.FromSeconds(60), s => s.State is (int)HubState.InHub or (int)HubState.Failed) != HubState.InHub)
        {
            Console.WriteLine($"FAIL: not in the hub: {Text(HubText.Error)}");
            return false;
        }

        s_api.ReportLoaded();
        Run(TimeSpan.FromSeconds(30), s => s.PlayerCount >= 4);
        Run(TimeSpan.FromSeconds(Math.Max(3, inHubSeconds)), _ => false);

        HubSocialState state;
        s_social.GetState(&state);
        Console.WriteLine($"state: valid {state.Valid} player {state.PlayerId} team {state.Team} chat serial {state.ChatSerial} signs rev {state.SignRevision} players {state.PlayerCount} destinations {state.DestinationCount} flags 0x{state.Flags:x}");

        var players = new HubSocialPlayer[16];
        int playerCount;
        fixed (HubSocialPlayer* p = players)
        {
            playerCount = s_social.GetPlayers(p, players.Length);
        }

        int placedBots = 0;
        for (int i = 0; i < Math.Min(playerCount, players.Length); i++)
        {
            fixed (HubSocialPlayer* p = &players[i])
            {
                bool placed = (p->Flags & HubSocialFlags.Placed) != 0;
                placedBots += placed && (p->Flags & HubSocialFlags.Me) == 0 ? 1 : 0;
                Console.WriteLine($"   player {p->PlayerId} '{new string(p->Name)}' team {p->Team} ping {p->PingMs} ms flags 0x{p->Flags:x}{(placed ? $" at ({p->X:F0}, {p->Y:F0}, {p->Z:F0})" : "")}");
            }
        }

        var pawn = HubBridge.Instance.Round?.LocalPawn;
        float ex = pawn?.Location.X ?? 0, ey = pawn?.Location.Y ?? 0, ez = (pawn?.Location.Z ?? 0) + 60f;
        var tags = new HubSocialTag[16];
        int tagCount;
        fixed (HubSocialTag* t = tags)
        {
            tagCount = s_social.GetNameTags(ex, ey, ez, t, tags.Length);
        }

        for (int i = 0; i < Math.Min(tagCount, tags.Length); i++)
        {
            fixed (HubSocialTag* t = &tags[i])
            {
                Console.WriteLine($"   tag '{new string(t->Name)}' team {t->Team} at {t->Distance:F0} uu caption '{new string(t->Caption)}' bubble '{new string(t->Bubble).Replace('\n', '|')}'");
            }
        }

        var signs = new HubSocialSign[64];
        int signCount;
        fixed (HubSocialSign* s = signs)
        {
            signCount = s_social.GetSigns(ex, ey, ez, s, signs.Length);
        }

        int visible = 0;
        for (int i = 0; i < Math.Min(signCount, signs.Length); i++)
        {
            fixed (HubSocialSign* s = &signs[i])
            {
                visible += (s->Flags & HubSocialFlags.Visible) != 0 ? 1 : 0;
                if (i < 6)
                {
                    Console.WriteLine($"   sign '{new string(s->Id)}' at {s->Distance:F0} uu flags 0x{s->Flags:x} scale {s->Scale:F2} rgba {s->Rgba:x8}: '{new string(s->Text).Replace('\n', '|')}'");
                }
            }
        }

        Console.WriteLine($"{signCount} signs within reach, {visible} in sight; help '{SocialText(HubSocialText.Help)}'");
        string prompt = Prompt(ex, ey, ez);
        Console.WriteLine($"prompt here: '{prompt}'");
        if (!Pads() || !Activities(ex, ey, ez))
        {
            return false;
        }

        // Chat: one line to everyone, one to the side; both come back from the hub.
        fixed (char* hello = "hello from the probe", side = "spies only")
        {
            s_social.SendChat((int)HubChatChannel.Everyone, hello);
            s_social.SendChat((int)HubChatChannel.Side, side);
        }

        Run(TimeSpan.FromSeconds(3), _ => false);
        var lines = new HubSocialChatLine[64];
        int lineCount;
        fixed (HubSocialChatLine* l = lines)
        {
            lineCount = s_social.GetChat(0, l, lines.Length);
        }

        bool heardHello = false, heardSide = false;
        for (int i = 0; i < Math.Min(lineCount, lines.Length); i++)
        {
            fixed (HubSocialChatLine* l = &lines[i])
            {
                string text = new string(l->Text);
                heardHello |= text == "hello from the probe" && l->Channel == (int)HubChatChannel.Everyone;
                heardSide |= text == "spies only" && l->Channel == (int)HubChatChannel.Side;
                Console.WriteLine($"   chat #{l->Serial} [{(HubChatChannel)l->Channel}] '{new string(l->Name)}' (team {l->Team}): {text}");
            }
        }

        // A side change and a return to the plaza, as the J/K and N keys send them.
        fixed (char* merc = "HubMerc")
        {
            s_social.RunCommand(merc);
        }

        Run(TimeSpan.FromSeconds(8), _ => HubBridge.Instance.Client?.Team == 1);
        int teamAfter = HubBridge.Instance.Client?.Team ?? -1;
        Run(TimeSpan.FromSeconds(4), _ => false);
        fixed (char* plaza = "HubPlaza")
        {
            s_social.RunCommand(plaza);
        }

        Run(TimeSpan.FromSeconds(6), _ => false);
        s_social.GetState(&state);
        Console.WriteLine($"after HubMerc: team {teamAfter}; after HubPlaza: state team {state.Team}, local pawn {(HubBridge.Instance.Round?.LocalPawn is { } lp ? lp.GetType().Name : "none")}");

        bool ok = state.Valid == 1 && playerCount >= 4 && placedBots >= 3 && tagCount >= 1 && heardHello && heardSide && teamAfter == 1;
        if (!ok)
        {
            Console.WriteLine($"FAIL: players {playerCount} (placed bots {placedBots}), tags {tagCount}, heard everyone {heardHello} side {heardSide}, team after HubMerc {teamAfter}");
        }

        s_api.Leave();
        Drain();
        return ok;
    }

    private static string SocialText(HubSocialText kind)
    {
        char* buffer = stackalloc char[1024];
        s_social.GetText((int)kind, buffer, 1024);
        return new string(buffer);
    }

    private static string Prompt(float x, float y, float z)
    {
        char* buffer = stackalloc char[512];
        s_social.GetPrompt(x, y, z, buffer, 512);
        return new string(buffer);
    }
}
