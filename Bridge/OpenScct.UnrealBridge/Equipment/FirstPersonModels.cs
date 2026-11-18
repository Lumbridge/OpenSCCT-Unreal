using System;
using System.Collections.Generic;
using System.Numerics;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Camera;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
using OpenScct.Physics.Movement;
using OpenScct.Server;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary>
/// The merc's first-person arms and gun (the weapon's own mesh, SWeaponFirst.1DEF_01 by default): drawn while the
/// local view is first person and the weapon draws its view model (bDrawWeapon), placed at the camera, and posed
/// from the weapon's view model channels (<see cref="ViewModelAnim"/>) by the <see cref="ChannelPoseApplier"/>. The
/// one-shots are sampled after every client tick and drawn between ticks like the camera. Reads the simulation and
/// never writes it.
/// <para>A port of the placing and posing of OpenSCCT's game/Scripts/Views/FirstPersonView.cs. Not ported (they
/// belong to Unreal's drawing): lighting at the eye, the stencil mask, cosmetics and map outfits on the sleeves, the
/// aim-down-sights blend of OpenSCCT's own rifles (OpenScctRifles, which this content does not hold).</para>
/// <para>A merc watched in a demo's first-person camera gets the same view model, built from his recorded
/// third-person weapon by OpenSCCT's ObservedWeaponViewModel (linked from game/Scripts/Views) and sampled after
/// every tick as FirstPersonView.OnObservedTick and DrawObserved do: <see cref="OnObservedTick"/> and
/// <see cref="DrawObserved"/>.</para>
/// <para>Not run yet: OpenSCCT's own pistols (OpenScctSidearms.*_first). Their sight offset, revolver kick and
/// reload handling are ported below and the files load (their clips move scene nodes outside the skin, the gun's
/// parts and a "Root" node above the skeleton, which <see cref="GlbCharacter"/> lists as extra bones), but no
/// probe has put a pistol in a merc's hands, so the view model of a merc holding one is unchecked.</para>
/// </summary>
internal sealed class MercFirstPerson
{
    private static readonly Name s_firstWaitStFd2 = ViewModelClips.FirstWaitStFd2;

    private readonly ViewModelPose _pose = new();
    private readonly OpenScctGame.Views.ObservedWeaponViewModel _observed = new();
    private readonly ViewModelClipRates _rates = new();
    private readonly Dictionary<string, Loaded?> _loaded = new(StringComparer.OrdinalIgnoreCase);
    private readonly string? _sharedRoot;
    private SWeaponDefense? _lastWeapon;
    private int _lastShot;
    private float _revolverKick;

    public MercFirstPerson(string? sharedRoot) => _sharedRoot = sharedRoot;

    /// <summary>The model drawn by the last <see cref="Draw"/>, or null.</summary>
    public EquipmentModel? Model { get; private set; }

    /// <summary>The pose of the last draw (bone by bone relative to the parent), when <see cref="Model"/> is not null.</summary>
    public ChannelPoseApplier? Applier { get; private set; }

    /// <summary>The channels of the last draw (for the probe).</summary>
    public ViewModelPose Pose => _pose;

    /// <summary>
    /// The muzzle ("FX") of the last draw in the camera's frame (metres, X forward, Y up, Z right), and the same bone
    /// in the middle of the idle clip (FirstWaitStFd2), the neutral pose the laser sight is zeroed in; null when the
    /// last draw showed no model or the model has no muzzle bone. The sight's beam is laid from them every frame
    /// (EquipmentFeature.MountedBeam; MercLaserView.LocalBeam of the Godot client).
    /// </summary>
    public (Xform Barrel, Xform? Neutral)? Sight { get; private set; }

    /// <summary>After each client tick: samples the view model channels of the local merc's weapon.</summary>
    public void OnTick(SPlayerController? pc) => _pose.OnTick(((pc?.Pawn as SBasePawn)?.MyWeapon as SWeaponDefense)?.ViewAnim);

    /// <summary>
    /// After each client tick of a demo: moves the weapon rebuilt for the watched <paramref name="pawn"/> on by a
    /// tick (its shots and the body's reloads) and samples its channels (FirstPersonView.OnObservedTick).
    /// </summary>
    public void OnObservedTick(SBasePawn? pawn) => _pose.OnTick(_observed.Update(pawn, Sim.DeltaTime)?.ViewAnim);

