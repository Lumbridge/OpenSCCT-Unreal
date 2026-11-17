using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScct.UnrealBridge.Hud;

namespace OpenScct.UnrealBridge;

/// <summary>
/// The view Unreal draws this frame (<see cref="HudMotionApi.Build"/>): the camera, its projection and the area the rows
/// are placed on. Source/OpenSCCT/Runtime/ScctHudMotionApi.h mirrors it field for field.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudMotionView
{
    /// <summary>The camera's location in the world.</summary>
    public float X, Y, Z;

    /// <summary>The camera's rotation in degrees, as Unreal's FRotator has it.</summary>
    public float Pitch, Yaw, Roll;

    /// <summary>
    /// The projection: a point at view-space right r, up u and depth d lands at (CentreX + r / d x FocalX,
    /// CentreY - u / d x FocalY), in the units of <see cref="Width"/> and <see cref="Height"/>.
    /// </summary>
    public float FocalX, FocalY;

    public float CentreX, CentreY;

    /// <summary>The area the rows are placed on (the HUD's full-screen layer), in its own units.</summary>
    public float Width, Height;

    /// <summary>Which page draws the tracker: 0 none (nothing is drawn), 1 the merc's own page, 2 the camera network page.</summary>
    public int Page;

    public int Reserved;
}

/// <summary>What one <see cref="HudMotionApi.Build"/> drew. The header mirrors it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudMotionState
{
    /// <summary>1 while the local merc's goggles track (motion tracking vision on), else 0.</summary>
    public int Tracking;

    /// <summary>The goggles' locks (moving pawns and projectiles in sight) and the HUD tracker's locks (the laser's target).</summary>
    public int Locks;

    public int LaserLocks;

    /// <summary>Locks drawn (in front of the camera), and of those the ones drawn as a friend.</summary>
    public int Drawn;

    public int Friends;

    /// <summary>Rows written, and rows the draw list had (more than written when the buffer was short).</summary>
    public int Rows;

    public int TotalRows;

    /// <summary>1 when a row is the laser target's name over its bracket (the reticle then leaves the name out).</summary>
    public int NameRow;

    /// <summary>The view's units per unit of the 640 x 480 frame the tracker draws in.</summary>
    public float FrameScale;

    /// <summary>The level time the draw ran at.</summary>
    public float Time;
}

/// <summary>
/// One primitive of the tracker's HUD (<see cref="HudMotionApi.Build"/>), in draw order. The header mirrors it.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudMotionRow
{
    public const int TextLength = 32;

    /// <summary>0 a lock corner (SHudTextures.Vision_lock), 1 the sweep band (balayage_H), 2 a friend mark (Vision_friend), 3 a text.</summary>
    public int Kind;

    /// <summary>A text's font: 0 the tiny canvas font (speed and distance), 1 the medium one (Hold Fire, the target name).</summary>
    public int Font;

    /// <summary>
    /// A tile's corner (X, Y) and size (XL, YL; a negative size mirrors it), in the view's units; a text's top left, with
    /// YL its line height.
    /// </summary>
    public float X, Y, XL, YL;

    /// <summary>A tile's texel region.</summary>
    public float U, V, UL, VL;

    /// <summary>The colour as the canvas holds it (0 to 255 a channel; the canvas draws each channel doubled).</summary>
    public byte R, G, B, A;

    public fixed char Text[TextLength];
}

/// <summary>A lock as the last <see cref="HudMotionApi.Build"/> placed it (for the log and checks). The header mirrors it.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct HudMotionLock
{
    /// <summary>The locked actor (FScctBridgePawn::ActorId for a pawn).</summary>
    public int ActorId;

    /// <summary>0 a spy, 1 a merc, -1 anything else (a projectile).</summary>
    public int Side;

    /// <summary>1 when drawn as a friend.</summary>
    public int Friend;

    /// <summary>1 for a lock of the HUD's own tracker (the laser sight's target), 0 for the goggles'.</summary>
    public int Laser;

    /// <summary>1 when its box is in front of the camera.</summary>
    public int InFront;

    /// <summary>1 while it is held after the actor stopped (or left sight), fading out after its hold.</summary>
    public int Removing;

    /// <summary>The box in the view's units.</summary>
    public float X1, Y1, X2, Y2;

    /// <summary>The actor's speed (world units a second) and when the lock began (level time).</summary>
    public float Speed;

    public float StartTime;

    /// <summary>The actor's location in the world (for checks against Unreal's own projection).</summary>
    public float WorldX, WorldY, WorldZ;
}

