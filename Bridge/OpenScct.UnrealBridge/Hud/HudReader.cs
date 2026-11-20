using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.Objectives;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
using SpyCloak = OpenScct.Game.Equipment.SpyCloak;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>A reused buffer of blittable rows: filled each tick, copied out to Unreal each frame.</summary>
public sealed class HudRows<T>
    where T : unmanaged
{
    private T[] _items = new T[16];

    public int Count { get; private set; }

    public ReadOnlySpan<T> Span => _items.AsSpan(0, Count);

    public void Clear() => Count = 0;

    public void Add(in T item)
    {
        if (Count == _items.Length)
        {
            Array.Resize(ref _items, Count * 2);
        }

        _items[Count++] = item;
    }

    /// <summary>Copies what fits into <paramref name="target"/>. Returns the full count.</summary>
    public int CopyTo(Span<T> target)
    {
        Span.Slice(0, Math.Min(Count, target.Length)).CopyTo(target);
        return Count;
    }
}

/// <summary>Everything the HUD shows for one tick, in the form Unreal reads it.</summary>
public sealed class HudFrame
{
    public HudState State;
    public HudRound Round;

    public HudRows<HudGadget> Gadgets { get; } = new();

    public HudRows<HudInteraction> Interactions { get; } = new();

    public HudRows<HudMenuLine> Menu { get; } = new();

    public HudRows<HudObjective> Objectives { get; } = new();

    public HudRows<HudObjective> BinocularTargets { get; } = new();

    public HudRows<HudObjectiveLine> ObjectiveLines { get; } = new();

    public HudRows<HudBlip> Radar { get; } = new();

    public HudRows<HudShownMessage> Shown { get; } = new();

    public HudRows<HudScoreRow> Score { get; } = new();

    public void Clear()
    {
        State = default;
        Round = default;
        Gadgets.Clear();
        Interactions.Clear();
        Menu.Clear();
        Objectives.Clear();
        BinocularTargets.Clear();
        ObjectiveLines.Clear();
        Radar.Clear();
        Shown.Clear();
        Score.Clear();
    }
}

/// <summary>
/// Reads the local player's HUD from the simulation: the controller, its pawn, its SHud, the game replication info
/// and the player replication infos. Ported from OpenSCCT's game/Scripts/Hud/HudSimReader.cs (which fills the Godot
/// client's HudSnapshot by the original's page drivers), with the gadget views of game/Scripts/Hud/Gadgets. It
/// reads only: nothing here writes simulation state. Where the Godot reader formats a text from numbers ("2/3",
/// "0:45", "Inf.") this one hands over the numbers.
/// </summary>
public sealed class HudReader
{
    // Numbers of the original's page drivers, as OpenSCCT's HudK has them.

    /// <summary>The merc reticle (and the scope's sights) turn red this long after the local merc's shot hit a pawn.</summary>
    private const float ReticleHitTime = 0.4f;

    /// <summary>The scoped merc's aim point: this far out from the eye along MyDesiredRotation.</summary>
    private const float BinocularAimDistance = 1000f;

    /// <summary>The range finder's line length.</summary>
    private const float BinocularRange = 15000f;

    /// <summary>A Disk Hunt disk's marker shows within this distance of the viewer.</summary>
    private const float HuntDiskErRange = 1500f;

    /// <summary>A disk carrier moving faster than this shows on the merc radar (Reloaded's 200; stock 170).</summary>
    private const float FlagCarrierRadarSpeed = 200f;

    /// <summary>The alarm box shows alarms of a message priority above this.</summary>
    private const int AlarmBoxPriority = 4;

    /// <summary>The spectator page shows the respawn countdown below this many seconds.</summary>
    private const int RespawnShownBelow = 50;

    /// <summary>"Switch player" shows for a watched team of at least this size.</summary>
    private const int SwitchPlayerTeamSize = 3;

    /// <summary>Marker distances when the HUD has no unit set: metres per uu (1 uu = 1 cm).</summary>
    private const float DefaultDistanceScale = 0.01f;

    private static readonly Name NAME_s_Binoculars_Att = new("s_Binoculars_Att");
    private static readonly Name NAME_s_Binoculars_Def = new("s_Binoculars_Def");
    private static readonly Name NAME_s_Spectating_Visitor = new("s_Spectating_Visitor");

    private readonly HudTextTable _texts;
    private readonly HudStrings _strings;
    private readonly string _map;
    private string? _mapTitle;
    private readonly HashSet<PlayerReplicationInfo> _alive = new();
    private readonly Dictionary<PlayerReplicationInfo, int> _pawnIds = new();
    private readonly Dictionary<string, int> _lineIds = new(StringComparer.Ordinal);
    private readonly List<SObjectiveTrigger> _triggers = new();
    private readonly Dictionary<string, (int U, int V)> _weaponIcons = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<Type, int> _classIds = new();

    public HudReader(HudTextTable texts, HudStrings strings, string map)
    {
        _texts = texts;
        _strings = strings;
        _map = map;
    }

