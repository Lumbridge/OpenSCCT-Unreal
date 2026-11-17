using System;
using System.Collections.Generic;
using System.Globalization;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Combat;
using OpenScct.Game.Objectives;
using OpenScct.Game.SBase;
using CombatK = OpenScct.Game.Combat.K;
using ObjectiveK = OpenScct.Game.Objectives.K;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// The receiving side of the localized messages on a client, ported from OpenSCCT's
/// game/Scripts/Hud/HudMessages.cs (which is Godot-free but lives in the Godot project): the text of a message is
/// built on the receiving machine from the replicated PRIs and object, then SHud routes it to the game message queue
/// or one of its stacks. These are the original's SGameMessages.GetString, SDeathMessage.GetString,
/// Engine.GameMessage.GetString and SHud.LocalizedMessage / AddMessageToStack / AddMessageToAuxiliaryStack /
/// AddHudNumberMessageToStack.
/// <para>
/// Every text comes from the class defaults of the player's content (UcText fills them: SGameMessages, SDeathMessage,
/// GameMessage, SHud, the objective and alarm actors of the map). The few English strings below are OpenSCCT's own
/// wording for things the original has no text for (the PS2 tube objective, the last life notice, self-elimination),
/// kept as OpenSCCT has them.
/// </para>
/// </summary>
public static class HudMessageRules
{
    public const string GameMessagesClass = "SBase.SGameMessages";
    public const string DeathMessageClass = "SBase.SDeathMessage";
    public const string EngineGameMessageClass = "Engine.GameMessage";

    /// <summary>
    /// SHud.LocalizedMessage: these SGameMessages switches go to the game message queue with priority 2; every other
    /// switch goes to the stack.
    /// </summary>
    private static readonly int[] GameQueueSwitches = [0, 2, 3, 4, 5, 6, 7, 15, 16, 17, 20, 53, 54, 55];

    /// <summary>Seconds a stack message stays by default, and the "hacking in progress" line (switch 39).</summary>
    public const int DefaultStackTime = 10;
    public const int HackingStackTime = 4;

    public const byte GameMessagePriority = 2;
    public const byte OtherMessagePriority = 1;

    /// <summary>An alarm's line stays its Duration when that is under 500 s, else this long; 1 s when the alarm went off.</summary>
    public const int AlarmLongTime = 5;

    /// <summary>Seconds a kill feed line stays in the Godot client's feed (HudActivityFeed.Lifetime): the Duration of activity messages.</summary>
    public const float ActivityLifetime = 6f;

    // Made on first use, after the content is configured, so their defaults hold the player's text.
    private static SGameMessages? s_texts;
    private static SDeathMessage? s_deathTexts;
    private static GameMessage? s_engineTexts;

    private static SGameMessages Texts => s_texts ??= new SGameMessages();

    private static SDeathMessage DeathTexts => s_deathTexts ??= new SDeathMessage();

    private static GameMessage EngineTexts => s_engineTexts ??= new GameMessage();

    // ---- texts ---------------------------------------------------------------------------------------------

