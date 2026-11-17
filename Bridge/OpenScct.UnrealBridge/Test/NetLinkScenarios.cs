using System;
using System.Collections.Generic;
using OpenScct.Bots;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Movement;
using OpenScct.Game.SBase;
using OpenScct.Physics.Collision;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Test;

/// <summary>
/// A scripted moment between two bot clients of a netcode check's dedicated server (Test/NetLinkLaunch.cs), stepped
/// once a tick after the server and the bots: each bot owns its pawn over the simulated link, so the moment is played
/// by two network owners while the local client watches it as a third.
/// </summary>
public abstract class NetLinkScenario
{
    /// <summary>The server's bots (the dedicated match's players besides the local one).</summary>
    public abstract IReadOnlyList<BotSpec> Bots { get; }

    /// <summary>Where the scenario is: 0 waiting for the players, then the scenario's own stages.</summary>
    public int Stage { get; protected set; }

    public bool Done { get; protected set; }

    /// <summary>Why the scenario stopped, or empty.</summary>
    public string Failure { get; protected set; } = "";

    /// <summary>What happened so far, for the check's log and verdict.</summary>
    public abstract string Detail { get; }

    /// <summary>The server pawn the scenario is about (the grabbed mercenary, the climber), or 0.</summary>
    public int SubjectActorId { get; protected set; }

    public void Step(BotMatch match, PlaySession play)
    {
        if (Done || Failure.Length > 0)
        {
            return;
        }

        try
        {
            OnStep(match, play, match.Server.World.TickCount);
        }
        catch (Exception e)
        {
            Failure = $"stage {Stage}: {e.Message}";
        }
    }

    protected abstract void OnStep(BotMatch match, PlaySession play, long tick);

    /// <summary>
    /// The real character clips on the server, the local client and every bot (the bots' levels are loaded without
    /// the animation timing the grab and the co-op moves wait on), as the Godot smokes install them.
    /// </summary>
    protected static void UseClips(BotMatch match, PlaySession play)
    {
        string chars = PlayMapLoader.FindCharsDir(play.MapFolder) ?? throw new System.IO.FileNotFoundException("Character clips");
        PawnAnimationSource animations = PawnAnimationSource.FromContent(chars);
        void Use(Level level) => level.SetService(new MovementService(animations, level.GetService<MovementService>()?.Hooks));
        Use(match.Server.World.Level);
        Use(play.Client.World.Level);
        foreach (BotClient bot in match.Bots)
        {
            Use(bot.Session.World.Level);
        }
    }

    protected const int Seconds = Sim.Hz;
}

/// <summary>
/// PlayGrabNetcodeSmoke: a bot spy and a bot mercenary are put face to face (the spy behind the merc), the spy grabs
/// him, knocks him out and the merc gets up again. Done when he has recovered with exactly one knockout's damage.
/// </summary>
public sealed class GrabScenario : NetLinkScenario
{
    private int _health;
    private long _at;
    private bool _down;

    public override IReadOnlyList<BotSpec> Bots =>
    [
        new("Spy2", BotTeam.Spy, () => new IdleBrain { Stand = true }),
        new("Merc1", BotTeam.Merc, () => new IdleBrain()),
        new("Merc2", BotTeam.Merc, () => new IdleBrain()),
    ];

    public override string Detail => $"grab stage {Stage} ({StageName}), merc health drop {HealthDrop}";

    /// <summary>The grabbed merc's health lost so far (a knockout costs 51).</summary>
    public int HealthDrop { get; private set; }

    private string StageName => Stage switch
    {
        0 => "waiting",
        1 => "settling",
        2 => "arranged",
        3 => "grabbing",
        4 => "knocked out",
        5 => "recovered",
        _ => "?",
    };

    protected override void OnStep(BotMatch match, PlaySession play, long tick)
    {
        if (match.Bots[1].Controller is not { Pawn: SBasePawn ownMerc } ||
            match.ServerController(match.Bots[1]) is not { Pawn: SBasePawn sourceMerc } mercAuthority ||
            match.Bots[0].Controller is not { Pawn: SBasePawn ownSpy } spyOwner ||
            match.ServerController(match.Bots[0]) is not { Pawn: SBasePawn sourceSpy } spyAuthority)
        {
            return;
        }

        if (Stage > 1)
        {
            HealthDrop = _health - sourceMerc.Health;
        }

        switch (Stage)
        {
            case 0:
                // The players are in; give the standing bot its moment to get up.
                if (play.LocalPawn is not null && !ownSpy.bIsCrouched)
                {
                    Stage = 1;
                    _at = tick;
                }

                break;
            case 1:
                if (tick >= _at + 2 * Seconds)
                {
                    Arrange(match, play, ownSpy, sourceSpy, spyAuthority, ownMerc, sourceMerc, mercAuthority);
                    Stage = 2;
                    _at = tick;
                }

                break;
            case 2:
                if (tick >= _at + 2 * Seconds && spyOwner.AskToGrabPawn())
                {
                    Stage = 3;
                    _at = tick;
                }
                else if (tick > _at + 8 * Seconds)
                {
                    Failure = "the spy's grab was never accepted";
                }

                break;
            case 3:
                // Godot holds the merc for the rest of the 6 s grab step before the knockout.
                if (tick >= _at + 6 * Seconds && spyOwner.IsInState("s_GrabbingPlayer") &&
                    spyOwner.CheckForRoomToFreeGrabbedGuy(GrabReleaseMethode_.KO))
                {
                    Stage = 4;
                    _at = tick;
                }
                else if (tick > _at + 12 * Seconds)
                {
                    Failure = $"the spy never knocked the merc out (spy state {spyOwner.GetStateName()})";
                }

                break;
            case 4:
                _down |= sourceMerc.bSleeping;
                if (_down && !sourceMerc.bSleeping && !sourceMerc.bLyingOnGround)
                {
                    Stage = 5;
                    Done = true;
                    if (HealthDrop != 51)
                    {
                        Failure = $"the merc lost {HealthDrop} health, not one knockout's 51";
                    }
                }
                else if (tick > _at + 20 * Seconds)
                {
                    Failure = _down ? "the merc never got up" : "the merc never went down";
                }

                break;
        }
    }

