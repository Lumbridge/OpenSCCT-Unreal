using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Combat;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Server;

namespace OpenScct.UnrealBridge.Devices;

// The per-actor values the goggles draw by (Source/OpenSCCT/Runtime/ScctVisionApi.h mirrors these field for field;
// change both together and bump VisionExports.ApiVersion). The original's vision modes do not light everything the
// same: thermal draws an actor by its HeatIntensity and HeatOpacity, EMF by its EMFIntensity while bEMFIsOn and by
// its EMFOpacity (0 hides it, below 0.99 it is translucent), and a spy shows in EMF only while EMF visible
// (PawnVision.TestAndSetEMFVisible: weapon drawn, a vision mode on, binoculars, a spy mark, the camo suit, phospho).

/// <summary>One pawn's vision values (<c>GetPawns</c>), as of the last client tick.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeVisionPawn
{
    /// <summary>The pawn (BridgePawn.ActorId).</summary>
    public int ActorId;

    /// <summary><see cref="BridgeVisionPawnFlags"/>.</summary>
    public int Flags;

    /// <summary>Actor.HeatIntensity: how hot thermal draws the body (1 for both sides by default).</summary>
    public float HeatIntensity;

    /// <summary>Actor.HeatOpacity: 0 hides the body in thermal, 1 opaque.</summary>
    public float HeatOpacity;

    /// <summary>Actor.EMFIntensity as replicated, whether or not bEMFIsOn.</summary>
    public float EMFIntensity;

    /// <summary>Actor.EMFOpacity: 0 hides the body in EMF, below 0.99 translucent.</summary>
    public float EMFOpacity;

    /// <summary>What EMF draws the body with: EMFIntensity while bEMFIsOn, else 0 (EmfVision.Glow).</summary>
    public float EmfGlow;

    /// <summary>Shock protection left, 0 to 1 of its whole time (MercShockPresentation; 0 while protected by state only).</summary>
    public float ShockFraction;

    /// <summary>Seconds of shock protection left (0 while protected by state only or not protected).</summary>
    public float ShockSeconds;

    /// <summary>The camo suit: GlassEffect / 255 (0 none, 1 fully cloaked).</summary>
    public float Cloak;

    /// <summary>Speed, cm per second (what the motion tracker compares with MinDetectedVelocity).</summary>
    public float Speed;
}

public static class BridgeVisionPawnFlags
{
    /// <summary>Actor.bEMFIsOn.</summary>
    public const int EmfOn = 1;

    /// <summary>EmfVision.Shows: not hidden, bEMFIsOn and EMFOpacity above 0. A spy with his weapon away and no other cause is not.</summary>
    public const int EmfShows = 2;

    /// <summary>Faster than the local motion tracker's MinDetectedVelocity (or its class default when there is none).</summary>
    public const int Moving = 4;

    /// <summary>The local merc's motion tracker holds a lock on it now.</summary>
    public const int Tracked = 8;

    /// <summary>A merc whose shocker protection is on (MercShockImmunity: the post-shock timer or a protecting state).</summary>
    public const int ShockImmune = 16;

    /// <summary>The protection comes from the merc's state (a blast, a fall), not the timer: no time left to show.</summary>
    public const int ShockStateProtected = 32;

    /// <summary>SBasePawn.bStickySkinned: the Stickess skin while sticky shocked.</summary>
    public const int StickySkinned = 64;

    /// <summary>SBasePawn.bWet: in a rain volume (the wet skins in thermal and EMF).</summary>
    public const int Wet = 128;

    /// <summary>Actor.bHidden.</summary>
    public const int Hidden = 256;

    /// <summary>A merc (SPAWNDEFENSE); clear for a spy.</summary>
    public const int Merc = 512;

    /// <summary>The weapon is drawn (WeaponLoaded != 0), one of the spy's EMF causes.</summary>
    public const int WeaponDrawn = 1024;
}

