using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using OpenScct.Core;
using OpenScct.Net;
using OpenScct.Net.Discovery;
using OpenScct.Server;
using OpenScct.Server.Hub;
using OpenScct.Server.Online;
using OpenScct.UnrealBridge.Online;

namespace OpenScct.UnrealBridge.Hub;

/// <summary>
/// The hub's match pads for the Unreal client (ported from the Godot client's HubService.GoToMatch, MatchAnswered and
/// ReturnFromMatch and OnlineService.JoinFromHub, HubMatchState and ReturnToHub): the hub's match offer closes the hub
/// connection and hands the match's address and token to <see cref="OnlineBridge"/>, which plays it as any online
/// visit; after its results (and after a demo download still running, at most five minutes) the visit joins the hub
/// again with the pad it came from, so the hub puts the player back beside it. The pad laptop's draft (a lobby's
/// session parameters for the pad's next match) and the unattended pad run (-ScctHubPad) are here too.
/// </summary>
public sealed partial class HubBridge
{
    /// <summary>Seconds the match's results stay up before the way back to the hub starts.</summary>
    public const float ResultsHoldSeconds = 8f;

    /// <summary>Seconds a note about the match (going, back, why) stays on the hub HUD.</summary>
    private const float NoteSeconds = 20f;

    private HubMatchOffer? _offerTaken;
    private MatchTrip? _trip;
    private string? _returnPad;
    private string _note = string.Empty;
    private long _noteUntil;
    private PadRun? _padRun;

    /// <summary>A trip to a pad's match: the offer, where it is, and how far the online visit got.</summary>
    private sealed class MatchTrip(HubMatchOffer offer, string address)
    {
        public HubMatchOffer Offer { get; } = offer;

        public string Address { get; } = address;

        public Task<JoinResponse>? MasterJoin { get; set; }

        public bool Joined { get; set; }

        public bool Played { get; set; }

        public long ResultsSince { get; set; } = -1;

        public long DemoWaitSince { get; set; } = -1;

        public OnlineClientState? Last { get; set; }
    }

    /// <summary>The pad the player is at the match of, or null.</summary>
    public string? MatchPad => _trip?.Offer.PadId;

    /// <summary>The map of the pad's match the player is at, or null.</summary>
    public string? MatchMap => _trip?.Offer.Map;

    /// <summary>The pad the visit came back from (the hub puts the player beside it), or null.</summary>
    public string? ReturnPad => _returnPad;

    /// <summary>The note about the last match (going, back, why), empty once it is old.</summary>
    public string Note => _note.Length > 0 && _clock.NowMicroseconds < _noteUntil ? _note : string.Empty;

    /// <summary>What the trip to a pad's match is doing, for the hub HUD and the log; empty when there is none.</summary>
    public string MatchStatus
    {
        get
        {
            if (_trip is not { } trip)
            {
                return string.Empty;
            }

            string pad = PadTitle(trip.Offer.PadId);
            if (!trip.Joined)
            {
                return $"Asking the master server the way to the match of {pad}...";
            }

            return trip.Last switch
            {
                OnlineClientState.Connecting => $"Joining the match of {pad} at {trip.Address}...",
                OnlineClientState.Lobby or OnlineClientState.Travelling or OnlineClientState.Loading => $"Loading the match of {pad}...",
                OnlineClientState.Match => $"Playing the match of {pad}",
                OnlineClientState.Results => $"The match of {pad} is over",
                _ => $"At the match of {pad}",
            };
        }
    }

    private void SetNote(string text)
    {
        _note = text;
        _noteUntil = _clock.NowMicroseconds + (long)(NoteSeconds * 1_000_000);
        Touch();
    }

    private static string PadTitle(string padId) =>
        padId.StartsWith("Pad", StringComparison.OrdinalIgnoreCase) && padId.Length > 3 ? "Pad " + padId[3..] : padId;

    // ---- going to the match -------------------------------------------------------------------------------------

