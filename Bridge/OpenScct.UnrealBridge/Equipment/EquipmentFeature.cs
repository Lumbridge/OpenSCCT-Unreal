using System;
using System.Collections.Generic;
using System.Numerics;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Animation;
using OpenScct.Game.Equipment;
using OpenScct.Game.Lighting;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
using OpenScct.Physics.Collision;
using OpenScct.UnrealBridge.Pose;
using OpenScctGame.Play.Session;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary>
/// What the pawns of a session hold and wear, and the local player's first-person view model, as Unreal reads them
/// each frame. It reads the client world after the simulation's ticks and never writes it.
/// <para>Ported from the Godot client: the attachments from game/Scripts/Views/PawnAttachments.cs and
/// game/Scripts/Render/Characters/GoggleGlowView.cs, the first-person models from FirstPersonView.cs and
/// SpyFirstPersonView.cs, the torch's shape from game/Scripts/Render/FlashLightView.cs and SpotShadowLight.cs. Which
/// first-person view is drawn follows game/Scripts/Play/PlayRoot.cs.</para>
/// </summary>
public sealed class EquipmentFeature
{
    /// <summary>Shots this recent count as "firing" (a tenth of a second).</summary>
    private const int FiringTicks = 6;

    /// <summary>A frame longer than this is drawn as this long (a hitch must not throw a loop far ahead).</summary>
    private const float MaxFrameSeconds = 0.25f;

    private static readonly Name s_boneRightHand = new("B R Hand");
    private static readonly Name s_boneLeftHand = new("B L Hand");
    private static readonly Name s_boneHolster = new("HOLSTER");
    private static readonly Name s_boneLaser = new("Laser");

    /// <summary>NativeReplaceLaserDistant (game 0x10a99430): another merc's beam ends this far above his location when he crouches.</summary>
    private const float DistantRaiseCrouched = 48f;
    private static readonly Name s_firing = new("s_Firing");
    private static readonly Name s_reloading = new("s_Reloading");
    private static readonly Name s_fullReloading = new("s_FullReloading");
    private static readonly SStickyShocker s_clonePistol = new();

    private readonly BridgeSession _session;
    private readonly string? _sharedRoot;
    private readonly Dictionary<int, Gear> _gear = new();
    private readonly List<int> _gone = new();
    private readonly Dictionary<Name, int>[] _boneOf = [new(), new()];
    private readonly Dictionary<string, EquipmentModel?> _models = new(StringComparer.OrdinalIgnoreCase);
    private readonly GlbCharacter?[] _characters = new GlbCharacter?[2];
    private readonly bool[] _characterRead = new bool[2];
    private readonly BridgeBone[] _pawnPose = new BridgeBone[256];
    private readonly MercFirstPerson _merc;
    private readonly SpyFirstPerson _spy;
    private BridgeFirstPerson _firstPerson;
    private SBasePawn? _eyePawn;
    private long _ticks;
    private long _frameTicks = -1;
    private float _frameAlpha = -1f;
    private double _frameTime;
    private float _frameDt;
    private long _frame;

    private EquipmentFeature(BridgeSession session)
    {
        _session = session;
        _sharedRoot = SharedContent.FindRoot(session.Session.MapFolder);
        _merc = new MercFirstPerson(_sharedRoot);
        _spy = new SpyFirstPerson(_sharedRoot);
        session.Ticked += OnTick;
    }

    /// <summary>The equipment state of <paramref name="session"/>, made on first use.</summary>
    public static EquipmentFeature For(BridgeSession session) => session.Feature(static s => new EquipmentFeature(s));

    /// <summary>The shared content folder the session's meshes come from, or null.</summary>
    public string? SharedRoot => _sharedRoot;

    /// <summary>The view model channels of the merc's weapon as the last frame blended them (for the probe).</summary>
    public ReadOnlySpan<ChannelPose> FirstPersonChannels => _merc.Pose.Channels;

    /// <summary>
    /// How far the spy's support wrist ended from the stock stance's place when his eye-view grip was fitted, in
    /// metres; -1 before the eye view was first drawn or when its models are missing (for the probe).
    /// </summary>
    public float SpySupportContactError => _spy.SupportContactError;

    // ---- per tick -------------------------------------------------------------------------------------------

    /// <summary>After each client tick: counts the shots of every pawn and samples the local view model's channels.</summary>
    private void OnTick()
    {
        _ticks++;
        foreach (SBasePawn pawn in _session.Session.Client.World.Level.AllActors<SBasePawn>())
        {
            if (!pawn.bDeleteMe)
            {
                GearOf(pawn).CountShots(pawn, _ticks);
            }
        }

        foreach (KeyValuePair<int, Gear> pair in _gear)
        {
            if (pair.Value.Pawn.bDeleteMe || pair.Value.SeenTick != _ticks)
            {
                _gone.Add(pair.Key);
            }
        }

        foreach (int id in _gone)
        {
            _gear.Remove(id);
        }

        _gone.Clear();
        _merc.OnTick(_session.Session.LocalController);
    }

