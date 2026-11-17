using System;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Combat;
using OpenScct.Game.SBase;

namespace OpenScct.UnrealBridge.Hud;

/// <summary>
/// The HUD's eight screen-effect slots: colour washes over the view (black, red, white, the knock-out screen) and
/// the two render factors the slots drive (cross-eye blur, flash after-image). Ported from OpenSCCT's
/// game/Scripts/Hud/HudSfx.cs without its drawing; it is the HUD side of the original's SHud.AddVisualSfx,
/// SfxForceSlot and ProcessSFX. It never writes simulation state.
/// </summary>
public sealed class HudSfxSlots
{
    public const int Slots = HudState.SfxSlots;

    /// <summary>AddVisualSfx's duration when none is given.</summary>
    public const float DefaultDuration = 5f;

    /// <summary>A start value that means "from the slot's current value".</summary>
    public const float KeepCurrent = -10000f;

    private readonly SFXType[] _type = new SFXType[Slots];
    private readonly SFXAttenuation[] _evolution = new SFXAttenuation[Slots];
    private readonly float[] _current = new float[Slots];
    private readonly float[] _startTime = new float[Slots];
    private readonly float[] _startValue = new float[Slots];
    private readonly float[] _endTime = new float[Slots];
    private readonly float[] _endValue = new float[Slots];
    private readonly bool[] _kill = new bool[Slots];
    private int _forcedSlot = -1;

    /// <summary>PlayerController.CrossEyedFactor as the slots set it (the render's double vision, 0..1).</summary>
    public float CrossEyedFactor { get; private set; }

    /// <summary>PlayerController.RemanenceBlendFactor as the slots set it (the flash-bang after-image, 0..1).</summary>
    public float RemanenceBlendFactor { get; private set; }

    /// <summary>A knock-out slot is running (the HUD hides while it is dark).</summary>
    public bool Sleeping => Find(SFXType.SFXT_TextureColorSleep) >= 0;

    public SFXType TypeOf(int slot) => _type[slot];

    public float ValueOf(int slot) => _current[slot];

    /// <summary>The first slot of <paramref name="type"/>, or -1.</summary>
    public int Find(SFXType type) => Array.IndexOf(_type, type);

    /// <summary>SHud.SfxForceSlot: the next effect goes to slot <paramref name="slot"/>.</summary>
    public void SfxForceSlot(int slot) => _forcedSlot = slot;

    /// <summary>SHud.AddVisualSfx; false when no slot was free.</summary>
    public bool AddVisualSfx(SFXType type, float startValue, float endValue, float duration, SFXAttenuation evolution, float now,
        bool dontKill = false)
    {
        int slot = _forcedSlot;
        if (slot < 0)
        {
            // The first free slot; a new flash takes over the slot of a running one.
            slot = 0;
            while (slot < Slots && _type[slot] != SFXType.SFXT_None && !(_type[slot] == SFXType.SFXT_FlashEffect && type == SFXType.SFXT_FlashEffect))
            {
                slot++;
            }
        }
        else
        {
            _forcedSlot = -1;
            if (slot < Slots)
            {
                switch (_type[slot])
                {
                    case SFXType.SFXT_CrossEye:
                        CrossEyedFactor = 0f;
                        break;
                    case SFXType.SFXT_Smoke when type != SFXType.SFXT_Smoke:
                        CrossEyedFactor = 0f;
                        break;
                    case SFXType.SFXT_FlashEffect:
                        RemanenceBlendFactor = 0f;
                        break;
                }
            }
        }

        if (slot >= Slots)
        {
            return false;
        }

        _kill[slot] = !dontKill;
        _startTime[slot] = now;
        _endTime[slot] = now + (duration != 0f ? duration : DefaultDuration);
        _evolution[slot] = evolution;
        _type[slot] = type;
        _startValue[slot] = startValue == KeepCurrent ? _current[slot] : startValue;
        _endValue[slot] = endValue;
        return true;
    }