/// <summary>
/// The motion tracking's table of entry points, beside the HUD's own (<see cref="HudExports"/>): every frame Unreal hands
/// over its view, and the goggles' tracker of the local merc (and, after a tick that ran the laser sight's target name
/// pass, the HUD's own tracker) has its locks projected with it (<see cref="SMotionTrackingVision.ProjectLocks"/>) and its
/// draw list built (<see cref="SMotionTrackingVision.BuildHud"/>), which comes back as rows: lock corners, sweep bands,
/// friend marks and texts with their colours, as the Godot client's HudMotionTracking turns them into quads. Unreal asks
/// for <see cref="GetApi"/> once (<c>Host.GetFunction("OpenScct.UnrealBridge.HudMotionExports", "GetApi")</c>).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct HudMotionApi
{
    public int Version;

    /// <summary>
    /// Projects and builds the trackers' HUD for the view, fills the state and writes up to <c>capacity</c> rows in draw
    /// order; returns how many were written (0 when nothing is drawn).
    /// </summary>
    public delegate* unmanaged[Cdecl]<HudMotionView*, HudMotionState*, HudMotionRow*, int, int> Build;

    /// <summary>Writes up to <c>capacity</c> of the locks the last Build placed; returns how many.</summary>
    public delegate* unmanaged[Cdecl]<HudMotionLock*, int, int> GetLocks;

    /// <summary>
    /// Unattended checks only: on the local match's server, puts the nearest pawn of the other side (0) or of the local
    /// player's side (1) a few metres in front of the local pawn, where it then walks on as its controller wants (a bot
    /// is far away across the map otherwise). 1 when it was queued.
    /// </summary>
    public delegate* unmanaged[Cdecl]<int, int> Summon;
}

/// <summary>
/// The draw behind <see cref="HudMotionApi"/> (ported from game/Scripts/Hud/HudMotionTracking.cs and HudMotionFrame.cs):
/// the frame is the 640 x 480 HUD frame centred on the view at the HUD's 4:3 scale (height / 480), as the Godot client
/// lays it out on a picture of at most 16:9 (Unreal draws the picture at the screen's own shape, so there is no stretch).
/// </summary>
public sealed class HudMotionDraw
{
    private readonly MotionHudDrawList _list = new();
    private readonly MotionHudInputs _inputs = new();
    private readonly List<HudMotionLock> _locks = new();

    public static HudMotionDraw Shared { get; } = new();

    /// <summary>The locks placed by the last <see cref="Build"/>.</summary>
    public IReadOnlyList<HudMotionLock> Locks => _locks;

    /// <summary>The tracker the local controller's HUD draws: the goggles' tracker of a merc, else null.</summary>
    public static SMotionTrackingVision? TrackerOf(SPlayerController? pc) =>
        pc is { SPawnType: PawnType.SPAWNDEFENSE } && pc.Goggle?.MotionTrackingVision is { bDeleteMe: false } t ? t : null;