    private Gear GearOf(SBasePawn pawn)
    {
        if (!_gear.TryGetValue(pawn.ActorId, out Gear? gear) || !ReferenceEquals(gear.Pawn, pawn))
        {
            // A respawn is a new pawn: its counts start again.
            gear = new Gear(pawn);
            _gear[pawn.ActorId] = gear;
        }

        return gear;
    }

    // ---- per frame ------------------------------------------------------------------------------------------

    /// <summary>
    /// Starts the frame the first time anything is read after the simulation moved on: how much time passed since the
    /// frame before, and the first-person view (posed once a frame; its blend keeps state from draw to draw). Reading
    /// the same frame again changes nothing.
    /// </summary>
    private void BeginFrame()
    {
        float alpha = _session.Alpha;
        if (_frameTicks == _ticks && _frameAlpha == alpha)
        {
            return;
        }

        double time = (_ticks + (double)alpha) * Sim.DeltaTime;
        _frameDt = _frame == 0 ? 0f : (float)Math.Clamp(time - _frameTime, 0.0, MaxFrameSeconds);
        _frameTime = time;
        _frameTicks = _ticks;
        _frameAlpha = alpha;
        _frame++;
        DrawFirstPerson(alpha);
    }

    /// <summary>Which first-person view this frame shows, as PlayRoot decides it: the merc's weapon, else the spy's eye view.</summary>
    private void DrawFirstPerson(float alpha)
    {
        _firstPerson = default;
        _eyePawn = null;
        PlaySession play = _session.Session;
        SPlayerController? pc = play.LocalController;
        SBasePawn? local = play.LocalPawn;
        if (pc is null || local is null || !play.View.Valid)
        {
            return;
        }

        // The pawn seen from its own eyes (PlayRoot's firstPerson): its body leaves the main view whether or not a
        // view model is drawn over it (the merc in his scope, the spy's eye view with the pistol put away).
        _eyePawn = pc.FreeCameraFlying == 0 && (!pc.bBehindView || play.View.SpyFirstPerson) ? local : null;

        bool shown = _merc.Draw(pc, _frameDt, alpha, ref _firstPerson);
        if (!shown && play.View.SpyFirstPerson && pc is { SpyFirstPersonActive: true, FreeCameraFlying: 0 } && local is SPawnAttaque spy)
        {
            shown = _spy.Draw(pc, spy, alpha, ref _firstPerson);
        }

        if (!shown)
        {
            _firstPerson = default;
            return;
        }

        _firstPerson.ActorId = local.ActorId;
        _firstPerson.ShotCount = _gear.TryGetValue(local.ActorId, out Gear? gear) ? gear.Shots : 0;
        _firstPerson.Cloak = local.GlassEffect / 255f;
    }

    /// <summary>The first-person view model of this frame. False (and a cleared struct) when none is drawn.</summary>
    public bool GetFirstPerson(out BridgeFirstPerson view)
    {
        BeginFrame();
        view = _firstPerson;
        return view.Kind != BridgeFirstPersonKind.None;
    }

    /// <summary>
    /// Writes the pose of a part of this frame's first-person view (each bone relative to its parent, in the order of
    /// the part's skeleton). Returns the bone count, 0 when there is no such part.
    /// </summary>
    public int GetFirstPersonPose(int part, Span<BridgeBone> bones)
    {
        BeginFrame();
        switch (_firstPerson.Kind)
        {
            case BridgeFirstPersonKind.Merc when part == 0 && _merc.Applier is { } applier:
                return Write(applier.Positions, applier.Rotations, applier.Scales, bones);
            case BridgeFirstPersonKind.Spy when part == 0:
                return Write(_spy.ArmPositions, _spy.ArmRotations, _spy.ArmScales, bones);
            case BridgeFirstPersonKind.Spy when part == 1:
                return Write(_spy.WeaponPositions, _spy.WeaponRotations, _spy.WeaponScales, bones);
            default:
                return 0;
        }
    }

    /// <summary>Writes what every pawn holds and wears this frame. Returns the count written.</summary>
    public int GetEquipment(Span<BridgeEquipment> equipment)
    {
        BeginFrame();
        int count = 0;
        SBasePawn? local = _session.Session.LocalPawn;
        foreach (SBasePawn pawn in _session.Session.Client.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            if (count >= equipment.Length)
            {
                break;
            }

            equipment[count++] = EquipmentOf(pawn, ReferenceEquals(pawn, local));
        }

        return count;
    }

