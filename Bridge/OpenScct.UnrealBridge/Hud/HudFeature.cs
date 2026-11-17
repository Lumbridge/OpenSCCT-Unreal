using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Net;
using OpenScct.Server;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>One console line of the HUD (chat and other TeamMessage texts) and the level time it leaves.</summary>
public readonly record struct HudConsoleLine(string Text, int Category, float Life);

/// <summary>
/// The HUD's console lines: what PlayerController.TeamMessage and ClientMessage hand to HUD.Message. Four lines at
/// most, each living max(0.09 s a character, 5 s). Ported from OpenSCCT's game/Scripts/Hud/HudConsole.cs without
/// its drawing (Engine.HUD.Message, AddTextMessage and DisplayMessages).
/// </summary>
public sealed class HudConsoleLines
{
    /// <summary>Engine.HUD's ConsoleMessageCount.</summary>
    public const int Lines = 4;

    /// <summary>Engine.LocalMessage's Lifetime, and AddTextMessage's seconds per character.</summary>
    public const int Lifetime = 5;
    public const float SecondsPerCharacter = 0.09f;

    private readonly List<HudConsoleLine> _lines = new();

    public IReadOnlyList<HudConsoleLine> Current => _lines;

    /// <summary>HUD.AddTextMessage: the first free of four slots, or the oldest goes. Returns the seconds it lives.</summary>
    public float Add(string text, int category, float now)
    {
        if (_lines.Count >= Lines)
        {
            _lines.RemoveAt(0);
        }

        float life = Math.Max((int)(text.Length * SecondsPerCharacter), Lifetime);
        _lines.Add(new HudConsoleLine(text, category, now + life));
        return life;
    }

    /// <summary>HUD.DisplayMessages drops one expired line a draw; here every expired line a tick.</summary>
    public void Prune(float now)
    {
        for (int i = _lines.Count - 1; i >= 0; i--)
        {
            if (_lines[i].Life < now)
            {
                _lines.RemoveAt(i);
            }
        }
    }

    public void Clear() => _lines.Clear();
}

/// <summary>
/// The HUD of the session's local player as data: one snapshot per simulation tick (state, round, gadgets,
/// interactions, objective markers, radar, scoreboard, the messages the original shows) and a queue of new messages
/// with their final text. Unreal reads it through <see cref="HudExports"/> and builds its own HUD from it.
/// <para>
/// It also does the part of the HUD that is not drawing but that the simulation leaves to the player's machine, as
/// OpenSCCT's Godot HUD layer does (game/Scripts/Hud/HudLayer.cs Step and HudReceiver.cs): it receives the server's
/// HUD calls (ReceiveLocalizedMessage, TeamMessage, ClientMessage, ClientNotifyNoise) and the chat lines, routes
/// messages through the player's SHud, runs the SHud message driver, the screen-effect slots and the merc's aim
/// point, once a tick on Unreal's game thread.
/// </para>
/// </summary>
public sealed class HudFeature : IDisposable
{
    public const int ApiVersion = 2;

    private static readonly Name NAME_s_Dead = new("s_Dead");
    private static readonly Name NAME_s_Binoculars_Def = new("s_Binoculars_Def");
    private static readonly Name NAME_s_MultiSpectating = new("s_MultiSpectating");
    private static readonly Name NAME_s_Spectating_Visitor = new("s_Spectating_Visitor");
    private static readonly Name NAME_s_MultiSpectating_WhenKilled = new("s_MultiSpectating_WhenKilled");

    private readonly BridgeSession _bridge;
    private readonly PlaySession _session;
    private readonly HudReader _reader;
    private readonly HudFrame _frame = new();
    private readonly HudEffectState _effects = new();
    private readonly HudConsoleLines _console = new();

    // What was on the SHud at the end of the last tick, to tell which entries are new.
    private readonly List<(string Text, float Started)> _seenSystem = new(), _seenState = new(), _seenInfo = new();
    private readonly List<string> _seenQueue = new();
    private string _seenReticle = "";