    /// <summary>
    /// The view model of a merc watched from his eyes in a demo, from the weapon rebuilt out of his recorded
    /// third-person one (FirstPersonView.DrawObserved). False when he is not a living merc with a gun in his hands.
    /// </summary>
    public bool DrawObserved(SBasePawn? pawn, float dt, float alpha, ref BridgeFirstPerson view)
    {
        Model = null;
        Applier = null;
        SWeaponDefense? weapon = _observed.Update(pawn, 0f);
        if (!ReferenceEquals(_lastWeapon, weapon))
        {
            _pose.OnTick(weapon?.ViewAnim);
        }

        return weapon is not null && !weapon.Mesh.IsNone && DrawWeapon(weapon, _observed.ShotCount, dt, alpha, ref view);
    }

    /// <summary>
    /// Poses the view model of <paramref name="pc"/>'s weapon for a frame <paramref name="dt"/> seconds after the
    /// last and <paramref name="alpha"/> of the way into the next tick, and fills <paramref name="view"/>. False when
    /// no view model is drawn (third person, no weapon, weapon put away, the file missing).
    /// (Estimated in OpenSCCT: the view model sits at the camera's location and rotation; the original's weapon draw
    /// offset is not in the specs.)
    /// </summary>
    public bool Draw(SPlayerController? pc, float dt, float alpha, ref BridgeFirstPerson view)
    {
        Model = null;
        Applier = null;
        Sight = null;
        var weapon = (pc?.Pawn as SBasePawn)?.MyWeapon as SWeaponDefense;
        bool show = pc is { bBehindView: false } && weapon is { bDrawWeapon: true, bDeleteMe: false } && !weapon.Mesh.IsNone &&
            ReferenceEquals(pc.ViewTarget ?? pc.Pawn, pc.Pawn);
        return show && DrawWeapon(weapon!, weapon!.ShotCounter, dt, alpha, ref view);
    }

    private bool DrawWeapon(SWeaponDefense weapon, int shotCount, float dt, float alpha, ref BridgeFirstPerson view)
    {
        if (Load(weapon.Mesh.Path!) is not { } loaded)
        {
            return false;
        }

        if (!ReferenceEquals(_lastWeapon, weapon))
        {
            _lastWeapon = weapon;
            _lastShot = shotCount;
            _revolverKick = 0f;
        }

        if (weapon is MercSidearm { Kind: (int)SidearmKind.Magnum44 } && shotCount != _lastShot)
        {
            _revolverKick = 0.42f;
        }

        _lastShot = shotCount;

        // A pistol's reload, and a reload of OpenSCCT's own rifles, owns both arms: nothing else is layered over it.
        Name action = weapon.ViewAnim.Sequence(SWeaponDefense.ACTIONCHANNEL);
        bool customHandling = weapon is MercSidearm or MercRifle &&
            (weapon is MercSidearm { ReloadUntil: > 0 } || action == weapon.AFirstReloGrenSt || action == weapon.AFirstReloSt);
        // A clip of an animation project bound to this model for gameplay runs at the project clip's frame rate.
        _rates.Bind(weapon.XLevel?.GetService<AnimationWorld>()?.Project, weapon.Mesh.Path!);
        loaded.Applier.Apply(_pose.Update(weapon.ViewAnim, loaded.Model.Character, dt, alpha, exclusiveAction: customHandling,
            blendLandingRecovery: weapon is MercSidearm, rates: _rates));

        Xform root = Xform.Identity;
        if (weapon is MercSidearm && loaded.SightOffset is { } sight)
        {
            root *= sight;
        }

        if (_revolverKick > 0f)
        {
            // The actor's X is forward and its Z is screen-right, so the muzzle climbs about Z; a turn about X would
            // only roll the gun.
            root = new Xform(root.Basis * SpyFirstPersonRig.Turn(Vector3.UnitZ, _revolverKick) *
                SpyFirstPersonRig.Turn(Vector3.UnitY, -_revolverKick * 0.45f), root.Origin);
            _revolverKick *= MathF.Exp(-10f * dt);
        }

        Model = loaded.Model;
        Applier = loaded.Applier;
        Sight = loaded.Model.MuzzleBone >= 0
            ? (root * loaded.Model.ActorToSkeleton * loaded.Model.Global(loaded.Model.MuzzleBone, loaded.Applier.Positions,
                loaded.Applier.Rotations, loaded.Applier.Scales), loaded.NeutralMuzzle is { } neutral ? root * neutral : null)
            : null;
        view.Kind = BridgeFirstPersonKind.Merc;
        view.PartCount = 1;
        view.WeaponClass = EquipmentNames.ClassId(weapon);
        // The rifle's rate-of-fire screen is Skins[3], which SetMode swaps between SI_rof_single, SI_rof_burst and
        // SI_rof_auto (spec/equipment/merc-rifle.md rule 16).
        view.RateOfFireSkin = weapon.Skins.Count > SWeaponDefense.ROF_MaterialID
            ? EquipmentNames.Id(weapon.Skins[SWeaponDefense.ROF_MaterialID].Path) : 0;
        view.Part0 = FirstPersonParts.Part(loaded.Model, root, loaded.Applier.Positions, loaded.Applier.Rotations, loaded.Applier.Scales);
        return true;
    }