    private BridgeEquipment EquipmentOf(SBasePawn pawn, bool isLocal)
    {
        var merc = pawn as SPAWNDEFENSE;
        Gear? gear = _gear.TryGetValue(pawn.ActorId, out Gear? known) && ReferenceEquals(known.Pawn, pawn) ? known : null;
        var e = new BridgeEquipment
        {
            ActorId = pawn.ActorId,
            WeaponLoaded = pawn.WeaponLoaded,
            ActiveWeaponSlot = NetworkPawnAttachments.For(pawn).ActiveSlot,
            ShotCount = gear?.Shots ?? 0,
            ClipAmmo = -1,
            Ammo = -1,
            RateOfFire = -1,
            SelectedGadgetClass = EquipmentNames.ClassId(pawn.SelectedGadget),
            TorchBone = -1,
            Cloak = pawn.GlassEffect / 255f,
        };
        int flags = merc is not null ? BridgeEquipmentFlags.Merc : 0;
        flags |= pawn.bWet ? BridgeEquipmentFlags.Wet : 0;
        flags |= pawn.bStickySkinned ? BridgeEquipmentFlags.StickySkinned : 0;
        flags |= pawn.bGogglesUp ? 0 : BridgeEquipmentFlags.GogglesDown;
        flags |= ReferenceEquals(pawn, _eyePawn) ? BridgeEquipmentFlags.FirstPerson : 0;
        flags |= pawn.bBinocularsOn ? BridgeEquipmentFlags.Binoculars : 0;
        if (gear is not null && gear.Shots > 0 && _ticks - gear.LastShotTick <= FiringTicks)
        {
            flags |= BridgeEquipmentFlags.Firing;
        }

        if (gear is not null && gear.BodyReloads(pawn))
        {
            flags |= BridgeEquipmentFlags.Reloading;
        }

        if (pawn.GoggleBeam is { bDeleteMe: false } beam)
        {
            e.GoggleMode = beam.CurrentMode;
            flags |= beam.bHidden || beam.Mesh.IsNone ? 0 : BridgeEquipmentFlags.GogglesGlow;
        }

        if (merc is not null)
        {
            SWeaponDefense? weapon = OwnWeapon(merc);
            SWeaponDefenseDummy? dummy = weapon?.Dummy ?? merc.WeaponDummy;
            e.WeaponClass = EquipmentNames.ClassId((object?)weapon ?? dummy);
            if ((NetworkPawnAttachments.For(pawn).WeaponInHand ?? dummy?.InHand) == true)
            {
                flags |= BridgeEquipmentFlags.WeaponInHand;
            }

            if (weapon is not null)
            {
                e.ClipAmmo = weapon.ClipAmmo;
                e.Ammo = weapon.Ammo;
                e.RateOfFire = (int)weapon.ROFMode;
                Name state = weapon.GetStateName();
                flags |= state == s_firing ? BridgeEquipmentFlags.Firing : 0;
                flags |= state == s_reloading || state == s_fullReloading || weapon is MercSidearm { ReloadUntil: > 0 }
                    ? BridgeEquipmentFlags.Reloading : 0;
                if (weapon.InScope)
                {
                    flags |= BridgeEquipmentFlags.InScope;
                    e.ScopeFov = (pawn.Controller as SPlayerController)?.fBinocularVisionFOV ?? 0f;
                }
            }

            flags |= Torch(merc, ref e);
            flags |= MercLaser(merc, isLocal, ref e);
        }
        else if (pawn is SPawnAttaque spy)
        {
            SWeaponAttaque? pistol = spy.MyWeapon as SWeaponAttaque ?? spy.WeaponDummy;
            e.WeaponClass = EquipmentNames.ClassId(pistol);
            flags |= pawn.WeaponLoaded != 0 ? BridgeEquipmentFlags.WeaponInHand : 0;
            if (pistol is { bDeleteMe: false })
            {
                flags |= SpyLaser(pistol, ref e);
            }
        }
        else if (pawn is SpyClone clone)
        {
            e.WeaponClass = EquipmentNames.ClassId(s_clonePistol);
            flags |= clone.PistolDrawn ? BridgeEquipmentFlags.WeaponInHand : 0;
        }

        e.Flags = flags;
        return e;
    }

    /// <summary>The merc's own weapon on this machine: his first-person one when he has it, else the selected slot's.</summary>
    private static SWeaponDefense? OwnWeapon(SPAWNDEFENSE merc)
    {
        if (merc.MyWeapon is SWeaponDefense { bDeleteMe: false } mine)
        {
            return mine;
        }

        SWeaponDefense? selected = NetworkPawnAttachments.For(merc).ActiveSlot == 0 ? merc.PrimaryWeapon as SWeaponDefense : merc.SidearmWeapon;
        return selected is { bDeleteMe: false } ? selected : null;
    }