    /// <summary>
    /// SGameMessages.GetString: the text the receiving machine builds. A reference that did not arrive (not relevant
    /// to this client) leaves its part empty.
    /// </summary>
    public static string GetString(int Switch, PlayerReplicationInfo? pri1, PlayerReplicationInfo? pri2, UObject? obj)
    {
        string trigger = (obj as SObjectiveTrigger)?.TriggerName ?? "";
        string objective = (obj as SObjectiveTrigger)?.Objective?.ObjectiveName ?? "";
        string name1 = pri1?.PlayerName ?? "";
        string name2 = pri2?.PlayerName ?? "";
        // The original PS2 game calls this objective a tube; the PC disk texts come from the player's content.
        bool ps2Flag = obj is SFlag { bPS2Objective: true };
        return Switch switch
        {
            1 or 40 => AlarmText(obj as SAlarm, longIntruder: Switch == 1),
            2 => Texts.MatchRestart,
            ObjectiveK.MsgMissionSpy => (obj as SObjective)?.Description ?? "",
            4 => Texts.Win,
            5 => Texts.Lose,
            7 => obj is LevelInfo li
                ? Texts.Score + Texts.Spies + li.NbPointsAttack.ToString(CultureInfo.InvariantCulture) + Texts.Sentries +
                    li.NbPointsDefense.ToString(CultureInfo.InvariantCulture)
                : "",
            ObjectiveK.MsgDataStolen => Join(trigger, ps2Flag ? ": Tube stolen" : Texts.FlagTaken),
            ObjectiveK.MsgDataDropped => Join(trigger, ps2Flag ? ": Tube dropped" : Texts.FlagDropped),
            ObjectiveK.MsgDataSecured => Join(trigger, ps2Flag ? ": Tube secured" : Texts.FlagRetaken),
            13 => Texts.EnemyLocked,
            14 => Texts.SpyOff,
            ObjectiveK.MsgMissionMerc => (obj as SObjective)?.DescriptionDEF ?? "",
            16 => Join(Join(name1, Texts.DropAttack1), name2),
            17 => Join(Join(name2, Texts.ElbowShock1), name1) + Texts.ElbowShock2,
            18 => Join(Join(name1, Texts.Grabbed1), name2) + Texts.Grabbed2,
            ObjectiveK.MsgWarnHacking => (obj as SInterrupteur)?.sWarnHackingText ?? "",
            20 => Join(name2, Texts.GrabbedAssom),
            21 => Texts.GPOChaffed,
            ObjectiveK.MsgDataCaptured => Join(trigger, ps2Flag ? ": Tube captured" : Texts.FlagCaptured),
            // Terminal hacking shares an entry of the original whose text says "Decontamination"; OpenSCCT words it.
            ObjectiveK.MsgInterrupted => "Hacking interrupted",
            24 => obj is SAlarm alarm ? alarm.AlarmDescriptionSpy : Texts.AlarmActive,
            25 => Texts.UnlockingAccess,
            26 => Texts.AccessUnlocked,
            27 => Join(objective, Texts.BombUnderAttack),
            28 => Join(objective, Texts.BombSecured),
            29 => Join(objective, Texts.BombNeutralized),
            ObjectiveK.MsgNeutralizationInProgress => Join(trigger, Texts.NeutralizeUnderAttack),
            ObjectiveK.MsgNeutralized => Join(objective, Texts.NeutralizeTaken),
            ObjectiveK.MsgHackingInProgress => Texts.DoorTriggered,
            41 => Texts.NoStickyCamAvailable,
            50 => Join(name2, Texts.GrabbedTimeElapsed),
            51 => Texts.PresDetectorActivated,
            52 => Texts.SpyTrapActivated,
            ObjectiveK.MsgGoToDropZone => Texts.ExtractionAtt,
            ObjectiveK.MsgRetrieveHardDrive => Texts.ExtractionDef,
            55 => Texts.SabotageNoModem,
            56 => Join(name1, "used " + PlayerPronouns.For(pri1?.SvmPronouns).Possessive + " last life"),
            57 => Texts.CounterGrabWin,
            58 => Texts.CounterGrabLose,
            60 => Texts.SpyLockedByDef,
            61 => Texts.SpyUnLockedByDef,
            253 => Join(Join(Texts.Intruder, "-"), Texts.IntruderArea),
            254 => Texts.Intruder,
            255 => " ",
            _ => "",
        };
    }

    /// <summary>The alarm texts of switches 1 and 40 by the alarm's message priority.</summary>
    private static string AlarmText(SAlarm? alarm, bool longIntruder)
    {
        if (alarm is null)
        {
            return Texts.AlarmActive;
        }

        return alarm.MessagePriority switch
        {
            0 or 1 => alarm.AlarmDescription,
            2 or 3 => Join(Join(alarm.AlarmDescription, ":"), Texts.Intruder),
            4 or 5 => Join(Join(alarm.AlarmDescription, ":"), longIntruder ? Texts.IntruderArea : Texts.Intruder),
            _ => "",
        };
    }

