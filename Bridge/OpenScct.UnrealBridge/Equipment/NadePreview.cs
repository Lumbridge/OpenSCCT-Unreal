using System;
using System.Collections.Generic;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Physics.Collision;
using OpenScct.Physics.Movement;
using OpenScctGame.Play.Session;
using CombatNatives = OpenScct.Game.Combat.CombatNatives;
using CombatServices = OpenScct.Game.Combat.CombatServices;
using GK = OpenScct.Game.Equipment.K;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary>How a predicted flight ended.</summary>
public enum NadeEnd
{
    /// <summary>The fuse ran out (the grenades go off where they are).</summary>
    Fuse,

    /// <summary>It caught on the first surface (the flare).</summary>
    Stuck,

    /// <summary>The bounce rules stopped it (no surface left one second of flight ahead).</summary>
    Stopped,

    /// <summary>The prediction's own tick limit.</summary>
    Limit,
}

/// <summary>Where and how the simulation would spawn the projectile.</summary>
public struct NadeLaunch
{
    public FVector Origin;
    public FRotator Rotation;
    public FVector Velocity;
    public int Force;
}

/// <summary>A predicted flight: one position per simulation tick, and where it ends.</summary>
public sealed class NadeFlight
{
    public readonly List<FVector> Points = new(256);

    /// <summary>Indices into <see cref="Points"/> where the projectile bounced hard enough to mark.</summary>
    public readonly List<int> BouncePoints = new(16);

    public FVector Land;
    public FVector LastHitNormal;
    public FVector LastHitAt;
    public bool AnyHit;
    public NadeEnd End;
    public int Ticks;

    public float Seconds => Ticks * Sim.DeltaTime;

    public void Clear()
    {
        Points.Clear();
        BouncePoints.Clear();
        Land = default;
        LastHitNormal = default;
        LastHitAt = default;
        AnyHit = false;
        End = NadeEnd.Limit;
        Ticks = 0;
    }
}

/// <summary>
/// The flight a grenade gadget's projectile would make if thrown now, by the simulation's own rules: the launch of
/// SGadGrenLauncher.TryUseGren and ThrowGren (origin and rotation from the weapon's FX placement, rounded as the
/// throw rounds them), the velocity of SProjGrenade.Launch, then tick by tick the fuse timer, the falling step of
/// PhysicsDriver.physFallingActor (half-step gravity, the move sweep the simulation itself makes:
/// <see cref="PhysicsDriver.TraceMove"/>) and the bounce of SProjGrenade.HitWall (SProjFlare catches at its first
/// hit). The real projectile cannot be stepped off to the side: its launch, bounce and explosion play sounds, spawn
/// actors and draw random numbers. Bridge/Probes/NadeProbe throws real grenades and compares.
///
/// One deliberate difference: pawns are not in the way. The line is where the projectile goes if nobody steps into
/// it (and so shows nothing about players the thrower cannot see).
/// </summary>
public static class NadePredictor
{
    /// <summary>The longest flight followed, in ticks (the flare's 10 s fuse).</summary>
    public const int MaxTicks = 660;

    /// <summary>The speed into a surface (units a second) from which a bounce is marked on the drawn path.</summary>
    public const float MarkedBounceSpeed = 60f;

    /// <summary>
    /// The launch of an aimed throw at charge value <paramref name="v"/>, with the thrower's aim
    /// <paramref name="aim"/> (the merc's desired rotation, the spy's view) and <paramref name="offset"/> added to
    /// the pawn's place (zero for the tick's own). False when the simulation would not throw at all (no weapon out).
    /// </summary>
    public static bool Launch(SBasePawn pawn, SGadGrenLauncher gadget, FRotator aim, FVector offset, float v, out NadeLaunch launch)
    {
        launch = default;
        if (pawn.WeaponLoaded == 0 || pawn.MyWeapon is not { } weapon)
        {
            return false;
        }

        float speed = gadget.ProjectileDefaults?.ProjectileSpeed ?? 0f;
        int force = WeaponRules.ThrowForce(v, speed);
        FRotator rotation;
        FVector origin;
        if (pawn.SPawnType == PawnType.SPAWNDEFENSE)
        {
            FCoords fx = pawn.Controller is SPlayerController
                ? CombatNatives.MercThrowFxCoords(pawn, aim)
                : CombatServices.Ports(gadget).BoneCoords(weapon, SWeaponDefense.FxBone);
            rotation = pawn.Controller is SPlayerController ? aim : pawn.Rotation;
            origin = fx.Origin + (rotation.Vector() * GK.MercThrowForward);
        }
        else
        {
            FCoords fx = weapon is SWeaponAttaque && pawn is SPawnAttaque && pawn.Controller is SPlayerController
                ? CombatNatives.SpyPistolCoords(pawn, aim)
                : CombatServices.Ports(gadget).BoneCoords(weapon, SWeaponDefense.FxBone);
            rotation = fx.XAxis.Rotation();
            origin = fx.Origin + (rotation.Vector() * GK.SpyThrowForward);
        }

        origin += offset;
        // ThrowGren: whole units, the rotation cut to its high byte.
        launch.Origin = new FVector(SvmMath.Round(origin.X), SvmMath.Round(origin.Y), SvmMath.Round(origin.Z));
        launch.Rotation = new FRotator(rotation.Pitch & 0xFF00, rotation.Yaw & 0xFF00, rotation.Roll & 0xFF00);
        launch.Force = force;

        // SProjGrenade.Launch: along the rotation at Force + 210 plus the thrower's own speed that way.
        FVector dir = launch.Rotation.Vector();
        FVector owner = pawn.Velocity;
        float ownerSpeed = owner.Size();
        FVector along = ownerSpeed > 0f ? owner * (1f / ownerSpeed) : owner;
        float total = ((dir | along) * ownerSpeed) + (force + GK.GrenadeLaunchBonus);
        launch.Velocity = dir * total;
        return true;
    }

