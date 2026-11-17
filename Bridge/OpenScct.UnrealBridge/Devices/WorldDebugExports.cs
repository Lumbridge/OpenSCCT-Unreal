using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Core.Uc;
using OpenScct.Game.Equipment;
using OpenScct.Game.Movement;
using OpenScct.Physics.Collision;
using OpenScct.Game.SBase;
using OpenScct.Server;

namespace OpenScct.UnrealBridge;

/// <summary>
/// Entry points for unattended checks of the level state only (Source/OpenSCCT/Play/ScctLevelStateDebug.h mirrors
/// the table; the game's -ScctLevelDo switch calls them). They make things happen on the SERVER's world of a local
/// match, as a player or a script would, so what Unreal then draws is what came back through replication. Nothing
/// in normal play calls them, and on a remote round (no server here) every entry returns 0 and does nothing.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct WorldDebugApi
{
    public int Version;
    public delegate* unmanaged[Cdecl]<int> FireMovers;
    public delegate* unmanaged[Cdecl]<int> FireLights;
    public delegate* unmanaged[Cdecl]<float, float, float, int, int> BreakLights;
    public delegate* unmanaged[Cdecl]<float, float, float, int, int> BreakProps;
    public delegate* unmanaged[Cdecl]<int> FireEmitters;
    public delegate* unmanaged[Cdecl]<float, float, float, int, int, int> ShootNear;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, int> ThrowFlare;
    public delegate* unmanaged[Cdecl]<int> DirtyFeet;
    public delegate* unmanaged[Cdecl]<int> CamnetOff;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, int> PlaceModem;
    public delegate* unmanaged[Cdecl]<int> ShootModems;
    public delegate* unmanaged[Cdecl]<float, float, float, float, float, float, int> PopSmoke;
    public delegate* unmanaged[Cdecl]<float, float, float, int, int> ShootSmoke;
}

public static unsafe class WorldDebugExports
{
    /// <summary>2: FireEmitters added. 3: ShootNear added. 4: ThrowFlare and DirtyFeet added. 5: CamnetOff, PlaceModem and ShootModems added. 6: PopSmoke and ShootSmoke added.</summary>
    public const int ApiVersion = 6;

    /// <summary>
    /// What PlaceModem gives a modem on a map without a PS2 sabotage target (no installed map has one): the PC
    /// sabotage bomb of the shared gameplay pack as its mesh, and names for its smoke and fire pictures, which only
    /// have to be set for the modem to leave its destruction preset (Ps2Modem.TakeDamage).
    /// </summary>
    private const string StandInModemMesh = "SGameplayObjects.mode_bomb";
    private const string StandInModemSmoke = "PS2_Objectives.OriginalSmoke";
    private const string StandInModemFire = "PS2_Objectives.OriginalFlash";

    /// <summary>How far ahead of the view PlaceModem looks for a wall, and how far off it the modem sits.</summary>
    private const float ModemReach = 400f;
    private const float ModemOffWall = 1f;

    /// <summary>The merc flare launcher's throw (SProjFlare.ProjectileSpeed).</summary>
    private const int FlareForce = 900;

    /// <summary>ShootNear's targets: the map's movers (doors, lifts), or its breakable props (panes, lamps).</summary>
    private const int ShootMovers = 0, ShootProps = 1;

    /// <summary>How many marks ShootNear puts on each target, and how far apart they must be.</summary>
    private const int MarksPerTarget = 5;
    private const float MarkSpacing = 14f;

    /// <summary>Fills <paramref name="api"/>. Returns the API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(WorldDebugApi* api)
    {
        api->Version = ApiVersion;
        api->FireMovers = &FireMovers;
        api->FireLights = &FireLights;
        api->BreakLights = &BreakLights;
        api->BreakProps = &BreakProps;
        api->FireEmitters = &FireEmitters;
        api->ShootNear = &ShootNear;
        api->ThrowFlare = &ThrowFlare;
        api->DirtyFeet = &DirtyFeet;
        api->CamnetOff = &CamnetOff;
        api->PlaceModem = &PlaceModem;
        api->ShootModems = &ShootModems;
        api->PopSmoke = &PopSmoke;
        api->ShootSmoke = &ShootSmoke;
        return ApiVersion;
    }

    /// <summary>
    /// Puts every network camera (SCamNetwork) out as a spy's sticky shocker does: its TakeDamage with a spy as the
    /// instigator and the shocker's damage type starts the 30 second outage, which replicates to every machine. A
    /// match without a spy pawn on the server cannot do it. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CamnetOff() => Exports.Current is { } session ? CamnetOff(session) : 0;