    private void Arrange(BotMatch match, PlaySession play, SBasePawn ownSpy, SBasePawn sourceSpy, SPlayerController spyAuthority,
        SBasePawn ownMerc, SBasePawn sourceMerc, SPlayerController mercAuthority)
    {
        UseClips(match, play);
        _health = sourceMerc.Health;
        SubjectActorId = sourceMerc.ActorId;
        FRotator facing = sourceMerc.Rotation;
        facing.Pitch = facing.Roll = 0;
        FVector mercAt = sourceMerc.Location, spyAt = mercAt - facing.Vector() * 85;
        spyAt.Z += ownSpy.CollisionHeight - ownMerc.CollisionHeight;

        static void Place(SBasePawn pawn, FVector at, FRotator facing)
        {
            MovementNatives.Driver(pawn)!.FarMoveActor(pawn, at);
            pawn.Rotation = facing;
            pawn.Velocity = pawn.Acceleration = default;
            ((SPlayerController)pawn.Controller!).SetViewRotation(facing);
        }

        Place(sourceSpy, spyAt, facing);
        Place(ownSpy, spyAt, facing);
        Place(sourceMerc, mercAt, facing);
        Place(ownMerc, mercAt, facing);
        sourceSpy.ResetNetworkHistory();
        sourceMerc.ResetNetworkHistory();
        match.Server.Movement.PeekEndpoint(spyAuthority)!.Sequenced!.ResetLifetime();
        match.Server.Movement.PeekEndpoint(mercAuthority)!.Sequenced!.ResetLifetime();
        sourceMerc.iLastWakeTime = -100;
        mercAuthority.TimeShockForGrab = -100;
        if (spyAuthority.GameReplicationInfo is { } gri)
        {
            gri.bAvlb_SPY_Grab = true;
        }

        // The local player watches from beside the merc, as Godot's observer camera does (the frames show the grab).
        uint mercId = (uint)sourceMerc.ActorId;
        play.ViewOverride = view =>
        {
            if (play.Client.Driver.FindActorByNetGuid(mercId) is not SBasePawn shown)
            {
                return view;
            }

            FVector focus = shown.Location + new FVector(0, 0, 15), camera = focus + new FVector(150, 200, 95);
            if (play.Client.Map?.Physics?.Collision is { } collision &&
                collision.LineCheck(out var hit, shown, camera, focus, new FVector(4, 4, 4), ETraceFlags.TRACE_World))
            {
                camera = hit.Location + hit.Normal * 4;
            }

            return view with { Location = camera, Rotation = (focus - camera).Rotation(), Fov = 80, Valid = true };
        };
    }

    private sealed class IdleBrain : BotBrain
    {
        public bool Stand;
        private bool _pressed;

        public override BotInput Think(BotContext context)
        {
            if (Stand && !_pressed && context.Controller.IsInState("s_PlayerWalkingCr0H"))
            {
                _pressed = true;
                return new BotInput { bCrouch = 1 };
            }

            return BotInput.Idle;
        }
    }
}

/// <summary>
/// PlayCoopNetcodeSmoke: two bot spies, each the owner of his pawn, boost and hoist at SteeE's GE63 wall while the
/// local mercenary watches: placed side by side, the helper offers his hands, the climber steps up onto the ledge,
/// then offers his hand and pulls the helper up. Done when the helper stands crouched on top (over 340 uu up).
/// </summary>
public sealed class CoopScenario : NetLinkScenario
{
    private long _arranged;

    public override IReadOnlyList<BotSpec> Bots =>
    [
        new("Spy1", BotTeam.Spy, () => new IdleBrain()),
        new("Spy2", BotTeam.Spy, () => new IdleBrain()),
        new("Merc2", BotTeam.Merc, () => new IdleBrain()),
    ];

    public override string Detail => $"coop stage {Stage} ({StageName})";

    private string StageName => Stage switch
    {
        0 => "waiting",
        1 => "placed",
        2 => "helper offers a boost",
        3 => "climber boosted",
        4 => "climber offers a hoist",
        6 => "helper hoisted",
        5 => "both up",
        _ => "?",
    };