    /// <summary>
    /// The merc's torch (SGadTorche, spec/equipment/merc-flashlight.md): lit while its LightType is on, which the
    /// simulation's NativeTickTorche decides; a spot of EchelonRange with half-sizes SpotWidth and SpotHeight at that
    /// range, coloured as an LE_ESpotShadow light.
    /// </summary>
    private int Torch(SPAWNDEFENSE merc, ref BridgeEquipment e)
    {
        if (merc.Torche is not { bDeleteMe: false } torch)
        {
            return 0;
        }

        e.TorchBone = BoneIndex(1, s_boneLaser);
        e.TorchX = torch.Location.X;
        e.TorchY = torch.Location.Y;
        e.TorchZ = torch.Location.Z;
        e.TorchPitch = EquipmentMath.Degrees(torch.Rotation.Pitch);
        e.TorchYaw = EquipmentMath.Degrees(torch.Rotation.Yaw);
        e.TorchRoll = EquipmentMath.Degrees(torch.Rotation.Roll);
        e.TorchRange = torch.EchelonRange;
        e.TorchSpotWidth = torch.SpotWidth;
        e.TorchSpotHeight = torch.SpotHeight;
        e.TorchHalfAngle = torch.EchelonRange > 0f ? MathF.Atan(torch.SpotWidth / torch.EchelonRange) * (180f / MathF.PI) : 0f;
        LightRgb colour = ActorLightRules.SpotShadowColour(torch.LightHue, torch.LightSaturation, torch.LightBrightness);
        e.TorchR = colour.R;
        e.TorchG = colour.G;
        e.TorchB = colour.B;
        e.TorchBrightness = torch.LightBrightness;
        // FlashLightView draws a torch only while its merc lives.
        int flags = torch.LightType != ELightType.LT_None && merc.Health > 0 ? BridgeEquipmentFlags.TorchOn : 0;
        flags |= torch.bCorona ? BridgeEquipmentFlags.TorchCorona : 0;
        // Inside a network camera the simulation puts the light at the camera (20 uu ahead, 11 aside, 11 above) and
        // the Torch place and rotation are used as they are. Outside one the light sits on the Laser bone and
        // points along it while the merc has no aim IK.
        bool inCamera = merc.bUsingCamNetwork && merc.UsedObject is SCamNetwork;
        flags |= inCamera ? BridgeEquipmentFlags.TorchInCamera : 0;
        flags |= !inCamera && SGadTorche.AimRotation(merc) is null ? BridgeEquipmentFlags.TorchFollowsBone : 0;
        return flags;
    }

    /// <summary>
    /// The merc's laser sight (SGadTorcheLaser, spec/equipment/merc-laser.md rules 8 and 9). The beam shows only
    /// while its merc lives and has the rifle up (WeaponLoaded 2) or looks through a network camera, as
    /// MercLaserView.Draw decides; switched on with the rifle down only <see cref="BridgeEquipmentFlags.LaserOn"/> is
    /// set. The local merc's beam is the one the simulation placed (from the rifle's FX bone pulled back 50 uu to
    /// what it hits). For every other merc the simulation places none, so it is worked out here as the Godot client
    /// does (<see cref="DistantBeam"/>).
    /// </summary>
    private int MercLaser(SPAWNDEFENSE merc, bool isLocal, ref BridgeEquipment e)
    {
        if (merc.TorcheLaser is not { bDeleteMe: false, bInUse: true } laser)
        {
            return 0;
        }

        int flags = BridgeEquipmentFlags.LaserOn;
        bool inCamera = merc.bUsingCamNetwork && merc.UsedObject is SCamNetwork;
        if (merc.Health <= 0 || (merc.WeaponLoaded != 2 && !inCamera))
        {
            // The simulation stops placing the beam here and keeps its last ends: they are stale, not a beam.
            return flags;
        }

        FVector start, end;
        if (isLocal)
        {
            if (laser.BeamStart == default || laser.BeamStart == laser.HitLocation)
            {
                return flags;
            }

            start = laser.BeamStart;
            end = laser.HitLocation;
        }
        else
        {
            (start, end) = DistantBeam(merc);
        }

        e.LaserStartX = start.X;
        e.LaserStartY = start.Y;
        e.LaserStartZ = start.Z;
        e.LaserEndX = end.X;
        e.LaserEndY = end.Y;
        e.LaserEndZ = end.Z;
        return flags | BridgeEquipmentFlags.LaserBeam;
    }