    /// <summary>
    /// SHud.ProcessSFX without drawing: updates every slot's value and the render factors for the time
    /// <paramref name="now"/>, and ends finished slots.
    /// </summary>
    public void Process(float now)
    {
        for (int i = 0; i < Slots; i++)
        {
            if (_type[i] == SFXType.SFXT_None)
            {
                continue;
            }

            float start = _startValue[i], end = _endValue[i];
            if (_evolution[i] == SFXAttenuation.SFXA_Constant)
            {
                _current[i] = start;
            }
            else if (_evolution[i] == SFXAttenuation.SFXA_Linear)
            {
                float span = _endTime[i] - _startTime[i];
                _current[i] = span != 0f ? start + (end - start) * (now - _startTime[i]) / span : end;
            }
            else
            {
                _type[i] = SFXType.SFXT_None;
                continue;
            }

            _current[i] = Math.Clamp(_current[i], Math.Min(start, end), Math.Max(start, end));
            switch (_type[i])
            {
                case SFXType.SFXT_CrossEye:
                case SFXType.SFXT_Smoke:
                    CrossEyedFactor = Math.Clamp(_current[i], 0f, 1f);
                    break;
                case SFXType.SFXT_FlashEffect:
                    RemanenceBlendFactor = _current[i];
                    break;
            }

            if (_endTime[i] <= now && _kill[i])
            {
                if (_type[i] is SFXType.SFXT_CrossEye or SFXType.SFXT_Smoke)
                {
                    CrossEyedFactor = 0f;
                }
                else if (_type[i] == SFXType.SFXT_FlashEffect)
                {
                    RemanenceBlendFactor = 0f;
                }

                _type[i] = SFXType.SFXT_None;
                _current[i] = end;
            }
        }
    }

    /// <summary>Ends every knock-out slot at once (the pawn died or the match ended).</summary>
    public void EndSleep()
    {
        for (int i = 0; i < Slots; i++)
        {
            if (_type[i] == SFXType.SFXT_TextureColorSleep)
            {
                _type[i] = SFXType.SFXT_None;
                _current[i] = _endValue[i];
            }
        }
    }

    /// <summary>Clears every slot (a new HUD).</summary>
    public void Reset()
    {
        Array.Clear(_type);
        Array.Clear(_current);
        _forcedSlot = -1;
        CrossEyedFactor = 0f;
        RemanenceBlendFactor = 0f;
    }
}

/// <summary>
/// The HUD's own effect state, fed by the simulation's presentation notifications for the local player (raised where
/// the original called the SHud natives): the screen-effect slots, the damage feedback of ClientDamageHudEffect, the
/// merc's flash white-out, foam and shutdown, the throw charge and the goggle messages. Ported from OpenSCCT's
/// game/Scripts/Hud/HudEffects.cs; it keeps presentation state only, and the few messages it adds go on the
/// player's SHud as the original's do.
/// </summary>
public sealed class HudEffectState
{
    /// <summary>ClientDamageHudEffect on a spy: a red wash from alpha 120 to 0 in 0.5 s.</summary>
    public const float DamageRedStart = 120f;
    public const float DamageRedTime = 0.5f;

    /// <summary>ClientDamageHudEffect type 8 (flash-bang): the after-image fades from 1 to 0 in 8 s.</summary>
    public const float FlashTime = 8f;

    /// <summary>The merc HUD reboots this many whole seconds after an EMP shut it down (MercHudPage.ShutDownSeconds).</summary>
    public const float ShutDownSeconds = 12f;

    /// <summary>ClientDamageHudEffect type 7 (extinguisher): SetHudProjection(1, 5, 10).</summary>
    private const int FoamSplash = 1, FoamStay = 5, FoamClean = 10;

