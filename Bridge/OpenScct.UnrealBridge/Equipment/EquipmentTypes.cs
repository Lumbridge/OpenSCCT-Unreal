using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

// The equipment structs cross to C++ as they are. Source/OpenSCCT/Runtime/ScctEquipmentApi.h mirrors them field for
// field; change both together and bump EquipmentExports.ApiVersion. Every field is four bytes wide (int or float),
// so there is no padding to keep in step.
//
// Names (classes, meshes, bones, skins, file paths) cross as ids: EquipmentApi.GetName gives the text of an id, and
// an id keeps its text for the life of the process. Id 0 is "no name".

/// <summary>What one pawn holds and wears this frame (<c>GetEquipment</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeEquipment
{
    public int ActorId;

    /// <summary><see cref="BridgeEquipmentFlags"/>.</summary>
    public int Flags;

    /// <summary>
    /// Name id of the class of the weapon in use (SWeaponDefense, SWeaponDefense_Uzi, SStickyShocker...). For a merc
    /// other than the local player only the third-person dummy replicates, so this is the dummy's class
    /// (SWeaponDefenseDummy...). 0 when the pawn has no weapon on this machine.
    /// </summary>
    public int WeaponClass;

    /// <summary>
    /// SBasePawn.WeaponLoaded as the simulation keeps it. Merc: above 0 the rifle is in hand, 0 it hangs at the
    /// holster. Spy: 0 holstered, 1 quick-drawn, 2 aimed, 3 drawing, 4 holstering, 7 in the other (left) hand.
    /// </summary>
    public int WeaponLoaded;

    /// <summary>0 the primary weapon, 1 the sidearm (OpenSCCT's own pistols, when the host allows them).</summary>
    public int ActiveWeaponSlot;

    /// <summary>
    /// Counts up by one for every shot this pawn fired since the bridge first saw it (from the replicated muzzle
    /// counts; the local merc's from his own weapon). A change means: play the muzzle effects.
    /// </summary>
    public int ShotCount;

    /// <summary>Rounds in the clip and in total, or -1 when this machine does not know (another player's weapon).</summary>
    public int ClipAmmo, Ammo;

    /// <summary>The rifle's SRateOfFireMode (0 single, 1 burst, 2 auto), or -1 when not known.</summary>
    public int RateOfFire;

    /// <summary>Name id of the class of the selected gadget (SGadStickyCam, SGadMultiMinesLauncher...), 0 for none or unknown.</summary>
    public int SelectedGadgetClass;

    /// <summary>
    /// The goggle glow's mode (SGoggleBeam.CurrentMode): 0 none, 1 thermal, 2 night, 3 motion tracking, 4 EMF,
    /// 5 normal. The lens colour other players see follows it; the glow itself is an attachment (slot 2).
    /// </summary>
    public int GoggleMode;

    /// <summary>Index of the bone the torch sits on in the pawn's skeleton ("Laser" on the merc), or -1.</summary>
    public int TorchBone;

    /// <summary>The scope's field of view in degrees (30, 20 or 10) while <see cref="BridgeEquipmentFlags.InScope"/>, else 0.</summary>
    public float ScopeFov;

    /// <summary>The camo suit: Actor.GlassEffect / 255, 0 off to 1 fully on (already ramped and flickered by the simulation).</summary>
    public float Cloak;

    /// <summary>
    /// The torch light as the simulation placed it (unreal units): at the merc's eye point, pointing along his aim.
    /// The original hangs it on the "Laser" bone of the drawn skeleton (<see cref="TorchBone"/>) and, with
    /// <see cref="BridgeEquipmentFlags.TorchFollowsBone"/>, points it along that bone's X axis instead.
    /// </summary>
    public float TorchX, TorchY, TorchZ;

    /// <summary>Degrees.</summary>
    public float TorchPitch, TorchYaw, TorchRoll;

    /// <summary>EchelonRange: how far the light reaches, unreal units (1500).</summary>
    public float TorchRange;

    /// <summary>SpotWidth and SpotHeight: half the lit width and height at <see cref="TorchRange"/>, unreal units (400).</summary>
    public float TorchSpotWidth, TorchSpotHeight;

    /// <summary>Half the cone's angle in degrees: atan(SpotWidth / EchelonRange), about 14.9.</summary>
    public float TorchHalfAngle;

    /// <summary>
    /// The light's colour as the original computes it for a spot shadow light: FGetHSV(LightHue, LightSaturation) x
    /// LightBrightness / 510, capped (ActorLightRules.SpotShadowColour). Linear, about 0 to 1.
    /// </summary>
    public float TorchR, TorchG, TorchB;

    /// <summary>LightBrightness as the class sets it (304).</summary>
    public float TorchBrightness;

    /// <summary>
    /// The laser beam (unreal units) while <see cref="BridgeEquipmentFlags.LaserBeam"/>: from the gun to what it
    /// hits. With only <see cref="BridgeEquipmentFlags.LaserDot"/> the end is the dot's place on the surface it hit
    /// and the start is not set.
    /// </summary>
    public float LaserStartX, LaserStartY, LaserStartZ;

    public float LaserEndX, LaserEndY, LaserEndZ;
}