    /// <summary>
    /// What <see cref="HudMotionApi.Build"/> does. <paramref name="targetName"/>: the last tick ran the laser target name
    /// pass, so the HUD's own tracker is drawn too (its friend marks come from there: the goggles' tracker never marks a
    /// friend).
    /// </summary>
    public int Build(SPlayerController? pc, in HudMotionView view, bool targetName, out HudMotionState state, Span<HudMotionRow> rows)
    {
        state = default;
        _list.Clear();
        _locks.Clear();
        SMotionTrackingVision? tracker = TrackerOf(pc);
        state.Tracking = tracker is not null && tracker.IsActive() ? 1 : 0;
        state.Locks = tracker?.LockedObjects.Count ?? 0;
        if (pc is null || view.Page == 0 || !(view.Width > 0f) || !(view.Height > 0f))
        {
            return 0;
        }

        SHud? hud = targetName ? pc.myHUD as SHud : null;
        SMotionTrackingVision? laserTracker = hud?.TargetTracker;
        state.LaserLocks = laserTracker?.LockedObjects.Count ?? 0;
        bool goggles = state.Locks > 0;
        bool laser = state.LaserLocks > 0;
        if (!goggles && !laser)
        {
            return 0;
        }

        // The frame: 640 x 480 at height / 480, centred.
        float scale = view.Height / SMotionTrackingVisionK.FrameHeight;
        float offsetX = view.Width * 0.5f - SMotionTrackingVisionK.HudCentreX * scale;
        state.FrameScale = scale;
        float now = pc.Level?.TimeSeconds ?? 0f;
        state.Time = now;

        // Unreal's camera axes (FRotationMatrix) and its projection to the view, then to the frame.
        Axes(view.Pitch, view.Yaw, view.Roll, out FVector forward, out FVector right, out FVector up);
        var origin = new FVector(view.X, view.Y, view.Z);
        HudMotionView v = view;
        FVector ToFrame(FVector p)
        {
            FVector d = p - origin;
            float depth = d | forward;
            if (!(depth > 1f))
            {
                // Behind the camera: depth 2 keeps the corner out of the draw (FrontDepth 1).
                return new FVector(0f, 0f, 2f);
            }

            float sx = v.CentreX + (d | right) / depth * v.FocalX;
            float sy = v.CentreY - (d | up) / depth * v.FocalY;
            return new FVector((sx - offsetX) / scale, sy / scale, 0.5f);
        }

        Fill(pc, now, tracker ?? laserTracker);
        FRotator boxAxes = pc.Pawn?.Rotation ?? pc.Rotation;
        if (goggles)
        {
            tracker!.ProjectLocks(ToFrame, boxAxes, SMotionTrackingVisionK.FrameWidth, SMotionTrackingVisionK.FrameHeight, now);
            tracker.BuildHud(_list, _inputs);
        }

        int names = 0;
        if (laser)
        {
            laserTracker!.ProjectLocks(ToFrame, boxAxes, SMotionTrackingVisionK.FrameWidth, SMotionTrackingVisionK.FrameHeight, now);
            laserTracker.BuildHud(_list, _inputs);

            // SHud.DrawTargetName's name part: the name over the bracket (and the lock marked a friend for a teammate).
            if (hud!.TargetNameLabel(t => _inputs.Measure(t, MotionHudFont.Medium)) is { } name)
            {
                _list.Add(name);
                names = 1;
            }
        }

        AddLocks(tracker, false, scale, offsetX, ref state);
        AddLocks(laser ? laserTracker : null, true, scale, offsetX, ref state);
        state.TotalRows = _list.Order.Count;
        int written = Emit(rows, scale, offsetX);
        state.Rows = written;
        state.NameRow = names;
        return written;
    }

    private void Fill(SPlayerController pc, float now, SMotionTrackingVision? textSource)
    {
        _inputs.Now = now;
        _inputs.LastShotPawn = pc.fDateLastShootedPawn;
        _inputs.ViewerLocation = pc.Pawn?.Location ?? pc.Location;
        if (pc.myHUD is { } hud)
        {
            _inputs.FriendR = (byte)Math.Clamp(hud.iColorFriend_R, 0, 255);
            _inputs.FriendG = (byte)Math.Clamp(hud.iColorFriend_G, 0, 255);
            _inputs.FriendB = (byte)Math.Clamp(hud.iColorFriend_B, 0, 255);
            if (hud is SHud shud && shud.StrDistanceUnit.Length > 0)
            {
                _inputs.DistanceUnit = shud.fDistanceUnit;
                _inputs.DistanceUnitText = shud.StrDistanceUnit;
            }
        }

        if (textSource is { TextHold.Length: > 0 })
        {
            _inputs.TextHold = textSource.TextHold;
        }
    }