    /// <summary>
    /// The hub sent this player to a pad's match (HubService.GoToMatch): the hub connection closes (a hub this PC
    /// hosts goes on, its match runs there), and the online bridge joins the match at the hub's host and the match's
    /// port with its token, or through the master when the match has its own entry there.
    /// </summary>
    private void GoToMatch(HubClient client, HubMatchOffer offer)
    {
        _offerTaken = offer;
        string host = offer.Host is { Length: > 0 } h ? h : HostOf(_target);
        if (host.Contains(':', StringComparison.Ordinal) && !host.StartsWith('['))
        {
            host = "[" + host + "]";
        }

        string address = host + ":" + offer.Port.ToString(CultureInfo.InvariantCulture);
        if (client.Team is 0 or 1)
        {
            _team = client.Team;
        }

        CloseLaptop();
        _padRun?.Sent();
        StopRound();
        Client = null;
        Voice.VoiceBridge.Existing?.EndHub();
        try
        {
            client.Session.Disconnect();
        }
        finally
        {
            client.Dispose();
        }

        var trip = new MatchTrip(offer, address);
        _trip = trip;
        string what = offer.Spectator ? "Watching" : "Joining";
        SetNote($"{what} the match of {PadTitle(offer.PadId)}...");
        _status = $"{what} the match of {PadTitle(offer.PadId)} at {address}...";
        Log($"the hub offered the match of {offer.PadId}: {offer.Map} {LobbySetup.ModeName(offer.Mode)} on port {offer.Port}"
            + $" as {(offer.Spectator ? "a spectator" : offer.Team == 1 ? "a merc" : "a spy")}; joining it at {address}"
            + (offer.MasterId is null ? string.Empty : $" (master entry {offer.MasterId})"));
        if (offer.MasterId is { } matchId && Master.Client is { } master && !IsThisMachine(address))
        {
            // The match has its own master entry: asked like a listed server (its routes, the player's ticket), with
            // the hub's address to fall back on. A match hosted on this machine is joined at its own address: the
            // master's routes are the machine's outside addresses, which a hub hosted for loopback does not answer on.
            trip.MasterJoin = master.JoinAsync(matchId);
        }
        else
        {
            StartMatchJoin(trip, JoinTarget.FromAddress(address), null);
        }

        Touch();
    }

    /// <summary>OnlineService.JoinFromHub: the online visit to the match, on the offered side.</summary>
    private void StartMatchJoin(MatchTrip trip, JoinTarget target, string? ticket)
    {
        int preferred = trip.Offer.Spectator ? SessionProtocol.SpectatorTeam : trip.Offer.Team == 1 ? 1 : 0;
        OnlineBridge.Instance.Join(target, trip.Offer.Password, ticket, preferred, loopbackOnly: IsThisMachine(trip.Address));
        trip.Joined = true;
        trip.Last = OnlineClientState.Connecting;
        Log($"joining the match of {trip.Offer.PadId} through the online visit ({(ticket is null ? "with the match's token" : "with the master's ticket")})");
        Touch();
    }