    /// <summary>
    /// Fills <paramref name="f"/> (already cleared) for <paramref name="pc"/>. <paramref name="aimRan"/> says the
    /// merc's aim point helper ran this tick (SHud.SpotReticule is current); <paramref name="targetNameRan"/> that
    /// the laser's target name pass ran (SHud.MyTargetName and the HUD tracker's lock are current).
    /// </summary>
    public void Read(SPlayerController pc, HudFrame f, HudEffectState effects, HudConsoleLines console, bool aimRan, bool targetNameRan)
    {
        Level level = pc.XLevel;
        LevelInfo info = pc.Level;
        var pawn = pc.Pawn as SBasePawn;
        if (pawn is { bDeleteMe: true })
        {
            pawn = null;
        }

        ref HudState s = ref f.State;
        float now = info.TimeSeconds;

        // SPlayerController.SpawnMagmaHud opens the side's page once the player is ready: its PRI has a team, or it
        // only spectates. Before that the client shows no HUD.
        s.Valid = pc.PlayerReplicationInfo is { } ready && (ready.bOnlySpectator || ready.Team is not null) ? 1 : 0;
        s.TimeSeconds = now;
        PawnType type = pawn?.SPawnType ?? pc.SPawnType;
        bool merc = type == PawnType.SPAWNDEFENSE;
        s.Side = merc ? 1 : 0;
        s.Yaw = pc.Rotation.Yaw * (360f / 65536f);
        s.StateNameId = _texts.Id(pc.GetStateName().ToString());
        s.VisionMode = pc.RendMap;
        s.WeaponKind = -1;
        s.SelectedGadget = -1;
        s.SelectedInteraction = -1;
        s.GasMaskAirSeconds = -1;
        s.AlarmSeconds = s.HackCountdown = s.ObjectiveCountdown0 = s.ObjectiveCountdown1 = -1;
        s.RespawnWeaponIconU = s.RespawnWeaponIconV = -1;

        HUD? hud = pc.myHUD;
        var shud = hud as SHud;
        int flags = 0, fx = 0;
        if (hud is not null)
        {
            flags |= (hud.bHideHUD ? HudFlags.HideHud : 0) | (hud.bLifeOff ? HudFlags.LifeOff : 0) |
                (hud.bGadgetOff ? HudFlags.GadgetOff : 0) | (hud.bTimeOff ? HudFlags.TimeOff : 0) |
                (hud.bAMMOOff ? HudFlags.AmmoOff : 0) | (hud.EnhancedRealityLevel != 0 ? HudFlags.EnhancedReality : 0);
            s.Ballistic = hud.BallisticValue;
            ReadArrows(hud, now, ref s);
            s.DistanceUnitId = _texts.Id(hud.StrDistanceUnit);
            s.DistanceScale = hud.fDistanceUnit > 0f ? hud.fDistanceUnit : DefaultDistanceScale;
        }

        if (s.Ballistic == 0f)
        {
            s.Ballistic = effects.Ballistic;
        }

        // The page hides while the knock-out screen is dark, and for the cinematic camera's Hide HUD.
        if ((effects.Sfx.Sleeping && effects.Sfx.ValueOf(effects.Sfx.Find(SFXType.SFXT_TextureColorSleep)) < 255f) || pc.bCinematicHideHud)
        {
            flags |= HudFlags.HideHud;
        }

        flags |= pc.bFastSpeed ? HudFlags.FastSpeed : 0;
        flags |= pc.bGameEnded ? HudFlags.GameEnded : 0;
        GameReplicationInfo? gri = pc.GameReplicationInfo ?? info.GRI;
        if (pawn is not null)
        {
            ReadPawn(pc, pawn, f, ref flags, ref fx, gri is not null && Loadouts.IsBombMode(gri.GameMode));
        }

        int objectivesLeft = MissionRules.GetObjectivesLeft(level);
        if (pc.PlayerReplicationInfo is { } pri)
        {
            s.Lives = pri.NumLives;
            s.TeamGadgetSlots = (pri.Team as STeamInfo)?.iNBGadgets ?? 0;
        }

        if (shud is not null)
        {
            float elapsed = gri?.PreciseElapsedTime() ?? 0f;
            s.HackingTextId = _texts.Id(shud.ObjectiveHackingSpy);
            s.SecuredTextId = _texts.Id(shud.ObjectiveSecuredSpy);
            ReadCountdowns(shud, elapsed, ref s);
            ReadAlarm(shud, elapsed, ref s);
            ReadShownMessages(shud, console, now, f);
            flags |= shud.bShowConnectedPlayers ? HudFlags.ShowPlayers : 0;
            s.TutorialId = shud.sTutoMap.Length > 4 ? _texts.Id(shud.sTutoMap) : 0;
            s.SleepingTextId = _texts.Id(shud.TextSleeping);
            if (aimRan && (shud.SpotReticule.X != 0f || shud.SpotReticule.Y != 0f))
            {
                flags |= HudFlags.SpotValid;
                s.SpotX = shud.SpotReticule.X;
                s.SpotY = shud.SpotReticule.Y;
            }

            FStackedMessage rm = shud.ReticuleMessage;
            if (!string.IsNullOrEmpty(rm.Message) && rm.StartedTime + rm.StayInStackTime > now)
            {
                s.ReticleMessageId = _texts.Id(rm.Message);
            }

            if (targetNameRan && (shud.P ?? pawn) is SPAWNDEFENSE { TorcheLaser: { bInUse: true, TargetPawn: { bDeleteMe: false } target } } aimer)
            {
                // SHud.TargetNameLabel: the name goes over the target's bracket TimeToScanName after the lock began,
                // in the friend colour for a pawn of the viewer's side.
                s.TargetNameId = _texts.Id(shud.MyTargetName);
                s.TargetActorId = target.ActorId;
                s.TargetNameDelay = shud.TimeToScanName;
                s.TargetLockTime = now;
                if (shud.TargetTracker is { } tracker)
                {
                    foreach (FLockedObject l in tracker.LockedObjects)
                    {
                        if (ReferenceEquals(l.Actor, target))
                        {
                            s.TargetLockTime = l.StartTime;
                            break;
                        }
                    }
                }

                flags |= target.SPawnType == aimer.SPawnType ? HudFlags.TargetFriend : 0;
            }
        }
        else
        {
            ReadConsole(console, now, f);
        }

        // The same local deadline as s_LyingOnGround's timer, including its final partial second.
        s.SleepSeconds = RemainingSeconds(pc.fLyingOnGroundTime + pc.fLyingOnGroundDuration - now);
        s.LocationNameId = _texts.Id(LocationName(pc.ViewTarget as Pawn ?? pc.Pawn));
        ReadRespawnWeapon(pc, ref s);
        ReadEffects(pc, pawn, shud, effects, now, ref s, ref fx);
        ReadObjectives(pc, pawn, level, gri, f);
        ReadRadar(pc, pawn, shud, merc, f);
        ReadInteractions(pc, pawn, f, ref flags);
        ReadPrompts(pc, pawn, merc, ref s);
        ReadGadgetViews(pc, shud, f, ref fx);
        ReadBinoculars(pc, pawn, shud, gri, merc, f, ref flags);
        bool spectating = pc.IsSpectating();
        flags |= spectating ? HudFlags.Spectating : 0;
        if (gri is not null)
        {
            ReadRound(pc, info, gri, objectivesLeft, spectating, ref f.Round);
            ReadScore(pc, level, gri, f);
            ReadObjectiveLines(pc, level, gri, f);
        }

        s.Flags = flags;
        s.EffectFlags = fx;
    }

    private static int RemainingSeconds(float seconds) => (int)MathF.Ceiling(Math.Max(0f, seconds));

    /// <summary>The simulation's class name of <paramref name="o"/>, as a text id.</summary>
    private int ClassId(object o)
    {
        Type type = o.GetType();
        if (!_classIds.TryGetValue(type, out int id))
        {
            id = _texts.Id(type.Name);
            _classIds[type] = id;
        }

        return id;
    }

    /// <summary>
    /// The six sound arrows (HUD.Left/Right/Top/Bottom/Forward/BackwardArrowTime, set by SHud.NotifyHotSound): each
    /// shows while trunc(TimeSeconds) is below its time.
    /// </summary>
    private static unsafe void ReadArrows(HUD hud, float now, ref HudState s)
    {
        float whole = MathF.Truncate(now);
        for (int i = 0; i < HudState.ArrowCount; i++)
        {
            float until = i switch
            {
                0 => hud.LeftArrowTime,
                1 => hud.RightArrowTime,
                2 => hud.TopArrowTime,
                3 => hud.BottomArrowTime,
                4 => hud.ForwardArrowTime,
                _ => hud.BackwardArrowTime,
            };
            // Never 0 or less while the original still shows the arrow (its test is on the whole second).
            s.ArrowSeconds[i] = whole < until ? Math.Max(until - now, 1f / 64f) : 0f;
        }
    }

    /// <summary>Engine.Volume's class default LocationName: a volume left with it names no place.</summary>
    private const string UnspecifiedLocation = "unspecified";