    private NetFunctionHandler? _fallback, _previousFallback;
    private bool _ended;
    private SPlayerController? _lastController;
    private SHud? _lastHud;
    private float _aspect = 16f / 9f;
    private bool _feet;
    private bool _showPlayers;

    // The camera of the tick the merc's aim point is projected through, and the projection itself, made once so the
    // tick does not allocate a new closure.
    private FVector _aimCamera;
    private FRotator _aimView;
    private float _aimFov;
    private readonly Func<FVector, (float X, float Y)?> _projectAim;

    private HudFeature(BridgeSession bridge)
    {
        _bridge = bridge;
        _session = bridge.Session;
        Strings = HudStrings.Load(SharedContent.FindRoot(_session.MapFolder));
        _reader = new HudReader(Texts, Strings);
        _projectAim = p => SHud.ProjectToCanvas(_aimCamera, _aimView, _aimFov, _aspect, p);
        InstallReceiver(_session.Client);

        // The late notifications are read from the client's own hook, not BridgeSession.BeforeTick: that one is raised
        // before the play session applies the player's exec commands (SwitchHeadset, Fire...), and what those raise is
        // dropped when the world steps. This hook is added after the play session's, so it runs after the commands,
        // right before the step, as the Godot client's PlayRoot.PostLateHudEvents does.
        _session.Client.BeforeWorldTick += OnBeforeWorldTick;
        bridge.Ticked += OnTicked;
    }

    /// <summary>The session's HUD, made on first use. Ask for it right after the session starts, before its first Update, so no early message is missed.</summary>
    public static HudFeature Of(BridgeSession session) => session.Feature(static s => new HudFeature(s));

    public HudTextTable Texts { get; } = new();

    public HudStrings Strings { get; }

    public HudMessageQueue Messages { get; } = new();

    /// <summary>The last tick's snapshot (the probe reads it directly).</summary>
    public HudFrame Frame => _frame;

    /// <summary>Server HUD calls and chat lines received so far.</summary>
    public int Received { get; private set; }

    /// <summary>
    /// Whether the last tick ran the laser sight's target name pass (SHud.DrawTargetName): the motion tracking draw
    /// (<see cref="HudMotionExports"/>) draws the HUD's own tracker only then, as the Godot client's HudLayer does.
    /// </summary>
    public bool TargetNameRan { get; private set; }

    /// <summary>
    /// The view's width over its height, for the merc's aim point (SHud.SpotReticule is projected through the camera
    /// onto the 640 x 480 canvas). 16:9 until set.
    /// </summary>
    public void SetViewport(int width, int height)
    {
        if (width > 0 && height > 0)
        {
            _aspect = (float)width / height;
        }
    }

    /// <summary>The profile's bInFeet: marker distances in feet instead of metres (SHud.HudUnit). Applies at once.</summary>
    public void SetDistanceInFeet(bool feet)
    {
        _feet = feet;
        _lastHud?.HudUnit(feet);
    }

    /// <summary>
    /// SHud.bShowConnectedPlayers: the player's "show players" toggle, kept on the SHud as the original does. It is
    /// remembered, so it also holds for an SHud that arrives later (set before the player has a HUD, or a new HUD
    /// after a map change), as Reloaded keeps it.
    /// </summary>
    public void SetShowPlayers(bool show)
    {
        _showPlayers = show;
        if (_lastHud is { } hud)
        {
            hud.bShowConnectedPlayers = show;
        }
    }