    private Loaded? Load(string asset)
    {
        if (_loaded.TryGetValue(asset, out Loaded? known))
        {
            return known;
        }

        Loaded? loaded = null;
        if (EquipmentModel.Load(asset, _sharedRoot) is { } model)
        {
            Xform? neutral = NeutralMuzzle(model);
            loaded = new Loaded(model, new ChannelPoseApplier(model.Character), SidearmSight(asset, neutral), neutral);
        }

        _loaded[asset] = loaded;
        return loaded;
    }

    /// <summary>
    /// A pistol's sights set on a point ten metres ahead, worked out once from where its muzzle sits in the idle
    /// clip (OpenSCCT's own pistols; port of FirstPersonView.SidearmSightOffset). Null for the game's own guns.
    /// </summary>
    private static Xform? SidearmSight(string asset, Xform? neutralMuzzle)
    {
        if (!asset.StartsWith("OpenScctSidearms.", StringComparison.OrdinalIgnoreCase) || neutralMuzzle is not { } neutral)
        {
            return null;
        }

        Vector3 direction = Vector3.Normalize(new Vector3(10f, 0f, 0f) - neutral.Origin);
        Basis3 rotation = Basis3.FromQuaternion(EquipmentMath.ShortestArc(neutral.Basis.X, direction));
        return new Xform(rotation, neutral.Origin - (rotation * neutral.Origin));
    }

    /// <summary>
    /// The muzzle ("FX") where it sits in the middle of the idle clip, in the model's actor frame: the neutral pose
    /// the pistols' sights and every gun's laser sight are set in. Null without the bone or the clip.
    /// </summary>
    private static Xform? NeutralMuzzle(EquipmentModel model)
    {
        if (model.MuzzleBone < 0 || model.Character.Find(s_firstWaitStFd2.ToString()) is not { } idle)
        {
            return null;
        }

        int count = model.Character.BoneCount;
        var positions = new Vector3[count];
        var rotations = new Quaternion[count];
        var scales = new Vector3[count];
        model.Sample(idle, .5f, positions, rotations, scales);
        return model.ActorToSkeleton * model.Global(model.MuzzleBone, positions, rotations, scales);
    }

    private sealed record Loaded(EquipmentModel Model, ChannelPoseApplier Applier, Xform? SightOffset, Xform? NeutralMuzzle);
}

/// <summary>
/// The pistol and its grip in the local spy's eye view, relative to the camera: his arms (SPerso.ATT_01_fp) in the
/// stock two-handed grip fitted once at load (<see cref="SpyFirstPersonRig"/>) and the pistol (SWeaponFirst.p190_fp)
/// in his right hand. While drawing or holstering both swing between a lowered place and the grip; the pistol plays
/// its own draw clip.
/// <para>A port of the placing and posing of OpenSCCT's game/Scripts/Views/SpyFirstPersonView.cs (the eye view is
/// OpenSCCT's own). With the eye of the frame it also traces from the eye to the laser and muzzle sockets
/// (TryGetSocket): a socket with a wall between it and the eye is flagged hidden, and Unreal then starts neither the
/// muzzle flash nor the laser there. Not ported: the actions of an animation project's first-person profile
/// (DrawStudioActions), lighting, cosmetics and outfits.</para>
/// </summary>
internal sealed class SpyFirstPerson
{
    private const string ArmsAsset = "SPerso.ATT_01_fp";
    private const string WeaponAsset = "SWeaponFirst.p190_fp";
    private static readonly FVector s_probeExtent = new(1.25f, 1.25f, 1.25f);

