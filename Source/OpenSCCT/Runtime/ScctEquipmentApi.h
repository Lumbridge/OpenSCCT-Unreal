#pragma once

#include "CoreMinimal.h"
#include "ScctBridgeApi.h"

// The equipment interface of the C# bridge: what pawns hold and wear, what hangs on their bones, and the local
// player's first-person view model. These mirror Bridge/OpenScct.UnrealBridge/Equipment/EquipmentTypes.cs and
// EquipmentExports.cs field for field; change both together and bump the version.
//
// Fetch the table once, like the main one:
//   Host.GetFunction(TEXT("OpenScct.UnrealBridge.EquipmentExports, OpenScct.UnrealBridge"), TEXT("GetApi"))
// which is  int32 (CORECLR_DELEGATE_CALLTYPE*)(FScctEquipmentApi*)  and returns the version.
//
// Each frame, after FScctBridgeApi::Update: GetEquipment, GetAttachments (then GetAttachmentPose for the entries
// flagged Animated), GetFirstPerson (then GetFirstPersonPose for each part). They describe the same drawn moment
// as GetPawns and GetPawnPose. Call them once a frame, on the game thread.
//
// Names (classes, meshes, bones, skins, file paths) cross as int32 ids. GetName gives the text of an id; an id
// keeps its text for the life of the process, so cache what you resolve. Id 0 is "no name".
//
// Every field is 4 bytes wide (int32 or float), so the structs have no padding.

constexpr int32 ScctEquipmentApiVersion = 1;

namespace EScctEquipmentFlags
{
	constexpr int32 Firing = 1 << 0;           // a shot in the last 0.1 s, or the local rifle is in its firing state
	constexpr int32 Reloading = 1 << 1;        // the local weapon's reload state, or the body's reload clip for others
	constexpr int32 LaserOn = 1 << 2;          // the laser is switched on; draw one only with LaserBeam or LaserDot
	constexpr int32 TorchOn = 1 << 3;          // the merc's torch is lit (and he lives)
	constexpr int32 InScope = 1 << 4;          // the local merc looks through the scope (no view model is drawn then)
	constexpr int32 GogglesGlow = 1 << 5;      // the lit goggle lenses are shown on this machine (attachment slot 2)
	constexpr int32 Wet = 1 << 6;              // bWet: the original swaps to the wet skins in the vision modes
	constexpr int32 StickySkinned = 1 << 7;    // shocked by the sticky shocker: the original draws the electric skin
	constexpr int32 WeaponInHand = 1 << 8;     // the weapon is in a hand, else at the holster bone
	constexpr int32 FirstPerson = 1 << 9;      // the local pawn seen from its own eyes: its body and attachments leave the
	                                           // main view and only cast shadows, with or without a view model
	                                           // (GetFirstPerson draws none in the scope or with the spy's pistol away)
	constexpr int32 TorchCorona = 1 << 10;     // the torch shows its corona (everyone but its owner sees it)
	constexpr int32 TorchFollowsBone = 1 << 11; // point the torch along its bone's X axis, not along Torch pitch/yaw
	constexpr int32 LaserBeam = 1 << 12;       // draw the beam from LaserStart to LaserEnd (the local player's as the
	                                           // simulation placed it; another merc's from his rifle's FX bone along
	                                           // his aim to the first thing a line hits)
	constexpr int32 LaserDot = 1 << 13;        // LaserEnd holds the laser dot on the surface it hit (another spy's pistol)
	constexpr int32 GogglesDown = 1 << 14;     // the goggles are down over the eyes
	constexpr int32 Merc = 1 << 15;            // SPawnDefense, else a spy
	constexpr int32 Binoculars = 1 << 16;      // the spy looks through his binoculars (local player only; FScctBridgeView::Fov
	                                           // carries the zoom)
	constexpr int32 TorchInCamera = 1 << 17;   // the merc looks through a network camera and his torch shines from it:
	                                           // place the light at Torch X/Y/Z and pitch/yaw as given, not on his bone
}

