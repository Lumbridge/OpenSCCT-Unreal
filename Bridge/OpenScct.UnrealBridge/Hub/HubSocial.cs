using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Server.Hub;
using OpenScct.UnrealBridge.Controls;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// What the hub's HUD reads and sends (an OpenSCCT addition), ported from the Godot client's game/Scripts/Hub/HubHud.cs,
/// HubHud.Travel.cs, HubHud.Bindings.cs, HubHud.Activities.cs (the U prompt), HubPlayerList.cs and HubSignsView.cs with
/// their drawing taken out: Unreal draws (Source/OpenSCCT/Hub/ScctHubHud). It reads the visit of <see cref="HubBridge"/>:
/// the name tags with the bubble of what each player last said and the emote he plays, the chat with its channel, the
/// roster with pings and where each player stands, the world signs (a board's lines in place of its sign's text), the
/// travel destinations, the U prompt and the key hints from the player's own bindings. It sends chat (everyone, the
/// side or nearby players; "/wave" plays an emote and "/l" speaks to nearby players, as in Godot), the side change, the
/// return to the plaza, the activities' keys, emotes and travel. Chat mutes are the visit's (HubClient keeps them);
/// voice volume and voice mute are kept in Unreal's hub subsystem. Single-threaded: Unreal's game thread only.
/// </summary>
public sealed class HubSocial
{
    /// <summary>Chat lines kept for GetChat (the HubClient keeps 64 too).</summary>
    private const int ChatKept = 64;

    /// <summary>A chat line shows over its speaker's head this long (seconds; HubHud.BubbleSeconds).</summary>
    private const double BubbleSeconds = 6.0;

    /// <summary>A sign this far from the eye (UE units) is not shown (the server stops sending it at 7000).</summary>
    private const float SignRange = 7000f;

    /// <summary>A return to the plaza counts as done once the pawn jumped this far (UE units).</summary>
    private const float PlazaJump = 256f;

    private readonly HubBridge _hub;
    private readonly List<(int Serial, HubChatEntry Entry)> _chat = new();
    private readonly List<HubNameTag> _tags = new();
    private readonly Dictionary<string, FVector> _placed = new(StringComparer.OrdinalIgnoreCase);
    private HubClient? _client;
    private int _chatSerial;
    private int _signRevision;
    private int _travelSerial;
    private FVector? _plazaFrom;
    private double _plazaAsked;

    public HubSocial(HubBridge hub)
    {
        _hub = hub ?? throw new ArgumentNullException(nameof(hub));
    }

    /// <summary>The visit whose events this reads, rewired whenever the bridge's visit changes.</summary>
    private HubClient? Client
    {
        get
        {
            HubClient? now = _hub.Client;
            if (!ReferenceEquals(now, _client))
            {
                Attach(now);
            }

            return _client;
        }
    }

    private void Attach(HubClient? client)
    {
        if (_client is { } old)
        {
            old.ChatAdded -= OnChat;
            old.SideChanged -= OnSide;
            old.SignChanged -= OnSign;
            old.SignRemoved -= OnSignRemoved;
            old.Play.BoardChanged -= OnSignRemoved;
            old.Play.TravelMenuOpened -= OnTravelMenu;
        }

        _client = client;
        _chat.Clear();
        _plazaFrom = null;
        _signRevision++;
        if (client is null)
        {
            return;
        }

        foreach (HubChatEntry entry in client.Chat)
        {
            OnChat(entry);
        }

        client.ChatAdded += OnChat;
        client.SideChanged += OnSide;
        client.SignChanged += OnSign;
        client.SignRemoved += OnSignRemoved;
        client.Play.BoardChanged += OnSignRemoved;
        client.Play.TravelMenuOpened += OnTravelMenu;
    }

    private void OnChat(HubChatEntry entry)
    {
        _chat.Add((++_chatSerial, entry));
        if (_chat.Count > ChatKept)
        {
            _chat.RemoveAt(0);
        }
    }