    // X is forward, Y is up and Z is screen-right. Distances are metres from the eye.
    private static readonly Xform s_lowered = new(
        SpyFirstPersonRig.Turn(Vector3.UnitY, -.23f) * SpyFirstPersonRig.Turn(Vector3.UnitZ, -.44f) * SpyFirstPersonRig.Turn(Vector3.UnitX, .30f),
        new Vector3(.20f, -.35f, .27f));

    private readonly string? _sharedRoot;
    private bool _preloaded;
    private Xform _gripped;
    private Xform _gripInverse;
    private int _stockBone = -1;
    private Vector3 _stockRest;
    private Name _sampledClip;
    private float _sampledPhase = float.NaN;
    private string? _sampledName;

    public SpyFirstPerson(string? sharedRoot) => _sharedRoot = sharedRoot;

    public EquipmentModel? Arms { get; private set; }

    public EquipmentModel? Weapon { get; private set; }

    /// <summary>The arms' pose, fixed once the grip is fitted; the pistol's, sampled each draw.</summary>
    public Vector3[] ArmPositions { get; private set; } = [];

    public Quaternion[] ArmRotations { get; private set; } = [];

    public Vector3[] ArmScales { get; private set; } = [];

    public Vector3[] WeaponPositions { get; private set; } = [];

    public Quaternion[] WeaponRotations { get; private set; } = [];

    public Vector3[] WeaponScales { get; private set; } = [];

    /// <summary>How far the support wrist ended from the stock stance's place when the grip was fitted, metres (-1 before).</summary>
    public float SupportContactError { get; private set; } = -1f;

    /// <summary>Reads the two models and fits the grip (on first use; a content without them leaves the view off).</summary>
    public void Preload()
    {
        if (_preloaded)
        {
            return;
        }

        _preloaded = true;
        if (EquipmentModel.Load(ArmsAsset, _sharedRoot) is not { } arms || EquipmentModel.Load(WeaponAsset, _sharedRoot) is not { } gun ||
            gun.LaserBone < 0 || gun.MuzzleBone < 0)
        {
            return;
        }

        var positions = new Vector3[arms.Character.BoneCount];
        var rotations = new Quaternion[positions.Length];
        var scales = new Vector3[positions.Length];
        if (!SpyFirstPersonRig.TryPose(arms, SpyFirstPersonRig.Ready, positions, rotations, scales, out _gripped, out float error))
        {
            return;
        }

        _gripInverse = _gripped.AffineInverse();
        SupportContactError = error;
        ArmPositions = positions;
        ArmRotations = rotations;
        ArmScales = scales;
        WeaponPositions = (Vector3[])gun.Character.RestPosition.Clone();
        WeaponRotations = (Quaternion[])gun.Character.RestRotation.Clone();
        WeaponScales = (Vector3[])gun.Character.RestScale.Clone();
        _stockBone = gun.Character.FindBone("crosse");
        if (_stockBone >= 0)
        {
            _stockRest = gun.Character.RestPosition[_stockBone];
        }

        Arms = arms;
        Weapon = gun;
    }