    /// <summary>
    /// SPlayerController.SwitchEnhancedReality (the F9 exec, which only touches the local HUD, so the simulation's
    /// command list does not run it): the HUD's Enhanced Reality level flips between 0 and 1; a spy gets "Display
    /// off" / "Display all objective positions" on the stack, a merc "Display intrusions" / "Display all objectives
    /// and intrusions" on the state area. Dead and spectating controllers ignore it. Ported from
    /// game/Scripts/Hud/HudCommands.cs. The new level shows in the next tick's EnhancedReality flag, and the line
    /// arrives as a message.
    /// </summary>
    public void SwitchEnhancedReality()
    {
        if (Controller is not { myHUD: SHud hud } pc || pc.IsInState(NAME_s_Dead) || pc.IsSpectating())
        {
            return;
        }

        hud.EnhancedRealityLevel = hud.EnhancedRealityLevel >= 1 ? 0 : hud.EnhancedRealityLevel + 1;
        bool spy = (pc.SBPawn?.SPawnType ?? pc.SPawnType) != PawnType.SPAWNDEFENSE;
        if (spy)
        {
            HudMessageRules.AddMessageToStack(hud, hud.EnhancedRealityLevel == 0 ? pc.EROff : pc.ERAllObjectives, 3, 0);
        }
        else
        {
            HudMessageRules.AddMessageToAuxiliaryStack(hud, hud.EnhancedRealityLevel == 0 ? pc.ERAlarmsOnly : pc.ERAlarmsAndObjectives, 3, 0);
        }
    }

    // ---- reading (Unreal's frame) ------------------------------------------------------------------------------

    public bool GetState(out HudState state)
    {
        state = _frame.State;
        return state.Valid != 0;
    }

    public bool GetRound(out HudRound round)
    {
        round = _frame.Round;
        return round.Valid != 0;
    }

    /// <summary>
    /// Moves up to <paramref name="target"/>.Length new messages out of the queue, oldest first. Returns how many were
    /// written; call again while it fills the buffer.
    /// </summary>
    public unsafe int DrainMessages(Span<HudMessage> target)
    {
        int count = 0;
        while (count < target.Length && Messages.TryTake(out HudPendingMessage m))
        {
            ref HudMessage o = ref target[count++];
            o.Category = m.Category;
            o.Kind = m.Kind;
            o.Cause = m.Cause;
            o.Trigger = m.Trigger;
            o.Flags = m.Flags;
            o.Switch = m.Switch;
            o.Time = m.Time;
            o.Duration = m.Duration;
            o.Priority = m.Priority;
            o.ActorPlayerId = m.Actor.PlayerId;
            o.ActorTeam = m.Actor.Team;
            o.ActorNameId = Texts.Id(m.Actor.Name);
            o.VictimPlayerId = m.Victim.PlayerId;
            o.VictimTeam = m.Victim.Team;
            o.VictimNameId = Texts.Id(m.Victim.Name);
            o.TriggeredByPlayerId = m.TriggeredBy.PlayerId;
            o.TriggeredByTeam = m.TriggeredBy.Team;
            o.TriggeredByNameId = Texts.Id(m.TriggeredBy.Name);
            string text = m.Text ?? "";
            int length = Math.Min(text.Length, HudMessage.TextCapacity - 1);
            if (length > 0 && length < text.Length && char.IsHighSurrogate(text[length - 1]))
            {
                // Never cut a character in two.
                length--;
            }

            fixed (char* chars = o.Text)
            {
                text.AsSpan(0, length).CopyTo(new Span<char>(chars, length));
                chars[length] = '\0';
            }
        }

        return count;
    }

    // ---- receiving (ported from game/Scripts/Hud/HudReceiver.cs) -----------------------------------------------

    /// <summary>
    /// Puts the HUD behind the client's received-call fallback (the calls no port of the simulation runs), passing
    /// everything that is not a HUD call on to the fallback that was there before, and listens to the chat lines of
    /// the control channel.
    /// </summary>
    private void InstallReceiver(ClientSession client)
    {
        NetFunctionHandler? previous = client.Driver.Functions.Fallback;
        _previousFallback = previous;
        _fallback = (self, function, args, from) =>
        {
            if (!_ended && !client.Driver.IsServer && self is PlayerController pc && Handle(pc, NetGameHost.ToGameCall(function, args)))
            {
                return;
            }

            previous?.Invoke(self, function, args, from);
        };
        client.Driver.Functions.Fallback = _fallback;
        client.ChatReceived += OnChat;
    }