    /// <summary>
    /// Another merc's beam (NativeReplaceLaserDistant, decoded game 0x10a99430; port of MercLaserView.DistantBeam):
    /// from the FX bone of the rifle in his hands along his packed desired aim (the signed pitch and yaw words of
    /// iDesiredYawPitch) toward his location raised by 55.5 uu (48 crouched) plus 15000 uu of that direction; a line
    /// to there stops it at the first hit of the world or a pawn, else it ends 15000 uu from the start. The start is
    /// taken from the pose of the last tick; while the rifle or its file is missing the right hand 24 uu along the
    /// aim stands in, and without a skeleton a point 40 uu ahead of his chest.
    /// (Public for the probe, which runs it on the local merc beside the beam the simulation placed.)
    /// </summary>
    public (FVector Start, FVector End) DistantBeam(SPAWNDEFENSE merc)
    {
        ArgumentNullException.ThrowIfNull(merc);
        int packed = merc.iDesiredYawPitch;
        int pitch = (packed >> 16) & 0xFFFF;
        int yaw = packed & 0xFFFF;
        var aim = new FRotator(pitch > 0x8000 ? pitch - 0x10000 : pitch, yaw > 0x8000 ? yaw - 0x10000 : yaw, merc.Rotation.Roll);
        FVector dir = aim.Vector();
        FVector start = merc.Location + new FVector(0f, 0f, merc.EyeHeight * 0.6f) + (dir * 40f);
        if (TryBoneWorld(merc, 1, BoneIndex(1, s_boneRightHand), out Xform hand))
        {
            NetworkPawnAttachments presentation = NetworkPawnAttachments.For(merc);
            SWeaponDefense? selected = presentation.ActiveSlot == 0 ? merc.PrimaryWeapon as SWeaponDefense : merc.SidearmWeapon;
            if ((selected?.Dummy ?? merc.WeaponDummy) is { bDeleteMe: false } dummy && !dummy.Mesh.IsNone &&
                (presentation.WeaponInHand ?? dummy.InHand) && Model(dummy.Mesh.Path!) is { MuzzleBone: >= 0 } gun)
            {
                // The gun hangs on the hand's frame with the scale taken out; its FX bone stays at rest (the
                // third-person guns have no clips).
                GlbCharacter c = gun.Character;
                Xform fx = new Xform(hand.Basis.Orthonormalized(), hand.Origin) * gun.ActorToSkeleton *
                    gun.Global(gun.MuzzleBone, c.RestPosition, c.RestRotation, c.RestScale);
                start = RenderSpace.DirectionToUe(fx.Origin) * RenderSpace.UnitsPerMetre;
            }
            else
            {
                start = (RenderSpace.DirectionToUe(hand.Origin) * RenderSpace.UnitsPerMetre) + (dir * 24f);
            }
        }

        float raise = merc.bIsCrouched ? DistantRaiseCrouched : K.LaserEndRaiseStanding;
        FVector far = merc.Location + (dir * K.TraceLength) + new FVector(0f, 0f, raise);
        FVector end = GadgetWorld.Line(merc, far, start, ETraceFlags.TRACE_World | ETraceFlags.TRACE_Pawns, out FCheckResult hit)
            ? hit.Location
            : start + (dir * K.TraceLength);
        return (start, end);
    }

    /// <summary>
    /// The spy pistol's laser (spec/equipment/spy-shocker.md rule 14): the simulation's view update places a dot (an
    /// SLaserProjector) for every spy pistol and a beam (an SLaserBeam, 1 uu long stretched along its X) for the
    /// local player's own. Both are placed from the pistol's measured frame, not from a drawn skeleton: the Godot
    /// client starts its own beam at the drawn hand (30 X, -4 Y, 8 Z of "B R Hand") or, in the eye view, at the view
    /// model's "Laser" socket.
    /// </summary>
    private static int SpyLaser(SWeaponAttaque pistol, ref BridgeEquipment e)
    {
        if (!pistol.bLaserOn)
        {
            return 0;
        }

        int flags = BridgeEquipmentFlags.LaserOn;
        if (pistol.LaserBeam is { bDeleteMe: false } beam)
        {
            FVector end = beam.Location + (beam.Rotation.Vector() * MathF.Max(0f, beam.DrawScale3D.X - 1f));
            flags |= BridgeEquipmentFlags.LaserBeam;
            e.LaserStartX = beam.Location.X;
            e.LaserStartY = beam.Location.Y;
            e.LaserStartZ = beam.Location.Z;
            e.LaserEndX = end.X;
            e.LaserEndY = end.Y;
            e.LaserEndZ = end.Z;
        }
        else if (pistol.LaserProjector is { bDeleteMe: false } dot)
        {
            // The projector sits half its MaxTraceDistance (10 uu) back from what the laser hit, turned along the
            // line (SWeaponAttaque.PlaceLaser); the dot is on the surface ahead of it.
            FVector at = dot.Location + (dot.Rotation.Vector() * (0.5f * dot.MaxTraceDistance));
            flags |= BridgeEquipmentFlags.LaserDot;
            e.LaserEndX = at.X;
            e.LaserEndY = at.Y;
            e.LaserEndZ = at.Z;
        }

        return flags;
    }

    // ---- attachments ----------------------------------------------------------------------------------------