    /// <summary>
    /// Places and poses the eye view of <paramref name="pawn"/> for a frame <paramref name="alpha"/> of the way into
    /// the next tick, and fills <paramref name="view"/>. False when it is not drawn (no pistol out, a move that
    /// hides it, the models missing).
    /// </summary>
    public bool Draw(SPlayerController pc, SPawnAttaque pawn, float alpha, ref BridgeFirstPerson view,
        (FVector Location, FRotator Rotation)? eye = null)
    {
        MuzzleHidden = false;
        LaserHidden = false;
        MuzzleDistance = 0f;
        Preload();
        if (Arms is not { } arms || Weapon is not { } gun ||
            pawn is not { bDeleteMe: false, bHidden: false, Health: > 0 } || !ReferenceEquals(pc.Pawn, pawn))
        {
            return false;
        }

        // The stance can arrive before the weapon references on the owning client.
        SWeaponAttaque? pistol = pawn.MyWeapon is SWeaponAttaque { bDeleteMe: false } own ? own
            : pawn.WeaponDummy is { bDeleteMe: false } dummy ? dummy : null;
        if (pawn.WeaponLoaded == 0 || HideForAction(pc, pawn))
        {
            return false;
        }

        bool drawing = pawn.WeaponLoaded is 3 or 4;
        float amount = drawing ? DrawAmount(pawn, pistol?.ViewAnim, alpha) : 1f;
        float eased = amount * amount * (3f - (2f * amount));
        Xform gunRoot = drawing ? EquipmentMath.Interpolate(s_lowered, _gripped, eased) : _gripped;
        Xform armsRoot = drawing ? gunRoot * _gripInverse : Xform.Identity;
        PoseWeapon(gun, drawing ? ViewModelClips.DrawP190 : ViewModelClips.SnipeP190, drawing ? amount : 0f);

        view.Kind = BridgeFirstPersonKind.Spy;
        view.PartCount = 2;
        view.WeaponClass = EquipmentNames.ClassId(pistol);
        view.Part0 = FirstPersonParts.Part(arms, armsRoot, ArmPositions, ArmRotations, ArmScales);
        view.Part1 = FirstPersonParts.Part(gun, gunRoot, WeaponPositions, WeaponRotations, WeaponScales);
        if (eye is { } camera)
        {
            MuzzleHidden = Behind(pawn, camera, gun, gunRoot, gun.MuzzleBone, out float muzzleDistance);
            LaserHidden = Behind(pawn, camera, gun, gunRoot, gun.LaserBone, out _);
            MuzzleDistance = muzzleDistance;
        }

        return true;
    }

    /// <summary>True when the last <see cref="Draw"/> found a wall between the eye and the pistol's muzzle socket "FX".</summary>
    public bool MuzzleHidden { get; private set; }

    /// <summary>True when the last <see cref="Draw"/> found a wall between the eye and the pistol's laser socket "Laser".</summary>
    public bool LaserHidden { get; private set; }

    /// <summary>How far the muzzle socket was from the eye in the last <see cref="Draw"/>, in units.</summary>
    public float MuzzleDistance { get; private set; }

    /// <summary>
    /// SpyFirstPersonView.TryGetSocket: whether the client world's collision (the camera's trace flags, a 1.25 unit
    /// box) is between the eye and a socket of the pistol as drawn. Only the effects that start at the socket are
    /// moved off it (the muzzle flash, the laser's start); the pistol itself stays fixed to the eye.
    /// </summary>
    private bool Behind(SPawnAttaque pawn, (FVector Location, FRotator Rotation) eye, EquipmentModel gun, in Xform root,
        int bone, out float distance)
    {
        distance = 0f;
        if (bone < 0 || pawn.XLevel?.PhysicsDriver is not PhysicsDriver physics)
        {
            return false;
        }

        // The camera's frame of the parts: metres, X forward, Y up, Z right.
        Vector3 local = (root * gun.ActorToSkeleton * gun.Global(bone, WeaponPositions, WeaponRotations, WeaponScales)).Origin;
        eye.Rotation.GetAxes(out FVector forward, out FVector right, out FVector up);
        FVector socket = eye.Location + (((forward * local.X) + (up * local.Y) + (right * local.Z)) * RenderSpace.UnitsPerMetre);
        distance = (socket - eye.Location).Size();
        return physics.Collision.LineCheck(out _, null, socket, eye.Location, s_probeExtent, CameraK.CameraTraceFlags);
    }