    /// <summary>
    /// Follows the online visit to a pad's match (OnlineService.HubMatchState): back to the hub after the results
    /// (held <see cref="ResultsHoldSeconds"/>, longer while a demo download runs), when the match could not be
    /// joined, its connection ended, or the player left it.
    /// </summary>
    private void PumpMatch()
    {
        if (_trip is not { } trip)
        {
            return;
        }

        if (trip.MasterJoin is { IsCompleted: true } answer)
        {
            trip.MasterJoin = null;
            JoinTarget target = JoinTarget.FromAddress(trip.Address);
            string? ticket = null;
            if (answer.IsCompletedSuccessfully && answer.Result is { } join)
            {
                target = JoinTarget.FromJoin(join);
                ticket = join.Ticket is { Length: > 0 } t ? t : null;
            }
            else
            {
                Log("the master did not answer for the match; joining at " + trip.Address);
            }

            StartMatchJoin(trip, target, ticket);
        }

        if (!trip.Joined)
        {
            return;
        }

        if (OnlineBridge.Existing?.Client is not { } online)
        {
            // The online visit was ended from elsewhere (the match menu's leave button).
            ReturnFromMatch("You left the match.");
            return;
        }

        OnlineClientState state = online.State;
        if (state != trip.Last)
        {
            Log($"the match of {trip.Offer.PadId}: {trip.Last} -> {state}" + (state == OnlineClientState.Results && online.Results is { } r
                ? string.Create(CultureInfo.InvariantCulture, $" (results: {(r.WinningTeam == 0 ? "the spies won" : r.WinningTeam == 1 ? "the mercs won" : "no winner")}, {MatchResults.ReasonText(r.WinningReason)}, {r.Rows.Count} players)")
                : string.Empty));
            trip.Last = state;
            Touch();
        }

        long now = _clock.NowMicroseconds;
        switch (state)
        {
            case OnlineClientState.Loading or OnlineClientState.Match:
                trip.Played = true;
                break;
            case OnlineClientState.Results:
            case OnlineClientState.Lobby when trip.Played:
                if (trip.ResultsSince < 0)
                {
                    trip.ResultsSince = now;
                }

                if (state == OnlineClientState.Results && now - trip.ResultsSince < (long)(ResultsHoldSeconds * 1_000_000))
                {
                    break;
                }

                if (online.PendingDemoSave)
                {
                    if (trip.DemoWaitSince < 0)
                    {
                        trip.DemoWaitSince = now;
                        Log("the match's demo is still downloading: the way back waits for it (five minutes at most)");
                    }

                    if (now - trip.DemoWaitSince < OnlineClient.HubDemoWaitSeconds * 1_000_000L)
                    {
                        break;
                    }

                    ReturnFromMatch(online.PendingTrainingSave
                        ? "Replay saved. Its training data download did not finish within five minutes; back at the pads without the incomplete training file."
                        : "The demo download did not finish within five minutes. Back at the pads; the incomplete demo was not saved.");
                    break;
                }

                ReturnFromMatch("The match is over. Back at the pads." + (online.RecordDemos ? " " + online.DemoRecordingStatus : string.Empty));
                break;
            case OnlineClientState.Failed:
                ReturnFromMatch(trip.Played
                    ? "The match connection ended: " + (online.Error ?? "the server closed.")
                    : "Could not join the match: " + (online.Error ?? "no answer from its server."));
                break;
            case OnlineClientState.Left:
                ReturnFromMatch("You left the match.");
                break;
        }
    }

    /// <summary>
    /// Ends the online visit to the match and joins the hub again with the pad it came from (HubService.ReturnFromMatch):
    /// a listed hub is asked of the master again (a relay route does not outlive the match), any other at its address.
    /// </summary>
    private void ReturnFromMatch(string note)
    {
        if (_trip is not { } trip)
        {
            return;
        }

        _trip = null;
        OnlineBridge.Existing?.Leave();
        _returnPad = trip.Offer.PadId;
        SetNote(note);
        Log($"back to the hub from the match of {trip.Offer.PadId}: {note}");
        if (_masterId is not null && Master.Client is { } master)
        {
            _pendingJoin = master.JoinAsync(_masterId);
            _status = $"Asking the master server the way back to {_masterName}...";
            Touch();
            return;
        }

        if (_target.Length == 0)
        {
            Fail("The hub to go back to is not known.");
            return;
        }

        Connect(_target, _name, _team);
    }

    /// <summary>Ends a trip without going back (the visit is left): the online visit to the match is left too.</summary>
    private void EndTrip()
    {
        if (_trip is not null)
        {
            _trip = null;
            OnlineBridge.Existing?.Leave();
        }

        _returnPad = null;
        _padRun = null;
        CloseLaptop();
    }

    // ---- the unattended pad run (-ScctHubPad=<n>:spy|merc) --------------------------------------------------------