    /// <summary>
    /// Writes everything hanging on the pawns' bones this frame: the weapons a pawn carries and its goggle glow.
    /// Returns the count written.
    /// <para>The merc's third-person rifle (SWeaponDefenseDummy) hangs in the right hand ("B R Hand") while
    /// WeaponLoaded is above 0 and at the "HOLSTER" bone otherwise (spec/equipment/merc-rifle.md rule 22). The spy's
    /// pistol (SWeaponAttaque) hangs in the right hand while drawn (the left hand, with its relative place, at
    /// WeaponLoaded 7) and at the holster otherwise (spec/equipment/spy-shocker.md rule 14.1), posed by its own
    /// clips. The goggle glow (SGoggleBeam) hangs on the beam's bone with its relative place
    /// (spec/equipment/goggle-glow.md rule 8).</para>
    /// </summary>
    public int GetAttachments(Span<BridgeAttachment> attachments)
    {
        BeginFrame();
        int count = 0;
        foreach (SBasePawn pawn in _session.Session.Client.World.Level.AllActors<SBasePawn>())
        {
            if (pawn.bDeleteMe)
            {
                continue;
            }

            Gear gear = GearOf(pawn);
            gear.PistolAnimated = false;
            NetworkPawnAttachments presentation = NetworkPawnAttachments.For(pawn);
            SWeaponDefense? selected = presentation.ActiveSlot == 0 ? pawn.PrimaryWeapon as SWeaponDefense : pawn.SidearmWeapon;
            if (pawn is SPAWNDEFENSE merc && (selected?.Dummy ?? merc.WeaponDummy) is { bDeleteMe: false } dummy && !dummy.Mesh.IsNone)
            {
                Name bone = (presentation.WeaponInHand ?? dummy.InHand) ? s_boneRightHand : s_boneHolster;
                Add(attachments, ref count, pawn, BridgeAttachmentSlot.Weapon, dummy, dummy.Mesh.Path!, bone, default, default, 0, false);
                SWeaponDefense? holstered = presentation.ActiveSlot == 0 ? merc.SidearmWeapon : merc.PrimaryWeapon as SWeaponDefense;
                if (holstered is { bDeleteMe: false, Dummy: { } carried } && !carried.Mesh.IsNone)
                {
                    Add(attachments, ref count, pawn, BridgeAttachmentSlot.Carried, carried, carried.Mesh.Path!, s_boneHolster, default, default, 0, false);
                }
            }
            else if (pawn is SPawnAttaque spy && (spy.MyWeapon ?? spy.WeaponDummy) is SWeaponAttaque { bDeleteMe: false } pistol && !pistol.Mesh.IsNone)
            {
                // MyWeapon replicates to the owner only; other players' spies (bots too) have the pistol in
                // WeaponDummy, which replicates to everyone.
                bool animated = PosePistol(gear, pistol);
                switch (pawn.WeaponLoaded)
                {
                    case 0:
                        Add(attachments, ref count, pawn, BridgeAttachmentSlot.Weapon, pistol, pistol.Mesh.Path!, s_boneHolster, default, default, 0, animated);
                        break;
                    case 7:
                        Add(attachments, ref count, pawn, BridgeAttachmentSlot.Weapon, pistol, pistol.Mesh.Path!, s_boneLeftHand,
                            SWeaponAttaque.OtherHandOffset, new FRotator(0, 0, SWeaponAttaque.OtherHandRoll), 0, animated);
                        break;
                    default:
                        Add(attachments, ref count, pawn, BridgeAttachmentSlot.Weapon, pistol, pistol.Mesh.Path!, s_boneRightHand, default, default, 0, animated);
                        break;
                }
            }
            else if (pawn is SpyClone clone)
            {
                Add(attachments, ref count, pawn, BridgeAttachmentSlot.Weapon, s_clonePistol, s_clonePistol.Mesh.Path!,
                    clone.PistolDrawn ? s_boneRightHand : s_boneHolster, default, default, 0, false);
            }

            if (pawn.GoggleBeam is { bDeleteMe: false, bHidden: false } beam && !beam.Mesh.IsNone)
            {
                Add(attachments, ref count, pawn, BridgeAttachmentSlot.GoggleGlow, beam, beam.Mesh.Path!, beam.AttachmentBone,
                    beam.RelativeLocation, beam.RelativeRotation, EquipmentNames.Id(beam.Skin.Path), false);
            }
        }

        return count;
    }

    /// <summary>
    /// Writes the own pose of an animated attachment (the spy's pistol) as of the last <see cref="GetAttachments"/>:
    /// each bone relative to its parent, in the order of the attachment's skeleton. Returns the bone count, 0 when
    /// the attachment is not animated (it stays in its rest pose).
    /// </summary>
    public int GetAttachmentPose(int actorId, int slot, Span<BridgeBone> bones)
    {
        if (slot != BridgeAttachmentSlot.Weapon || !_gear.TryGetValue(actorId, out Gear? gear) ||
            !gear.PistolAnimated || gear.PistolApplier is not { } applier)
        {
            return 0;
        }

        return Write(applier.Positions, applier.Rotations, applier.Scales, bones);
    }

    /// <summary>
    /// Poses a spy pistol from its own channels (DrawP190, SnipeP190, WaitP190), once a frame. False when the content
    /// has no pistol model.
    /// </summary>
    private bool PosePistol(Gear gear, SWeaponAttaque pistol)
    {
        if (!ReferenceEquals(gear.Pistol, pistol))
        {
            gear.Pistol = pistol;
            gear.PistolModel = Model(pistol.Mesh.Path!);
            gear.PistolApplier = gear.PistolModel is { } model ? new ChannelPoseApplier(model.Character) : null;
            gear.PistolPose = new ViewModelPose();
            gear.PistolFrame = -1;
        }

        if (gear.PistolModel is not { } pistolModel || gear.PistolApplier is not { } applier)
        {
            return false;
        }

        if (gear.PistolFrame != _frame)
        {
            gear.PistolFrame = _frame;
            applier.Apply(gear.PistolPose!.Update(pistol.ViewAnim, pistolModel.Character, _frameDt));
        }

        gear.PistolAnimated = true;
        return true;
    }