/** What one pawn holds and wears this frame. Simulation units are Unreal centimetres, same axes. */
struct FScctEquipment
{
	int32 ActorId;
	int32 Flags;               // EScctEquipmentFlags
	int32 WeaponClass;         // name id: SWeaponDefense, SWeaponDefense_Uzi, SStickyShocker...; for another player's
	                           // merc only the third-person dummy is known (SWeaponDefenseDummy...). 0: no weapon here
	int32 WeaponLoaded;        // SBasePawn.WeaponLoaded. Merc: >0 rifle in hand, 0 at the holster. Spy: 0 holstered,
	                           // 1 quick-drawn, 2 aimed, 3 drawing, 4 holstering, 7 in the left hand
	int32 ActiveWeaponSlot;    // 0 primary, 1 sidearm (OpenSCCT's own pistols)
	int32 ShotCount;           // +1 for every shot since the bridge first saw the pawn: on a change play the muzzle effects
	int32 ClipAmmo;            // rounds in the clip, -1 when this machine does not know (another player's weapon)
	int32 Ammo;                // rounds in total, -1 when not known
	int32 RateOfFire;          // 0 single, 1 burst, 2 auto; -1 when not known
	int32 SelectedGadgetClass; // name id: SGadFragGrenade, SGadStickyCam...; 0 none or not known
	int32 GoggleMode;          // SGoggleBeam.CurrentMode: 0 none, 1 thermal, 2 night, 3 motion tracking, 4 EMF, 5 normal
	int32 TorchBone;           // index of the torch's bone ("Laser") in the pawn's skeleton, -1 when the pawn has no torch
	float ScopeFov;            // degrees (30, 20, 10) while InScope, else 0
	float Cloak;               // the camo suit, 0 off to 1 fully on (ramped and flickered by the simulation)
	float TorchX, TorchY, TorchZ;              // cm: the merc's eye point. The original hangs the light on TorchBone
	float TorchPitch, TorchYaw, TorchRoll;     // degrees: along the merc's aim
	float TorchRange;                          // cm the light reaches (1500)
	float TorchSpotWidth, TorchSpotHeight;     // cm: half the lit width and height at TorchRange (400)
	float TorchHalfAngle;                      // degrees: atan(SpotWidth / Range), about 14.9 (outer cone half angle)
	float TorchR, TorchG, TorchB;              // linear colour as the original computes it, about 0 to 1
	float TorchBrightness;                     // LightBrightness as the class sets it (304)
	float LaserStartX, LaserStartY, LaserStartZ; // cm, valid with LaserBeam
	float LaserEndX, LaserEndY, LaserEndZ;       // cm, valid with LaserBeam or LaserDot: what the laser hit
};
static_assert(sizeof(FScctEquipment) == 136, "FScctEquipment must match BridgeEquipment");

namespace EScctAttachmentSlot
{
	constexpr int32 Weapon = 0;     // the weapon in use: in a hand, or at the holster while put away
	constexpr int32 Carried = 1;    // the other weapon a merc carries at the holster
	constexpr int32 GoggleGlow = 2; // the lit goggle lenses
}

namespace EScctAttachmentFlags
{
	constexpr int32 Animated = 1;          // GetAttachmentPose gives its own bone pose (the spy pistol); else rest pose
	constexpr int32 OnFirstPersonPawn = 2; // its pawn has EScctEquipmentFlags::FirstPerson: main view off, shadows only
}

/** One thing hanging on a pawn's bone this frame. (ActorId, Slot) names it from frame to frame. */
struct FScctAttachment
{
	int32 ActorId;   // the pawn it hangs on
	int32 Slot;      // EScctAttachmentSlot
	int32 Mesh;      // name id of the mesh asset as the simulation names it ("SWeaponFirst.M160")
	int32 MeshPath;  // name id of the file relative to the content root ("shared/chars/SWeaponFirst/M160.glb"); 0: no file
	int32 Class;     // name id of the attached actor's class (SWeaponDefenseDummy, SStickyShocker, SGoggleBeam_DEF...)
	int32 Bone;      // name id of the pawn bone ("B R Hand", "B L Hand", "HOLSTER", "B Head")
	int32 BoneIndex; // that bone in the pawn's skeleton (GetSkeleton order), -1 when the skeleton lacks it
	int32 Skin;      // name id of the material over the first slot (the goggle glow's colour), 0: the mesh's own
	int32 Flags;     // EScctAttachmentFlags
	int32 BoneCount; // bones in the attachment's own skeleton, 0 when the file is missing