    /// <summary>OpenSCCT's own: a kill line's name with the player's level tag before it ("(P3 42) Name"); guests as they are.</summary>
    private static string Ranked(PlayerReplicationInfo pri) =>
        OpenScct.Net.Cosmetics.PlayerRank.Unpack(pri.SvmRank) is { IsNone: false } rank ? "(" + rank.Tag + ") " + pri.PlayerName : pri.PlayerName;

    /// <summary>SDeathMessage.GetString: the kill line ("Killer killed Victim", "Victim has resigned...").</summary>
    public static string DeathString(int Switch, PlayerReplicationInfo? killer, PlayerReplicationInfo? victim)
    {
        Switch = CombatActivityDeath.LegacySwitch(Switch);
        string victimName = victim is not null ? Ranked(victim) : DeathTexts.SomeoneString;
        string killerName = killer is not null ? Ranked(killer) : DeathTexts.SomeoneString;
        List<string> words = DeathTexts.DeathString;
        string Word(int i) => i >= 0 && i < words.Count ? words[i] : "";
        switch (Switch)
        {
            case 18:
                return Join(victimName, Word(17));
            case 19:
                return Join(Join(killerName, DeathTexts.Grabbed1), victimName) + DeathTexts.Grabbed2;
            case 23:
                return Join(Join(killerName, DeathTexts.SnipedString), victimName);
            case 24:
                return Join(victimName, DeathTexts.ResignedString);
        }

        if (killer is not null && ReferenceEquals(killer, victim))
        {
            return Join(victimName, "eliminated " + PlayerPronouns.For(victim?.SvmPronouns).Reflexive);
        }

        return Join(Join(killerName, Word(Switch < 18 ? Switch : 5)), victimName);
    }

    /// <summary>Engine.GameMessage.GetString: entered (1) and left (4) the game.</summary>
    public static string EngineGameString(int Switch, PlayerReplicationInfo? pri1) => Switch switch
    {
        1 => pri1 is null || pri1.PlayerName.Length == 0 ? EngineTexts.NewPlayerMessage : Join(pri1.PlayerName, EngineTexts.EnteredMessage),
        4 => pri1 is null || pri1.PlayerName.Length == 0 ? "" : Join(pri1.PlayerName, EngineTexts.LeftMessage),
        _ => "",
    };

    /// <summary>The text of a localized message of any class the game sends.</summary>
    public static string TextOf(string messageClass, int Switch, PlayerReplicationInfo? pri1, PlayerReplicationInfo? pri2, UObject? obj) =>
        IsClass(messageClass, GameMessagesClass) ? GetString(Switch, pri1, pri2, obj)
        : IsClass(messageClass, DeathMessageClass) ? DeathString(Switch, pri1, pri2)
        : IsClass(messageClass, EngineGameMessageClass) ? EngineGameString(Switch, pri1)
        : "";

    /// <summary>UnrealScript's <c>@</c>: the two strings joined by one space.</summary>
    private static string Join(string a, string b) => a + " " + b;