    private void AddLocks(SMotionTrackingVision? tracker, bool laser, float scale, float offsetX, ref HudMotionState state)
    {
        if (tracker is null)
        {
            return;
        }

        foreach (FLockedObject l in tracker.LockedObjects)
        {
            if (l.Actor is not { } a)
            {
                continue;
            }

            bool front = l.ScreenPosition1.Z < SMotionTrackingVisionK.FrontDepth;
            state.Drawn += front ? 1 : 0;
            state.Friends += front && l.ItsMyFriend ? 1 : 0;
            _locks.Add(new HudMotionLock
            {
                ActorId = a.ActorId,
                Side = a is SPAWNDEFENSE ? 1 : a is SPawnAttaque or OpenScct.Game.Svm.SpyClone ? 0 : -1,
                Friend = l.ItsMyFriend ? 1 : 0,
                Laser = laser ? 1 : 0,
                InFront = front ? 1 : 0,
                Removing = l.bRemove ? 1 : 0,
                X1 = offsetX + l.ScreenPosition1.X * scale,
                Y1 = l.ScreenPosition1.Y * scale,
                X2 = offsetX + l.ScreenPosition2.X * scale,
                Y2 = l.ScreenPosition2.Y * scale,
                Speed = a.Velocity.Size(),
                StartTime = l.StartTime,
                WorldX = a.Location.X,
                WorldY = a.Location.Y,
                WorldZ = a.Location.Z,
            });
        }
    }

    private unsafe int Emit(Span<HudMotionRow> rows, float scale, float offsetX)
    {
        int n = 0;
        foreach ((bool text, int index) in _list.Order)
        {
            if (n >= rows.Length)
            {
                break;
            }

            ref HudMotionRow row = ref rows[n];
            row = default;
            if (text)
            {
                MotionHudText t = _list.Texts[index];
                if (t.Colour.A == 0)
                {
                    // An enemy lock's labels: alpha 0 under the canvas's alpha style, so nothing shows.
                    continue;
                }

                row.Kind = 3;
                row.Font = t.Font == MotionHudFont.Tiny ? 0 : 1;
                row.X = offsetX + t.X * scale;
                row.Y = t.Y * scale;
                row.YL = MathF.Round((t.Font == MotionHudFont.Tiny ? TinyFontHeight : MediumFontHeight) * scale);
                (row.R, row.G, row.B, row.A) = (t.Colour.R, t.Colour.G, t.Colour.B, t.Colour.A);
                fixed (char* into = row.Text)
                {
                    int count = Math.Min(t.Text.Length, HudMotionRow.TextLength - 1);
                    for (int i = 0; i < count; i++)
                    {
                        into[i] = t.Text[i];
                    }

                    into[count] = '\0';
                }

                n++;
                continue;
            }

            MotionHudTile tile = _list.Tiles[index];
            if (tile.Colour.A == 0)
            {
                continue;
            }

            row.Kind = (int)tile.Texture;
            row.X = offsetX + tile.X * scale;
            row.Y = tile.Y * scale;
            row.XL = tile.XL * scale;
            row.YL = tile.YL * scale;
            (row.U, row.V, row.UL, row.VL) = (tile.U, tile.V, tile.UL, tile.VL);
            (row.R, row.G, row.B, row.A) = (tile.Colour.R, tile.Colour.G, tile.Colour.B, tile.Colour.A);
            n++;
        }

        return n;
    }

    /// <summary>Unreal's FRotationMatrix axes of a rotation in degrees: forward, right and up.</summary>
    public static void Axes(float pitch, float yaw, float roll, out FVector forward, out FVector right, out FVector up)
    {
        float p = pitch * (MathF.PI / 180f), y = yaw * (MathF.PI / 180f), r = roll * (MathF.PI / 180f);
        float sp = MathF.Sin(p), cp = MathF.Cos(p), sy = MathF.Sin(y), cy = MathF.Cos(y), sr = MathF.Sin(r), cr = MathF.Cos(r);
        forward = new FVector(cp * cy, cp * sy, sp);
        right = new FVector(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp);
        up = new FVector(-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp);
    }

    /// <summary>Canvas.TinyFont and MedFont line heights in frame units (the Godot client's HudMotionK: the fonts are not converted).</summary>
    public const float TinyFontHeight = 9f;

    public const float MediumFontHeight = 12f;
}

public static unsafe class HudMotionExports
{
    public const int ApiVersion = 1;

    /// <summary>Fills <paramref name="api"/>. Returns the motion API version.</summary>
    [UnmanagedCallersOnly]
    public static int GetApi(HudMotionApi* api)
    {
        api->Version = ApiVersion;
        api->Build = &Build;
        api->GetLocks = &GetLocks;
        api->Summon = &Summon;
        return ApiVersion;
    }