    /// <summary>
    /// Follows the flight from <paramref name="launch"/> into <paramref name="flight"/>. <paramref name="projectile"/>
    /// is a detached instance of the gadget's projectile class (never spawned; its collision size and rules are
    /// read, its bCollideActors is written and put back).
    /// </summary>
    public static void Fly(SBasePawn pawn, SProjGrenade projectile, in NadeLaunch launch, NadeFlight flight)
    {
        flight.Clear();
        FVector position = launch.Origin;
        FVector velocity = launch.Velocity;
        flight.Points.Add(position);
        flight.Land = position;
        if (PhysicsDriver.Of(pawn) is not { } physics || GadgetWorld.Collision(pawn) is not { } world)
        {
            return;
        }

        float gravityZ = PhysicsDriver.GetGravityZ(pawn);
        float fuse = projectile.TempsExplosion;
        float lifeSpan = projectile.LifeSpan;
        bool flare = projectile is SProjFlare;
        bool frag = projectile is SProjFrag;
        bool collideActors = projectile.bCollideActors;
        bool bounce = projectile.bBounce;
        float timer = 0f;
        float dt = Sim.DeltaTime;
        try
        {
            for (int tick = 0; tick < MaxTicks; tick++)
            {
                // SProjFrag.NativeTick: actors are in its way only once its fuse timer has counted a tick.
                if (frag)
                {
                    projectile.bCollideActors = timer != 0f;
                }

                flight.Ticks = tick + 1;
                // Actor.Tick: the timers, then the life span, then the physics.
                if (fuse > 0f && (timer += dt) >= fuse)
                {
                    flight.End = NadeEnd.Fuse;
                    return;
                }

                if (lifeSpan != 0f)
                {
                    lifeSpan -= dt;
                    if (lifeSpan <= 0.0001f)
                    {
                        flight.End = NadeEnd.Fuse;
                        return;
                    }
                }

                // physFallingActor: one sub-step a tick (the tick is shorter than the longest sub-step).
                velocity += new FVector(0f, 0f, gravityZ) * (0.5f * dt);
                FVector delta = velocity * dt;
                if (!physics.TraceMove(projectile, position, delta, out FCheckResult hit, null, bIgnorePawns: true) || !hit.bHit)
                {
                    position += delta;
                    flight.Points.Add(position);
                    flight.Land = position;
                    continue;
                }

                position = hit.Location;
                flight.Points.Add(position);
                flight.Land = position;
                flight.AnyHit = true;
                flight.LastHitNormal = hit.Normal;
                flight.LastHitAt = position;
                if (flare)
                {
                    // SProjFlare.HitWall and Explode: the burning flare stands off the surface it caught on.
                    flight.Land = position + (hit.Normal * projectile.ExploWallOut) + (hit.Normal.SafeNormal() * GK.FlareStandOff);
                    flight.End = NadeEnd.Stuck;
                    return;
                }

                if (!bounce)
                {
                    // A mover stopped it dead (below): it lies where it next comes down.
                    flight.End = NadeEnd.Stopped;
                    return;
                }

                // A bounce worth a mark: one that turns the path. The hops of a grenade already lying on the floor are not.
                if (SvmMath.Abs(velocity | hit.Normal) >= MarkedBounceSpeed)
                {
                    flight.BouncePoints.Add(flight.Points.Count - 1);
                }

                // SProjGrenade.HitWall.
                if (hit.Actor is LevelInfo || hit.Actor is null)
                {
                    FVector ahead = position + velocity;
                    if (!(world.LineCheck(out FCheckResult still, projectile, ahead, position, FVector.Zero, (ETraceFlags)GK.GrenadeBounceTraceFlags) && still.bHit))
                    {
                        flight.End = NadeEnd.Stopped;
                        return;
                    }
                }

                float vn = velocity | hit.Normal;
                velocity = (velocity - (hit.Normal * (2f * vn))) * projectile.WallReboundFactor;
                float speed = velocity.Size();
                if (velocity.Z <= GK.GrenadeUpBounceCap)
                {
                    if (speed < GK.GrenadeRestSpeed && projectile.TempsExplosion == 0)
                    {
                        flight.End = NadeEnd.Stopped;
                        return;
                    }
                }
                else
                {
                    velocity = new FVector(velocity.X, velocity.Y, (velocity.Z + GK.GrenadeUpBounceCap) * 0.5f);
                }

                if (hit.Actor is Mover { bCollideActors: true } mover && !projectile.bIgnoreEncroachers)
                {
                    FVector mv = mover.Velocity;
                    float size = mv.Size();
                    if (size > 0f && SvmMath.Abs((mv * (1f / size)) | hit.Normal) > GK.GrenadeMoverStopCos)
                    {
                        velocity = FVector.Zero;
                        bounce = false;
                    }
                }
            }

            flight.End = NadeEnd.Limit;
        }
        finally
        {
            projectile.bCollideActors = collideActors;
        }
    }