/// <summary>
/// One actor other than a pawn whose values the vision modes draw by (<c>GetActors</c>): electronics (mines, sticky
/// cams, grenades, objective computers, ammo crates, sensors, the spy's pistol), emitters and volumes thermal hides
/// or thins, anything whose HeatIntensity, HeatOpacity, EMFIntensity or EMFOpacity is not the Actor default.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeVisionActor
{
    /// <summary>The simulation actor's ActorId (stable while it lives; what the other tables call a thrown gadget by).</summary>
    public int ActorId;

    /// <summary>World name table id of its map name (the level actor's "Scct:" tag), 0 for an actor spawned in play.</summary>
    public int NameId;

    /// <summary>World name table id of its class without the package (SMines, SStickyCam, SComputerObjectiveTrigger...).</summary>
    public int ClassId;

    /// <summary>The pawn that owns or carries it (a weapon's holder), 0 when none.</summary>
    public int OwnerPawnId;

    /// <summary><see cref="BridgeVisionActorFlags"/>.</summary>
    public int Flags;

    public float HeatIntensity;
    public float HeatOpacity;
    public float EMFIntensity;
    public float EMFOpacity;

    /// <summary>EMFIntensity while bEMFIsOn, else 0.</summary>
    public float EmfGlow;

    /// <summary>Where it is, cm.</summary>
    public float X, Y, Z;
}

public static class BridgeVisionActorFlags
{
    /// <summary>Actor.bEMFIsOn.</summary>
    public const int EmfOn = 1;

    /// <summary>EmfVision.Shows.</summary>
    public const int EmfShows = 2;

    /// <summary>Actor.bHidden.</summary>
    public const int Hidden = 4;

    /// <summary>Placed by the map (NameId is its map name).</summary>
    public const int MapActor = 8;

    /// <summary>The local merc's motion tracker holds a lock on it (a thrown grenade).</summary>
    public const int Tracked = 16;
}

/// <summary>The level's vision values (<c>GetLevel</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeVisionLevel
{
    /// <summary>LevelInfo.MTVSlope, 0 to 255 (class default 85): motion vision's contrast, s = 1 + 3 MTVSlope / 255.</summary>
    public int MtvSlope;

    /// <summary>LevelInfo.MTVOffset, 0 to 255 (class default 52): motion vision's brightness offset, MTVOffset / 255.</summary>
    public int MtvOffset;

    /// <summary><see cref="BridgeVisionLevelFlags"/>.</summary>
    public int Flags;

    /// <summary>Locks the local merc's motion tracker holds now (GetMotionLocks writes them).</summary>
    public int LockCount;

    /// <summary>The tracker's FocusTime and OutTime, seconds (0 without a tracker).</summary>
    public float FocusTime;

    public float OutTime;

    /// <summary>The client's LevelInfo.TimeSeconds of the last tick: the clock lock times run on.</summary>
    public float TimeSeconds;

    /// <summary>LevelInfo.GlowThreshold, 0 to 255 (class default 224): the glow's bright pass keeps what is brighter than this / 255; 255 no glow.</summary>
    public int GlowThreshold;
}

public static class BridgeVisionLevelFlags
{
    /// <summary>The local player is a merc with a motion tracker.</summary>
    public const int HasTracker = 1;

    /// <summary>The tracker scans now (its s_Active state: motion vision on).</summary>
    public const int TrackerActive = 2;
}

/// <summary>
/// The view the motion tracker's locks are boxed from (<c>GetMotionLocks</c>), and how its 640 x 480 HUD frame sits
/// on the screen (Godot's MotionHudFrame: screen = OffsetX + x ScaleX, y ScaleY).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeVisionCamera
{
    /// <summary>The camera, cm, and its rotation in degrees (as FRotator takes them).</summary>
    public float X, Y, Z;

    public float Pitch, Yaw, Roll;

    /// <summary>The horizontal field of view across the whole screen, degrees.</summary>
    public float FovX;

    /// <summary>The screen (view) size in pixels.</summary>
    public float ScreenWidth, ScreenHeight;

    /// <summary>The 640 x 480 frame on the screen: pixels per frame unit across and down, and the frame's left edge in pixels.</summary>
    public float FrameScaleX, FrameScaleY, FrameOffsetX;
}