    private void OnSide(int team) => _hub.Log($"the hub says this player now plays {(team == 1 ? "merc" : "spy")} (side change took effect)");

    private void OnSign(HubSign sign) => _signRevision++;

    private void OnSignRemoved(string id) => _signRevision++;

    private void OnTravelMenu() => _travelSerial++;

    // ---- reads --------------------------------------------------------------------------------------------------

    public HubSocialState State()
    {
        if (Client is not { } client)
        {
            return new HubSocialState { Team = -1 };
        }

        WatchPlazaReturn(client);
        int flags = (_hub.Round is not null ? HubSocialFlags.InHub : 0)
            | (client.Play.Running ? HubSocialFlags.Running : 0)
            | (client.Play.InRound ? HubSocialFlags.InRound : 0)
            | (client.Queue.PadId is not null ? HubSocialFlags.OnPad : 0)
            | (_hub.Host is not null ? HubSocialFlags.Hosting : 0);
        return new HubSocialState
        {
            Valid = 1,
            PlayerId = client.PlayerId,
            Team = client.Team,
            ChatSerial = _chatSerial,
            SignRevision = _signRevision,
            PlayerCount = client.Roster.Count,
            DestinationCount = client.Play.Destinations.Count,
            TravelMenuSerial = _travelSerial,
            Flags = flags,
            Now = (float)client.Play.Now,
        };
    }

    /// <summary>The roster as the player list shows it: by side, then by name; with where each pawn stands.</summary>
    public int Players(Span<HubSocialPlayer> target)
    {
        if (Client is not { } client)
        {
            return 0;
        }

        Placements(client);
        var roster = new List<HubRosterEntry>(client.Roster);
        roster.Sort(static (a, b) => a.Team != b.Team ? a.Team.CompareTo(b.Team) : string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase));
        for (int i = 0; i < roster.Count && i < target.Length; i++)
        {
            HubRosterEntry p = roster[i];
            bool placed = _placed.TryGetValue(p.Name, out FVector at);
            ref HubSocialPlayer row = ref target[i];
            row.PlayerId = p.PlayerId;
            row.Team = p.Team;
            row.PingMs = p.PingMs;
            row.Flags = (p.PlayerId == client.PlayerId ? HubSocialFlags.Me : 0) | (p.SignedIn ? HubSocialFlags.SignedIn : 0)
                | (client.IsChatMuted(p.Name) ? HubSocialFlags.ChatMuted : 0) | (placed ? HubSocialFlags.Placed : 0);
            row.X = at.X;
            row.Y = at.Y;
            row.Z = at.Z;
            unsafe
            {
                fixed (HubSocialPlayer* r = &row)
                {
                    OnlineTextCopy.Fixed(r->Name, HubSocialPlayer.NameCapacity, p.Name);
                }
            }
        }