    /// <summary>The reach of the projectile's effect in units (blast, cloud, flash), 0 for none.</summary>
    public static float Radius(SProjGrenade projectile) => projectile switch
    {
        SProjFrag frag => frag.Range,
        SProjChaffe chaff => chaff.Range,
        SProjFlashBang flash => flash.Range,
        SProjSmoke smoke => smoke.DispersionRange,
        SProjPhospho phospho => phospho.DispersionRange,
        _ => 0f,
    };

    public static int Kind(SProjGrenade projectile) => projectile switch
    {
        SProjFrag => NadePreviewKind.Frag,
        SProjFlare => NadePreviewKind.Flare,
        SProjChaffe => NadePreviewKind.Chaff,
        SProjFlashBang => NadePreviewKind.FlashBang,
        SProjSmoke => NadePreviewKind.Smoke,
        SProjPhospho => NadePreviewKind.Phosphorus,
        _ => NadePreviewKind.Other,
    };
}

/// <summary>
/// The grenade preview of the local player (the console command nadepreview): each rendered frame Unreal asks for
/// the path the selected grenade gadget's projectile would fly if the throw button were let go now, from the aim of
/// the frame being drawn (<see cref="Controls.ControlsFeature.DrawnAim"/>, the mouse turn not yet simulated in it)
/// and the pawn where it is drawn between the last two ticks. The path is worked out again only when something it
/// depends on changed. Nothing here runs unless Unreal calls <see cref="Read"/>, which it does only while the
/// command is on.
/// </summary>
public sealed class NadePreviewFeature
{
    private const float UnitsToDegrees = 360f / 65536f;
    private static readonly Name s_fullReloading = new("s_FullReloading");
    private static readonly Name s_reloading = new("s_Reloading");
    private static readonly Name s_fireGrenade = new("s_FireGrenade");
    private static readonly Name s_elbowShocking = new("s_ElbowShocking");

    private readonly BridgeSession _session;
    private readonly Dictionary<string, SProjGrenade?> _detached = new(StringComparer.OrdinalIgnoreCase);
    private readonly NadeFlight _flight = new();
    private NadeLaunch _key;
    private SProjGrenade? _keyProjectile;
    private long _keyTick = -1;
    private bool _haveKey;
    private int _revision;
    private FVector _pip;
    private bool _pipValid;
    private FVector _markerAt;
    private FVector _markerNormal;
    private bool _airborne;

    private NadePreviewFeature(BridgeSession session) => _session = session;

    public static NadePreviewFeature Of(BridgeSession session) => session.Feature(static s => new NadePreviewFeature(s));

    /// <summary>How many times the path was worked out (for the probe and the log).</summary>
    public int Revision => _revision;

    /// <summary>The flight of the last <see cref="Read"/>.</summary>
    public NadeFlight Flight => _flight;