    /// <summary>
    /// Queues the host's own player on pad <paramref name="padNumber"/> as a spy (0) or a merc (1), the way a player
    /// does it, for a run nobody watches: once he stands in the hub he takes the side, walks (is placed) up to the
    /// pad's laptop and presses U there, the laptop's draft takes <paramref name="laptopOptions"/> (a lobby's options,
    /// "?TimeLimit=1?SvmBots=2") and is applied, then he is placed on the pad and readies up. The pad's countdown, the
    /// hub's offer and the trip to the match follow as for anyone. Only in a hub this PC hosts (the placing runs on its
    /// thread); false with the reason logged otherwise.
    /// </summary>
    public bool QueueOnPad(int padNumber, int side, string? laptopOptions)
    {
        if (Host is null)
        {
            Log("the unattended pad run needs a hub this PC hosts (-ScctHostHub)");
            return false;
        }

        _padRun = new PadRun(this, "Pad" + padNumber.ToString(CultureInfo.InvariantCulture), side == 1 ? 1 : 0, laptopOptions ?? string.Empty);
        Log($"unattended: queueing on {_padRun.PadId} as a {(side == 1 ? "merc" : "spy")}" + (string.IsNullOrEmpty(laptopOptions) ? string.Empty : $", laptop options {laptopOptions}"));
        return true;
    }

    /// <summary>The steps of the unattended pad run, one a frame from <see cref="Update"/>.</summary>
    private sealed class PadRun(HubBridge bridge, string padId, int side, string options)
    {
        private enum Step { WaitIn, Side, Laptop, Draft, Applied, Pad, Ready, Waiting, Done }

        private Step _step = Step.WaitIn;
        private long _since = -1;
        private long _tried = -1;
        private int _tries;

        public string PadId { get; } = padId;

        public void Sent() => _step = Step.Done;

        public void Tick(HubClient client)
        {
            long now = bridge._clock.NowMicroseconds;
            if (_since < 0)
            {
                _since = now;
            }

            double inStep = (now - _since) / 1_000_000.0;
            double sinceTry = _tried < 0 ? double.MaxValue : (now - _tried) / 1_000_000.0;
            switch (_step)
            {
                case Step.WaitIn when bridge.Round is not null && bridge._loadReported && client.LocalPlayer?.Pawn is not null && inStep > 4:
                    Next(client.Team == side ? Step.Laptop : Step.Side);
                    break;
                case Step.Side:
                    if (client.Team == side && inStep > 3)
                    {
                        Next(Step.Laptop);
                    }
                    else if (client.Team != side && sinceTry > 4)
                    {
                        Try();
                        client.RequestSide(side);
                        bridge.Log($"unattended: asking to play the {(side == 1 ? "mercs" : "spies")}");
                    }

                    break;
                case Step.Laptop:
                    if (bridge._laptopPad == PadId)
                    {
                        Next(Step.Draft);
                    }
                    else if (sinceTry > 3 && _tries < 6)
                    {
                        Try();
                        bridge.PlaceHostPlayer(client.PlayerId, PadId, atLaptop: true);
                        bridge.UseSoon(client, 0.6);
                    }
                    else if (_tries >= 6 && sinceTry > 3)
                    {
                        bridge.Log($"unattended: the laptop of {PadId} never opened; queueing with the pad's own match");
                        Next(Step.Pad);
                    }

                    break;
                case Step.Draft when inStep > 3:
                    if (options.Length > 0)
                    {
                        bridge.LaptopMerge(options);
                    }

                    bridge.ApplyLaptop();
                    Next(Step.Applied);
                    break;
                case Step.Applied:
                    if (client.PadSetups.TryGetValue(PadId, out (LobbySetup Setup, bool Changed) set) && set.Changed && inStep > 1.5)
                    {
                        bridge.Log($"unattended: {PadId} now plays {set.Setup.Describe()}");
                        bridge.CloseLaptop();
                        Next(Step.Pad);
                    }
                    else if (inStep > 10)
                    {
                        bridge.Log($"unattended: the laptop's settings were not taken ({bridge._laptopStatus}); queueing with the pad's own match");
                        bridge.CloseLaptop();
                        Next(Step.Pad);
                    }

                    break;
                case Step.Pad:
                    if (client.Queue.PadId is { } on && string.Equals(on, PadId, StringComparison.OrdinalIgnoreCase) && inStep > 2)
                    {
                        Next(Step.Ready);
                    }
                    else if (sinceTry > 3)
                    {
                        Try();
                        bridge.PlaceHostPlayer(client.PlayerId, PadId, atLaptop: false);
                    }

                    break;
                case Step.Ready:
                    if (client.Queue.Text.Contains("you are ready", StringComparison.Ordinal)
                        || (client.Pads.TryGetValue(PadId, out HubPadInfo? info) && info.State is HubPadState.Countdown or HubPadState.Starting))
                    {
                        bridge.Log($"unattended: ready on {PadId}: {client.Queue.Text}");
                        Next(Step.Waiting);
                    }
                    else if (sinceTry > 3)
                    {
                        Try();
                        client.Use();
                    }

                    break;
                case Step.Waiting:
                    if (inStep > 120)
                    {
                        bridge.Log($"unattended: no match offer 120 s after readying on {PadId} ({client.Queue.Text})");
                        Next(Step.Done);
                    }
                    else if (client.Pads.TryGetValue(PadId, out HubPadInfo? padNow) && padNow.State is HubPadState.Countdown or HubPadState.Starting)
                    {
                        // Counting down: nothing to do but wait for the offer.
                    }
                    else if (client.Queue.PadId is not { } queuedOn || !string.Equals(queuedOn, PadId, StringComparison.OrdinalIgnoreCase))
                    {
                        // Pushed off the pad (a wandering visitor, a respawn): back onto it and ready again.
                        bridge.Log($"unattended: no longer on {PadId}; placing again");
                        Next(Step.Pad);
                    }
                    else if (!client.Queue.Text.Contains("you are ready", StringComparison.Ordinal) && sinceTry > 4)
                    {
                        // The hub resets everyone's ready flag when the lineup or the match changes: ready up again.
                        Try();
                        client.Use();
                        bridge.Log($"unattended: readying again on {PadId} ({client.Queue.Text})");
                    }

                    break;
            }
        }