    public static bool IsClass(string path, string expected) =>
        string.Equals(path, expected, StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(expected[expected.IndexOf('.', StringComparison.Ordinal)..], StringComparison.OrdinalIgnoreCase);

    // ---- routing -------------------------------------------------------------------------------------------

    /// <summary>
    /// <c>ReceiveLocalizedMessage</c> on the receiving controller for any message class
    /// (LocalMessage.ClientReceive, then SHud.LocalizedMessage): match activity (kills, knock-outs, joins, the match
    /// result) goes to <paramref name="queue"/> as an activity message, as the Godot client's activity feed takes it;
    /// SGameMessages go to the game queue or the stack (alarms with their own time and priority); a teammate's death
    /// would go to the queue with priority 2 and anything else with priority 1. What lands on the SHud is picked up
    /// from there (HudFeature compares the stacks each tick), so messages the simulation adds itself are seen the
    /// same way.
    /// </summary>
    public static void ReceiveLocalizedMessage(PlayerController receiver, string messageClass, int Switch,
        PlayerReplicationInfo? pri1, PlayerReplicationInfo? pri2, UObject? obj, HudMessageQueue queue)
    {
        ArgumentNullException.ThrowIfNull(receiver);
        ArgumentNullException.ThrowIfNull(queue);
        if (receiver.myHUD is not SHud hud)
        {
            return;
        }

        string text = TextOf(messageClass, Switch, pri1, pri2, obj);

        // The message's jingles and sounds (the simulation raises them as sound events).
        hud.LocalizedMessageSounds(messageClass, Switch, pri1, pri2, obj);
        PlayerReplicationInfo? own = hud.PlayerOwner?.PlayerReplicationInfo ?? hud.P?.Controller?.PlayerReplicationInfo;
        if (HudActivity.TryCreate(messageClass, Switch, pri1, pri2, obj, own, hud.Level.TimeSeconds, text, out HudPendingMessage activity))
        {
            queue.Add(activity);
            return;
        }

        if (IsClass(messageClass, GameMessagesClass))
        {
            if (Array.IndexOf(GameQueueSwitches, Switch) >= 0)
            {
                hud.SetGameMessage(text, GameMessagePriority);
                return;
            }

            int time;
            byte prio;
            if (obj is SAlarm alarm)
            {
                time = alarm.bAlarmOn ? (alarm.Duration < 500 ? alarm.Duration : AlarmLongTime) : 1;
                prio = alarm.MessagePriority;
                if (hud.Level.TimeSeconds > hud.AlarmHighlightTime)
                {
                    hud.AlarmHighlightTime = hud.Level.TimeSeconds + hud.AlarmListBlinkingTime;
                }
            }
            else if (Switch == ObjectiveK.MsgHackingInProgress)
            {
                time = HackingStackTime;
                prio = 0;
            }
            else
            {
                time = DefaultStackTime;
                prio = 0;
            }

            AddMessageToStack(hud, text, time, prio);
            if (Switch == ObjectiveK.MsgInterrupted)
            {
                AddMessageToStack(hud, " ", 1, 0);
            }

            return;
        }

        if (IsClass(messageClass, DeathMessageClass) && pri2 is not null && IsLocalTeamDeath(hud, pri2))
        {
            hud.SetGameMessage(text, GameMessagePriority);
            return;
        }

        hud.SetGameMessage(text, OtherMessagePriority);
    }

    /// <summary>The victim is on the local player's team and is not the local player.</summary>
    private static bool IsLocalTeamDeath(SHud hud, PlayerReplicationInfo victim)
    {
        PlayerReplicationInfo? own = (hud.P?.Controller ?? hud.PlayerOwner)?.PlayerReplicationInfo;
        int victimTeam = victim.Team?.TeamIndex ?? 0;
        int ownTeam = own?.Team?.TeamIndex ?? 0;
        return victimTeam == ownTeam && !ReferenceEquals(victim, own);
    }

    /// <summary>
    /// SHud.AddMessageToStack: empty texts are ignored; a text already on the stack stays
    /// max(time, MessageMustStayTime) from now; otherwise it is appended, and past MaxStackedMessages the first entry
    /// of the lowest priority goes.
    /// </summary>
    public static void AddMessageToStack(SHud hud, string text, int time, byte prio)
    {
        ArgumentNullException.ThrowIfNull(hud);
        if (text.Length == 0)
        {
            return;
        }

        List<FStackedMessage> stack = hud.MessageStack;
        float now = hud.Level.TimeSeconds;
        int stay = Math.Max(time, (int)hud.MessageMustStayTime);
        for (int i = 0; i < stack.Count; i++)
        {
            if (stack[i].Message == text)
            {
                // The entry keeps its start; its stay reaches max(time, MessageMustStayTime) from now.
                stack[i] = stack[i] with { StayInStackTime = (int)(now + stay - stack[i].StartedTime) };
                return;
            }
        }

        stack.Add(new FStackedMessage { Message = text, StartedTime = now, StayInStackTime = stay, prio = prio });
        RemoveLowest(stack, hud.MaxStackedMessages);
    }

    /// <summary>
    /// SHud.AddMessageToAuxiliaryStack (the state message area, one entry): a text already there restarts when it had
    /// expired and stays time + 1 s; a new one is appended with its own time, and the lowest priority entry goes past
    /// one entry.
    /// </summary>
    public static void AddMessageToAuxiliaryStack(SHud hud, string text, int time, byte prio)
    {
        ArgumentNullException.ThrowIfNull(hud);
        if (text.Length == 0)
        {
            return;
        }

        float now = hud.Level.TimeSeconds;
        List<FStackedMessage> stack = hud.AuxiliaryMessageStack;
        for (int i = 0; i < stack.Count; i++)
        {
            if (stack[i].Message == text)
            {
                FStackedMessage m = stack[i];
                if (m.StartedTime + m.StayInStackTime <= now)
                {
                    m.StartedTime = now;
                }

                m.StayInStackTime = (int)(time + 1f);
                stack[i] = m;
                return;
            }
        }

        stack.Add(new FStackedMessage { Message = text, StartedTime = now, StayInStackTime = time, prio = prio });
        RemoveLowest(stack, 1);
    }

    /// <summary>SHud.AddHudNumberMessageToStack: the numbered HUD texts (detected, mine, malfunction, rebooting...).</summary>
    public static void AddHudNumberMessageToStack(SHud hud, int message, int time, byte prio)
    {
        ArgumentNullException.ThrowIfNull(hud);
        string text = message switch
        {
            1 => hud.DetectedMessage,
            2 => hud.PlayerOwner?.GameReplicationInfo is { } gri && gri.GameMode != EGameMode.GM_Multi ? hud.DetectedMessage : hud.AreaIsolatedMessage,
            3 => hud.MineOppositeWall,
            4 => hud.TextMineLaunched,
            5 => hud.TextNoiseEmitterOn,
            6 => hud.TextNoiseEmitterOff,
            7 => hud.TextMalfunction,
            8 => hud.TextRebooting,
            _ => "",
        };
        AddMessageToStack(hud, text, time, prio);
    }

    private static void RemoveLowest(List<FStackedMessage> stack, int max)
    {
        if (max > 0 && stack.Count > max)
        {
            int lowest = 0;
            for (int i = 1; i < stack.Count; i++)
            {
                if (stack[i].prio < stack[lowest].prio)
                {
                    lowest = i;
                }
            }

            stack.RemoveAt(lowest);
        }
    }

    // ---- the page's message driver --------------------------------------------------------------------------

    /// <summary>
    /// The page's message driver (the original runs it once a draw; here once a tick, the level clock only moves
    /// then): expired entries leave the three stacks; the game queue shows its head for GameMessageDisplayTime and
    /// then moves on, and waits while the tutorial message shows.
    /// </summary>
    public static void Tick(SHud hud, bool tutorialShown)
    {
        ArgumentNullException.ThrowIfNull(hud);
        float now = hud.Level.TimeSeconds;
        Prune(hud.MessageStack, now);
        Prune(hud.AuxiliaryMessageStack, now);
        Prune(hud.InfoMessageStack, now);
        if (hud.GameMessageQueue.Count == 0 || tutorialShown)
        {
            return;
        }

        if (hud.GameMessageEndTime == 0f)
        {
            hud.GameMessageStartTime = now;
            hud.GameMessageEndTime = now + hud.GameMessageDisplayTime;
        }
        else if (hud.GameMessageEndTime < now)
        {
            hud.GameMessageQueue.RemoveAt(0);
            if (hud.GameMessageQueue.Count > 0)
            {
                hud.GameMessageStartTime = now;
                hud.GameMessageEndTime = now + hud.GameMessageDisplayTime;
            }
            else
            {
                hud.GameMessageEndTime = 0f;
            }
        }
    }

    private static void Prune(List<FStackedMessage> stack, float now)
    {
        for (int i = stack.Count - 1; i >= 0; i--)
        {
            if (stack[i].StartedTime + stack[i].StayInStackTime < now)
            {
                stack.RemoveAt(i);
            }
        }
    }
}

/// <summary>A player as a message names them, captured when it arrives so the row stays right after they leave.</summary>
public readonly record struct HudParticipant(int PlayerId, int Team, string Name)
{
    public static readonly HudParticipant None = new(0, -1, "");
}

/// <summary>A message waiting for Unreal to drain it; <see cref="HudMessage"/> is its blittable form.</summary>
public struct HudPendingMessage
{
    public int Category;
    public int Kind;
    public int Cause;
    public int Trigger;
    public int Flags;
    public int Switch;
    public float Time;
    public float Duration;
    public int Priority;
    public HudParticipant Actor;
    public HudParticipant Victim;
    public HudParticipant TriggeredBy;
    public string Text;