/// <summary>One lock of the local merc's motion tracker, boxed in the 640 x 480 HUD frame.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeVisionLock
{
    /// <summary>The locked actor (a pawn's ActorId, or a projectile's).</summary>
    public int ActorId;

    /// <summary><see cref="BridgeVisionLockFlags"/>.</summary>
    public int Flags;

    /// <summary>The box (ASMotionTrackingVision::Tick): left, right, top, bottom in the 640 x 480 frame.</summary>
    public float X1, X2, Y1, Y2;

    /// <summary>When the lock began and, once it is let go, when its hold ends (LevelInfo.TimeSeconds).</summary>
    public float StartTime, EndTime;
}

public static class BridgeVisionLockFlags
{
    /// <summary>A clean view rectangle: in front of the camera, focused and not faded out (motion vision redraws the scene clean in it).</summary>
    public const int Clean = 1;

    /// <summary>In front of the camera.</summary>
    public const int InFront = 2;

    /// <summary>The target is no longer seen: the lock holds until EndTime, then fades over OutTime.</summary>
    public const int Removing = 4;

    /// <summary>The target is a teammate (ItsMyFriend).</summary>
    public const int Friend = 8;

    /// <summary>The target is a pawn.</summary>
    public const int Pawn = 16;
}

/// <summary>
/// The vision values of the client world for Unreal: per pawn and per electronic or heat-changed actor, the level's
/// motion vision tweak and the local motion tracker's locks. It reads the simulation and never writes it, except for
/// the screen boxes of the tracker's locks (<see cref="GetMotionLocks"/>), which the original's HUD tick works out on
/// the merc's own machine every frame and nothing else here reads.
/// <para>The Godot client reads the same values where it draws: Render/GameplayObjects.VisionOf (heat and EMF of a
/// gameplay mesh), Views/SimPawnView (the body's values, the sticky and wet skins, the shock protection cue through
/// OpenScct.Server.MercShockPresentation) and Hud/HudMotionTracking (the clean rectangles).</para>
/// </summary>
public sealed class VisionState
{
    /// <summary>How often the actor list is searched again when the actor count has not changed (half a second of ticks).</summary>
    private const int RescanTicks = 30;

    /// <summary>The tracker's own class default, for pawns when the local player has no tracker.</summary>
    private const float DefaultMinDetectedVelocity = 150f;

    private readonly BridgeSession _session;
    private readonly List<Actor> _candidates = [];
    private readonly Dictionary<Actor, int> _mapNames = new(ReferenceEqualityComparer.Instance);
    private readonly Dictionary<Type, int> _classIds = [];
    private readonly HashSet<Actor> _locked = new(ReferenceEqualityComparer.Instance);
    private object? _mappedLevel;
    private long _scannedTick = -1;
    private int _scannedActorCount = -1;

    private VisionState(BridgeSession session)
    {
        _session = session;
    }

    /// <summary>The session's vision state, made on first use. It works on demand: nothing runs on the ticks.</summary>
    public static VisionState Of(BridgeSession session) => session.Feature(s => new VisionState(s));

    private WorldState World => WorldState.Of(_session);

    private Level? ClientLevel => _session.Session.Client.Map?.Loaded is not null ? _session.Session.Client.World.Level : null;

    /// <summary>The local merc's tracker, or null (HudMotionTracking.TrackerOf).</summary>
    private SMotionTrackingVision? Tracker =>
        _session.Session.LocalController is SPlayerController { SPawnType: PawnType.SPAWNDEFENSE } pc &&
        pc.Goggle?.MotionTrackingVision is { bDeleteMe: false } t ? t : null;

    // ---- pawns -----------------------------------------------------------------------------------------------