        private void Next(Step step)
        {
            _step = step;
            _since = -1;
            _tried = -1;
            _tries = 0;
        }

        private void Try()
        {
            _tried = bridge._clock.NowMicroseconds;
            _tries++;
        }
    }

    private long _useAt = -1;

    /// <summary>Presses U for the player after <paramref name="seconds"/> (the placing has to reach the hub's thread first).</summary>
    private void UseSoon(HubClient client, double seconds) => _useAt = _clock.NowMicroseconds + (long)(seconds * 1_000_000);

    /// <summary>Places the host's own player at a pad's laptop or in the middle of the pad, on the hub's thread.</summary>
    private void PlaceHostPlayer(int playerId, string padId, bool atLaptop)
    {
        if (Host is not { } host)
        {
            return;
        }

        host.Post(server =>
        {
            if (server.Pads?.Find(padId) is not { } pad)
            {
                Log($"unattended: the hub has no pad {padId}");
                return;
            }

            foreach (HubPlayer p in server.Players)
            {
                if (p.Slot.PlayerId == playerId && p.Slot.Controller?.Pawn is { bDeleteMe: false } pawn)
                {
                    // Beside the laptop (off the pad, so U opens the laptop and does not ready up), else the pad's middle;
                    // the first spot the pawn fits in (the laptop's desk and the pad's rails take some).
                    // Beside the laptop: rings round the desk (128 x 64), south side first, all inside the use range.
                    List<FVector> spots = [];
                    if (atLaptop)
                    {
                        foreach (float radius in new[] { 100f, 130f, 160f, 185f })
                        {
                            for (int step = 0; step < 16; step++)
                            {
                                double angle = -Math.PI / 2 + (step + 1) / 2 * (Math.PI / 8) * (step % 2 == 0 ? 1 : -1);
                                spots.Add(new FVector(radius * (float)Math.Cos(angle), radius * (float)Math.Sin(angle), 0f));
                            }
                        }
                    }
                    else
                    {
                        spots.AddRange([new(0f, 0f, 0f), new(0f, 120f, 0f), new(120f, 0f, 0f)]);
                    }
                    FVector origin = atLaptop ? pad.Area.Laptop : pad.Area.Centre;
                    foreach (FVector spot in spots)
                    {
                        FVector at = origin + spot + new FVector(0f, 0f, 90f);
                        if (pawn.SetLocation(at))
                        {
                            pawn.Velocity = default;
                            pawn.Acceleration = default;
                            Log(string.Create(CultureInfo.InvariantCulture, $"unattended: placed {p.Slot.Name} {(atLaptop ? "at the laptop of" : "on")} {padId} ({at.X:0}, {at.Y:0}, {at.Z:0})"));
                            return;
                        }
                    }

                    Log($"unattended: {p.Slot.Name} does not fit anywhere {(atLaptop ? "beside the laptop of" : "on")} {padId}");
                    return;
                }
            }
        });
    }