    /// <summary>
    /// What <see cref="HudMotionApi.Summon"/> does on the server's thread (the probe calls it too): the nearest pawn of the
    /// other side (<paramref name="mate"/> false) or of the player's own side is moved to the first free spot 3.5 to 9 m in
    /// front of the player's pawn. Returns the moved pawn's actor id on the server, or 0.
    /// </summary>
    public static int SummonOnServer(ServerHost host, int playerId, bool mate)
    {
        SPlayerController? owner = null;
        foreach (SPlayerController pc in host.World.Level.AllActors<SPlayerController>())
        {
            if (!pc.bDeleteMe && pc.PlayerReplicationInfo?.PlayerID == playerId && pc.Pawn is { Health: > 0 })
            {
                owner = pc;
                break;
            }
        }

        if (owner?.Pawn is not SBasePawn me)
        {
            return 0;
        }

        SBasePawn? best = null;
        float bestDistance = float.MaxValue;
        foreach (SBasePawn p in host.World.Level.AllActors<SBasePawn>())
        {
            if (p.bDeleteMe || ReferenceEquals(p, me) || p.Health <= 0 || (p.SPawnType == me.SPawnType) != mate)
            {
                continue;
            }

            float d = (p.Location - me.Location).SizeSquared();
            if (d < bestDistance)
            {
                (best, bestDistance) = (p, d);
            }
        }

        if (best is null)
        {
            return 0;
        }

        // Spots inside the view (straight ahead first, then up to 40 degrees to a side), each in clear sight of the
        // player's eye, so the tracker's sight rule passes and the box is on the screen.
        IActorNatives? natives = host.World.Level.GetService<IActorNatives>();
        FVector eye = me.Location + new FVector(0f, 0f, me.BaseEyeHeight);
        float yaw = owner.Rotation.Yaw * (MathF.PI * 2f / 65536f);
        foreach (float ahead in (ReadOnlySpan<float>)[500f, 700f, 350f, 900f, 1200f])
        {
            foreach (float turn in (ReadOnlySpan<float>)[0f, 20f, -20f, 40f, -40f])
            {
                float a = yaw + turn * (MathF.PI / 180f);
                FVector spot = me.Location + new FVector(MathF.Cos(a), MathF.Sin(a), 0f) * ahead;
                if (natives is not null && !natives.FastTrace(spot, eye))
                {
                    continue;
                }

                if (best.SetLocation(spot))
                {
                    return best.ActorId;
                }
            }
        }

        return 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Summon(int mate)
    {
        try
        {
            if (Exports.Current is not { IsRemote: false, IsDisposed: false } session || session.Session.Host is not { } host ||
                session.Session.LocalController?.PlayerReplicationInfo is not { } pri)
            {
                return 0;
            }

            int id = pri.PlayerID;
            void Run(ServerHost running)
            {
                running.BeforeWorldTick -= Run;
                try
                {
                    SummonOnServer(running, id, mate != 0);
                }
                catch (Exception)
                {
                    // A check that fails shows as nothing happening; it must not take the server down.
                }
            }

            host.BeforeWorldTick += Run;
            return 1;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Build(HudMotionView* view, HudMotionState* state, HudMotionRow* rows, int capacity)
    {
        try
        {
            HudMotionState built = default;
            int written = 0;
            if (view != null && Exports.Current is { IsDisposed: false } session)
            {
                SPlayerController? pc = session.Session.LocalController is { bDeleteMe: false } local ? local : null;
                written = HudMotionDraw.Shared.Build(pc, *view, HudFeature.Of(session).TargetNameRan, out built,
                    rows != null && capacity > 0 ? new Span<HudMotionRow>(rows, capacity) : Span<HudMotionRow>.Empty);
            }

            if (state != null)
            {
                *state = built;
            }

            return written;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetLocks(HudMotionLock* locks, int capacity)
    {
        try
        {
            IReadOnlyList<HudMotionLock> placed = HudMotionDraw.Shared.Locks;
            int n = locks != null ? Math.Min(capacity, placed.Count) : 0;
            for (int i = 0; i < n; i++)
            {
                locks[i] = placed[i];
            }

            return n;
        }
        catch (Exception e)
        {
            Exports.ReportFailure(e.ToString());
            return 0;
        }
    }
}