    /// <summary>Writes every pawn's vision values. Returns how many pawns there are (only the span's length are written).</summary>
    public int GetPawns(Span<BridgeVisionPawn> rows)
    {
        if (ClientLevel is not { } level)
        {
            return 0;
        }

        SMotionTrackingVision? tracker = Tracker;
        FillLocked(tracker);
        float minSpeed = tracker?.MinDetectedVelocity ?? DefaultMinDetectedVelocity;
        float alpha = _session.Alpha;
        int count = 0;
        foreach (SBasePawn pawn in level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            if (count < rows.Length)
            {
                float speed = pawn.Velocity.Size();
                ShockImmunityView shock = MercShockPresentation.Read(pawn, alpha);
                int flags = (pawn.bEMFIsOn ? BridgeVisionPawnFlags.EmfOn : 0) |
                    (EmfVision.Shows(pawn) ? BridgeVisionPawnFlags.EmfShows : 0) |
                    (speed >= minSpeed ? BridgeVisionPawnFlags.Moving : 0) |
                    (_locked.Contains(pawn) ? BridgeVisionPawnFlags.Tracked : 0) |
                    (shock.Active ? BridgeVisionPawnFlags.ShockImmune : 0) |
                    (shock.StateProtected ? BridgeVisionPawnFlags.ShockStateProtected : 0) |
                    (pawn.bStickySkinned ? BridgeVisionPawnFlags.StickySkinned : 0) |
                    (pawn.bWet ? BridgeVisionPawnFlags.Wet : 0) |
                    (pawn.bHidden ? BridgeVisionPawnFlags.Hidden : 0) |
                    (pawn.SPawnType == PawnType.SPAWNDEFENSE ? BridgeVisionPawnFlags.Merc : 0) |
                    (pawn.WeaponLoaded != 0 ? BridgeVisionPawnFlags.WeaponDrawn : 0);
                rows[count] = new BridgeVisionPawn
                {
                    ActorId = pawn.ActorId,
                    Flags = flags,
                    HeatIntensity = pawn.HeatIntensity,
                    HeatOpacity = pawn.HeatOpacity,
                    EMFIntensity = pawn.EMFIntensity,
                    EMFOpacity = pawn.EMFOpacity,
                    EmfGlow = EmfVision.Glow(pawn),
                    ShockFraction = shock.Fraction,
                    ShockSeconds = shock.RemainingSeconds,
                    Cloak = pawn.GlassEffect / 255f,
                    Speed = speed,
                };
            }

            count++;
        }

        return count;
    }

    // ---- other actors ----------------------------------------------------------------------------------------

    /// <summary>Writes every other actor the vision modes draw by its own values. Returns how many there are.</summary>
    public int GetActors(Span<BridgeVisionActor> rows)
    {
        if (ClientLevel is not { } level)
        {
            return 0;
        }

        Scan(level);
        FillLocked(Tracker);
        WorldState world = World;
        int count = 0;
        foreach (Actor a in _candidates)
        {
            if (a.bDeleteMe || !Matters(a))
            {
                continue;
            }

            if (count < rows.Length)
            {
                int nameId = _mapNames.TryGetValue(a, out int mapName) ? mapName : 0;
                rows[count] = new BridgeVisionActor
                {
                    ActorId = a.ActorId,
                    NameId = nameId,
                    ClassId = ClassIdOf(world, a),
                    OwnerPawnId = OwnerPawnOf(a)?.ActorId ?? 0,
                    Flags = (a.bEMFIsOn ? BridgeVisionActorFlags.EmfOn : 0) |
                        (EmfVision.Shows(a) ? BridgeVisionActorFlags.EmfShows : 0) |
                        (a.bHidden ? BridgeVisionActorFlags.Hidden : 0) |
                        (nameId != 0 ? BridgeVisionActorFlags.MapActor : 0) |
                        (_locked.Contains(a) ? BridgeVisionActorFlags.Tracked : 0),
                    HeatIntensity = a.HeatIntensity,
                    HeatOpacity = a.HeatOpacity,
                    EMFIntensity = a.EMFIntensity,
                    EMFOpacity = a.EMFOpacity,
                    EmfGlow = EmfVision.Glow(a),
                    X = a.Location.X,
                    Y = a.Location.Y,
                    Z = a.Location.Z,
                };
            }

            count++;
        }

        return count;
    }