    /// <summary>The unattended run's step for this frame and a U press that was due.</summary>
    private void PumpPadRun(HubClient client)
    {
        if (_useAt >= 0 && _clock.NowMicroseconds >= _useAt)
        {
            _useAt = -1;
            client.Use();
        }

        _padRun?.Tick(client);
    }

    // ---- the pad laptop ---------------------------------------------------------------------------------------------

    private string? _laptopPad;
    private int _laptopSerial;
    private LobbySetup? _draft;
    private string _laptopStatus = string.Empty;

    /// <summary>The pad whose laptop is open, or null.</summary>
    public string? LaptopPad => _laptopPad;

    /// <summary>Goes up each time the hub opened a pad's laptop for the player.</summary>
    public int LaptopSerial => _laptopSerial;

    /// <summary>What the laptop says (applied, why not, waiting for the hub).</summary>
    public string LaptopStatus => _laptopStatus;

    /// <summary>The laptop's draft: the pad's next match as it will be applied, or null when no laptop is open.</summary>
    public LobbySetup? LaptopDraft => _draft;

    private void OnLaptopOpened(string padId)
    {
        if (Client is not { } client)
        {
            return;
        }

        _laptopPad = padId;
        _laptopSerial++;
        _draft = client.PadSetups.TryGetValue(padId, out (LobbySetup Setup, bool Changed) set)
            ? set.Setup
            : client.Pads.TryGetValue(padId, out HubPadInfo? info)
                ? LobbySetup.ForMap(info.Map, info.Mode, Catalog.Maps.GetMapSettings(info.Map), Math.Max(2, info.SpySlots + info.MercSlots))
                : null;
        _laptopStatus = _draft is null ? "The hub has not said this pad's match yet." : $"{PadTitle(padId)}: set its next match, then Apply.";
        Log($"the laptop of {padId} is open" + (_draft is null ? string.Empty : ": " + _draft.Describe()));
        Touch();
    }

    /// <summary>Closes the laptop (its draft is dropped).</summary>
    public void CloseLaptop()
    {
        if (_laptopPad is null)
        {
            return;
        }

        _laptopPad = null;
        _draft = null;
        _laptopStatus = string.Empty;
        Touch();
    }

    /// <summary>The maps the laptop offers: the versus maps installed here (the hub plays the same catalog), the draft's first.</summary>
    public IReadOnlyList<MapCatalogEntry> LaptopMaps()
    {
        MapCatalog maps = Catalog.Maps;
        return maps.GetHostMaps().Where(e => maps.FindMap(e.Id) is not null && e.PlayableModes.Any()).ToList();
    }

    /// <summary>Picks the draft's map (its mode stays when the map plays it, else the map's default mode).</summary>
    public bool SetLaptopMap(string mapId)
    {
        if (_draft is not null && !string.IsNullOrEmpty(mapId) && StartLaptopDownload(mapId))
        {
            return true;
        }

        if (_draft is not { } draft || Catalog.Maps.GetCatalogEntry(mapId) is not { } entry || !entry.PlayableModes.Any())
        {
            return false;
        }

        EGameMode mode = entry.PlayableModes.Contains(draft.Mode) ? draft.Mode : entry.DefaultMode;
        _draft = draft with { Map = entry.Id, Mode = mode };
        _laptopStatus = $"{entry.Name}, {LobbySetup.ModeName(mode)}. Apply to set it on {PadTitle(_laptopPad!)}.";
        Touch();
        return true;
    }