public static class BridgeEquipmentFlags
{
    /// <summary>A shot in the last tenth of a second, or the local rifle is in its firing state.</summary>
    public const int Firing = 1 << 0;

    /// <summary>The weapon is being reloaded (the local weapon's state, or the body's reload clip for others).</summary>
    public const int Reloading = 1 << 1;

    /// <summary>
    /// The laser sight is switched on (the merc's SGadTorcheLaser, the spy pistol's bLaserOn). A beam or dot is drawn
    /// only with <see cref="LaserBeam"/> or <see cref="LaserDot"/>: a merc's shows only with the rifle up.
    /// </summary>
    public const int LaserOn = 1 << 2;

    /// <summary>The merc's torch is lit (its LightType is steady and he lives).</summary>
    public const int TorchOn = 1 << 3;

    /// <summary>The local merc looks through the scope (s_Binoculars_Def).</summary>
    public const int InScope = 1 << 4;

    /// <summary>The goggle glow is shown on this machine (the beam actor exists and is not hidden).</summary>
    public const int GogglesGlow = 1 << 5;

    /// <summary>bWet: the pawn stood in rain or water; the original swaps to the wet skins in the vision modes.</summary>
    public const int Wet = 1 << 6;

    /// <summary>bStickySkinned: shocked by the sticky shocker; the original draws the electric skin.</summary>
    public const int StickySkinned = 1 << 7;

    /// <summary>The weapon is in a hand (else at the holster bone).</summary>
    public const int WeaponInHand = 1 << 8;

    /// <summary>
    /// The local pawn seen from its own eyes this frame (the merc's view, the spy's eye view): its body and what
    /// hangs on it leave the main view and only cast shadows, whether or not a view model is drawn
    /// (<c>GetFirstPerson</c> draws none in the scope or with the spy's pistol put away).
    /// </summary>
    public const int FirstPerson = 1 << 9;

    /// <summary>The torch shows its corona (to everyone but its owner).</summary>
    public const int TorchCorona = 1 << 10;

    /// <summary>The torch points along its bone, not along the Torch rotation (IK_Type is IKT_NONE).</summary>
    public const int TorchFollowsBone = 1 << 11;

    /// <summary>
    /// Draw the beam from LaserStart to LaserEnd: the local player's traced for the view drawn this frame (as the
    /// simulation placed it at its last tick only where there is no such view: a network camera, a pistol being
    /// drawn or put away), another merc's from the FX bone of the rifle in his hands along his aim, between the last
    /// two ticks, to the first thing a line hits.
    /// </summary>
    public const int LaserBeam = 1 << 12;

    /// <summary>LaserEnd holds the laser dot on the surface it hit (another spy's pistol), with no beam on this machine.</summary>
    public const int LaserDot = 1 << 13;

    /// <summary>The goggles are down over the eyes (bGogglesUp is false).</summary>
    public const int GogglesDown = 1 << 14;

    /// <summary>The pawn is a merc (SPawnDefense), else a spy.</summary>
    public const int Merc = 1 << 15;

    /// <summary>bBinocularsOn: the spy looks through his binoculars (known for the local player only; the view's Fov carries the zoom).</summary>
    public const int Binoculars = 1 << 16;

    /// <summary>
    /// The merc looks through a network camera and his torch shines from it: place the light at the Torch place and
    /// rotation as given, not on his bone.
    /// </summary>
    public const int TorchInCamera = 1 << 17;