    /// <summary>
    /// The name of the place <paramref name="pawn"/> is in, or "" when the map names none there: the highest-priority
    /// named volume the pawn touches, else its zone's name, as PlayerReplicationInfo.GetLocationName picks it but on
    /// this machine (ported from game/Scripts/Hud/HudLocation.cs NameAt: Reloaded's location readout above the radar).
    /// </summary>
    private static string LocationName(Pawn? pawn)
    {
        if (pawn is null || pawn.bDeleteMe)
        {
            return "";
        }

        Volume? best = null;
        foreach (Actor a in pawn.Touching)
        {
            if (a is Volume v && Named(v.LocationName) && (best is null || v.LocationPriority > best.LocationPriority))
            {
                best = v;
            }
        }

        if (best is not null)
        {
            return best.LocationName;
        }

        return pawn.Region.Zone is { } zone && Named(zone.LocationName) ? zone.LocationName : "";

        static bool Named(string? name) =>
            !string.IsNullOrWhiteSpace(name) && !string.Equals(name, UnspecifiedLocation, StringComparison.OrdinalIgnoreCase);
    }

    private void ReadPawn(SPlayerController pc, SBasePawn pawn, HudFrame f, ref int flags, ref int fx, bool bombMode)
    {
        ref HudState s = ref f.State;
        flags |= HudFlags.HasPawn;
        s.Health = pawn.Health;
        s.X = pawn.Location.X;
        s.Y = pawn.Location.Y;
        s.Z = pawn.Location.Z;
        s.Speed = pawn.Velocity.Size();
        flags |= (pawn.bPoisonned ? HudFlags.Poisoned : 0) | (pawn.bIsCrouched ? HudFlags.Crouched : 0) |
            (pawn.bSleeping ? HudFlags.Sleeping : 0) | (pawn.bLyingOnGround ? HudFlags.LyingOnGround : 0) |
            (pawn.CarriedFlag is not null ? HudFlags.CarryingFlag : 0);

        var immunity = OpenScct.Server.MercShockPresentation.Read(pawn);
        fx |= (immunity.Active ? HudEffectFlags.ShockImmune : 0) | (immunity.StateProtected ? HudEffectFlags.ShockImmuneByState : 0);
        s.ShockImmunityRemaining = immunity.RemainingSeconds;
        s.ShockImmunityFraction = immunity.Fraction;

        if (pawn is SPawnAttaque spy)
        {
            s.EnergyLevel = spy.EnergyLevel;
            s.EnergyLevelMax = spy.EnergyLevelMax;
            flags |= SpyCloak.IsEngaged(spy) ? HudFlags.CloakEngaged : 0;
        }

        bool laser = pawn is SPAWNDEFENSE { TorcheLaser: { bInUse: true } };
        flags |= laser ? HudFlags.LaserOn : 0;
        if (pawn.MyWeapon is SWeaponDefense { bDeleteMe: false } weapon)
        {
            flags |= HudFlags.HasWeapon | (pawn.SidearmWeapon is not null ? HudFlags.HasSidearm : 0);
            // The standard rifle (the class itself, not the rifles derived from it) is named FN2000 as in the Godot client
            // (HudSimReader); every other weapon keeps the item name of the content.
            s.WeaponNameId = _texts.Id(weapon.GetType() == typeof(SWeaponDefense) ? "FN2000" : weapon.ItemName);
            s.ClipAmmo = weapon.ClipAmmo;
            s.Ammo = weapon.Ammo;
            s.FireMode = (int)weapon.ROFMode;
            s.FireModeNameId = _texts.Id(weapon.GetROFName());
            s.WeaponKind = weapon switch
            {
                SWeaponDefense_Uzi or CompactSMG => 1,
                SWeaponDefense_Shotgun => 2,
                _ => 0,
            };
            if (pawn.WeaponLoaded != 0 && !pc.bNoReticule && !laser && !pc.bLaserEtaitAllume && !(weapon is MercRifle or MercSidearm && weapon.AimRequested))
            {
                flags |= HudFlags.ShowReticle;
            }

            s.Accuracy = weapon.GetAccuracy();
            s.MinDispersion = weapon.MinDispersion;
            s.MaxDispersion = weapon.MaxDispersion;
            float range = weapon.MaxDispersion - weapon.MinDispersion;
            s.ReticleFrame = range > 0f ? (int)(10f * s.Accuracy / range) : 0;
            if (pc.fDateLastShootedPawn > 0f && pc.fDateLastShootedPawn + ReticleHitTime > pc.Level.TimeSeconds)
            {
                flags |= HudFlags.ReticleHit;
            }
        }

        // The gadgets in the order the pawn holds them (SBasePawn.MyGadget, then each SGadget.MyGadget).
        int guard = 0;
        for (SGadget? gadget = pawn.MyGadget; gadget is not null && guard++ < 32; gadget = gadget.MyGadget)
        {
            if (gadget.bDeleteMe)
            {
                continue;
            }

            // The modern plant interaction keeps an internal SGadBomb with 5,000 charges. Only the PS2
            // placement gadget belongs in this strip. Its profile and remaining charge come from the server.
            if (gadget is SGadBomb modem && (!modem.Ps2ModemProfile || !bombMode))
            {
                continue;
            }

            bool selected = ReferenceEquals(gadget, pawn.SelectedGadget);
            bool unavailable = gadget is SGadCamNetwork { HasNetwork: false };
            if (selected)
            {
                s.SelectedGadget = f.Gadgets.Count;
                flags |= unavailable ? HudFlags.GadgetUnavailable : 0;
            }

            f.Gadgets.Add(new HudGadget
            {
                ClassId = ClassId(gadget),
                NameId = _texts.Id(gadget.ItemName),
                IconNameId = _texts.Id(gadget.MagmaIconName),
                IconU = gadget.IconU,
                IconV = gadget.IconV,
                Charge = gadget.Charge,
                Flags = (selected ? HudGadgetFlags.Selected : 0) | (unavailable ? HudGadgetFlags.Unavailable : 0),
                MineHackPercent = (gadget as MineHackGadget)?.HeldHackPercent ?? 0,
                RechargeRemaining = (gadget as CloneGadget)?.RechargeRemaining ?? 0f,
            });
        }

        s.GadgetCount = f.Gadgets.Count;
    }

    /// <summary>The merc page's effects and the screen-effect slots.</summary>
    private static unsafe void ReadEffects(SPlayerController pc, SBasePawn? pawn, SHud? shud, HudEffectState effects, float now,
        ref HudState s, ref int fx)
    {
        fx |= ((shud?.bPerturbation ?? false) || now < effects.PerturbedUntil ? HudEffectFlags.Perturbed : 0) |
            (pawn?.bStickyShocking ?? false ? HudEffectFlags.StickyShocking : 0) |
            (pawn?.bStickyShockedInTheHead ?? false ? HudEffectFlags.StickyShockHead : 0) |
            (pawn?.CurrentRainVolume is not null ? HudEffectFlags.Wet : 0) |
            (effects.Projection || (shud?.bProjection ?? false) ? HudEffectFlags.FoamSplash : 0) |
            (effects.NeigeDisp || (shud?.bNeigeDisp ?? false) ? HudEffectFlags.FoamOn : 0) |
            (effects.ShutDown ? HudEffectFlags.ShutDown : 0) |
            (effects.Sfx.Sleeping ? HudEffectFlags.SleepScreen : 0);
        s.FoamStopTime = effects.NeigeDisp ? effects.TimeStopProj : shud?.TimeStopProj ?? 0;
        s.FoamStaySeconds = effects.NeigeDisp ? effects.TimeSnowToStay : shud?.TimeSnowToStay ?? 0;
        s.ShutDowns = effects.ShutDowns;
        s.ShutDownTime = effects.ShutDownTime;
        s.Flashes = effects.Flashes;
        s.FlashTime = effects.LastFlashTime;
        s.DamageCount = effects.DamageCount;
        s.LastDamageType = effects.LastDamageType;
        s.LastDamageTime = effects.LastDamageTime;
        s.CrossEyedFactor = effects.Sfx.CrossEyedFactor;
        s.RemanenceBlendFactor = effects.Sfx.RemanenceBlendFactor;
        s.RemanenceCaptures = effects.RemanenceCaptures;
        for (int i = 0; i < HudState.SfxSlots; i++)
        {
            s.SfxType[i] = (int)effects.Sfx.TypeOf(i);
            s.SfxValue[i] = effects.Sfx.ValueOf(i);
        }
    }