    /// <summary>The map's own settings for the draft's map and mode (bots and players stay).</summary>
    public bool LaptopDefaults()
    {
        if (_draft is not { } draft)
        {
            return false;
        }

        _draft = LobbySetup.ForMap(draft.Map, draft.Mode, Catalog.Maps.GetMapSettings(draft.Map), draft.MaxPlayers)
            with { Bots = draft.Bots, BotsPerSide = draft.BotsPerSide, BotSkill = draft.BotSkill };
        _laptopStatus = "The map's own settings.";
        Touch();
        return true;
    }

    /// <summary>Lays a lobby's options ("?TimeLimit=1?SvmBots=2") over the draft.</summary>
    public bool LaptopMerge(string options)
    {
        if (_draft is not { } draft || string.IsNullOrWhiteSpace(options))
        {
            return false;
        }

        _draft = LobbySetup.FromOptions(options.Trim(), draft);
        Log($"the laptop's draft takes {options.Trim()}: {_draft.Describe()}");
        Touch();
        return true;
    }

    /// <summary>Sends the draft to the hub (it checks the player stands at the laptop); the pad's sign and lines follow.</summary>
    public bool ApplyLaptop()
    {
        if (Client is not { } client || _laptopPad is not { } pad || _draft is not { } draft)
        {
            return false;
        }

        client.SetPadSetup(pad, draft);
        _laptopStatus = $"Sent to the hub: {draft.Describe()}";
        Log($"applied the laptop of {pad}: {draft.Describe()}");
        Touch();
        return true;
    }

    /// <summary>The draft's rows, in the order the laptop shows them.</summary>
    public IReadOnlyList<LaptopRow> LaptopRows()
    {
        if (_draft is not { } d)
        {
            return [];
        }

        List<EGameMode> modes = ModesOf(d.Map);
        bool mercsOnly = d.Mode == EGameMode.GM_MercDeathMatch;
        return
        [
            new("Mode", "Mode", modes.ConvertAll(m => LobbySetup.ModeName(m)), Math.Max(0, modes.IndexOf(d.Mode)), modes.Count > 1),
            new("TimeLimit", "Time limit", Range(0, LobbySetup.MaxTimeLimit, v => v == 0 ? "None" : v + " min"), d.TimeLimit, true),
            new("SpyLives", "Spy lives", Range(0, LobbySetup.MaxLives, v => v == 0 ? "Unlimited" : v.ToString(CultureInfo.InvariantCulture)), d.SpyLives, !mercsOnly),
            new("MercLives", "Merc lives", Range(0, LobbySetup.MaxLives, v => v == 0 ? "Unlimited" : v.ToString(CultureInfo.InvariantCulture)), d.MercLives, true),
            new("MaxPlayers", "Max players", Range(LobbySetup.MinMaxPlayers, LobbySetup.MaxMaxPlayers, v => v.ToString(CultureInfo.InvariantCulture)), d.MaxPlayers - LobbySetup.MinMaxPlayers, true),
            new("Bots", "Bots", ["Off", "Per side", "Fill empty slots"], Math.Clamp((int)d.Bots, 0, 2), true),
            new("BotsPerSide", "Bots per side", Range(1, LobbySetup.MaxMaxPlayers / 2, v => v.ToString(CultureInfo.InvariantCulture)), Math.Clamp(d.BotsPerSide, 1, LobbySetup.MaxMaxPlayers / 2) - 1, d.Bots == BotFill.PerSide),
            new("BotSkill", "Bot skill", ["Easy", "Normal", "Hard", "Expert"], Math.Clamp((int)d.BotSkill, 0, 3), d.Bots != BotFill.Off),
            new("Practice", "Practice", ["Off", "On"], d.Practice ? 1 : 0, true),
            new("Sidearms", "Sidearms only", ["Off", "On"], d.Sidearms ? 1 : 0, true),
            new("Rifles", "Rifles only", ["Off", "On"], d.Rifles ? 1 : 0, true),
        ];
    }