    /// <summary>A message that is only text (a stack line, a chat line).</summary>
    public static HudPendingMessage Plain(int category, string text, float time, float duration, int priority) => new()
    {
        Category = category,
        Kind = -1,
        Switch = -1,
        Time = time,
        Duration = duration,
        Priority = priority,
        Actor = HudParticipant.None,
        Victim = HudParticipant.None,
        TriggeredBy = HudParticipant.None,
        Text = text,
    };
}

/// <summary>
/// The new HUD messages since Unreal last drained them. It holds the last <see cref="Capacity"/>; if Unreal stops
/// draining (a loading screen), the oldest go first.
/// </summary>
public sealed class HudMessageQueue
{
    public const int Capacity = 256;

    private readonly Queue<HudPendingMessage> _pending = new();

    public int Count => _pending.Count;

    /// <summary>Messages added so far (the probe's evidence).</summary>
    public long Total { get; private set; }

    public void Add(in HudPendingMessage message)
    {
        if (_pending.Count >= Capacity)
        {
            _pending.Dequeue();
        }

        _pending.Enqueue(message);
        Total++;
    }

    public bool TryTake(out HudPendingMessage message) => _pending.TryDequeue(out message);

    public void Clear() => _pending.Clear();
}

/// <summary>What happened (<see cref="HudMessage.Kind"/>), as the Godot client's HudActivityKind.</summary>
public enum HudActivityKind
{
    Kill, Death, Suicide, Resign, Sleep, Knockout, Join, Leave, Match, LastLife,
}

/// <summary>How it happened (<see cref="HudMessage.Cause"/>), in the order of the Godot client's HudActivityCause.</summary>
public enum HudActivityCause
{
    None, Firearm, Headshot, Explosion, Charge, DropAttack, Elbow, Grab, NeckBreak, Choke,
    Fall, Crush, Poison, StickyShock, StickyBlast, Smoke, StickyCamera, Other,
    ProximityMine, LaserMine, PoisonMine, FragGrenade, BarrelExplosion, ExplosiveProp,
    MolotovExplosion, MolotovFire, Fire, Hazard, LedgePull,
    Rifle, Shotgun, Smg, Sniper, Pistol, Revolver,
    GasExplosion, ExplosiveExtinguisher, SodaExplosion,
    Taser, ShockCollision,
}

/// <summary>
/// Reads who did what to whom out of a localized message before it becomes text, ported from OpenSCCT's
/// game/Scripts/Hud/HudActivityClassifier.cs. The Godot client words its feed rows itself in English; here the
/// message's text is always the one the content builds (<see cref="HudMessageRules.TextOf"/>), and the parts are
/// there for a HUD that draws its own row.
/// </summary>
public static class HudActivity
{
    public static bool TryCreate(string messageClass, int message, PlayerReplicationInfo? actor,
        PlayerReplicationInfo? victim, UObject? source, PlayerReplicationInfo? localPlayer, float now,
        string text, out HudPendingMessage entry)
    {
        HudParticipant a = Capture(actor);
        HudParticipant v = Capture(victim);
        bool local = IsLocal(actor, localPlayer) || IsLocal(victim, localPlayer);
        int received = message;
        entry = default;

        if (HudMessageRules.IsClass(messageClass, HudMessageRules.DeathMessageClass))
        {
            HudActivityCause cause = DeathCause(message);
            CombatDeathTrigger trigger = CombatActivityDeath.Trigger(message);
            PlayerReplicationInfo? triggerPlayer = trigger != CombatDeathTrigger.None ? source as PlayerReplicationInfo : null;
            local |= IsLocal(triggerPlayer, localPlayer);
            message = CombatActivityDeath.LegacySwitch(message);
            bool self = actor is not null && victim is not null && ReferenceEquals(actor, victim);
            bool triggeredByAnother = self && triggerPlayer is not null && !ReferenceEquals(triggerPlayer, victim);
            bool environment = message is (int)SDamageType.SFell or (int)SDamageType.Crushed;
            HudActivityKind kind = message == 24 ? HudActivityKind.Resign : triggeredByAnother || environment && (self || actor is null)
                ? HudActivityKind.Death : self || message == (int)SDamageType.Suicided
                ? HudActivityKind.Suicide : actor is null ? HudActivityKind.Death : HudActivityKind.Kill;
            // Resign, suicide and an unknown killer are one-participant outcomes, never invented killers.
            if (kind != HudActivityKind.Kill)
            {
                a = HudParticipant.None;
            }

            entry = Make(kind, cause, trigger, received, now, text, local, a, v, Capture(triggerPlayer), headshot: message == 23);
            return true;
        }

        if (HudMessageRules.IsClass(messageClass, HudMessageRules.EngineGameMessageClass) && message is 1 or 4)
        {
            entry = Make(message == 1 ? HudActivityKind.Join : HudActivityKind.Leave, HudActivityCause.None, CombatDeathTrigger.None,
                received, now, text, local, a, HudParticipant.None, HudParticipant.None);
            return true;
        }

        if (!HudMessageRules.IsClass(messageClass, HudMessageRules.GameMessagesClass))
        {
            return false;
        }

        if (message is 16 or 17 or 20 or CombatK.MsgSmokeSleep or CombatK.MsgStickyCameraSleep)
        {
            bool sleep = message is CombatK.MsgSmokeSleep or CombatK.MsgStickyCameraSleep;
            HudActivityCause cause = message switch
            {
                16 => HudActivityCause.DropAttack,
                17 => HudActivityCause.Elbow,
                20 => HudActivityCause.Grab,
                CombatK.MsgSmokeSleep => HudActivityCause.Smoke,
                _ => HudActivityCause.StickyCamera,
            };
            entry = Make(sleep ? HudActivityKind.Sleep : HudActivityKind.Knockout, cause, CombatDeathTrigger.None, received, now, text,
                local, a, v, HudParticipant.None);
            return true;
        }

        if (message is 2 or 4 or 5 or 7)
        {
            entry = Make(HudActivityKind.Match, HudActivityCause.None, CombatDeathTrigger.None, received, now, text, false,
                HudParticipant.None, HudParticipant.None, HudParticipant.None);
            if (message == 2)
            {
                // The Godot client empties its feed on a match restart before it adds this line (HudMessages.LocalizedMessage).
                entry.Flags |= HudMessageFlags.ClearsFeed;
            }

            return true;
        }

        if (message == 56)
        {
            entry = Make(HudActivityKind.LastLife, HudActivityCause.None, CombatDeathTrigger.None, received, now, text, local, a,
                HudParticipant.None, HudParticipant.None);
            return true;
        }

        return false;
    }