    private void Add(Span<BridgeAttachment> attachments, ref int count, SBasePawn pawn, int slot, Actor actor, string asset, Name bone,
        FVector relativeLocation, FRotator relativeRotation, int skin, bool animated)
    {
        if (count >= attachments.Length)
        {
            return;
        }

        int kind = pawn is SPAWNDEFENSE ? 1 : 0;
        EquipmentModel? model = Model(asset);
        int boneIndex = BoneIndex(kind, bone);
        int flags = animated ? BridgeAttachmentFlags.Animated : 0;
        flags |= ReferenceEquals(pawn, _eyePawn) ? BridgeAttachmentFlags.OnFirstPersonPawn : 0;
        attachments[count++] = new BridgeAttachment
        {
            ActorId = pawn.ActorId,
            Slot = slot,
            Mesh = EquipmentNames.Id(asset),
            MeshPath = model?.PathId ?? 0,
            Class = EquipmentNames.ClassId(actor),
            Bone = EquipmentNames.Id(bone),
            BoneIndex = boneIndex,
            Skin = skin,
            Flags = flags,
            BoneCount = model?.Character.BoneCount ?? 0,
            RelX = relativeLocation.X,
            RelY = relativeLocation.Y,
            RelZ = relativeLocation.Z,
            RelPitch = EquipmentMath.Degrees(relativeRotation.Pitch),
            RelYaw = EquipmentMath.Degrees(relativeRotation.Yaw),
            RelRoll = EquipmentMath.Degrees(relativeRotation.Roll),
            ModelToBone = EquipmentMath.ToBone(ModelToBone(pawn, kind, boneIndex, relativeLocation, relativeRotation, model)),
        };
    }

    /// <summary>
    /// Where an attachment's skeleton sits relative to the pawn bone it hangs on, in the character files' space.
    /// UE2 hangs an actor on a bone by the bone's frame with its scale taken out, then the relative place
    /// (PawnAttachments.Place: <c>world.Orthonormalized() * relative</c>), and the attachment's mesh keeps its own
    /// RotOrigin and scale below that. Seen from the bone as the pawn's skeleton has it, that is: the pawn mesh's
    /// placement and the bone's pose taken back out, the unscaled bone frame, the relative place, the attachment's
    /// mesh placement.
    /// </summary>
    private Xform ModelToBone(SBasePawn pawn, int kind, int boneIndex, FVector relativeLocation, FRotator relativeRotation, EquipmentModel? model)
    {
        Xform relative = EquipmentMath.FromUe(relativeLocation, relativeRotation);
        Xform own = model?.ActorToSkeleton ?? Xform.Identity;
        if (Character(kind) is not { } character || boneIndex < 0)
        {
            return relative * own;
        }

        // The yaw the pawn is drawn at turns both sides alike, so it is left out.
        Xform inActor = new Xform(RenderSpace.SkeletonBasis(0, character.RotOrigin, character.MeshScale), Vector3.Zero) *
            BoneInSkeleton(pawn, character, boneIndex);
        Xform unscaled = new(inActor.Basis.Orthonormalized(), inActor.Origin);
        return inActor.AffineInverse() * unscaled * relative * own;
    }

    /// <summary>A pawn bone in its skeleton's space as posed this frame (its rest pose before the pawn's first tick).</summary>
    private Xform BoneInSkeleton(SBasePawn pawn, GlbCharacter character, int boneIndex)
    {
        int posed = _session.GetPawnPose(pawn.ActorId, _pawnPose);
        Xform bone = Xform.Identity;
        bool first = true;
        for (int b = boneIndex; b >= 0; b = character.Parents[b])
        {
            Xform local = posed > b && b < _pawnPose.Length
                ? EquipmentMath.FromBone(_pawnPose[b])
                : Xform.FromTrs(character.RestPosition[b], character.RestRotation[b], character.RestScale[b]);
            bone = first ? local : local * bone;
            first = false;
        }

        return bone;
    }

    /// <summary>
    /// A pawn bone in the world, in render space (metres, Y up), where the pawn stood at the last tick (port of
    /// SkeletalModel.Place: the mesh at Location + PrePivot turned by the body's yaw, then RotOrigin and scale).
    /// False when the skeleton lacks the bone.
    /// </summary>
    private bool TryBoneWorld(SBasePawn pawn, int kind, int boneIndex, out Xform world)
    {
        world = Xform.Identity;
        if (Character(kind) is not { } character || boneIndex < 0)
        {
            return false;
        }

        FVector at = pawn.Location + pawn.NetworkCorrectionOffset + pawn.PrePivot;
        world = new Xform(RenderSpace.SkeletonBasis(pawn.Rotation.Yaw, character.RotOrigin, character.MeshScale),
            RenderSpace.Direction(at) / RenderSpace.UnitsPerMetre) * BoneInSkeleton(pawn, character, boneIndex);
        return true;
    }