    /// <summary>Steps row <paramref name="row"/> of <see cref="LaptopRows"/> by <paramref name="delta"/> (it wraps round).</summary>
    public bool StepLaptopRow(int row, int delta)
    {
        IReadOnlyList<LaptopRow> rows = LaptopRows();
        if (_draft is not { } d || row < 0 || row >= rows.Count || !rows[row].Enabled || rows[row].Values.Count == 0 || delta == 0)
        {
            return false;
        }

        LaptopRow r = rows[row];
        int n = r.Values.Count;
        int i = ((r.Index + delta) % n + n) % n;
        _draft = r.Key switch
        {
            "Mode" => d with { Mode = ModesOf(d.Map)[i] },
            "TimeLimit" => d with { TimeLimit = i },
            "SpyLives" => d with { SpyLives = i },
            "MercLives" => d with { MercLives = i },
            "MaxPlayers" => d with { MaxPlayers = i + LobbySetup.MinMaxPlayers },
            "Bots" => d with { Bots = (BotFill)i },
            "BotsPerSide" => d with { BotsPerSide = i + 1 },
            "BotSkill" => d with { BotSkill = (OpenScct.Game.AI.AiSkill)i },
            "Practice" => d with { Practice = i == 1 },
            "Sidearms" => d with { Sidearms = i == 1 },
            "Rifles" => d with { Rifles = i == 1 },
            _ => d,
        };
        Touch();
        return true;
    }

    private List<EGameMode> ModesOf(string map) =>
        Catalog.Maps.GetCatalogEntry(map) is { } entry ? entry.PlayableModes.ToList() : [];

    private static List<string> Range(int from, int to, Func<int, string> name)
    {
        var values = new List<string>(to - from + 1);
        for (int v = from; v <= to; v++)
        {
            values.Add(name(v));
        }

        return values;
    }

    /// <summary>A row of the laptop: its key, what it says, its values and which is picked, whether it can change now.</summary>
    public sealed record LaptopRow(string Key, string Label, IReadOnlyList<string> Values, int Index, bool Enabled)
    {
        public string Value => Index >= 0 && Index < Values.Count ? Values[Index] : string.Empty;
    }

    // ---- the pads' looks ----------------------------------------------------------------------------------------------

    /// <summary>Map code prefix to the pad theme kit the hub map has for it (HubPadThemes; tools/hubmap/layout.py THEMES).</summary>
    private static readonly Dictionary<string, string> MapThemes = new(StringComparer.OrdinalIgnoreCase)
    {
        ["Pola"] = "polar", ["Stee"] = "steel", ["Fact"] = "steel", ["Ware"] = "steel", ["Aqua"] = "aquarius", ["Offs"] = "aquarius",
        ["Krau"] = "lab", ["Clar"] = "lab", ["Deft"] = "lab", ["Ziop"] = "lab", ["Muse"] = "museum", ["Bank"] = "museum",
        ["Pana"] = "museum", ["Cine"] = "museum", ["Club"] = "museum", ["Sanc"] = "museum", ["Sche"] = "museum",
    };

    /// <summary>The pad theme for a map code (PolaE gives polar); the neutral "hub" for any other map.</summary>
    public static string ThemeForMap(string? mapCode) =>
        mapCode is { Length: >= 4 } && MapThemes.TryGetValue(mapCode[..4], out string? theme) ? theme : "hub";

    /// <summary>The map list picture of a map installed here (a PNG path), or null.</summary>
    public string? PictureOf(string mapCode) =>
        Catalog.Maps.FindMap(mapCode) is null ? null : Catalog.Maps.GetCatalogEntry(mapCode)?.PreviewImage;

    /// <summary>The colour the hub gives a pad state (HubPads' colours).</summary>
    public static uint StateColour(HubPadState state) => state switch
    {
        HubPadState.Waiting => HubPads.WaitingColour,
        HubPadState.Countdown => HubPads.CountdownColour,
        HubPadState.Starting => HubPads.StartingColour,
        HubPadState.Off => HubPads.OffColour,
        _ => HubPads.IdleColour,
    };
}