    /// <summary>The managed side of <see cref="CamnetOff()"/> (the probes call it).</summary>
    public static int CamnetOff(BridgeSession session) => OnServer(session, static host =>
    {
        Level level = host.World.Level;
        SBasePawn? spy = null;
        foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe && pawn.SPawnType == PawnType.SPAWNOFFENSE)
            {
                spy = pawn;
                break;
            }
        }

        if (spy is null)
        {
            return;
        }

        foreach (SCamNetwork camera in level.AllActors<SCamNetwork>())
        {
            if (!camera.bDeleteMe)
            {
                camera.eventTakeDamage(1, spy, camera.Location, FVector.Zero, SDamageType.SDTStickyShocker);
            }
        }
    });

    /// <summary>
    /// Places a PS2 sabotage modem as the spy's modem gadget does (Ps2ModemRules.TryPlace): on a map with a PS2 bomb
    /// target, at that target with the target's own modem mesh, sound and destruction pictures, owned by a spy so its
    /// capture runs; on any other map, on the wall ahead of the point along the direction (or in front of it when no
    /// wall is near) with the shared pack's sabotage bomb as a stand-in mesh. Like the real one it reaches a client
    /// only while it is relevant to that client's pawn, so the point should be near the player (their own view).
    /// Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PlaceModem(float x, float y, float z, float dx, float dy, float dz) =>
        Exports.Current is { } session ? PlaceModem(session, new FVector(x, y, z), new FVector(dx, dy, dz)) : 0;

    /// <summary>The managed side of <see cref="PlaceModem(float, float, float, float, float, float)"/> (the probes call it).</summary>
    public static int PlaceModem(BridgeSession session, FVector from, FVector direction) => OnServer(session, host =>
    {
        Level level = host.World.Level;
        SBasePawn? spy = null;
        foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe && pawn.SPawnType == PawnType.SPAWNOFFENSE)
            {
                spy = pawn;
                break;
            }
        }

        foreach (SBombTargetObjectiveTrigger target in level.AllActors<SBombTargetObjectiveTrigger>())
        {
            if (target.bDeleteMe || !target.bPS2Objective || target.Ps2ModemMesh.IsNone)
            {
                continue;
            }

            var modem = new Ps2Modem
            {
                Target = target,
                StaticMesh = target.Ps2ModemMesh,
                ActiveSound = target.Ps2ModemSound,
                DestroySound = target.Ps2ModemDestroyedSound,
                DestroyedEmitter = target.Ps2ModemDestroyedEmitter,
                SmokeTexture = target.Ps2ModemSmokeTexture,
                FireTexture = target.Ps2ModemFireTexture,
            };
            level.SpawnActor(modem, target.Location, target.Rotation, spy, spy);
            return;
        }

        FVector forward = direction.SafeNormal();
        FVector at = from + (forward * 120f);
        FRotator facing = (forward * -1f).Rotation();
        // The level's walls only: from a camera behind the player the line must not stop on the player.
        if (GadgetWorld.Line(level.Info, from + (forward * ModemReach), from, GadgetWorld.SightFlags, out FCheckResult hit))
        {
            at = hit.Location + (hit.Normal * ModemOffWall);
            facing = hit.Normal.Rotation();
        }

        level.SpawnActor(new Ps2Modem
        {
            StaticMesh = new AssetRef(StandInModemMesh),
            SmokeTexture = new AssetRef(StandInModemSmoke),
            FireTexture = new AssetRef(StandInModemFire),
        }, at, facing, null, null);
    });

    /// <summary>
    /// Shoots every PS2 modem with one hit, as a merc's bullet does (Ps2Modem.TakeDamage): it stops its capture,
    /// leaves its destruction preset (Ps2ModemEffect: the smoke and the flash) and goes. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ShootModems() => Exports.Current is { } session ? ShootModems(session) : 0;

    /// <summary>The managed side of <see cref="ShootModems()"/> (the probes call it).</summary>
    public static int ShootModems(BridgeSession session) => OnServer(session, static host =>
    {
        var modems = new List<Ps2Modem>();
        foreach (Ps2Modem modem in host.World.Level.AllActors<Ps2Modem>())
        {
            if (!modem.bDeleteMe)
            {
                modems.Add(modem);
            }
        }

        foreach (Ps2Modem modem in modems)
        {
            modem.eventTakeDamage(1, null, modem.Location, FVector.Zero, SDamageType.SDamage_Explosion);
        }
    });

    /// <summary>How far ahead of the point PopSmoke lets the cloud out (less when a wall is nearer), and how far it looks down for the floor.</summary>
    private const float SmokeReach = 700f;
    private const float SmokeFloorReach = 600f;

    /// <summary>
    /// Pops a spy smoke grenade (SProjSmoke) on the floor ahead of the point along the direction (short of a wall in the
    /// way), as a thrown one pops where it lands: its SSmokeEmitter cloud is let out, which with responsive smoke on is
    /// the shared simulated field every machine draws and gas and AI read. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int PopSmoke(float x, float y, float z, float dx, float dy, float dz) =>
        Exports.Current is { } session ? PopSmoke(session, new FVector(x, y, z), new FVector(dx, dy, dz)) : 0;

    /// <summary>The managed side of <see cref="PopSmoke(float, float, float, float, float, float)"/> (the probes call it).</summary>
    public static int PopSmoke(BridgeSession session, FVector from, FVector direction) => OnServer(session, host =>
    {
        Level level = host.World.Level;
        FVector forward = direction.SafeNormal();
        float reach = SmokeReach;
        if (GadgetWorld.Line(level.Info, from + (forward * SmokeReach), from, GadgetWorld.SightFlags, out FCheckResult wall))
        {
            reach = MathF.Max(0f, FVector.Dist(from, wall.Location) - 60f);
        }

        FVector above = from + (forward * reach);
        FVector at = above;
        FVector normal = new(0f, 0f, 1f);
        if (GadgetWorld.Line(level.Info, above - new FVector(0f, 0f, SmokeFloorReach), above, GadgetWorld.SightFlags, out FCheckResult floor))
        {
            at = floor.Location;
            normal = floor.Normal;
        }

        SBasePawn? thrower = null;
        foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe && pawn.SPawnType == PawnType.SPAWNOFFENSE)
            {
                thrower = pawn;
                break;
            }
        }

        if (level.SpawnActor(new SProjSmoke(), at + (normal * 4f), default, thrower, thrower) is { } grenade)
        {
            grenade.Explode(at, normal);
        }
    });
    /// <summary>
    /// Fires a burst of shots from the point through every responsive smoke cloud of the server's world, as a gun's
    /// trace through it does (ResponsiveSmokeWorld.Bullet: each pushes a short-lived hole along its line that refills),
    /// fanned across the cloud's middle at eye height. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ShootSmoke(float x, float y, float z, int shots) =>
        Exports.Current is { } session ? ShootSmoke(session, new FVector(x, y, z), shots) : 0;

    /// <summary>The managed side of <see cref="ShootSmoke(float, float, float, int)"/> (the probes call it).</summary>
    public static int ShootSmoke(BridgeSession session, FVector from, int shots) => OnServer(session, host =>
    {
        Level level = host.World.Level;
        Actor? shooter = null;
        foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe)
            {
                shooter = pawn;
                break;
            }
        }

        if (shooter is null || level.GetService<ResponsiveSmokeWorld>() is not { } world)
        {
            return;
        }

        foreach (ResponsiveSmokeField field in world.Fields.Values)
        {
            FVector toward = field.Center - from;
            float length = toward.Size();
            if (length < 1f)
            {
                continue;
            }

            FVector forward = toward / length;
            FVector side = new FVector(-forward.Y, forward.X, 0f).SafeNormal();
            int count = Math.Clamp(shots, 1, 16);
            for (int i = 0; i < count; i++)
            {
                // Fanned across the cloud and a little up and down, through and out the far side.
                float across = (i - ((count - 1) * 0.5f)) * 45f;
                FVector target = field.Center + (side * across) + new FVector(0f, 0f, ((i % 3) - 1) * 30f);
                FVector line = (target - from).SafeNormal();
                ResponsiveSmokeWorld.Bullet(shooter, from, from + (line * (length + 700f)));
            }
        }
    });
    /// <summary>
    /// Throws a merc flare (SProjFlare) from the point along the direction, as the flare launcher does: it flies, catches
    /// on the first wall or floor it hits and burns there, and every machine that sees its copy hit leaves the burn
    /// mark (an abandoned SFlareBurnProjector). Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ThrowFlare(float x, float y, float z, float dx, float dy, float dz) =>
        Exports.Current is { } session ? ThrowFlare(session, new FVector(x, y, z), new FVector(dx, dy, dz)) : 0;

    /// <summary>The managed side of <see cref="ThrowFlare(float, float, float, float, float, float)"/> (the probes call it).</summary>
    public static int ThrowFlare(BridgeSession session, FVector from, FVector direction) => OnServer(session, host =>
    {
        FRotator rotation = direction.SafeNormal().Rotation();
        if (host.World.Level.SpawnActor(new SProjFlare(), from, rotation) is { } flare)
        {
            flare.Launch(FlareForce, false);
        }
    });

    /// <summary>
    /// Gives every pawn of the CLIENT's world the dirty feet a sand floor gives (SBasePawn.FootPrint's SetFootDirt for
    /// SURFACE_Sand, wearing off ten times slower), so the next steps leave footprints on any floor. A footprint is
    /// drawn by each machine for itself from its own copy's steps and the dirt is not replicated, so this is the one
    /// entry that changes the client's world; it runs on the game thread, between the client's ticks. Returns the
    /// number of pawns.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int DirtyFeet()
    {
        try
        {
            return Exports.Current is { IsDisposed: false } session ? DirtyFeet(session) : 0;
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>The managed side of <see cref="DirtyFeet()"/> (the probes call it).</summary>
    public static int DirtyFeet(BridgeSession session)
    {
        int count = 0;
        foreach (SBasePawn pawn in session.Session.Client.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            pawn.FootDirtyness = 1f;
            pawn.DecreaseFootDirtyness = 0.1f;
            pawn.StepColor = new FColor { R = 140, G = 140, B = 140, A = 80 };
            count++;
        }

        return count;
    }

    /// <summary>Fires the tag of every mover, as a switch or a trigger would: doors open, lifts start. Returns 1 when queued.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FireMovers() => OnServer(static host =>
    {
        Level level = host.World.Level;
        var tags = new HashSet<Name>();
        foreach (Mover mover in level.AllActors<Mover>())
        {
            tags.Add(mover.Tag);
        }

        Fire(level, tags);
    });

    /// <summary>Fires the tag of every switched light (STriggerLight): each one toggles. Returns 1 when queued.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FireLights() => OnServer(static host =>
    {
        Level level = host.World.Level;
        var tags = new HashSet<Name>();
        foreach (STriggerLight light in level.AllActors<STriggerLight>())
        {
            tags.Add(light.Tag);
        }

        Fire(level, tags);
    });

    /// <summary>
    /// Breaks the <paramref name="count"/> lit switched lights nearest the point, as a shot or a chaff grenade does
    /// (STriggerLight.LightOff(true): half a second of flicker, then dark). Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int BreakLights(float x, float y, float z, int count)
    {
        var near = new FVector(x, y, z);
        return OnServer(host =>
        {
            var lit = new List<STriggerLight>();
            foreach (STriggerLight light in host.World.Level.AllActors<STriggerLight>())
            {
                if (!light.bDeleteMe && light.LightType != ELightType.LT_None)
                {
                    lit.Add(light);
                }
            }

            lit.Sort((a, b) => FVector.DistSquared(a.Location, near).CompareTo(FVector.DistSquared(b.Location, near)));
            for (int i = 0; i < lit.Count && i < count; i++)
            {
                lit[i].LightOff(true);
            }
        });
    }

    /// <summary>
    /// Destroys the <paramref name="count"/> breakable props nearest the point (lamps, vents, glass panes) with one
    /// heavy hit each. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int BreakProps(float x, float y, float z, int count)
    {
        var near = new FVector(x, y, z);
        return OnServer(host =>
        {
            var targets = new List<SGamePlayObject>();
            foreach (SGamePlayObject prop in host.World.Level.AllActors<SGamePlayObject>())
            {
                if (prop.bDamageable && !prop.bDeleteMe && prop is not SObjectiveTrigger)
                {
                    targets.Add(prop);
                }
            }

            targets.Sort((a, b) => FVector.DistSquared(a.Location, near).CompareTo(FVector.DistSquared(b.Location, near)));
            for (int i = 0; i < targets.Count && i < count; i++)
            {
                SGamePlayObject prop = targets[i];
                SDamageType type = prop.TakeDamageTypes.Count > 0 ? prop.TakeDamageTypes[0] : SDamageType.SDTBase;
                prop.eventTakeDamage(10000, null, prop.Location + new FVector(0f, 0f, 4f), new FVector(300f, 0f, 0f), type);
            }
        });
    }

    /// <summary>
    /// Fires the tag of every map Emitter, as the map's events would: the waiting ones start, the running ones
    /// stop, the bursts fire. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int FireEmitters() => OnServer(static host =>
    {
        Level level = host.World.Level;
        var tags = new HashSet<Name>();
        foreach (Emitter emitter in level.AllActors<Emitter>())
        {
            tags.Add(emitter.Tag);
        }

        Fire(level, tags);
    });

    /// <summary>
    /// Shoots the <paramref name="count"/> movers (<paramref name="kind"/> 0) or breakable props (1) nearest the point:
    /// a few bullet marks on each, put through the impact manager as a gun's trace does (the surface's own impact
    /// class, the hit actor as the mark's surface), so every machine draws them on that actor. The marks go where
    /// lines across the target meet it, from the side facing the point first. Returns 1 when queued.
    /// </summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ShootNear(float x, float y, float z, int count, int kind)
    {
        var near = new FVector(x, y, z);
        return OnServer(host =>
        {
            var targets = new List<Actor>();
            foreach (Actor a in host.World.Level.AllActors<Actor>())
            {
                if (!a.bDeleteMe && (kind == ShootMovers ? a is Mover : kind == ShootProps && a is SGamePlayObject { bDamageable: true } and not SObjectiveTrigger))
                {
                    targets.Add(a);
                }
            }

            targets.Sort((a, b) => FVector.DistSquared(a.Location, near).CompareTo(FVector.DistSquared(b.Location, near)));
            for (int i = 0; i < targets.Count && i < count; i++)
            {
                Shoot(targets[i], near);
            }
        });
    }

    private static void Shoot(Actor target, FVector from)
    {
        if (MovementNatives.Driver(target) is not { } driver)
        {
            return;
        }

        // The horizontal line from the target to the point first, then the target's own axes both ways.
        target.Rotation.GetAxes(out FVector ax, out FVector ay, out _);
        FVector toward = from - target.Location;
        toward.Z = 0f;
        var directions = new List<FVector>();
        if (toward.SizeSquared() > 1f)
        {
            directions.Add(toward.SafeNormal());
        }

        directions.AddRange([ax, ay, ax * -1f, ay * -1f]);
        var marks = new List<FVector>();
        float[] across = [0f, 24f, -24f, 48f, -48f, 72f, -72f];
        float[] heights = [40f, 0f, 70f, -30f];
        foreach (FVector d in directions)
        {
            FVector side = new FVector(-d.Y, d.X, 0f).SafeNormal();
            foreach (float h in heights)
            {
                foreach (float a in across)
                {
                    if (marks.Count >= MarksPerTarget)
                    {
                        return;
                    }

                    FVector through = target.Location + (side * a) + new FVector(0f, 0f, h);
                    if (!driver.Collision.LineCheck(out FCheckResult hit, null, through - (d * 160f), through + (d * 160f), default, ETraceFlags.TRACE_AllColliding) ||
                        !ReferenceEquals(hit.Actor, target) || marks.Exists(m => FVector.DistSquared(m, hit.Location) < MarkSpacing * MarkSpacing))
                    {
                        continue;
                    }

                    marks.Add(hit.Location);
                    FRotator facing = hit.Normal.Rotation();
                    ImpactManager.AddImpact(target, SWeaponDefense.ImpactClass((ESurfaceType)hit.SurfaceType), hit.Location, facing, null, target);
                }
            }
        }
    }

    private static void Fire(Level level, HashSet<Name> tags)
    {
        foreach (Name tag in tags)
        {
            if (!tag.IsNone)
            {
                level.TriggerEvent(tag, null, null);
            }
        }
    }

    /// <summary>
    /// Runs <paramref name="action"/> once on the local match's server thread, just before its next world step. The
    /// server's world belongs to that thread, so nothing of it is touched from here. A failure there is dropped: these
    /// are checks, and a failed one shows as nothing happening.
    /// </summary>
    internal static int OnServer(Action<ServerHost> action) => Exports.Current is { } session ? OnServer(session, action) : 0;

    /// <summary>The last exception an entry threw on the server thread (for the probes); null when none did.</summary>
    public static Exception? LastFailure { get; private set; }

    private static int OnServer(BridgeSession current, Action<ServerHost> action)
    {
        try
        {
            if (current is not { IsRemote: false, IsDisposed: false } session || session.Session.Host is not { } host)
            {
                return 0;
            }

            void Run(ServerHost running)
            {
                running.BeforeWorldTick -= Run;
                try
                {
                    action(running);
                }
                catch (Exception e)
                {
                    // See the summary: a check that fails must not take the server down. The probes print it.
                    LastFailure = e;
                }
            }

            host.BeforeWorldTick += Run;
            return 1;
        }
        catch (Exception)
        {
            return 0;
        }
    }
}