    /// <summary>
    /// The session ended (BridgeSession disposes its features). A remote round's connection lives on after its
    /// round, through the results and to the next map, so the HUD takes itself off it: the tick and chat hooks go,
    /// and the received-call fallback is put back as it was (when another wrapper was put over this one since, this
    /// one only passes calls on from now).
    /// </summary>
    public void Dispose()
    {
        if (_ended)
        {
            return;
        }

        _ended = true;
        ClientSession client = _session.Client;
        client.BeforeWorldTick -= OnBeforeWorldTick;
        client.ChatReceived -= OnChat;
        _bridge.Ticked -= OnTicked;
        if (ReferenceEquals(client.Driver.Functions.Fallback, _fallback))
        {
            client.Driver.Functions.Fallback = _previousFallback;
        }
    }

    /// <summary>A call for <paramref name="pc"/>; false when it is not a HUD call.</summary>
    private bool Handle(PlayerController pc, in GameCall call)
    {
        switch (call.Function.ToString())
        {
            case "ReceiveLocalizedMessage":
                Received++;
                HudMessageRules.ReceiveLocalizedMessage(pc, call.Class.Path ?? HudMessageRules.GameMessagesClass, call.Int, call.Pri1,
                    call.Pri2, call.Object ?? call.Actor, Messages);
                return true;
            case "TeamMessage":
                Received++;
                TeamMessage(pc, call.Pri1, call.Text ?? "", call.Name1.IsNone ? "" : call.Name1.ToString());
                return true;
            case "ClientMessage":
                // PlayerController.ClientMessage: a TeamMessage from the own PRI, typed Event when untyped.
                Received++;
                TeamMessage(pc, pc.PlayerReplicationInfo, call.Text ?? "", call.Name1.IsNone ? "Event" : call.Name1.ToString());
                return true;
            case "ClientNotifyNoise":
                // The sound arrows: the simulation's port sets them on this machine's HUD.
                Received++;
                pc.ClientNotifyNoise(call.Actor ?? call.Object as Actor, call.Float1);
                return true;
            default:
                return false;
        }
    }

    /// <summary>
    /// PlayerController.TeamMessage, then HUD.Message: Say and TeamSay lines get "Name: " in front and go to the
    /// console lines; every line is also a new message for Unreal, with its sender.
    /// </summary>
    private void TeamMessage(PlayerController pc, PlayerReplicationInfo? pri, string text, string type)
    {
        bool player = type is "Say" or "TeamSay";
        int category = type == "Say" ? HudMessageCategory.Chat : type == "TeamSay" ? HudMessageCategory.TeamChat : HudMessageCategory.Console;
        AddConsole(category, text, player && pri is not null ? new HudParticipant(pri.PlayerID, pri.Team?.TeamIndex ?? -1, pri.PlayerName) : HudParticipant.None,
            pc.Level.TimeSeconds);
    }

    /// <summary>A relayed chat line: the TeamMessage of the original, with the sender's name.</summary>
    private void OnChat(ChatLine line)
    {
        Received++;
        float now = _session.LocalController?.Level.TimeSeconds ?? _session.Client.World.TimeSeconds;
        AddConsole(line.Scope == EChatScope.TeamSay ? HudMessageCategory.TeamChat : HudMessageCategory.Chat, line.Text,
            new HudParticipant(line.SenderId, line.SenderTeam, line.SenderName), now);
    }

    private void AddConsole(int category, string text, HudParticipant sender, float now)
    {
        // HUD.Message: "Name: text" for a player's line.
        string shown = sender.Name.Length > 0 ? sender.Name + ": " + text : text;
        float life = _console.Add(shown, category, now);
        HudPendingMessage message = HudPendingMessage.Plain(category, text, now, life, 0);
        message.Actor = sender;
        Messages.Add(message);
    }