    /// <summary>
    /// True when an actor's values make it look different from an ordinary one in a vision mode: it has an EMF
    /// intensity (on or switched off), heat, or a heat or EMF opacity other than the Actor default of 1.
    /// </summary>
    private static bool Matters(Actor a) =>
        a.EMFIntensity != 0f || a.HeatIntensity != 0f || a.HeatOpacity != 1f || a.EMFOpacity != 1f;

    /// <summary>
    /// Finds the actors other than pawns and controllers that matter in the modes again when the level's actor count
    /// changed or every half second (a device's values change in play: a pistol drawn, a computer switched off).
    /// </summary>
    private void Scan(Level level)
    {
        MapNames(level);
        if (_scannedTick >= 0 && level.ActorCount == _scannedActorCount && level.TickCount - _scannedTick < RescanTicks)
        {
            return;
        }

        _scannedTick = level.TickCount;
        _scannedActorCount = level.ActorCount;
        _candidates.Clear();
        foreach (Actor a in level.AllActors<Actor>())
        {
            if (!a.bDeleteMe && a is not Pawn and not Controller && Matters(a))
            {
                _candidates.Add(a);
            }
        }
    }

    /// <summary>The map names of the client's placed actors, as the level's "Scct:" tags carry them (once per loaded map).</summary>
    private void MapNames(Level level)
    {
        if (ReferenceEquals(_mappedLevel, level) || _session.Session.Client.Map?.Loaded is not { } loaded)
        {
            return;
        }

        _mappedLevel = level;
        _mapNames.Clear();
        WorldState world = World;
        foreach (OpenScct.Content.ActorRecord record in loaded.Gameplay.Actors)
        {
            if (loaded.ActorOf(record) is { } actor)
            {
                _mapNames.TryAdd(actor, world.Intern(record.Id));
            }
        }
    }

    private int ClassIdOf(WorldState world, Actor a)
    {
        Type type = a.GetType();
        if (!_classIds.TryGetValue(type, out int id))
        {
            id = world.Intern(type.Name);
            _classIds[type] = id;
        }

        return id;
    }

    /// <summary>The pawn an actor belongs to: its Owner or its Base when that is a pawn (a weapon in a hand).</summary>
    private static Pawn? OwnerPawnOf(Actor a) => a.Owner as Pawn ?? a.Base as Pawn;

    // ---- level and motion tracker ------------------------------------------------------------------------------

    /// <summary>Writes the level's values. Returns 1, or 0 while no map is loaded.</summary>
    public int GetLevel(out BridgeVisionLevel row)
    {
        row = default;
        if (ClientLevel is not { } level)
        {
            return 0;
        }

        row.MtvSlope = level.Info.MTVSlope;
        row.MtvOffset = level.Info.MTVOffset;
        row.TimeSeconds = level.Info.TimeSeconds;
        row.GlowThreshold = level.Info.GlowThreshold;
        if (Tracker is { } tracker)
        {
            row.Flags = BridgeVisionLevelFlags.HasTracker | (tracker.IsActive() ? BridgeVisionLevelFlags.TrackerActive : 0);
            row.LockCount = tracker.LockedObjects.Count;
            row.FocusTime = tracker.FocusTime;
            row.OutTime = tracker.OutTime;
        }

        return 1;
    }