    private const int DamageExtinguisher = (int)SDamageType.SDTExtincteurProjection;
    private const int DamageFlash = (int)SDamageType.SDTFlash;
    private const int DamageStickyShocker = (int)SDamageType.SDTStickyShocker;

    private static readonly Name NAME_s_Dead = new("s_Dead");

    public HudSfxSlots Sfx { get; } = new();

    /// <summary>The last throw charge the simulation reported (SHud.SetBallisticValue).</summary>
    public float Ballistic { get; private set; }

    /// <summary>Damage perturbation the HUD runs itself (merc page), as an end time; 0 when none.</summary>
    public float PerturbedUntil { get; private set; }

    /// <summary>Level time of the last flash-bang white-out on the merc page, or -1.</summary>
    public float LastFlashTime { get; private set; } = -1f;

    /// <summary>Level time of the last merc HUD shutdown, or -1.</summary>
    public float ShutDownTime { get; private set; } = -1f;

    /// <summary>True from a shutdown until its reboot.</summary>
    public bool ShutDown => _shutDownEndsAt >= 0f;

    public int ShutDowns { get; private set; }

    public int Flashes { get; private set; }

    /// <summary>After-image captures asked for (the view freezes a frame on each change).</summary>
    public int RemanenceCaptures { get; private set; }

    /// <summary>Hits on the local player so far, the SDamageType of the last and when it landed.</summary>
    public int DamageCount { get; private set; }

    public int LastDamageType { get; private set; }

    public float LastDamageTime { get; private set; }

    // The foam (SHud.DrawProjection): kept here because the simulation does not run it.
    public bool Projection { get; private set; }

    public bool NeigeDisp { get; private set; }

    public int TimeStopProj { get; private set; }

    public int TimeSnowToStay { get; private set; }

    private int _timeToSplash;
    private int _timeToClean;
    private int _apparition;
    private int _disparition;
    private int _timeBeginClean;
    private float _shutDownEndsAt = -1f;

    /// <summary>
    /// Applies the simulation's notifications that concern the local player <paramref name="pc"/> (its controller,
    /// pawn or goggle). Call it with each batch of events as they are read.
    /// </summary>
    public void Take(SPlayerController pc, ReadOnlySpan<SimEvent> events, float now)
    {
        int pawnId = pc.Pawn?.ActorId ?? -1;
        int goggleId = (pc.Goggle as OpenScct.Core.Engine.Actor)?.ActorId ?? -1;
        foreach (ref readonly SimEvent e in events)
        {
            if (e.Kind == SimEventKind.Notify && e.ActorId != 0 && (e.ActorId == pc.ActorId || e.ActorId == pawnId || e.ActorId == goggleId))
            {
                Apply(pc, e, now);
            }
        }
    }

    /// <summary>The per-tick counters (foam, shutdown reboot, SFX slots) for the time <paramref name="now"/>.</summary>
    public void Update(SPlayerController pc, float now)
    {
        if (pc.SPawnType == PawnType.SPAWNDEFENSE && !pc.IsInState(NAME_s_Dead))
        {
            DrawProjection(pc.RendMap, now);
        }

        StopHudShutDown(pc, now);
        if (pc.Pawn is not { Health: > 0 } || pc.bEndGame)
        {
            Sfx.EndSleep();
        }

        Sfx.Process(now);
    }