    /// <summary>
    /// The first hacking panel and the first two objective deadlines, on the fractional match clock and rounded up
    /// once, so positive time never shows zero.
    /// </summary>
    private static void ReadCountdowns(SHud hud, float elapsed, ref HudState s)
    {
        foreach (Actor? a in hud.ActorToDraw)
        {
            if (a is STriggerInteraction { bDeleteMe: false } t && t.MinTriggerTime != 0f && s.HackCountdown < 0)
            {
                s.HackCountdown = RemainingSeconds(t.MinTriggerTime - elapsed);
            }
            else if (a is SObjective { bDeleteMe: false } o && o.MinTriggerTime != 0f)
            {
                if (s.ObjectiveCountdown0 < 0)
                {
                    s.ObjectiveCountdown0 = RemainingSeconds(o.MinTriggerTime - elapsed);
                }
                else if (s.ObjectiveCountdown1 < 0)
                {
                    s.ObjectiveCountdown1 = RemainingSeconds(o.MinTriggerTime - elapsed);
                }
            }
        }
    }

    /// <summary>The alarm box: the longest remaining alarm of a message priority above 4.</summary>
    private static void ReadAlarm(SHud hud, float elapsed, ref HudState s)
    {
        float best = 0f;
        foreach (SAlarm? a in hud.AlarmsList)
        {
            if (a is { bDeleteMe: false, MessagePriority: > AlarmBoxPriority })
            {
                best = Math.Max(best, a.Duration + a.StartTime - elapsed);
            }
        }

        if (best > 0f)
        {
            s.AlarmSeconds = RemainingSeconds(best);
        }
    }

    /// <summary>
    /// What the original's message areas show now: every live entry of the three SHud stacks, the head of the game
    /// queue while it is shown, and the console lines.
    /// </summary>
    private void ReadShownMessages(SHud hud, HudConsoleLines console, float now, HudFrame f)
    {
        Copy(hud.MessageStack, HudMessageCategory.System);
        Copy(hud.AuxiliaryMessageStack, HudMessageCategory.State);
        Copy(hud.InfoMessageStack, HudMessageCategory.Info);
        if (hud.GameMessageQueue.Count > 0 && hud.GameMessageEndTime > 0f && !string.IsNullOrEmpty(hud.GameMessageQueue[0].Message))
        {
            f.Shown.Add(new HudShownMessage
            {
                Category = HudMessageCategory.Game,
                TextId = _texts.Id(hud.GameMessageQueue[0].Message),
                Remaining = Math.Max(0f, hud.GameMessageEndTime - now),
                Priority = hud.GameMessageQueue[0].prio,
            });
        }

        ReadConsole(console, now, f);

        void Copy(List<FStackedMessage> from, int category)
        {
            foreach (FStackedMessage m in from)
            {
                if (m.StartedTime + m.StayInStackTime >= now && !string.IsNullOrEmpty(m.Message))
                {
                    f.Shown.Add(new HudShownMessage
                    {
                        Category = category,
                        TextId = _texts.Id(m.Message),
                        Remaining = m.StartedTime + m.StayInStackTime - now,
                        Priority = m.prio,
                    });
                }
            }
        }
    }

    private void ReadConsole(HudConsoleLines console, float now, HudFrame f)
    {
        foreach (HudConsoleLine line in console.Current)
        {
            f.Shown.Add(new HudShownMessage
            {
                Category = line.Category,
                TextId = _texts.Id(line.Text),
                Remaining = Math.Max(0f, line.Life - now),
            });
        }
    }

    /// <summary>
    /// SHud.DrawRespawnWeapon: while a dead merc may pick his respawn weapon (Merc Deathmatch), the weapon's tile of
    /// the weapon icons. Ported from game/Scripts/Hud/HudRespawnWeapon.cs.
    /// </summary>
    private void ReadRespawnWeapon(SPlayerController pc, ref HudState s)
    {
        if (pc.RespawnWeapon.Path is not { } path || pc.GameReplicationInfo is null || pc.PlayerReplicationInfo is null ||
            !pc.CanPickRespawnWeapon())
        {
            return;
        }

        if (!_weaponIcons.TryGetValue(path, out (int U, int V) icon))
        {
            icon = OpenScct.Game.Devices.DeviceClasses.Index.Find(path)?.Create() is Inventory inv ? (inv.IconU, inv.IconV) : (0, 0);
            _weaponIcons[path] = icon;
        }

        s.RespawnWeaponIconU = icon.U;
        s.RespawnWeaponIconV = icon.V;
        s.RespawnWeaponClassId = _texts.Id(path);
    }

    // ---- objectives ------------------------------------------------------------------------------------------

    /// <summary>
    /// The class of an objective trigger as SHud.BuildObjectivesAndTriggersList tests it, in its order: terminal, disk
    /// (SFlag), drop zone, bomb spot, then Disk Hunt's SDisk. 5 is a class the list leaves out.
    /// </summary>
    private static int ObjectiveKind(SObjectiveTrigger t) => t switch
    {
        SComputerObjectiveTrigger => 0,
        SFlag => 1,
        SFlagDropZone => 2,
        SBombTargetObjectiveTrigger => 3,
        SDisk => 4,
        _ => 5,
    };

    private const int KindDisk = 1, KindDropZone = 2, KindBomb = 3, KindHuntDisk = 4, KindOther = 5;
    private const int RoleTrigger = 0, RoleObjective = 1, RoleDropZone = 2, RoleHuntDisk = 3;

    /// <summary>A terminal's capture percent as the markers show it: clamped to 0..99, so it never shows 100.</summary>
    private static int CapturePercent(SComputerObjectiveTrigger t) => Math.Clamp(t.GetHackPercent(), 0, 99);

    private HudObjective Marker(string name, int kind, int role, FVector at, bool triggering, bool locked, bool taken, int percent,
        bool alert = false, bool marked = false) => new()
    {
        NameId = _texts.Id(name),
        Kind = kind,
        Role = role,
        Flags = (triggering ? HudObjectiveFlags.Triggering : 0) | (locked ? HudObjectiveFlags.Locked : 0) |
            (taken ? HudObjectiveFlags.Taken : 0) | (alert ? HudObjectiveFlags.Alert : 0) | (marked ? HudObjectiveFlags.Marked : 0),
        Percent = percent,
        X = at.X,
        Y = at.Y,
        Z = at.Z,
    };