        return roster.Count;
    }

    /// <summary>The name tags round <paramref name="eye"/> (HubHud.DrawTags), farthest first.</summary>
    public int NameTags(FVector eye, Span<HubSocialTag> target)
    {
        if (Client is not { } client || _hub.Round is null)
        {
            return 0;
        }

        client.NameTags(eye, _tags);
        for (int i = 0; i < _tags.Count && i < target.Length; i++)
        {
            HubNameTag tag = _tags[i];
            ref HubSocialTag row = ref target[i];
            row.X = tag.Location.X;
            row.Y = tag.Location.Y;
            row.Z = tag.Location.Z;
            row.Distance = tag.Distance;
            row.PlayerId = tag.PlayerId;
            row.Team = tag.Team;
            // The rank's level and prestige (the low half of the packed rank) ride in the flags' high half, for the emblem.
            row.Flags = (client.IsChatMuted(tag.Name) ? HubSocialFlags.ChatMuted : 0) | ((tag.Rank & 0xFFFF) << HubSocialFlags.TagRankShift);
            string caption = client.Play.EmoteOf(tag.PlayerId) is { } emote ? HubEmotes.Caption(emote) : string.Empty;
            unsafe
            {
                fixed (HubSocialTag* r = &row)
                {
                    OnlineTextCopy.Fixed(r->Name, HubSocialTag.NameCapacity, tag.Name);
                    OnlineTextCopy.Fixed(r->Caption, HubSocialTag.CaptionCapacity, caption);
                    OnlineTextCopy.Fixed(r->Bubble, HubSocialTag.BubbleCapacity, Bubble(client, tag.Name));
                }
            }
        }

        return _tags.Count;
    }

    /// <summary>The chat lines newer than <paramref name="after"/>, oldest first. Returns how many there are.</summary>
    public int Chat(int after, Span<HubSocialChatLine> target)
    {
        _ = Client;
        int first = _chat.FindIndex(c => c.Serial > after);
        if (first < 0)
        {
            return 0;
        }

        int count = _chat.Count - first;
        for (int i = 0; i < count && i < target.Length; i++)
        {
            (int serial, HubChatEntry entry) = _chat[first + i];
            ref HubSocialChatLine row = ref target[i];
            row.Serial = serial;
            row.PlayerId = entry.PlayerId;
            row.Team = entry.Team;
            row.Channel = (int)(entry.IsSystem ? HubChatChannel.System : entry.TeamOnly ? HubChatChannel.Side : entry.Local ? HubChatChannel.Nearby : HubChatChannel.Everyone);
            unsafe
            {
                fixed (HubSocialChatLine* r = &row)
                {
                    OnlineTextCopy.Fixed(r->Name, HubSocialChatLine.NameCapacity, entry.Name);
                    OnlineTextCopy.Fixed(r->Text, HubSocialChatLine.TextCapacity, entry.Text);
                }
            }
        }

        return count;
    }

    /// <summary>
    /// The world signs within reach of <paramref name="eye"/> (HubSignsView), nearest first: a board's lines in place of
    /// its sign's text, and whether the hub's world hides each one from the eye.
    /// </summary>
    public int Signs(FVector eye, Span<HubSocialSign> target)
    {
        if (Client is not { } client || _hub.Round is null)
        {
            return 0;
        }

        IActorNatives? world = client.Session.World.Level.GetService<IActorNatives>();
        var near = new List<(HubSign Sign, float Distance)>();
        foreach (HubSign sign in client.Signs.Values)
        {
            float d = (sign.Position - eye).Size();
            if (d <= SignRange)
            {
                near.Add((sign, d));
            }
        }

        near.Sort(static (a, b) => a.Distance.CompareTo(b.Distance));
        for (int i = 0; i < near.Count && i < target.Length; i++)
        {
            (HubSign sign, float distance) = near[i];
            string text = client.Play.Boards.TryGetValue(sign.Id, out HubBoard? board) ? string.Join('\n', board.Lines) : sign.Text;
            // A mounted sign is tested at a point just in front of its board, so the board itself does not hide it.
            FVector probe = sign.Position;
            if (sign.Face.Mounted)
            {
                float a = sign.Face.Yaw * MathF.Tau / 65536f;
                probe += new FVector(MathF.Cos(a), MathF.Sin(a), 0f) * 8f;
            }

            bool visible = world?.FastTrace(probe, eye) ?? true;
            ref HubSocialSign row = ref target[i];
            row.X = sign.Position.X;
            row.Y = sign.Position.Y;
            row.Z = sign.Position.Z;
            row.Scale = sign.Scale;
            row.Yaw = sign.Face.Yaw;
            row.Width = sign.Face.Width;
            row.Height = sign.Face.Height;
            row.Distance = distance;
            row.Rgba = sign.Rgba;
            row.Flags = (sign.Face.Mounted ? HubSocialFlags.Mounted : 0) | (visible ? HubSocialFlags.Visible : 0);
            unsafe
            {
                fixed (HubSocialSign* r = &row)
                {
                    OnlineTextCopy.Fixed(r->Id, HubSocialSign.IdCapacity, sign.Id);
                    OnlineTextCopy.Fixed(r->Text, HubSocialSign.TextCapacity, text);
                }
            }
        }

        return near.Count;
    }

    /// <summary>The travel menu's districts in the hub's order, with the player's best time on each.</summary>
    public int Destinations(Span<HubSocialDestination> target)
    {
        if (Client is not { } client)
        {
            return 0;
        }

        int i = 0;
        foreach ((string id, string title) in client.Play.Destinations)
        {
            if (i < target.Length)
            {
                ref HubSocialDestination row = ref target[i];
                row.BestMs = client.Play.PersonalBests.TryGetValue(id, out int best) ? best : -1;
                unsafe
                {
                    fixed (HubSocialDestination* r = &row)
                    {
                        OnlineTextCopy.Fixed(r->Id, HubSocialDestination.IdCapacity, id);
                        OnlineTextCopy.Fixed(r->Title, HubSocialDestination.TitleCapacity, title);
                    }
                }
            }

            i++;
        }

        return client.Play.Destinations.Count;
    }

    public string Text(HubSocialText kind)
    {
        if (Client is not { } client)
        {
            return string.Empty;
        }

        return kind switch
        {
            HubSocialText.Help => $"Hub   {Hint(client, "HubTravel")} travel   {Hint(client, "HubSpy")} spy   {Hint(client, "HubMerc")} merc   " +
                $"{Hint(client, "HubPlaza")} plaza   {Hint(client, "HubEmotes")} emotes   {Hint(client, "HubUse")} ready/use   {Hint(client, "HubCancel")} stop   " +
                $"{Hint(client, "HubWatch")} watch   {Hint(client, "HubPlayers")} players   " +
                $"{Hint(client, "Talk")} chat   {Hint(client, "TeamTalk")} team chat   {Hint(client, "ShowMenu")} menu",
            HubSocialText.EmoteMenu => EmoteMenu(client),
            HubSocialText.Queue => _hub.QueueTextOf(client),
            HubSocialText.HubName => client.ServerName,
            _ => string.Empty,
        };
    }

    /// <summary>The key the player's bindings put a command on ("J"), or "Unbound".</summary>
    public string KeyHint(string command) => Client is { } client ? Hint(client, command) : HubBindings.Hint(ControlsStore.Bindings.Spy.Keys, command);

    /// <summary>What U (or walking on) does where the player stands (HubHud.Prompt), or empty.</summary>
    public string Prompt(FVector at)
    {
        if (Client is not { } client || _hub.Round is null)
        {
            return string.Empty;
        }

        HubPlayState play = client.Play;
        FVector feet = client.LocalPlayer?.Pawn is { } pawn ? pawn.Location : at;
        if (client.Queue.PadId is not null)
        {
            // On a match pad U readies up (the queue line says who is ready).
            return client.Queue.Side >= 0
                ? Hint(client, "HubUse") + (client.Queue.Text.Contains("you are ready", StringComparison.Ordinal) ? "  unready" : "  ready up")
                : string.Empty;
        }

        // The hub checks the laptop's range from the pawn, not the camera.
        if (play.Nearest("laptop", feet, out float ld) is { } laptop && ld <= HubPads.LaptopUseRange)
        {
            return $"{Hint(client, "HubUse")}  set the match of Pad {laptop.Id[^1]} (laptop)";
        }

        if (play.Nearest("jukebox", at, out float jd) is not null && jd <= HubPlayK.JukeboxUseRange)
        {
            return $"{Hint(client, "HubUse")}  next record   (playing \"{play.TrackTitle}\")";
        }

        if (!play.InRound && play.Nearest("firingline", at, out _) is { } line &&
            at.X >= line.At.X - HubPlayK.FiringLineSlack && at.X <= line.At.X + 320f && MathF.Abs(at.Y - line.At.Y) <= line.Radius)
        {
            return client.Team == 1
                ? string.Create(CultureInfo.InvariantCulture, $"{Hint(client, "HubUse")}  start a {HubPlayK.RoundSeconds:0} s round at the aim range")
                : $"The aim range is for mercs ({Hint(client, "HubMerc")} to play merc)";
        }

        if (play.InRound || play.Running)
        {
            return Hint(client, "HubCancel") + "  stop";
        }

        if (play.Nearest("start", at, out float sd) is { } start && sd <= start.Radius + 256f)
        {
            return client.Team == 0
                ? "Spy course: step out of the green gate to start the clock"
                : $"The movement course is for spies ({Hint(client, "HubSpy")} to play spy)";
        }

        if (play.Nearest("travel", feet, out float td) is not null && td <= 320f)
        {
            return Hint(client, "HubTravel") + "  travel to another district";
        }

        return string.Empty;
    }

    // ---- sends --------------------------------------------------------------------------------------------------

    /// <summary>
    /// Says a chat line (HubHud.Send): "/wave" and the other emotes play the emote, "/l text" speaks to the players near,
    /// the Nearby channel speaks to them too, Side to the player's side, Everyone to the hub. False when nothing was sent.
    /// </summary>
    public bool Send(HubChatChannel channel, string text)
    {
        if (Client is not { } client)
        {
            return false;
        }

        string line = (text ?? string.Empty).Trim();
        if (line.Length == 0)
        {
            return false;
        }

        if (line.Length > 1 && line[0] == '/' && HubEmotes.IsEmote(line[1..].ToLowerInvariant()))
        {
            client.Emote(line[1..].ToLowerInvariant());
            _hub.Log($"emote '{line[1..].ToLowerInvariant()}' asked for");
        }
        else if (line.StartsWith("/l ", StringComparison.OrdinalIgnoreCase) && line.Length > 3)
        {
            client.SayLocal(line[3..]);
            _hub.Log($"said to nearby players: {line[3..]}");
        }
        else if (channel == HubChatChannel.Nearby)
        {
            client.SayLocal(line);
            _hub.Log($"said to nearby players: {line}");
        }
        else
        {
            client.Say(line, channel == HubChatChannel.Side);
            _hub.Log($"said to {(channel == HubChatChannel.Side ? "the side" : "everyone")}: {line}");
        }

        return true;
    }

    /// <summary>
    /// Runs one of the hub's commands (HubHud.RunHubCommand) that talk to the hub: HubSpy, HubMerc, HubPlaza (and its
    /// other name HubRespawn), HubUse, HubCancel, HubWatch, HubEmote1 to HubEmote8. The commands that open the HUD's own
    /// panels (HubPlayers, HubEmotes, HubTravel, HubArmoury) are Unreal's. False for a command this does not run.
    /// </summary>
    public bool Command(string command)
    {
        if (Client is not { } client)
        {
            return false;
        }

        switch (command)
        {
            case "HubSpy":
            case "HubMerc":
                int team = command == "HubMerc" ? 1 : 0;
                client.RequestSide(team);
                _hub.Log($"side change to {(team == 1 ? "merc" : "spy")} asked for (now {(client.Team == 1 ? "merc" : client.Team == 0 ? "spy" : "unknown")})");
                return true;
            case "HubPlaza":
            case "HubRespawn":
                client.Respawn();
                _plazaFrom = client.LocalPlayer?.Pawn?.Location;
                _plazaAsked = client.Play.Now;
                _hub.Log(_plazaFrom is { } from
                    ? string.Create(CultureInfo.InvariantCulture, $"return to the plaza asked for from ({from.X:F0}, {from.Y:F0}, {from.Z:F0})")
                    : "return to the plaza asked for (no pawn yet)");
                return true;
            case "HubUse":
                client.Use();
                return true;
            case "HubCancel":
                client.CancelActivity();
                return true;
            case "HubWatch" when client.Queue.PadId is { } pad:
                client.Watch(pad);
                return true;
        }

        if (command.StartsWith("HubEmote", StringComparison.Ordinal) && int.TryParse(command.AsSpan(8), NumberStyles.Integer, CultureInfo.InvariantCulture, out int choice)
            && choice >= 1 && choice <= HubEmotes.All.Count)
        {
            client.Emote(HubEmotes.All[choice - 1]);
            _hub.Log($"emote '{HubEmotes.All[choice - 1]}' asked for");
            return true;
        }

        return false;
    }

    /// <summary>Travels to a district of the travel menu.</summary>
    public bool Travel(string id)
    {
        if (Client is not { } client || !client.Play.Destinations.ContainsKey(id))
        {
            return false;
        }

        client.Travel(id);
        _hub.Log($"travel to '{id}' asked for");
        return true;
    }

    /// <summary>Hides (or shows again) a player's chat lines for the visit, by name.</summary>
    public void MuteChat(string name, bool muted)
    {
        if (Client is { } client && !string.IsNullOrEmpty(name))
        {
            client.MuteChat(name, muted);
            _hub.Log($"chat of '{name}' {(muted ? "muted" : "unmuted")}");
        }
    }

    // ---- helpers ------------------------------------------------------------------------------------------------

    private static string Hint(HubClient client, string command) => HubBindings.Hint(ControlsStore.Bindings.Of(client.Team == 1).Keys, command);

    private static string EmoteMenu(HubClient client)
    {
        var text = new StringBuilder("EMOTES\n");
        for (int i = 0; i < HubEmotes.All.Count; i++)
        {
            text.Append(Hint(client, "HubEmote" + (i + 1).ToString(CultureInfo.InvariantCulture))).Append("  ").Append(HubEmotes.All[i]).Append('\n');
        }

        return text.Append(Hint(client, "HubEmotes")).Append(" / ").Append(Hint(client, "ShowMenu")).Append("  close").ToString();
    }

    /// <summary>What the player said in the last few seconds, as a bubble (two short lines at most), or empty (HubHud.Bubble).</summary>
    private static string Bubble(HubClient client, string name)
    {
        if (!client.LastSaid(name, out string text, out double at) || client.Play.Now - at > BubbleSeconds)
        {
            return string.Empty;
        }

        const int Width = 34;
        if (text.Length <= Width)
        {
            return "“" + text + "”";
        }

        int cut = text.LastIndexOf(' ', Width);
        cut = cut > 8 ? cut : Width;
        string first = text[..cut].TrimEnd();
        string rest = text[cut..].TrimStart();
        if (rest.Length > Width)
        {
            rest = rest[..(Width - 3)] + "...";
        }

        return "“" + first + "\n" + rest + "”";
    }

    /// <summary>Where each named player's pawn stands in the visit's world.</summary>
    private void Placements(HubClient client)
    {
        _placed.Clear();
        foreach (SBasePawn pawn in client.Session.World.Level.DynamicActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe && pawn.Health > 0 && pawn.PlayerReplicationInfo is { } pri && pri.PlayerName.Length > 0)
            {
                _placed[pri.PlayerName] = pawn.Location;
            }
        }
    }

    /// <summary>Logs when a return to the plaza asked for has put the pawn somewhere else (the hub respawns it there).</summary>
    private void WatchPlazaReturn(HubClient client)
    {
        if (_plazaFrom is not { } from)
        {
            return;
        }

        if (client.LocalPlayer?.Pawn is { } pawn && (pawn.Location - from).Size() > PlazaJump)
        {
            FVector to = pawn.Location;
            _hub.Log(string.Create(CultureInfo.InvariantCulture,
                $"back at the plaza: the pawn moved from ({from.X:F0}, {from.Y:F0}, {from.Z:F0}) to ({to.X:F0}, {to.Y:F0}, {to.Z:F0}) {client.Play.Now - _plazaAsked:F1} s after asking"));
            _plazaFrom = null;
        }
        else if (client.Play.Now - _plazaAsked > 10.0)
        {
            _hub.Log("the return to the plaza did not move the pawn within 10 s (already at the plaza?)");
            _plazaFrom = null;
        }
    }
}