    /// <summary>
    /// Boxes the local merc's motion tracker locks from <paramref name="camera"/> as the original's HUD tick does
    /// (SMotionTrackingVision.ProjectLocks) and writes them in the 640 x 480 frame. A lock flagged Clean is one of
    /// motion vision's clean view rectangles (HudMotionTracking.CleanRects). Returns how many locks there are.
    /// </summary>
    public int GetMotionLocks(in BridgeVisionCamera camera, Span<BridgeVisionLock> rows)
    {
        if (ClientLevel is not { } level || Tracker is not { } tracker || tracker.LockedObjects.Count == 0 ||
            camera.ScreenWidth <= 0f || camera.ScreenHeight <= 0f || camera.FrameScaleX <= 0f || camera.FrameScaleY <= 0f)
        {
            return 0;
        }

        const float DegreesToRotator = 65536f / 360f;
        var view = new FRotator((int)MathF.Round(camera.Pitch * DegreesToRotator), (int)MathF.Round(camera.Yaw * DegreesToRotator),
            (int)MathF.Round(camera.Roll * DegreesToRotator));
        view.GetAxes(out FVector forward, out FVector right, out FVector up);
        var eye = new FVector(camera.X, camera.Y, camera.Z);
        float halfWidth = camera.ScreenWidth * 0.5f;
        float halfHeight = camera.ScreenHeight * 0.5f;
        float focal = halfWidth / MathF.Tan(Math.Clamp(camera.FovX, 1f, 170f) * (MathF.PI / 360f));
        BridgeVisionCamera frame = camera;

        // A world point to the frame, with its depth in Z below 1 in front of the camera (HudMotionTracking.Update).
        FVector ToFrame(FVector p)
        {
            FVector d = p - eye;
            float depth = d | forward;
            if (depth <= 1f)
            {
                return new FVector(0f, 0f, 2f);
            }

            float sx = halfWidth + ((d | right) / depth * focal);
            float sy = halfHeight - ((d | up) / depth * focal);
            return new FVector((sx - frame.FrameOffsetX) / frame.FrameScaleX, sy / frame.FrameScaleY, 0.5f);
        }

        float now = level.Info.TimeSeconds;
        FRotator boxAxes = _session.Session.LocalPawn?.Rotation ?? view;
        tracker.ProjectLocks(ToFrame, boxAxes, SMotionTrackingVisionK.FrameWidth, SMotionTrackingVisionK.FrameHeight, now);
        int count = 0;
        foreach (FLockedObject l in tracker.LockedObjects)
        {
            if (l.Actor is not { } a)
            {
                continue;
            }

            if (count < rows.Length)
            {
                bool front = l.ScreenPosition1.Z < SMotionTrackingVisionK.FrontDepth;
                // The same test ProjectLocks makes for its clean rectangles.
                bool clean = front && tracker.FocusTime + l.StartTime < now && (!l.bRemove || now < l.EndTime);
                rows[count] = new BridgeVisionLock
                {
                    ActorId = a.ActorId,
                    Flags = (clean ? BridgeVisionLockFlags.Clean : 0) | (front ? BridgeVisionLockFlags.InFront : 0) |
                        (l.bRemove ? BridgeVisionLockFlags.Removing : 0) | (l.ItsMyFriend ? BridgeVisionLockFlags.Friend : 0) |
                        (a is Pawn ? BridgeVisionLockFlags.Pawn : 0),
                    X1 = MathF.Min(l.ScreenPosition1.X, l.ScreenPosition2.X),
                    X2 = MathF.Max(l.ScreenPosition1.X, l.ScreenPosition2.X),
                    Y1 = MathF.Min(l.ScreenPosition1.Y, l.ScreenPosition2.Y),
                    Y2 = MathF.Max(l.ScreenPosition1.Y, l.ScreenPosition2.Y),
                    StartTime = l.StartTime,
                    EndTime = l.EndTime,
                };
            }

            count++;
        }

        return count;
    }

    private void FillLocked(SMotionTrackingVision? tracker)
    {
        _locked.Clear();
        if (tracker is null)
        {
            return;
        }

        foreach (FLockedObject l in tracker.LockedObjects)
        {
            if (l.Actor is { } a && !l.bRemove)
            {
                _locked.Add(a);
            }
        }
    }
}