    /// <summary>
    /// The Enhanced Reality markers (ASHud::ERDrawObjectives): per SObjective (missions left out) one objective marker
    /// while the viewer is outside its zones and its triggers' markers inside them; Disk Hunt disks near the viewer;
    /// free drop zones while they trigger. Every value read here is replicated, so a client shows the server's state.
    /// </summary>
    private void ReadObjectives(SPlayerController pc, SBasePawn? pawn, Level level, GameReplicationInfo? gri, HudFrame f)
    {
        Actor viewer = pawn ?? pc.ViewTarget ?? pc;
        FPointRegion region = viewer.Region;
        bool merc = pawn?.SPawnType == PawnType.SPAWNDEFENSE;
        bool diskHunt = gri?.GameMode == EGameMode.GM_DiskHunt;
        foreach (Actor a in level.AllActors<Actor>())
        {
            if (a.bDeleteMe)
            {
                continue;
            }

            if (a is SDisk disk)
            {
                // Disk Hunt only; shown when active or locked within 1500 uu.
                if (diskHunt && (disk.bActive || disk.bLocked) && (disk.Location - viewer.Location).Size() < HuntDiskErRange)
                {
                    f.Objectives.Add(Marker(disk.TriggerName, KindHuntDisk, RoleHuntDisk, disk.Location, disk.bTriggering, disk.bLocked, false, -1));
                }
            }
            else if (a is SObjective objective and not SMission)
            {
                ReadObjective(objective, region, merc, f);
            }
            else if (a is SFlagDropZone zone)
            {
                // Shown while triggering; alert colour for a merc or a spy carrying a disk.
                if (zone.bTriggering || ObjectiveHud.IsPs2DeliveryDestination(zone, pawn))
                {
                    f.Objectives.Add(Marker(zone.TriggerName, KindDropZone, RoleDropZone, zone.Location, true, zone.bLocked, false, -1,
                        alert: merc || pawn?.CarriedFlag is not null));
                }
            }
        }
    }

    /// <summary>
    /// One SObjective's markers: its own (outside its zones) and its triggers' (inside). The objective's marker
    /// triggers if any trigger does, is locked if any is, and shows the highest trigger percent; a bomb spot that is
    /// not armed hides from spies while another spot of the objective is armed.
    /// </summary>
    private void ReadObjective(SObjective objective, FPointRegion region, bool merc, HudFrame f)
    {
        GatherTriggers(objective);
        bool anyTriggering = false, anyLocked = false;
        int best = -1;
        int icon = KindOther;
        foreach (SObjectiveTrigger t in _triggers)
        {
            if (t.bDeleteMe)
            {
                continue;
            }

            anyTriggering |= t.bTriggering;
            anyLocked |= t.bLocked;
            int kind = ObjectiveKind(t);
            if (kind is not (KindOther or KindHuntDisk))
            {
                icon = kind;
            }

            if (t is SComputerObjectiveTrigger c)
            {
                best = Math.Max(best, CapturePercent(c));
            }
        }

        if (icon == KindOther)
        {
            _triggers.Clear();
            return;
        }

        // SObjective.ERObjectiveTargetVisibility: the objective's own marker shows while it is not taken and the viewer
        // does not see its triggers. SObjective.ERTriggerTargetVisibility is then, per trigger, "(the objective's
        // marker is off, or the viewer is in the trigger's zone) and the trigger is active or locked".
        bool objectiveShown = !objective.ObjectiveTaken && !SeesTriggersFrom(objective, region);
        foreach (SObjectiveTrigger t in _triggers)
        {
            if (t.bDeleteMe)
            {
                continue;
            }

            int kind = ObjectiveKind(t);
            bool zoneOk = !objectiveShown || t.Region.ZoneNumber == region.ZoneNumber;
            if (kind is KindOther or KindHuntDisk || !(zoneOk && (t.bActive || t.bLocked)))
            {
                continue;
            }

            if (kind == KindBomb && !t.bTriggering && anyTriggering && !merc)
            {
                continue;
            }

            int percent = t is SComputerObjectiveTrigger c ? CapturePercent(c) : -1;
            string name = t.TriggerName.Length > 0 ? t.TriggerName : objective.ObjectiveName;
            f.Objectives.Add(Marker(name, kind, RoleTrigger, t.Location, t.bTriggering, t.bLocked, objective.ObjectiveTaken, percent));
        }

        if (objectiveShown)
        {
            f.Objectives.Add(Marker(objective.ObjectiveName, icon, RoleObjective, objective.Location, anyTriggering, anyLocked,
                objective.ObjectiveTaken, icon == KindDisk ? -1 : best));
        }

        _triggers.Clear();
    }

    /// <summary>
    /// The triggers an objective is presented by, into the reused list (deleted ones included, null entries not).
    /// The same choice as the simulation's ObjectiveHud.PresentationTriggers: the objective's own list, or, when PS2
    /// triggers were replaced by dynamic ones, its PC triggers and then every PS2 trigger that points back at it.
    /// It is written out here because that helper is an iterator: walking it for each test (and again inside
    /// SObjective.ERSeesTriggersFrom) allocated more each tick than the whole client simulation does.
    /// </summary>
    private void GatherTriggers(SObjective objective)
    {
        _triggers.Clear();
        bool ps2 = false;
        foreach (SObjectiveTrigger? t in objective.Triggers)
        {
            if (t is { bPS2Objective: true })
            {
                ps2 = true;
                break;
            }
        }

        if (ps2 && objective.XLevel is { } level)
        {
            foreach (SObjectiveTrigger? t in objective.Triggers)
            {
                if (t is { bPS2Objective: false })
                {
                    _triggers.Add(t);
                }
            }

            foreach (SObjectiveTrigger t in level.AllActors<SObjectiveTrigger>())
            {
                if (t.bPS2Objective && !t.bDeleteMe && ReferenceEquals(t.Objective, objective))
                {
                    _triggers.Add(t);
                }
            }

            return;
        }

        foreach (SObjectiveTrigger? t in objective.Triggers)
        {
            if (t is not null)
            {
                _triggers.Add(t);
            }
        }
    }

    /// <summary>
    /// SObjective.ERSeesTriggersFrom over the gathered triggers: the viewer stands in one of the objective's listed
    /// zones, in a trigger's zone, or in the objective's own.
    /// </summary>
    private bool SeesTriggersFrom(SObjective objective, FPointRegion viewer)
    {
        foreach (ZoneInfo? zone in objective.TriggersERVisibilityZones)
        {
            if (zone is not null && ReferenceEquals(zone, viewer.Zone))
            {
                return true;
            }
        }

        foreach (SObjectiveTrigger t in _triggers)
        {
            if (t.Region.ZoneNumber == viewer.ZoneNumber)
            {
                return true;
            }
        }

        return objective.Region.ZoneNumber == viewer.ZoneNumber;
    }