    /// <summary>The index of a bone in a pawn kind's skeleton by its UE name (cached), or -1.</summary>
    private int BoneIndex(int kind, Name bone)
    {
        if (bone.IsNone)
        {
            return -1;
        }

        Dictionary<Name, int> known = _boneOf[kind];
        if (!known.TryGetValue(bone, out int index))
        {
            index = Character(kind)?.FindBone(bone.ToString()) ?? -1;
            known[bone] = index;
        }

        return index;
    }

    /// <summary>The model of a mesh asset, looked up on disk once (a frame names the same few meshes again and again).</summary>
    private EquipmentModel? Model(string asset)
    {
        if (!_models.TryGetValue(asset, out EquipmentModel? model))
        {
            model = EquipmentModel.Load(asset, _sharedRoot);
            _models[asset] = model;
        }

        return model;
    }

    /// <summary>The character file of a pawn kind (0 spy, 1 merc), asked of the session once.</summary>
    private GlbCharacter? Character(int kind)
    {
        if (!_characterRead[kind])
        {
            _characterRead[kind] = true;
            _characters[kind] = _session.Character(kind);
        }

        return _characters[kind];
    }

    private static int Write(ReadOnlySpan<Vector3> positions, ReadOnlySpan<Quaternion> rotations, ReadOnlySpan<Vector3> scales, Span<BridgeBone> bones)
    {
        int count = Math.Min(bones.Length, positions.Length);
        for (int b = 0; b < count; b++)
        {
            bones[b] = new BridgeBone
            {
                Px = positions[b].X,
                Py = positions[b].Y,
                Pz = positions[b].Z,
                Qx = rotations[b].X,
                Qy = rotations[b].Y,
                Qz = rotations[b].Z,
                Qw = rotations[b].W,
                Sx = scales[b].X,
                Sy = scales[b].Y,
                Sz = scales[b].Z,
            };
        }

        return positions.Length;
    }

    /// <summary>What the bridge keeps for one pawn between ticks and frames.</summary>
    private sealed class Gear(SBasePawn pawn)
    {
        private object? _shotSource;
        private int _shotBase;
        private Name _bodySequence;
        private bool _bodyReloads;

        public SBasePawn Pawn { get; } = pawn;

        /// <summary>The tick the pawn was last seen in the world.</summary>
        public long SeenTick { get; private set; }

        /// <summary>Shots counted since the pawn was first seen.</summary>
        public int Shots { get; private set; }

        public long LastShotTick { get; private set; } = long.MinValue / 2;

        public SWeaponAttaque? Pistol { get; set; }

        public EquipmentModel? PistolModel { get; set; }

        public ChannelPoseApplier? PistolApplier { get; set; }

        public ViewModelPose? PistolPose { get; set; }

        public long PistolFrame { get; set; } = -1;

        /// <summary>The pistol was posed for the frame's attachments.</summary>
        public bool PistolAnimated { get; set; }

        /// <summary>
        /// Adds the shots fired since the last tick. The count read is the replicated muzzle count of the gun others
        /// see (SWeaponDefenseDummy.MuzzleCount, SWeaponAttaque.MuzzleCount), or the local merc's own weapon's shot
        /// counter, which moves with his prediction. When the gun read changes, counting starts again from its value.
        /// </summary>
        public void CountShots(SBasePawn pawn, long tick)
        {
            SeenTick = tick;
            object? source = null;
            int count = 0;
            if (pawn is SPAWNDEFENSE merc)
            {
                if (merc.MyWeapon is SWeaponDefense { bDeleteMe: false } mine)
                {
                    source = mine;
                    count = mine.ShotCounter;
                }
                else if (OwnWeapon(merc)?.Dummy is { bDeleteMe: false } carried)
                {
                    source = carried;
                    count = carried.MuzzleCount;
                }
                else if (merc.WeaponDummy is { bDeleteMe: false } dummy)
                {
                    source = dummy;
                    count = dummy.MuzzleCount;
                }
            }
            else if (pawn is SPawnAttaque spy && (spy.MyWeapon as SWeaponAttaque ?? spy.WeaponDummy) is { bDeleteMe: false } pistol)
            {
                source = pistol;
                count = pistol.MuzzleCount;
            }

            if (!ReferenceEquals(source, _shotSource))
            {
                _shotSource = source;
                _shotBase = count;
                return;
            }

            if (count > _shotBase)
            {
                Shots += count - _shotBase;
                LastShotTick = tick;
            }

            _shotBase = count;
        }

        /// <summary>
        /// True while the body plays a reload clip on its weapon channel, which is how a reload shows for players
        /// whose weapon this machine does not have (as ObservedWeaponViewModel reads it).
        /// </summary>
        public bool BodyReloads(SBasePawn pawn)
        {
            var action = pawn.PawnAnim[SBasePawn.WEAPONCHANNEL];
            if (action.Sequence != _bodySequence)
            {
                _bodySequence = action.Sequence;
                _bodyReloads = !action.Sequence.IsNone && action.Sequence.ToString().StartsWith("Relo", StringComparison.Ordinal);
            }

            return _bodyReloads && action.BlendAlpha > 0f;
        }
    }
}