    protected override void OnStep(BotMatch match, PlaySession play, long tick)
    {
        if (match.Bots[0].Controller is not { Pawn: SBasePawn ownClimber } climber ||
            match.Bots[1].Controller is not { Pawn: SBasePawn ownHelper } helper ||
            match.ServerController(match.Bots[0]) is not { Pawn: SBasePawn sourceClimber } authorityClimber ||
            match.ServerController(match.Bots[1]) is not { Pawn: SBasePawn sourceHelper } authorityHelper)
        {
            if (tick > 30 * Seconds)
            {
                Failure = "the two spies never both had pawns";
            }

            return;
        }

        if (Stage == 0 && tick >= 5 * Seconds && play.LocalPawn is not null && !ownClimber.bIsCrouched && !ownHelper.bIsCrouched)
        {
            UseClips(match, play);
            SubjectActorId = sourceClimber.ActorId;
            _arranged = tick;
            void Place(SPlayerController pc, float y, int yaw)
            {
                var pawn = (SBasePawn)pc.Pawn!;
                if (!MovementNatives.Driver(pawn)!.FarMoveActor(pawn, new FVector(-3340, y, -168 + 89.5f)))
                {
                    throw new InvalidOperationException("cannot place a co-op pawn at GE63");
                }

                pawn.Velocity = pawn.Acceleration = default;
                pawn.Rotation = new FRotator(0, yaw, 0);
                pc.SetViewRotation(pawn.Rotation);
            }

            Place(climber, 2301, 16384);
            Place(authorityClimber, 2301, 16384);
            Place(helper, 2451, -16384);
            Place(authorityHelper, 2451, -16384);
            sourceClimber.ResetNetworkHistory();
            sourceHelper.ResetNetworkHistory();
            match.Server.Movement.PeekEndpoint(authorityClimber)!.Sequenced!.ResetLifetime();
            match.Server.Movement.PeekEndpoint(authorityHelper)!.Sequenced!.ResetLifetime();
            play.ViewOverride = view =>
            {
                FVector focus = new(-3340, 2450, 50), camera = focus + new FVector(400, -400, 200);
                if (play.Client.Map?.Physics?.Collision is { } collision &&
                    collision.LineCheck(out var hit, null, camera, focus, new FVector(4, 4, 4), ETraceFlags.TRACE_World))
                {
                    camera = hit.Location + hit.Normal * 4;
                }

                return view with { Location = camera, Rotation = (focus - camera).Rotation(), Fov = 80, Valid = true };
            };
            Stage = 1;
        }
        else if (Stage == 1 && tick >= _arranged + 3 * Seconds)
        {
            helper.eventPerformCoopMvt();
            Stage = 2;
        }
        else if (Stage == 2 && sourceHelper.bReadyForShortLadder && ownHelper.bReadyForShortLadder &&
            match.Bots[0].Session.Driver.FindActorByNetGuid((uint)sourceHelper.ActorId) is SBasePawn { bReadyForShortLadder: true })
        {
            if (climber.DoCoopInteraction())
            {
                Stage = 3;
            }
        }
        else if (Stage == 3 && climber.IsInState(MovementStateNames.OnLedge) && authorityClimber.IsInState(MovementStateNames.OnLedge) &&
            helper.IsInState(MovementStateNames.s_PlayerWalkingSt0H))
        {
            climber.eventPerformCoopMvt();
            Stage = 4;
        }
        else if (Stage == 4 && sourceClimber.bReadyForHoistClimb && ownClimber.bReadyForHoistClimb &&
            match.Bots[1].Session.Driver.FindActorByNetGuid((uint)sourceClimber.ActorId) is SBasePawn { bReadyForHoistClimb: true })
        {
            if (helper.DoCoopInteraction())
            {
                Stage = 6;
            }
        }
        else if (Stage == 6 && helper.IsInState(MovementStateNames.s_PlayerWalkingCr0H) &&
            authorityHelper.IsInState(MovementStateNames.s_PlayerWalkingCr0H) && sourceHelper.Location.Z - sourceHelper.CollisionHeight > 340)
        {
            Stage = 5;
            Done = sourceClimber.Physics == EPhysics.PHYS_Ledge;
            if (!Done)
            {
                Failure = $"the helper is up but the climber left the ledge ({sourceClimber.Physics})";
            }
        }

        if (!Done && Failure.Length == 0 && Stage > 0 && tick > _arranged + 40 * Seconds)
        {
            Failure = $"the co-op move stopped at stage {Stage} ({StageName}): climber {climber.GetStateName()}/{authorityClimber.GetStateName()}, " +
                $"helper {helper.GetStateName()}/{authorityHelper.GetStateName()}";
        }
    }

    private sealed class IdleBrain : BotBrain
    {
        private bool _stood;

        public override BotInput Think(BotContext context)
        {
            if (!_stood && context.Controller.Pawn is SPawnAttaque && context.Controller.IsInState("s_PlayerWalkingCr0H"))
            {
                _stood = true;
                return new BotInput { bCrouch = 1 };
            }

            return BotInput.Idle;
        }
    }
}