    /// <summary>The mission status page's objective list: the mission of the game mode, each objective for the player's side.</summary>
    private void ReadObjectiveLines(SPlayerController pc, Level level, GameReplicationInfo gri, HudFrame f)
    {
        bool spy = (pc.PlayerReplicationInfo?.Team?.TeamIndex ?? 0) == 0;
        foreach (SMission mission in level.AllActors<SMission>())
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

                // In a chained mission everything after the first open objective is still to do.
                bool open = !o.ObjectiveTaken || (mission.bChained && oneNotTaken);
                oneNotTaken |= open;
                // The map writes line breaks as '|'; the id is kept per description so no string is built each tick.
                string raw = (spy ? o.Description : o.DescriptionDEF) ?? "";
                if (!_lineIds.TryGetValue(raw, out int textId))
                {
                    textId = _texts.Id(raw.Replace('|', ' '));
                    _lineIds[raw] = textId;
                }

                f.ObjectiveLines.Add(new HudObjectiveLine { TextId = textId, Done = open ? 0 : 1 });
            }
        }
    }

    // ---- radar -----------------------------------------------------------------------------------------------

    private const int BlipTeammate = 0, BlipEnemy = 1, BlipAlarm = 2, BlipGpo = 3;

    /// <summary>
    /// Radar blips: other pawns of the player's side are teammates; enemies show when spy-bulleted by the player's
    /// side, when the heart beat sensor found them, and (for mercs) a spy carrying the disk faster than 200 uu/s;
    /// mercs also get an alarm blip for every malfunctioning game object and every trigger in SHud.ObjectivesList (a
    /// terminal being hacked). Spies get the malfunctioning objects as their own kind (the orange markers).
    /// </summary>
    private static void ReadRadar(SPlayerController pc, SBasePawn? own, SHud? hud, bool merc, HudFrame f)
    {
        bool heartDetected = !merc && GadgetHudRules.Heart(pc) == HeartCone.Detected;
        // The pawns, not the controllers: a client only has its own controller.
        foreach (SBasePawn q in pc.XLevel.AllActors<SBasePawn>())
        {
            if (q.bDeleteMe || ReferenceEquals(q, own) || q.Health <= 0)
            {
                continue;
            }

            bool sameSide = (q.SPawnType == PawnType.SPAWNDEFENSE) == merc;
            if (sameSide)
            {
                Add(BlipTeammate, q.Location);
            }
            else if (q.bSpyBulletOn && q.iTeamSpying == (merc ? 1f : 0f))
            {
                Add(BlipEnemy, q.Location);
            }
            else if (merc && q.CarriedFlag is not null && CarrierRadarSpeed(pc, q) > FlagCarrierRadarSpeed)
            {
                Add(BlipEnemy, q.Location);
            }
            else if (heartDetected && q.bDetectedByHBS)
            {
                Add(BlipEnemy, q.Location);
            }
        }

        if (hud is null)
        {
            return;
        }

        foreach (FVector v in hud.MalfunctionGamePlayObjects)
        {
            Add(merc ? BlipAlarm : BlipGpo, v);
        }

        if (!merc)
        {
            return;
        }

        foreach (SObjectiveTrigger? t in hud.ObjectivesList)
        {
            if (t is { bDeleteMe: false })
            {
                Add(BlipAlarm, t.Location);
            }
        }

        void Add(int kind, FVector at) => f.Radar.Add(new HudBlip { Kind = kind, X = at.X, Y = at.Y, Z = at.Z });
    }

    /// <summary>
    /// The disk carrier's speed the merc radar tests (Reloaded): the last replicated velocity on a client, not its
    /// smoothed proxy movement.
    /// </summary>
    private static float CarrierRadarSpeed(SPlayerController pc, SBasePawn q) =>
        (pc.Level.NetMode == ENetMode.NM_Client ? q.ReceivedVelocity : q.Velocity).Size();

    // ---- interactions and prompts ----------------------------------------------------------------------------

    /// <summary>
    /// The interaction menu: usable entries in slot order. The original shows the list when at least two are usable,
    /// the pawn is awake and the controller can interact; the selection shows while Use is held.
    /// </summary>
    private void ReadInteractions(SPlayerController pc, SBasePawn? pawn, HudFrame f, ref int flags)
    {
        if (pc.IManager is not { bDeleteMe: false } manager || pawn is null)
        {
            return;
        }

        ref HudState s = ref f.State;
        // Exit (the menu's own way back) is always usable, so it never counts as something to use.
        int things = 0;
        int onlyThing = -1;
        bool exitSelected = false;
        for (int i = 0; i < manager.NbObjects && i < manager.Interactions.Length; i++)
        {
            if (manager.Interactions[i] is { bDeleteMe: false } entry && entry.CanUse(pc))
            {
                bool selected = i == manager.SelectedInteractions;
                if (selected)
                {
                    s.SelectedInteraction = f.Interactions.Count;
                    exitSelected = entry is SLeaveInteraction;
                }

                if (entry is not SLeaveInteraction)
                {
                    onlyThing = things++ == 0 ? f.Interactions.Count : -1;
                }

                f.Interactions.Add(new HudInteraction
                {
                    DescriptionId = _texts.Id(entry.Description),
                    ClassId = ClassId(entry),
                    Selected = selected ? 1 : 0,
                });
            }
        }

        s.InteractionCount = f.Interactions.Count;
        bool canInteract = !pawn.bSleeping && CanInteract(pc);
        flags |= pc.bUsing != 0 ? HudFlags.InteractionFocus : 0;
        flags |= f.Interactions.Count >= 2 && canInteract ? HudFlags.ShowInteractions : 0;
        if (canInteract && f.Interactions.Count > 0)
        {
            // What Use acts on: the selection (Exit only while the menu is held open on it), or the only thing
            // there is to use. With nothing in reach there is no prompt.
            bool selection = s.SelectedInteraction >= 0 && (!exitSelected || pc.bUsing != 0);
            int use = selection ? s.SelectedInteraction : onlyThing;
            s.UsePromptId = use >= 0 ? f.Interactions.Span[use].DescriptionId : 0;
        }
    }

    /// <summary>
    /// SPlayerController.CanInteract as far as the HUD needs it: true in the plain walking states and in the menu,
    /// false elsewhere (as OpenSCCT's HudK.CanInteract).
    /// </summary>
    private static bool CanInteract(SPlayerController pc)
    {
        string state = pc.GetStateName().ToString();
        return state.StartsWith("s_PlayerWalking", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(state, "s_MenuInteraction", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>The spy page's prompts: Speak / Hack and the coop move icon.</summary>
    private void ReadPrompts(SPlayerController pc, SBasePawn? pawn, bool merc, ref HudState s)
    {
        if (merc)
        {
            return;
        }

        bool nearDowned = false;
        if (pawn is not null)
        {
            for (Controller? c = pc.Level.ControllerList; c is not null; c = c.nextController)
            {
                if (c.Pawn is not SBasePawn { bDeleteMe: false } q || ReferenceEquals(q, pawn))
                {
                    continue;
                }

                if (q.SPawnType != pawn.SPawnType)
                {
                    nearDowned |= q.Health > 0 && q.bLyingOnGround && (q.Location - pawn.Location).Size() < pc.fDistToTalkWhenOnGround;
                }
            }
        }

        // Speak while grabbing a merc or next to a downed one; Hack while listening to a spy-bulleted one.
        if (pc.bCanTalkToGuy || pc.bCanListenSpyBulletedGuy || nearDowned)
        {
            bool speak = pc.bCanTalkToGuy || nearDowned;
            s.SpeakPrompt = speak ? 1 : 2;
            s.SpeakPromptId = _texts.Id(_strings.Get("MagmaDynamic", "HUD", speak ? "TextChat1" : "TextChat2"));
        }

        s.CoopIcon = CoopIcon(pc, pawn);
    }

    private static readonly Name NAME_s_Hack_Use = new("s_Hack_Use");
    private static readonly Name NAME_s_Computer_Use = new("s_Computer_Use");

    /// <summary>
    /// The spy page's coop icon, as the native spy HUD driver picks it (ported from
    /// game/Scripts/Hud/InteractionPrompts.cs): none while hacking, in a tutorial level, or while the own pawn is a
    /// ready helper. Otherwise a ready boost helper in reach gives "climb on him" (2), a mate hanging ready for a hoist
    /// "be boosted up" (4); with neither, a walking pawn at a boost spot gets "offer a ladder" (1) and a pawn hanging
    /// where a hoist is possible "offer a hoist" (3).
    /// </summary>
    private static int CoopIcon(SPlayerController pc, SBasePawn? pawn)
    {
        if (pawn is null || pc.IsInState(NAME_s_Hack_Use) || pc.IsInState(NAME_s_Computer_Use) || pc.CoopTutorialLevel() ||
            pawn.bReadyForShortLadder || pawn.bReadyForHoistClimb)
        {
            return 0;
        }

        switch (pc.CheckIfCanInteractWithMate(out _, true))
        {
            case SPlayerController.CA_ShortLadder:
                return 2;
            case SPlayerController.CA_HoistClimb:
                return 4;
            case SPlayerController.CA_None:
                if (pawn.Physics == EPhysics.PHYS_Walking && pawn.CheckIfCanStartSHL(out _, out _))
                {
                    return 1;
                }

                return pawn.Physics == EPhysics.PHYS_Ledge && pawn.FindHoistClimbType() > 0 ? 3 : 0;
            default:
                return 0;
        }
    }

    // ---- gadget views and binoculars -------------------------------------------------------------------------

    /// <summary>
    /// The gadget views (OpenSCCT's GadgetHudRules, drawn by game/Scripts/Hud/Gadgets): the sticky cam and camera
    /// network views that replace the player page, the sticky cam and mine menus, the gas mask gauge and the heart
    /// beat sensor's cone.
    /// </summary>
    private void ReadGadgetViews(SPlayerController pc, SHud? shud, HudFrame f, ref int fx)
    {
        ref HudState s = ref f.State;
        GadgetView view = GadgetHudRules.ViewOf(pc);
        s.GadgetView = (int)view;
        if (view == GadgetView.CamNetwork)
        {
            s.CamNetworkNameId = _texts.Id(GadgetHudRules.CamNetworkName(pc));
            fx |= GadgetHudRules.CamNetworkAlert(shud) ? HudEffectFlags.CamNetworkAlert : 0;
            // A camera that was shot shows no picture until it reconnects (the Godot HUD blanks the feed and counts down).
            s.CamNetworkDisabledSeconds = pc.SBPawn?.UsedObject is SCamNetwork { IsShotDisabled: true } camera ? camera.DisabledSeconds : 0;
        }

        if ((GadgetHudRules.StickyCamMenu(pc) ?? GadgetHudRules.MineMenu(pc)) is { } menu)
        {
            foreach (GadgetMenuLine line in menu.Lines)
            {
                f.Menu.Add(new HudMenuLine { TextId = _texts.Id(line.Text), Look = (int)line.Look });
            }
        }

        s.MenuLineCount = f.Menu.Count;
        GasMaskView mask = GadgetHudRules.GasMask(pc);
        if (mask.Shown)
        {
            s.GasMaskAirSeconds = mask.AirSeconds;
            fx |= (mask.Worn ? HudEffectFlags.GasMaskWorn : 0) | (mask.LowAir ? HudEffectFlags.GasMaskLowAir : 0);
        }

        s.HeartCone = (int)GadgetHudRules.Heart(pc);
    }

    /// <summary>
    /// The binocular views: which one the controller's state picks, the zoom step, the merc's breath, hit flash, range
    /// finder and aim point, and the spy's objective triggers.
    /// </summary>
    private void ReadBinoculars(SPlayerController pc, SBasePawn? pawn, SHud? shud, GameReplicationInfo? gri, bool merc, HudFrame f,
        ref int flags)
    {
        ref HudState s = ref f.State;
        if (pawn is null)
        {
            return;
        }

        if (!merc && pc.IsInState(NAME_s_Binoculars_Att))
        {
            s.BinocularMode = 1;
        }
        else if (merc && pc.GetStateName() == NAME_s_Binoculars_Def)
        {
            // Entering the state starts the rifle's raise; the scope view begins when that animation hands over.
            if (pawn.MyWeapon is SWeaponDefense rifle && rifle.ViewAnim.IsAnimating(SWeaponDefense.SNIPECHANNEL))
            {
                return;
            }

            s.BinocularMode = 2;
        }
        else
        {
            return;
        }

        s.BinocularFov = pc.fBinocularVisionFOV;
        s.BreathTime = pc.BinocularHoldBreathTime;
        flags |= (pc.bHoldBreath != 0 ? HudFlags.HoldBreath : 0) |
            ((flags & HudFlags.ReticleHit) != 0 ? HudFlags.BinocularHit : 0);
        s.HackPromptId = _texts.Id(_strings.Get("MagmaDynamic", "HUD", "TextChat2"));
        flags |= pc.VoiceChatTarget is { bDeleteMe: false } ? HudFlags.BinocularHackPrompt : 0;

        if (s.BinocularMode == 2)
        {
            // The range finder: a line from the rifle's FX bone along its X axis for 15000 uu (a collision query only).
            if (pawn.MyWeapon is SWeaponDefense { bDeleteMe: false } weapon)
            {
                FCoords bone = OpenScct.Game.Combat.CombatServices.Ports(weapon).BoneCoords(weapon, SWeaponDefense.FxBone);
                FVector start = bone.Origin;
                FVector end = start + bone.XAxis * BinocularRange;
                s.BinocularDistance = OpenScct.Game.Movement.MovementNatives.Trace(weapon, out FVector hit, out _, end, start, true) is null
                    ? 0f
                    : (hit - start).Size();
            }

            pc.MyDesiredRotation.GetAxes(out FVector x, out _, out _);
            FVector aim = pawn.Location + OpenScct.Game.Combat.CombatServices.Ports(pc).EyePosition(pawn) + x * BinocularAimDistance;
            s.AimX = aim.X;
            s.AimY = aim.Y;
            s.AimZ = aim.Z;
            flags |= HudFlags.BinocularAim;
            return;
        }

        if (shud is not null)
        {
            s.LockTextId = _texts.Id(shud.BinocularsAttLock);
            s.LockedTextId = _texts.Id(shud.BinocularAttLocked);
            s.DropZoneTextId = _texts.Id(shud.DropZone);
        }

        bool story = gri?.GameMode == EGameMode.GM_Multi;
        foreach (SObjectiveTrigger t in pc.XLevel.AllActors<SObjectiveTrigger>())
        {
            if (t.bDeleteMe || !(t.bActive || t.bLocked || (story && t is SFlagDropZone)))
            {
                continue;
            }

            int kind = ObjectiveKind(t);
            if (kind == KindOther)
            {
                continue;
            }

            int percent = t is SComputerObjectiveTrigger c ? CapturePercent(c) : 0;
            f.BinocularTargets.Add(Marker(t.TriggerName, kind, RoleTrigger, t.Location, t.bTriggering, t.bLocked, false, percent,
                marked: shud is not null && ReferenceEquals(shud.LockedObjective, t)));
        }

        s.BinocularTargetCount = f.BinocularTargets.Count;
    }

    // ---- round and scoreboard --------------------------------------------------------------------------------

    private unsafe void ReadRound(SPlayerController pc, LevelInfo info, GameReplicationInfo gri, int objectivesLeft, bool spectating,
        ref HudRound r)
    {
        r.Valid = 1;
        r.GameMode = (int)gri.GameMode;
        r.TimeLimit = gri.TimeLimit;
        r.ElapsedTime = gri.ElapsedTime;
        // The match times out at elapsed >= limit x 60, so that is the time left (the original page showed a second less).
        r.RemainingSeconds = gri.TimeLimit > 0 ? Math.Max(0, gri.TimeLimit * 60 - gri.ElapsedTime) : -1;
        r.Launching = gri.bMatchLaunching ? 1 : 0;
        r.Ended = pc.bGameEnded ? 1 : 0;
        r.WinningTeam = gri.WinningTeam;
        r.GamesInARow = gri.iGamesInARow;
        r.TeamScore[0] = info.NbPointsAttack;
        r.TeamScore[1] = info.NbPointsDefense;
        for (int t = 0; t < 2; t++)
        {
            r.ScoresInARow[t] = gri.iScoresInARow[t];
            r.TeamLives[t] = gri.TotalTeamLives[t];
            r.TeamSize[t] = gri.Teams[t]?.Size ?? 0;
        }

        r.NumObjectives = gri.NumObjectives;
        r.ObjectivesLeft = objectivesLeft;
        r.NumDisks = gri.NumDisks;
        r.NumDisksToWin = gri.NumDisksToWin;
        r.CounterMode = gri.GameMode == EGameMode.GM_DeathMatch ? 1 : gri.GameMode == EGameMode.GM_DiskHunt ? 2 : 0;
        // Some maps leave the level title at the engine's default. The map catalog holds their display name.
        string title = info.Title;
        if (string.IsNullOrWhiteSpace(title) || title.Trim().Equals("Untitled", StringComparison.OrdinalIgnoreCase))
        {
            title = _mapTitle ??= MenuExports.MapTitle(_map);
        }
        r.MapTitleId = _texts.Id(title);
        if (!spectating || pc.bGameEnded || pc.PlayerReplicationInfo is not { } pri)
        {
            return;
        }

        // The spectator page (ported from HudSimReader.ReadSpectator).
        SBasePawn? target = pc.SpecTarget is { bDeleteMe: false } watched ? watched : null;
        PlayerReplicationInfo? tp = target?.PlayerReplicationInfo;
        r.Visitor = pri.bOnlySpectator ? 1 : 0;
        if (tp is not null)
        {
            r.WatchedNameId = _texts.Id(tp.PlayerName);
            r.WatchedPlayerId = tp.PlayerID;
        }

        r.ShowSwitchPlayer = pri.bOnlySpectator || (tp?.Team?.Size ?? 0) >= SwitchPlayerTeamSize ? 1 : 0;
        if (pri.Team is { } team && team.TeamIndex is >= 0 and < 2 && gri.Teams[team.TeamIndex] is STeamInfo teamInfo &&
            pri.iTipNumber >= 0 && pri.iTipNumber < teamInfo.aTipStrings.Count)
        {
            r.TipId = _texts.Id(teamInfo.aTipStrings[pri.iTipNumber]);
        }

        if (pri.NumLives > 0 && !pc.IsInState(NAME_s_Spectating_Visitor))
        {
            int seconds = RemainingSeconds(pc.fRespawnDate - pc.XLevel.WorldTimeSeconds);
            r.RespawnIn = seconds is > 0 and < RespawnShownBelow ? seconds : 0;
        }
        else
        {
            r.NoRespawn = 1;
        }
    }

    /// <summary>Every player for the scoreboard: spies, mercs, then spectators.</summary>
    private void ReadScore(SPlayerController pc, Level level, GameReplicationInfo gri, HudFrame f)
    {
        // Who is known to be alive here: a client has the pawns that are relevant to it, with their PRIs.
        _alive.Clear();
        _pawnIds.Clear();
        foreach (SBasePawn q in level.AllActors<SBasePawn>())
        {
            // A spy's decoy is a pawn too, but it is not the player's body.
            if (q.bDeleteMe || q is SpyClone || q.PlayerReplicationInfo is not { } owner)
            {
                continue;
            }

            if (q.Health > 0)
            {
                _alive.Add(owner);
                _pawnIds[owner] = q.ActorId;
            }
            else
            {
                _pawnIds.TryAdd(owner, q.ActorId);
            }
        }

        bool deathMatch = gri.GameMode is EGameMode.GM_DeathMatch or EGameMode.GM_MercDeathMatch;
        PlayerReplicationInfo? own = pc.PlayerReplicationInfo;
        for (int pass = 0; pass < 3; pass++)
        {
            foreach (PlayerReplicationInfo? pri in gri.PRIArray)
            {
                if (pri is not { bDeleteMe: false })
                {
                    continue;
                }

                int team = pri.Team?.TeamIndex ?? 0;
                bool spectator = pri.bOnlySpectator;
                if ((pass == 2) != spectator || (!spectator && team != pass))
                {
                    continue;
                }

                f.Score.Add(new HudScoreRow
                {
                    PlayerId = pri.PlayerID,
                    NameId = _texts.Id(pri.PlayerName),
                    Team = team,
                    Flags = (ReferenceEquals(pri, own) ? HudScoreFlags.Local : 0) | (spectator ? HudScoreFlags.Spectator : 0) |
                        (_alive.Contains(pri) ? HudScoreFlags.Alive : 0) | (pri.NumLives <= 0 ? HudScoreFlags.OutOfLives : 0) |
                        (pri.bHasFlag ? HudScoreFlags.HasFlag : 0),
                    ObjOrKills = team == 0 && !deathMatch ? pri.iNbObjectivesTaken : pri.Kills,
                    Kills = pri.Kills,
                    Deaths = pri.Deaths,
                    Score = (int)pri.Score,
                    Lives = pri.NumLives,
                    Ping = pri.Ping,
                    Rank = pri.SvmRank,
                    PawnActorId = _pawnIds.GetValueOrDefault(pri),
                });
            }
        }

        _alive.Clear();
        _pawnIds.Clear();
    }
}