	// The original's offset: RelativeLocation (cm) and RelativeRotation (degrees) in the bone's own UE axes.
	float RelX, RelY, RelZ;
	float RelPitch, RelYaw, RelRoll;

	// The same placement ready to use: the attachment's skeleton relative to the pawn bone, written like a pose
	// bone (the character files' space). Attach the weapon's component to the pawn bone with
	// FScctPose::ToUnreal(ModelToBone) as the relative transform.
	FScctBridgeBone ModelToBone;
};
static_assert(sizeof(FScctAttachment) == 104, "FScctAttachment must match BridgeAttachment");

namespace EScctFirstPersonKind
{
	constexpr int32 None = 0;
	constexpr int32 Merc = 1; // the merc's arms and gun: one model
	constexpr int32 Spy = 2;  // the spy's eye view: part 0 his arms, part 1 the pistol
}

/** One model of the first-person view. */
struct FScctFirstPersonPart
{
	int32 Mesh;       // name id of the mesh asset ("SWeaponFirst.1DEF_01", "SPerso.ATT_01_fp", "SWeaponFirst.p190_fp")
	int32 MeshPath;   // name id of the file relative to the content root
	int32 BoneCount;  // bones in its skeleton (GetMeshSkeleton, GetFirstPersonPose)
	int32 MuzzleBone; // index of the muzzle bone "FX", -1 when the model has none
	int32 LaserBone;  // index of the laser socket "Laser" (the spy's pistol), -1 when the model has none

	// The model's skeleton relative to the camera, written like a pose bone in the camera's own frame (metres, X
	// forward, Y up, Z right). FScctPose::ToUnreal gives it in Unreal's camera frame (X forward, Y right, Z up, cm):
	// component world transform = FScctPose::ToUnreal(ModelToCamera) * camera world transform.
	FScctBridgeBone ModelToCamera;
	FScctBridgeBone Muzzle; // the muzzle bone as posed this frame, relative to the camera the same way; X is the barrel
	FScctBridgeBone Laser;  // the laser socket as posed this frame, relative to the camera the same way
};
static_assert(sizeof(FScctFirstPersonPart) == 140, "FScctFirstPersonPart must match BridgeFirstPersonPart");

/** The local player's first-person view model this frame. */
struct FScctFirstPerson
{
	int32 Kind;           // EScctFirstPersonKind; None when nothing is drawn
	int32 ActorId;        // the pawn seen through
	int32 PartCount;      // 1 for the merc, 2 for the spy
	int32 WeaponClass;    // name id of the weapon's class
	int32 RateOfFireSkin; // name id of the material over the rifle's rate-of-fire screen (material slot 3 of the view
	                      // model: SWeaponTex.Armes.SI_rof_single / _burst / _auto); 0 when the weapon sets none
	int32 ShotCount;      // the same count as the pawn's FScctEquipment::ShotCount
	float Cloak;          // the camo suit on the arms and gun, 0 to 1
	FScctFirstPersonPart Parts[2];
};
static_assert(sizeof(FScctFirstPerson) == 308, "FScctFirstPerson must match BridgeFirstPerson");

namespace EScctEquipmentRole
{
	constexpr int32 MercFirstPerson = 1;      // arms and gun in one skeleton, with the First* clips
	constexpr int32 MercWeapon = 2;           // a merc gun as others see it, hung on "B R Hand" or "HOLSTER"
	constexpr int32 SpyWeapon = 3;            // the spy's pistol as others see it (DrawP190, SnipeP190, WaitP190)
	constexpr int32 SpyFirstPersonArms = 4;   // the spy's arms for his eye view
	constexpr int32 SpyFirstPersonWeapon = 5; // the spy's pistol for his eye view (has the "Laser" socket)
	constexpr int32 GoggleGlow = 6;           // the lit goggle lenses
	constexpr int32 Other = 7;
}