    /// <summary>
    /// The preview is an aim aid that also looks round corners, so it is for matches played on this machine and
    /// practice rounds, as the Godot client's practice aids (grabarea) are: not on someone else's server otherwise.
    /// </summary>
    public static bool IsAllowed(BridgeSession? session) =>
        session is not null && (!session.IsRemote || Test.GrabAreaReader.IsPracticeRound(session.Session));

    /// <summary>Writes this frame's preview. Returns the number of points written.</summary>
    public int Read(out BridgeNadePreview head, Span<BridgeNadePoint> points)
    {
        head = default;
        PlaySession play = _session.Session;
        if (!IsAllowed(_session) || play.LocalController is not { } pc || play.LocalPawn is not { Health: > 0, bDeleteMe: false } pawn ||
            pawn.SelectedGadget is not SGadGrenLauncher { bDeleteMe: false } gadget || pawn.WeaponLoaded == 0 || pawn.MyWeapon is not { } weapon ||
            Detached(gadget) is not { } projectile)
        {
            return 0;
        }

        LevelInfo level = pawn.Level;
        bool tutorial = GadgetWorld.CurLevelIsTutorial(gadget);
        if (gadget.Charge < 1 && gadget.Charge != GK.UnlimitedCharge && !tutorial)
        {
            return 0;
        }

        // The aim of the frame being drawn: the simulation's own (a merc throws along the desired rotation, a spy
        // along the view) plus the mouse turn the camera already shows.
        FRotator aim = pawn.SPawnType == PawnType.SPAWNDEFENSE ? pc.MyDesiredRotation : pc.Rotation;
        if (Controls.ControlsFeature.Of(_session).DrawnAim is { } drawn)
        {
            aim = new FRotator(aim.Pitch + (drawn.Pitch - pc.Rotation.Pitch), aim.Yaw + (drawn.Yaw - pc.Rotation.Yaw), aim.Roll);
        }

        // The gauge: a tap throws at full strength; held past the tap window it starts from nothing and fills.
        bool charging = pc.bUsingNow;
        float v = 1f;
        if (charging)
        {
            float held = level.TimeSeconds + (Sim.DeltaTime * _session.Alpha) - pc.TimerUSe;
            v = WeaponRules.ThrowValue(held, true, GK.ThrowTapWindow, GK.ThrowRamp);
        }

        if (!NadePredictor.Launch(pawn, gadget, aim, _session.DrawnOffset(pawn.ActorId), v, out NadeLaunch launch))
        {
            return 0;
        }

        // The world moves only on a tick, so between ticks the same launch flies the same path.
        long tick = _session.Ticks;
        bool moving = !pawn.Velocity.IsZero();
        if (!_haveKey || !ReferenceEquals(_keyProjectile, projectile) || launch.Origin != _key.Origin || launch.Rotation != _key.Rotation ||
            launch.Velocity != _key.Velocity || (moving && tick != _keyTick) || tick - _keyTick > Sim.Hz / 4)
        {
            _key = launch;
            _keyProjectile = projectile;
            _keyTick = tick;
            _haveKey = true;
            NadePredictor.Fly(pawn, projectile, launch, _flight);
            Finish(pawn, projectile, launch);
            _revision++;
        }

        int flags = 0;
        if (charging) flags |= NadePreviewFlags.Charging;
        if (_flight.End == NadeEnd.Stuck) flags |= NadePreviewFlags.Stuck;
        if (_flight.End == NadeEnd.Fuse) flags |= NadePreviewFlags.EndedByFuse;
        if (_airborne) flags |= NadePreviewFlags.Airborne;
        if (_pipValid) flags |= NadePreviewFlags.PipValid;
        if (level.TimeSeconds - gadget.LastUsed < gadget.RefireRate || pc.bIsInInventoryMode || pawn.bStickyShocking ||
            weapon.IsInState(s_fullReloading) || weapon.IsInState(s_reloading) || weapon.IsInState(s_fireGrenade) ||
            weapon.IsInState(s_elbowShocking) || !pc.CheckIfCanFire())
        {
            flags |= NadePreviewFlags.Blocked;
        }

        head = new BridgeNadePreview
        {
            State = NadePreviewState.Aimed,
            Flags = flags,
            Kind = NadePredictor.Kind(projectile),
            ActorId = pawn.ActorId,
            Charge = v,
            FlightSeconds = _flight.Seconds,
            Bounces = _flight.BouncePoints.Count,
            Revision = _revision,
            StartX = launch.Origin.X,
            StartY = launch.Origin.Y,
            StartZ = launch.Origin.Z,
            LandX = _markerAt.X,
            LandY = _markerAt.Y,
            LandZ = _markerAt.Z,
            NormalX = _markerNormal.X,
            NormalY = _markerNormal.Y,
            NormalZ = _markerNormal.Z,
            Radius = NadePredictor.Radius(projectile),
            PipX = _pip.X,
            PipY = _pip.Y,
            PipZ = _pip.Z,
        };
        return Write(points);
    }