    private void Apply(SPlayerController pc, in SimEvent e, float now)
    {
        string what = e.What.ToString();
        bool merc = pc.SPawnType == PawnType.SPAWNDEFENSE;
        switch (what)
        {
            case "ClientDamageHudEffect":
                DamageCount++;
                LastDamageType = e.Slot;
                LastDamageTime = now;
                ClientDamageHudEffect(pc, e.Slot, now, e.Location.X != 0f);
                break;
            case "SetBallisticValue":
                Ballistic = e.Location.X;
                break;
            case "HudShutDown" when merc:
                ShutDownTime = now;
                ShutDowns++;
                _shutDownEndsAt = MathF.Truncate(now) + ShutDownSeconds;
                if (pc.myHUD is SHud hud)
                {
                    // SHud.HudShutDown: AddHudNumberMessageToStack(7, 3), "System malfunction".
                    HudMessageRules.AddHudNumberMessageToStack(hud, 7, 3, 0);
                }

                break;
            case "OverlayFade":
            {
                // AddVisualSfx(TextureColor or TextureColorSleep, start, end, time, Linear): the notification names the
                // type in its parameter when it has one; a fade to black without one is the knock-out screen.
                float start = e.Location.X, end = e.Location.Y, time = e.Location.Z;
                SFXType type = e.Slot is > 0 and <= (int)SFXType.SFXT_TextureColorSleep
                    ? (SFXType)e.Slot
                    : end > start ? SFXType.SFXT_TextureColorSleep : SFXType.SFXT_TextureColor;
                SFXAttenuation evolution = time > 0f ? SFXAttenuation.SFXA_Linear : SFXAttenuation.SFXA_Constant;
                // Waking up replaces the held knock-out overlay; another slot would leave the first one dark for good.
                if ((type == SFXType.SFXT_TextureColorSleep || e.Slot == 0 && end <= start) &&
                    Sfx.Find(SFXType.SFXT_TextureColorSleep) is >= 0 and var sleepSlot)
                {
                    Sfx.SfxForceSlot(sleepSlot);
                }

                Sfx.AddVisualSfx(type, start, end, time, evolution, now, dontKill: type == SFXType.SFXT_TextureColorSleep || end > start);
                break;
            }

            case "CrossEyeBlur":
                Sfx.AddVisualSfx(SFXType.SFXT_CrossEye, e.Location.X, e.Location.Y, e.Location.Z, SFXAttenuation.SFXA_Linear, now);
                break;
            case "SmokeBlur":
                // GoInSmoke: SfxForceSlot(0) then AddVisualSfx(SFXT_Smoke, blur, 0, time, Linear).
                Sfx.SfxForceSlot(0);
                Sfx.AddVisualSfx(SFXType.SFXT_Smoke, e.Location.X, 0f, e.Location.Y, SFXAttenuation.SFXA_Linear, now);
                break;
            case "SetHudPerturbationLength" when merc:
                // Started or not, the end becomes now + L (a shorter call shortens it).
                PerturbedUntil = now + e.Location.X;
                break;
            case "GoggleMessage":
                // SGoggle.VisionMessage raises it as Notify(owner, GoggleMessage, time, which): the message number
                // travels in the second value (Location.Y) and the parameter stays 0. OpenSCCT's HudEffects reads the
                // parameter, so its goggle messages never show; read the value the goggle actually sends, and the
                // parameter too in case the simulation starts using it.
                GoggleMessage(pc, e.Slot != 0 ? e.Slot : (int)e.Location.Y, e.Location.X);
                break;
        }
    }

    /// <summary>
    /// The end of a merc HUD shutdown: 12 whole seconds after it started the page clears bShutDown and calls
    /// SHud.StopHudShutDown, which adds "Rebooting ..." (HUD number message 8) for 3 s.
    /// </summary>
    private void StopHudShutDown(SPlayerController pc, float now)
    {
        if (_shutDownEndsAt < 0f || _shutDownEndsAt >= now)
        {
            return;
        }

        _shutDownEndsAt = -1f;
        if (pc.SPawnType == PawnType.SPAWNDEFENSE && pc.myHUD is SHud hud)
        {
            hud.bShutDown = false;
            HudMessageRules.AddHudNumberMessageToStack(hud, 8, 3, 0);
        }
    }