    private static HudPendingMessage Make(HudActivityKind kind, HudActivityCause cause, CombatDeathTrigger trigger, int received,
        float now, string text, bool local, HudParticipant actor, HudParticipant victim, HudParticipant triggeredBy, bool headshot = false) => new()
    {
        Category = HudMessageCategory.Activity,
        Kind = (int)kind,
        Cause = (int)cause,
        Trigger = (int)trigger,
        Flags = (local ? HudMessageFlags.InvolvesLocal : 0) | (headshot ? HudMessageFlags.Headshot : 0),
        Switch = received,
        Time = now,
        Duration = HudMessageRules.ActivityLifetime,
        Priority = 0,
        Actor = actor,
        Victim = victim,
        TriggeredBy = triggeredBy,
        Text = text,
    };

    private static HudParticipant Capture(PlayerReplicationInfo? pri) => pri is null
        ? HudParticipant.None
        : new HudParticipant(pri.PlayerID, pri.Team?.TeamIndex ?? -1, Clean(pri.PlayerName));

    private static bool IsLocal(PlayerReplicationInfo? pri, PlayerReplicationInfo? own) =>
        pri is not null && own is not null && ReferenceEquals(pri, own);

    private static string Clean(string? text) => string.IsNullOrWhiteSpace(text) ? "" :
        string.Join(' ', text.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));