    /// <summary>The marker (where the projectile ends, on what) and the picture's camera, once per flight.</summary>
    private void Finish(SBasePawn pawn, SProjGrenade projectile, in NadeLaunch launch)
    {
        const ETraceFlags Solid = (ETraceFlags)GK.GrenadeBounceTraceFlags;
        FVector land = _flight.Land;
        _markerAt = land;
        _markerNormal = new FVector(0f, 0f, 1f);
        _airborne = true;
        _pipValid = false;
        if (GadgetWorld.Collision(pawn) is not { } world)
        {
            return;
        }

        if (_flight.End == NadeEnd.Stuck)
        {
            _markerNormal = _flight.LastHitNormal.SafeNormal();
            _airborne = false;
        }
        else if (world.LineCheck(out FCheckResult floor, projectile, land - new FVector(0f, 0f, 40f), land, FVector.Zero, Solid) && floor.bHit)
        {
            // What it lies on when it goes off.
            _markerNormal = floor.Normal.SafeNormal();
            _markerAt = floor.Location;
            _airborne = false;
        }
        else if (_flight.AnyHit && (land - _flight.LastHitAt).SizeSquared() < 30f * 30f)
        {
            _markerNormal = _flight.LastHitNormal.SafeNormal();
            _airborne = false;
        }

        // The picture's camera: behind the landing point as the thrower sees it, and above it; nearer when
        // something is in the way, from straight above when that leaves too little.
        FVector target = _markerAt + (_markerNormal * 8f);
        FVector back = new(launch.Origin.X - land.X, launch.Origin.Y - land.Y, 0f);
        back = back.SizeSquared() > 1f ? back.SafeNormal() : new FVector(-1f, 0f, 0f);
        Span<FVector> tries =
        [
            (back * 220f) + new FVector(0f, 0f, 150f),
            (back * 90f) + new FVector(0f, 0f, 220f),
            (_markerNormal * 200f) + (back * 20f),
        ];
        float best = 0f;
        foreach (FVector offset in tries)
        {
            FVector camera = target + offset;
            float length = offset.Size();
            if (world.LineCheck(out FCheckResult wall, projectile, camera, target, FVector.Zero, Solid) && wall.bHit)
            {
                camera = target + (offset * (wall.Time * 0.85f));
                length *= wall.Time * 0.85f;
            }

            if (length > best)
            {
                best = length;
                _pip = camera;
            }

            if (length >= 120f)
            {
                break;
            }
        }

        _pipValid = best >= 30f;
    }

    /// <summary>The path into Unreal's buffer: every point when it fits, else every n-th with the bounces and the last kept.</summary>
    private int Write(Span<BridgeNadePoint> points)
    {
        int n = _flight.Points.Count;
        if (points.Length < 2 || n == 0)
        {
            return 0;
        }

        int stride = 1;
        while (((n + stride - 1) / stride) + _flight.BouncePoints.Count + 1 > points.Length)
        {
            stride++;
        }

        int count = 0;
        int nextBounce = 0;
        for (int i = 0; i < n && count < points.Length; i++)
        {
            bool bounce = nextBounce < _flight.BouncePoints.Count && _flight.BouncePoints[nextBounce] == i;
            if (bounce)
            {
                nextBounce++;
            }

            if (!bounce && i % stride != 0 && i != n - 1)
            {
                continue;
            }

            FVector p = _flight.Points[i];
            points[count++] = new BridgeNadePoint { X = p.X, Y = p.Y, Z = p.Z, Flags = bounce ? 1 : 0 };
        }

        return count;
    }

    /// <summary>
    /// An instance of the gadget's projectile class that is never spawned, made as SGadGrenLauncher.ProjectileDefaults
    /// is: its collision size and bounce rules are the class's. The prediction writes its bCollideActors, so it is
    /// this feature's own and not the gadget's shared defaults.
    /// </summary>
    private SProjGrenade? Detached(SGadGrenLauncher gadget)
    {
        string? path = gadget.ProjectileClass.Path;
        if (path is null)
        {
            return null;
        }

        if (!_detached.TryGetValue(path, out SProjGrenade? projectile))
        {
            projectile = PawnInventory.IndexOf(gadget).Find(gadget.ProjectileClass)?.Create() as SProjGrenade;
            _detached[path] = projectile;
        }

        return projectile;
    }
}