    /// <summary>The HUD part of SPlayerController.ClientDamageHudEffect.</summary>
    private void ClientDamageHudEffect(SPlayerController pc, int damageType, float now, bool cloneFlash)
    {
        bool merc = pc.SPawnType == PawnType.SPAWNDEFENSE;
        switch (damageType)
        {
            case DamageFlash:
                // The goggle's CurrentMode, not RendMap (they differ while the goggle clip plays): thermal, motion and
                // EMF vision are not blinded.
                if (!cloneFlash && (pc.Goggle?.CurrentMode ?? pc.RendMap) is 36 or 37 or 38)
                {
                    return;
                }

                RemanenceCaptures++;
                Sfx.AddVisualSfx(SFXType.SFXT_FlashEffect, 1f, 0f, FlashTime, SFXAttenuation.SFXA_Linear, now);
                if (merc)
                {
                    LastFlashTime = now;
                    Flashes++;
                }

                return;
            case DamageExtinguisher:
                // The HUD's part is SetHudProjection(1, 5, 10), which does nothing for spies.
                if (merc)
                {
                    SetHudProjection(FoamSplash, FoamStay, FoamClean, now);
                }

                return;
            case DamageStickyShocker:
                return;
        }

        // The merc's 1.5 s perturbation arrives as its own SetHudPerturbationLength notification; only the spy's red
        // wash is started here.
        if (pc.Pawn is SPawnAttaque)
        {
            Sfx.AddVisualSfx(SFXType.SFXT_TextureColorRed, DamageRedStart, 0f, DamageRedTime, SFXAttenuation.SFXA_Linear, now);
        }
    }

    /// <summary>SHud.SetHudProjection: the foam splash, its stay and its clean time.</summary>
    private void SetHudProjection(int splash, int stay, int clean, float now)
    {
        _timeToSplash = splash;
        TimeSnowToStay = stay;
        _timeToClean = clean;
        if (NeigeDisp)
        {
            _timeBeginClean = 0;
            _disparition = 0;
            TimeStopProj = (int)now;
        }
        else
        {
            Projection = true;
        }
    }

    /// <summary>
    /// SHud.DrawProjection's counters. The original steps them once per HUD draw; here once per 60 Hz tick, on a merc
    /// that is not dead.
    /// </summary>
    private void DrawProjection(int rendMap, float now)
    {
        if (Projection)
        {
            if (rendMap is 37 or 38)
            {
                return;
            }

            _apparition++;
            if (_apparition >= _timeToSplash * 10)
            {
                TimeStopProj = (int)now;
                Projection = false;
                NeigeDisp = true;
            }
        }
        else if (NeigeDisp)
        {
            if (TimeStopProj + TimeSnowToStay < now)
            {
                if (_timeBeginClean == 0)
                {
                    _timeBeginClean = (int)now;
                }

                if (_timeBeginClean + _disparition * 0.1f < now)
                {
                    _disparition++;
                }
            }

            if (_disparition >= _timeToClean * 10)
            {
                _timeBeginClean = _apparition = _disparition = 0;
                Projection = NeigeDisp = false;
            }
        }
    }

    /// <summary>SGoggle's mode messages on the auxiliary stack: 1 MTOn, 2 MTOff, 3 EMFOn, 4 EMFOff.</summary>
    private static void GoggleMessage(SPlayerController pc, int which, float time)
    {
        if (pc.myHUD is not SHud hud || pc.Goggle is not SGoggle goggle)
        {
            return;
        }

        string text = which switch
        {
            1 => goggle.MTOn,
            2 => goggle.MTOff,
            3 => goggle.EMFOn,
            4 => goggle.EMFOff,
            _ => "",
        };
        HudMessageRules.AddMessageToAuxiliaryStack(hud, text, Math.Max(1, (int)time), 0);
    }

    /// <summary>Forgets every effect (a new local player).</summary>
    public void Reset()
    {
        Sfx.Reset();
        Ballistic = 0f;
        PerturbedUntil = 0f;
        LastFlashTime = ShutDownTime = _shutDownEndsAt = -1f;
        Projection = NeigeDisp = false;
        _apparition = _disparition = _timeBeginClean = 0;
    }
}