    /// <summary>The equipped primary has the M4's rail laser, including while the weapon is stowed.</summary>
    public const int RailLaser = 1 << 18;
}

/// <summary>
/// One thing hanging on a pawn's bone this frame (<c>GetAttachments</c>): the weapon in a hand or at the holster,
/// a second carried weapon, the goggle glow.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeAttachment
{
    /// <summary>The pawn it hangs on.</summary>
    public int ActorId;

    /// <summary><see cref="BridgeAttachmentSlot"/>.</summary>
    public int Slot;

    /// <summary>Name id of the mesh asset as the simulation names it ("SWeaponFirst.M160").</summary>
    public int Mesh;

    /// <summary>Name id of the mesh file, relative to the content root with forward slashes; 0 when the content has no such file.</summary>
    public int MeshPath;

    /// <summary>Name id of the class of the attached actor (SWeaponDefenseDummy, SStickyShocker, SGoggleBeam...).</summary>
    public int Class;

    /// <summary>Name id of the pawn bone it hangs on ("B R Hand", "B L Hand", "HOLSTER", the goggle beam's bone).</summary>
    public int Bone;

    /// <summary>Index of that bone in the pawn's skeleton (<c>GetSkeleton</c> order), or -1 when the skeleton lacks it.</summary>
    public int BoneIndex;

    /// <summary>Name id of the skin over the mesh's first material slot (the goggle glow's colour), 0 for the mesh's own.</summary>
    public int Skin;

    /// <summary><see cref="BridgeAttachmentFlags"/>.</summary>
    public int Flags;

    /// <summary>Bones in the attachment's own skeleton (<c>GetMeshSkeleton</c>), 0 when the file is missing.</summary>
    public int BoneCount;

    /// <summary>
    /// The original's attachment offset: RelativeLocation in unreal units along the bone's own axes (X along the
    /// bone, as UE's GetBoneCoords gives them) and RelativeRotation in degrees. Zero for everything but the spy's
    /// pistol in his left hand (2, -8, 2, roll 180) and the goggle glow.
    /// </summary>
    public float RelX, RelY, RelZ;

    public float RelPitch, RelYaw, RelRoll;

    /// <summary>
    /// The same placement ready to use: the attachment's skeleton relative to the pawn bone, in the character
    /// files' space like a pose bone (metres, Y up; FScctPose::ToUnreal converts it). It holds the offset above,
    /// the mesh's RotOrigin and scale, and takes the pawn mesh's own scale back out: the attachment's skeleton =
    /// the pawn bone's transform in the pawn's skeleton x this.
    /// </summary>
    public BridgeBone ModelToBone;
}

public static class BridgeAttachmentSlot
{
    /// <summary>The weapon in use: in a hand, or at the holster while put away.</summary>
    public const int Weapon = 0;

    /// <summary>The other weapon a merc carries at the holster (primary and sidearm).</summary>
    public const int Carried = 1;

    /// <summary>The lit goggle lenses (SGoggleBeam).</summary>
    public const int GoggleGlow = 2;
}

public static class BridgeAttachmentFlags
{
    /// <summary><c>GetAttachmentPose</c> gives the attachment's own bone pose (the spy pistol's slide and stock); else it stays in its rest pose.</summary>
    public const int Animated = 1;

    /// <summary>Its pawn is seen from its own eyes (<see cref="BridgeEquipmentFlags.FirstPerson"/>): it leaves the main view with the body and only casts shadows.</summary>
    public const int OnFirstPersonPawn = 2;
}

/// <summary>One model of the first-person view (<c>GetFirstPerson</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeFirstPersonPart
{
    /// <summary>Name id of the mesh asset ("SWeaponFirst.1DEF_01", "SPerso.ATT_01_fp", "SWeaponFirst.p190_fp").</summary>
    public int Mesh;

    /// <summary>Name id of the mesh file, relative to the content root with forward slashes.</summary>
    public int MeshPath;

    /// <summary>Bones in its skeleton (<c>GetMeshSkeleton</c>, <c>GetFirstPersonPose</c>).</summary>
    public int BoneCount;

    /// <summary>Index of the muzzle bone ("FX"), or -1.</summary>
    public int MuzzleBone;

    /// <summary>Index of the laser socket ("Laser", the spy's pistol only), or -1.</summary>
    public int LaserBone;

    /// <summary>
    /// The model's skeleton relative to the camera, as a transform in the camera's own frame written like a pose
    /// bone: metres, X forward, Y up, Z right. FScctPose::ToUnreal turns it into Unreal's camera frame (X forward,
    /// Y right, Z up, centimetres). It holds the mesh's RotOrigin and scale and the eye-view placement.
    /// </summary>
    public BridgeBone ModelToCamera;

    /// <summary>The muzzle bone as posed this frame, relative to the camera in the same frame (X is the barrel's direction).</summary>
    public BridgeBone Muzzle;

    /// <summary>The laser socket as posed this frame, relative to the camera in the same frame.</summary>
    public BridgeBone Laser;
}