    private static HudActivityCause DeathCause(int message) => CombatActivityDeath.Cause(message) switch
    {
        CombatDeathCause.ProximityMine => HudActivityCause.ProximityMine,
        CombatDeathCause.LaserMine => HudActivityCause.LaserMine,
        CombatDeathCause.PoisonMine => HudActivityCause.PoisonMine,
        CombatDeathCause.FragGrenade => HudActivityCause.FragGrenade,
        CombatDeathCause.BarrelExplosion => HudActivityCause.BarrelExplosion,
        CombatDeathCause.ExplosiveProp => HudActivityCause.ExplosiveProp,
        CombatDeathCause.MolotovExplosion => HudActivityCause.MolotovExplosion,
        CombatDeathCause.MolotovFire => HudActivityCause.MolotovFire,
        CombatDeathCause.Fire => HudActivityCause.Fire,
        CombatDeathCause.Hazard => HudActivityCause.Hazard,
        CombatDeathCause.LedgePull => HudActivityCause.LedgePull,
        CombatDeathCause.Rifle => HudActivityCause.Rifle,
        CombatDeathCause.Shotgun => HudActivityCause.Shotgun,
        CombatDeathCause.Smg => HudActivityCause.Smg,
        CombatDeathCause.Sniper => HudActivityCause.Sniper,
        CombatDeathCause.Pistol => HudActivityCause.Pistol,
        CombatDeathCause.Revolver => HudActivityCause.Revolver,
        CombatDeathCause.Poison => HudActivityCause.Poison,
        CombatDeathCause.GasExplosion => HudActivityCause.GasExplosion,
        CombatDeathCause.ExplosiveExtinguisher => HudActivityCause.ExplosiveExtinguisher,
        CombatDeathCause.SodaExplosion => HudActivityCause.SodaExplosion,
        CombatDeathCause.Crush => HudActivityCause.Crush,
        CombatDeathCause.Taser => HudActivityCause.Taser,
        CombatDeathCause.ShockCollision => HudActivityCause.ShockCollision,
        _ => LegacyDeathCause(CombatActivityDeath.LegacySwitch(message)),
    };

    private static HudActivityCause LegacyDeathCause(int message) => message switch
    {
        (int)SDamageType.SDamage_Charged => HudActivityCause.Charge,
        (int)SDamageType.SDamage_DropAttacked => HudActivityCause.DropAttack,
        (int)SDamageType.SDamage_ElbowShock => HudActivityCause.Elbow,
        (int)SDamageType.SDamage_Explosion => HudActivityCause.Explosion,
        (int)SDamageType.SDTSnipeShot => HudActivityCause.Firearm,
        (int)SDamageType.SDTStickyShocker => HudActivityCause.StickyShock,
        (int)SDamageType.SFell => HudActivityCause.Fall,
        (int)SDamageType.Crushed => HudActivityCause.Crush,
        (int)SDamageType.DamTypeGrabChoke => HudActivityCause.Choke,
        (int)SDamageType.DamTypeGrabBreak => HudActivityCause.NeckBreak,
        (int)SDamageType.SDamage_Poison => HudActivityCause.Poison,
        (int)SDamageType.SDTStickyBlast => HudActivityCause.StickyBlast,
        23 => HudActivityCause.Headshot,
        _ => HudActivityCause.None,
    };
}