    // ---- the tick ----------------------------------------------------------------------------------------------

    private SPlayerController? Controller => _session.LocalController is { bDeleteMe: false } pc ? pc : null;

    /// <summary>
    /// Right before each client world step: the notifications raised since the last tick ended (a server call that
    /// arrived in between, such as a ClientDamage's HUD effect, or one of the player's exec commands, such as a
    /// goggle switch's message) are still in the world's buffer; the step drops them.
    /// </summary>
    private void OnBeforeWorldTick(ClientSession client)
    {
        if (Controller is { } pc && ReferenceEquals(pc.XLevel.World, client.World))
        {
            _effects.Take(pc, client.World.Events.Late, pc.Level.TimeSeconds);
        }
    }

    /// <summary>After each client tick: the HUD's own per-frame work, then the snapshot.</summary>
    private void OnTicked()
    {
        SPlayerController? pc = Controller;
        if (!ReferenceEquals(pc, _lastController))
        {
            // A new local player starts with a clean HUD.
            _lastController = pc;
            _lastHud = null;
            _effects.Reset();
            _console.Clear();
            ForgetSeen();
        }

        _frame.Clear();
        if (pc is null)
        {
            return;
        }

        float now = pc.Level.TimeSeconds;
        HudHitZones.Ensure(pc.XLevel);
        var shud = pc.myHUD as SHud;
        if (shud is not null && !ReferenceEquals(shud, _lastHud))
        {
            // The HUD side of SHud.PostNetBeginPlay for a new SHud: Enhanced Reality on (level 1), the distance unit
            // of the profile, and the connected players list off.
            _lastHud = shud;
            shud.EnhancedRealityLevel = 1;
            shud.HudUnit(_feet);
            shud.bShowConnectedPlayers = _showPlayers;
            ForgetSeen();
        }

        // The tick's own notifications: everything in the buffer but what was raised after the tick ended (that part
        // is read before the next tick).
        SimEvents events = pc.XLevel.World.Events;
        ReadOnlySpan<SimEvent> current = events.Current;
        _effects.Take(pc, current.Slice(0, current.Length - events.Late.Length), now);
        _effects.Update(pc, now);
        _console.Prune(now);
        bool aimRan = false, targetName = false;
        TargetNameRan = false;
        if (shud is not null)
        {
            HudMessageRules.Tick(shud, shud.sTutoMap.Length > 4);

            // SHud.PostRender's spectator branch: while the player spectates a pawn the alarm list is emptied.
            if (pc.ViewTarget is SBasePawn && pc.IsSpectating() && pc.Level.GRI is { ServerOnly: false })
            {
                shud.EmptyAlarmsList();
            }

            aimRan = UpdateMercAim(pc, shud);

            // The laser sight's target name (SHud.DrawTargetName), once a HUD frame where the Godot client's
            // HudLayer.Step runs it: on the merc's own page (not scoped, not shut down by an EMP) when the aim point
            // helper got past its gates, and in the camera network view while the camera works. The lock holds the
            // target and names it in MyTargetName; then the first expired lock leaves the HUD's tracker. Without
            // the expiry a lock would stay for good, and looking at the same player again would never restart the
            // name or the "teammate in sight" sound.
            bool camNetwork = GadgetHudRules.ViewOf(pc) == GadgetView.CamNetwork;
            targetName = camNetwork
                ? pc.SBPawn?.UsedObject is not SCamNetwork { IsShotDisabled: true }
                : aimRan && !_effects.ShutDown && pc.GetStateName() != NAME_s_Binoculars_Def && shud.TargetNameInAimHelper();
            if (targetName)
            {
                shud.TargetNameLocks();
                shud.ExpireTargetLocks();
            }

            TargetNameRan = targetName;

            EmitNewMessages(shud, now);
        }

        _reader.Read(pc, _frame, _effects, _console, aimRan, targetName);
    }