/** One weapon or gadget mesh file of the content (shared/chars). */
struct FScctEquipmentMesh
{
	int32 Mesh;       // name id of the mesh asset ("SWeaponFirst.M160")
	int32 MeshPath;   // name id of the file relative to the content root
	int32 Role;       // EScctEquipmentRole
	int32 Class;      // name id of the simulation class that uses it by default, 0 when none does
	int32 BoneCount;
	int32 ClipCount;
	int32 MuzzleBone; // "FX", -1 when absent
	int32 CasingBone; // "FX_DOUILLE" (casing ejection), -1 when absent
	int32 LaserBone;  // "Laser", -1 when absent
	float RotOriginPitch, RotOriginYaw, RotOriginRoll; // degrees: how the mesh is turned inside its actor
	float ScaleX, ScaleY, ScaleZ;                      // the mesh's scale along the actor's UE axes
};
static_assert(sizeof(FScctEquipmentMesh) == 60, "FScctEquipmentMesh must match BridgeEquipmentMesh");

struct FScctEquipmentApi
{
	int32 Version;

	/** Copies the text of a name id (UTF-16, zero terminated, cut to fit). Returns its full length; 0 for id 0. */
	int32 (__cdecl* GetName)(int32 Id, TCHAR* Buffer, int32 Capacity);

	/** Writes what every pawn holds and wears this frame. Returns the count written. */
	int32 (__cdecl* GetEquipment)(FScctEquipment* Equipment, int32 Capacity);

	/** Writes everything hanging on the pawns' bones this frame. Returns the count written. */
	int32 (__cdecl* GetAttachments)(FScctAttachment* Attachments, int32 Capacity);

	/**
	 * Writes the own bone pose of an attachment flagged Animated by this frame's GetAttachments: each bone relative
	 * to its parent, in GetMeshSkeleton's order. Returns the bone count (only Capacity are written), 0 when it is
	 * not animated.
	 */
	int32 (__cdecl* GetAttachmentPose)(int32 ActorId, int32 Slot, FScctBridgeBone* Bones, int32 Capacity);

	/** Writes the first-person view model of this frame. Returns 1 when one is drawn, else 0 (View is cleared). */
	int32 (__cdecl* GetFirstPerson)(FScctFirstPerson* View);

	/**
	 * Writes the pose of a part of this frame's first-person view: each bone relative to its parent, in
	 * GetMeshSkeleton's order. Returns the bone count (only Capacity are written), 0 when there is no such part.
	 */
	int32 (__cdecl* GetFirstPersonPose)(int32 Part, FScctBridgeBone* Bones, int32 Capacity);

	/**
	 * Writes the weapon and gadget mesh files of the content. Returns the entry count (only Capacity are written).
	 * Works before a session starts, once the content is configured. Files the bridge cannot read are left out
	 * (today the first-person pistols OpenScctSidearms.*_first: no view model is drawn for a merc holding one).
	 */
	int32 (__cdecl* GetCatalog)(FScctEquipmentMesh* Meshes, int32 Capacity);

	/**
	 * Writes the skeleton of an equipment mesh by the name id of its asset, in the order its poses are written.
	 * Returns the bone count (only Capacity are written), 0 when the content has no such mesh. Works before a
	 * session starts, once the content is configured.
	 */
	int32 (__cdecl* GetMeshSkeleton)(int32 Mesh, FScctBridgeBoneInfo* Bones, int32 Capacity);
};

namespace FScctEquipmentNames
{
	/** The text of a name id (an empty string for id 0). Cache the result per id: ids never change their text. */
	inline FString Get(const FScctEquipmentApi& Api, int32 Id)
	{
		if (Id <= 0 || Api.GetName == nullptr)
		{
			return FString();
		}

		TCHAR Buffer[512];
		Api.GetName(Id, Buffer, UE_ARRAY_COUNT(Buffer));
		return FString(Buffer);
	}
}