    /// <summary>The moves during which the eye view shows no pistol: using objects, hacking, bombs, rolls, ladders, pipes, ledges.</summary>
    private static bool HideForAction(SPlayerController pc, SPawnAttaque pawn) =>
        pc.StateNode is SPlayerController.s_InteractWithObject or SPlayerController.s_InteractWithObjectWaitingEvent or
            SPlayerController.s_Computer_Enter or SPlayerController.s_Computer_Use or SPlayerController.s_Computer_Leave or
            SPlayerController.s_Computer_FastLeave or SPlayerController.s_Hack_Enter or SPlayerController.s_Hack_Use or
            SPlayerController.s_Hack_Leave or SPlayerController.s_Hack_FastLeave or SPlayerController.s_Maintain_Enter or
            SPlayerController.s_Maintain_Use or SPlayerController.s_Maintain_Leave or SPlayerController.s_Pickup or
            SPlayerController.s_PutFlagInDropZone or SPlayerController.s_PlaceBomb or SPlayerController.s_PlaceBomb_End or
            SPlayerController.s_PlayerRolling ||
        pawn.Physics is EPhysics.PHYS_OnLadder or EPhysics.PHYS_OnPipe or EPhysics.PHYS_OnPipeH or EPhysics.PHYS_Linear or
            EPhysics.PHYS_OnVerticalPlane or EPhysics.PHYS_Ledge or EPhysics.PHYS_Vandamme;

    /// <summary>
    /// How far the draw has come, 0 lowered to 1 gripped: from the pistol's own draw clip when it plays (backward for
    /// the holster), else from the body's draw clip.
    /// </summary>
    private static float DrawAmount(SPawnAttaque pawn, ViewModelAnim? anim, float alpha)
    {
        const int channel = SWeaponAttaque.DEFAULTCHANNEL;
        if (anim is not null && anim.Sequence(channel) == ViewModelClips.DrawP190)
        {
            float duration = anim.Data(channel).Seconds(anim.Rate(channel));
            if (duration > 0f)
            {
                float remaining = anim.Remaining(channel) + ((1f - Math.Clamp(alpha, 0f, 1f)) * Sim.DeltaTime);
                float progress = Math.Clamp(1f - (remaining / duration), 0f, 1f);
                return anim.IsBackward(channel) ? 1f - progress : progress;
            }
        }

        var body = pawn.PawnAnim[0];
        Name clip = body.Sequence;
        if (!body.bPlaying || body.EndFrame <= 0f ||
            (clip != pawn.ADrawCrouch2H && clip != pawn.ADrawStand2H && clip != pawn.AUnDrawCrouch2H && clip != pawn.AUnDrawStand2H))
        {
            return 1f;
        }

        float phase = Math.Clamp(body.Frame / body.EndFrame, 0f, 1f);
        return pawn.WeaponLoaded == 4 ? 1f - phase : phase;
    }

    private void PoseWeapon(EquipmentModel model, Name clip, float phase)
    {
        if (clip == _sampledClip && phase == _sampledPhase)
        {
            return;
        }

        if (clip != _sampledClip)
        {
            _sampledName = clip.ToString();
        }

        _sampledClip = clip;
        _sampledPhase = phase;
        if (model.Character.Find(_sampledName!) is not { } sequence)
        {
            return;
        }

        model.Sample(sequence, phase, WeaponPositions, WeaponRotations, WeaponScales);
        if (_stockBone >= 0)
        {
            // Shorten the visible stock travel to keep it below the HUD.
            WeaponPositions[_stockBone] = Vector3.Lerp(_stockRest, WeaponPositions[_stockBone], .34f);
        }
    }
}

internal static class FirstPersonParts
{
    /// <summary>One model of the first-person view: where its skeleton is in the camera's frame, and its muzzle and laser socket as posed.</summary>
    public static BridgeFirstPersonPart Part(EquipmentModel model, in Xform root, ReadOnlySpan<Vector3> positions,
        ReadOnlySpan<Quaternion> rotations, ReadOnlySpan<Vector3> scales)
    {
        Xform toCamera = root * model.ActorToSkeleton;
        var part = new BridgeFirstPersonPart
        {
            Mesh = model.AssetId,
            MeshPath = model.PathId,
            BoneCount = model.Character.BoneCount,
            MuzzleBone = model.MuzzleBone,
            LaserBone = model.LaserBone,
            ModelToCamera = EquipmentMath.ToBone(toCamera),
        };
        if (model.MuzzleBone >= 0)
        {
            part.Muzzle = EquipmentMath.ToBone(toCamera * model.Global(model.MuzzleBone, positions, rotations, scales));
        }

        if (model.LaserBone >= 0)
        {
            part.Laser = EquipmentMath.ToBone(toCamera * model.Global(model.LaserBone, positions, rotations, scales));
        }

        return part;
    }
}