    /// <summary>
    /// The merc's aim point for the reticle, what the global SHud.PostRender does before the merc page is drawn
    /// (ported from game/Scripts/Hud/HudMercAim.cs): P follows the view target; a merc's HUD that is not hidden,
    /// outside the spectating states, runs the aim point helper through the controller's calc view of the tick.
    /// True when it ran.
    /// </summary>
    private bool UpdateMercAim(SPlayerController pc, SHud hud)
    {
        if (pc.ViewTarget is SBasePawn viewed)
        {
            hud.P = viewed;
        }

        Name state = pc.GetStateName();
        if (hud.P is not { SPawnType: PawnType.SPAWNDEFENSE } || hud.bHideHUD || pc.bCinematicHideHud || pc.FreeCameraFlying != 0 ||
            state == NAME_s_MultiSpectating || state == NAME_s_Spectating_Visitor || state == NAME_s_MultiSpectating_WhenKilled)
        {
            return false;
        }

        // PostRender's merc colour, which the target name writes enemies in.
        (hud.Hud_R, hud.Hud_G, hud.Hud_B) = SHud.MercHudColour;
        _aimCamera = pc.vCamLocation;
        _aimView = pc.rCamRotation;
        _aimFov = pc.FovAngle;
        hud.UpdateSpotReticule(_projectAim);
        return true;
    }

    // ---- new messages off the SHud -----------------------------------------------------------------------------

    private void ForgetSeen()
    {
        _seenSystem.Clear();
        _seenState.Clear();
        _seenInfo.Clear();
        _seenQueue.Clear();
        _seenReticle = "";
    }

    /// <summary>
    /// Queues a message for every entry that appeared on the SHud since the last tick. The routing of the server's
    /// messages and the simulation's own code (gadgets, zones, the goggles) both write to the SHud, so comparing it
    /// tick to tick catches them all, once each.
    /// </summary>
    private void EmitNewMessages(SHud hud, float now)
    {
        EmitStack(hud.MessageStack, _seenSystem, HudMessageCategory.System, now);
        EmitStack(hud.AuxiliaryMessageStack, _seenState, HudMessageCategory.State, now);
        EmitStack(hud.InfoMessageStack, _seenInfo, HudMessageCategory.Info, now);

        // The game message queue holds each text once; the original shows its head for GameMessageDisplayTime.
        foreach (FStackedMessage m in hud.GameMessageQueue)
        {
            if (!string.IsNullOrWhiteSpace(m.Message) && !_seenQueue.Contains(m.Message))
            {
                Messages.Add(HudPendingMessage.Plain(HudMessageCategory.Game, m.Message, now, hud.GameMessageDisplayTime, m.prio));
            }
        }

        _seenQueue.Clear();
        foreach (FStackedMessage m in hud.GameMessageQueue)
        {
            _seenQueue.Add(m.Message ?? "");
        }

        FStackedMessage reticle = hud.ReticuleMessage;
        string reticleText = reticle.StartedTime + reticle.StayInStackTime > now ? reticle.Message ?? "" : "";
        if (reticleText.Length > 0 && reticleText != _seenReticle)
        {
            Messages.Add(HudPendingMessage.Plain(HudMessageCategory.Reticle, reticleText, now,
                reticle.StartedTime + reticle.StayInStackTime - now, reticle.prio));
        }

        _seenReticle = reticleText;
    }

    private void EmitStack(List<FStackedMessage> stack, List<(string Text, float Started)> seen, int category, float now)
    {
        foreach (FStackedMessage m in stack)
        {
            // " " is the original's blank spacer line, not a message.
            if (!string.IsNullOrWhiteSpace(m.Message) && !seen.Contains((m.Message, m.StartedTime)))
            {
                Messages.Add(HudPendingMessage.Plain(category, m.Message, now, Math.Max(0f, m.StartedTime + m.StayInStackTime - now), m.prio));
            }
        }

        seen.Clear();
        foreach (FStackedMessage m in stack)
        {
            seen.Add((m.Message ?? "", m.StartedTime));
        }
    }
}