/// <summary>The local player's first-person view model this frame (<c>GetFirstPerson</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeFirstPerson
{
    /// <summary><see cref="BridgeFirstPersonKind"/>; 0 when nothing is drawn.</summary>
    public int Kind;

    /// <summary>The pawn seen through.</summary>
    public int ActorId;

    /// <summary>Parts filled: 1 for the merc (arms and gun are one model), 2 for the spy (0 the arms, 1 the pistol).</summary>
    public int PartCount;

    /// <summary>Name id of the weapon's class.</summary>
    public int WeaponClass;

    /// <summary>
    /// Name id of the material over the gun's rate-of-fire screen (material slot 3 of the rifle's view model:
    /// SWeaponTex.Armes.SI_rof_single, _burst or _auto), 0 when the weapon sets none.
    /// </summary>
    public int RateOfFireSkin;

    /// <summary>The same count as the pawn's <see cref="BridgeEquipment.ShotCount"/>.</summary>
    public int ShotCount;

    /// <summary>The camo suit on the arms and gun, 0 to 1.</summary>
    public float Cloak;

    public BridgeFirstPersonPart Part0, Part1;
}

public static class BridgeFirstPersonKind
{
    public const int None = 0;

    /// <summary>The merc's arms and gun: one model posed from the weapon's view model channels.</summary>
    public const int Merc = 1;

    /// <summary>The spy's eye view (OpenSCCT's own): his arms and the pistol, two models.</summary>
    public const int Spy = 2;
}

/// <summary>One weapon or gadget mesh file of the content (<c>GetCatalog</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeEquipmentMesh
{
    /// <summary>Name id of the mesh asset ("SWeaponFirst.M160").</summary>
    public int Mesh;

    /// <summary>Name id of the file, relative to the content root with forward slashes.</summary>
    public int MeshPath;

    /// <summary><see cref="BridgeEquipmentRole"/>.</summary>
    public int Role;

    /// <summary>Name id of the simulation class that uses it (SWeaponDefense, SWeaponDefenseDummy, SStickyShocker...), 0 when none does by default.</summary>
    public int Class;

    /// <summary>Bones in its skeleton, clips in the file.</summary>
    public int BoneCount, ClipCount;

    /// <summary>Index of the "FX" bone (muzzle), "FX_DOUILLE" (casing ejection) and "Laser" socket, each -1 when absent.</summary>
    public int MuzzleBone, CasingBone, LaserBone;

    /// <summary>The mesh's RotOrigin in degrees: how the mesh is turned inside its actor.</summary>
    public float RotOriginPitch, RotOriginYaw, RotOriginRoll;

    /// <summary>The mesh's scale along the actor's UE axes.</summary>
    public float ScaleX, ScaleY, ScaleZ;
}

public static class BridgeEquipmentRole
{
    /// <summary>A merc first-person view model: arms and gun in one skeleton, with the First* clips.</summary>
    public const int MercFirstPerson = 1;

    /// <summary>A merc gun as other players see it, hung on "B R Hand" or "HOLSTER".</summary>
    public const int MercWeapon = 2;

    /// <summary>The spy's pistol as others see it, with its own clips (DrawP190, SnipeP190, WaitP190).</summary>
    public const int SpyWeapon = 3;

    /// <summary>The spy's arms for his eye view.</summary>
    public const int SpyFirstPersonArms = 4;

    /// <summary>The spy's pistol for his eye view (with the "Laser" socket).</summary>
    public const int SpyFirstPersonWeapon = 5;

    /// <summary>The lit goggle lenses.</summary>
    public const int GoggleGlow = 6;

    /// <summary>Anything else found beside them (a loader, shared arms).</summary>
    public const int Other = 7;
}
