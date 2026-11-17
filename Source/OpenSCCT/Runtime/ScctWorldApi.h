#pragma once

#include "CoreMinimal.h"

// The world part of the C# bridge's flat interface: the dynamic state of the level (movers and other map actors
// that move, hide or change what they show, the map's lights, laser beams, gameplay markers, skeletal props) and
// its state changes as events. These mirror Bridge/OpenScct.UnrealBridge/World/WorldTypes.cs and WorldExports.cs
// field for field; change both together and bump the version.
//
// Fetch the table once, like FScctBridgeApi:
//   Host.GetFunction(TEXT("OpenScct.UnrealBridge.WorldExports, OpenScct.UnrealBridge"), TEXT("GetApi"))
// and call it with an FScctWorldApi*; it returns ScctWorldApiVersion. Every entry works on the session FScctBridgeApi
// started and returns 0 while there is none. Call them on the game thread, after FScctBridgeApi::Update.
//
// Names (actors, meshes, skins, bones, classes, animation sequences) cross as ids into the session's name table:
// 0 is "none", an id never changes while the session runs, and new ids are only added at the end, so C++ can cache
// id -> FName / actor lookups in an array and grow it when GetNameCount grows. A map actor's name is the simulation
// actor's name: the imported level actor carries the tag "Scct:<name>" (Tools/Import/import_map.py).
//
// Locations are simulation units (1 unit = 1 Unreal cm, same axes). Rotations are Unreal rotators in degrees
// (Pitch, Yaw, Roll as FRotator takes them), -180 to 180.

// Version 2: FScctWorldLight carries the light's place and bone, GetActorSkins added.
// Version 3: the EmitterTriggered event.
constexpr int32 ScctWorldApiVersion = 3;

/** How many material slots GetActorSkins reports for an actor. */
constexpr int32 ScctWorldSkinSlots = 8;

namespace EScctWorldActorInfoFlags
{
	/** render/world.glb has a visible instance of this actor, so the imported level has an actor tagged with its name. */
	constexpr int32 InLevel = 1;
	/** An Engine.Mover or a subclass (a door, a lift, a shutter). */
	constexpr int32 Mover = 2;
	/** The render file has the instance but marks it hidden, so the importer placed nothing for it. */
	constexpr int32 HiddenInMap = 4;
	/** Spawned during play by its owner (a camera's wall mount "<camera>_Mount", a drop zone's lid "<zone>_Cover", a disk's rack "<flag>_Server"): never in the level. */
	constexpr int32 Spawned = 8;
}

/** What never changes about a followed map actor: enough to find the level's actor or, when there is none, to spawn one. */
struct FScctWorldActorInfo
{
	int32 NameId;       // the simulation actor's map name (level actor tag "Scct:" + name)
	int32 ClassId;      // the class without its package (Mover, SVitre, SCamera...)
	int32 MeshId;       // the static mesh the map gave it, as the simulation names it (Package.Group.Name); 0 none
	int32 RenderMeshId; // the mesh of its instance in world.glb (same asset plus the converter's variant suffix); 0 when it has no instance
	int32 Flags;        // EScctWorldActorInfoFlags
	float ScaleX, ScaleY, ScaleZ;          // DrawScale3D x DrawScale
	float PrePivotX, PrePivotY, PrePivotZ; // mesh pivot, mesh units: world = ((local - PrePivot) x Scale) rotated + Location
	float X, Y, Z;                         // where the map placed it (its Location, not pivot-corrected), cm
	float Pitch, Yaw, Roll;                // the map's rotation, degrees
};
static_assert(sizeof(FScctWorldActorInfo) == 68, "FScctWorldActorInfo must match BridgeWorldActorInfo");

namespace EScctWorldActorFlags
{
	/** Draw it. Clear: hide the level actor (hidden by script, destroyed, or showing no mesh). */
	constexpr int32 Visible = 1;
	/** It has left its map placement at some time (stays set): apply the transform. Clear: keep the importer's transform. */
	constexpr int32 Moved = 2;
	/** Its location or rotation changed in the last tick (a door that is opening right now). */
	constexpr int32 Moving = 4;
	/** Carried on a pawn's bone: X..Roll are relative to that bone. */
	constexpr int32 Attached = 8;
	/** The simulation actor is gone on this client (destroyed, or out of network relevancy); Visible is clear. */
	constexpr int32 Gone = 16;
}

/** One followed map actor as it should be drawn this frame. */
struct FScctWorldActor
{
	int32 NameId;
	int32 Flags;        // EScctWorldActorFlags
	int32 MeshId;       // the static mesh it shows now, as the simulation names it; 0 when it shows none (a broken pane)
	int32 AttachPawnId; // FScctBridgePawn::ActorId of the pawn carrying it, 0 when it stands in the world
	int32 AttachBoneId; // name id of the bone it hangs on, 0 when not carried
	// Standing in the world: where the Unreal actor goes, cm, the pivot already taken off (Location minus the
	// rotated PrePivot x Scale, as the importer placed it), blended between the last two ticks.
	// Carried: the attachment's RelativeLocation from the bone, simulation units (FScctWorldActorInfo's PrePivot
	// still applies to the mesh).
	float X, Y, Z;
	float Pitch, Yaw, Roll; // degrees; world rotation blended the shortest way, or RelativeRotation when carried
};
static_assert(sizeof(FScctWorldActor) == 44, "FScctWorldActor must match BridgeWorldActor");

namespace EScctWorldLightFlags
{
	constexpr int32 On = 1;
	/** An STriggerLight: events and broken devices switch it. */
	constexpr int32 Triggered = 2;
	/** A switched light in its half-second break flicker (shot out or chaffed) before it goes dark. */
	constexpr int32 Breaking = 4;
	/** The light draws its corona now (only known for Triggered lights). */
	constexpr int32 Corona = 8;
	/** It has left its placed location or rotation at some time (stays set): apply X..Roll to the level light. */
	constexpr int32 Moved = 16;
	/** It hangs on a bone of a skeletal map prop: attach it to bone AttachBoneId of the prop AttachNameId, X..Roll relative. */
	constexpr int32 OnBone = 32;
}

/** One light of render/lighting.json as it should shine and stand this frame (level light tag "Scct:" + name). */
struct FScctWorldLight
{
	int32 NameId;
	int32 Flags;     // EScctWorldLightFlags
	int32 LightType; // the original's ELightType now: 0 none, 1 steady, 2 pulse, 3 blink, 4 flicker, 5 strobe, 7 subtle pulse
	float Scale;     // multiply the light's placed intensity by this: 0 when off, else the light type's curve (1 steady) x any brightness change
	float R, G, B;   // colour at full value, linear 0..1 (lighting.json "color"); follows a changed hue or saturation
	// Where the light is, cm: the placed location until Moved is set, then the simulation actor's, blended between
	// the last two ticks (a lift's lamp, a light riding a mover). With OnBone: the RelativeLocation from the bone.
	float X, Y, Z;
	float Pitch, Yaw, Roll; // degrees (a spot shines along the forward axis); with OnBone: RelativeRotation from the bone
	int32 AttachNameId;     // with OnBone: FScctWorldAnimated::NameId of the skeletal prop it hangs on; else 0
	int32 AttachBoneId;     // with OnBone: name id of the bone; else 0
};
static_assert(sizeof(FScctWorldLight) == 60, "FScctWorldLight must match BridgeWorldLight");

namespace EScctWorldLaserFlags
{
	/** The beam shows only in the vision modes (thermal, EMF), not to the naked eye. */
	constexpr int32 HideInNormalVision = 1;
}

/** One laser beam to draw this frame. A beam that is not in the list any more is gone. */
struct FScctWorldLaser
{
	int32 Id;     // stable while the beam lives: the simulation ActorId of its tripwire, or of the mine beam
	int32 Kind;   // 0 an SLaserTrigger's tripwire, 1 a mine's beam
	int32 NameId; // the tripwire's map name, 0 for a mine beam
	int32 Flags;  // EScctWorldLaserFlags
	float StartX, StartY, StartZ; // cm
	float EndX, EndY, EndZ;       // cm
	float Heat;   // the beam's HeatIntensity (brightness in thermal vision; 1 by default)
};
static_assert(sizeof(FScctWorldLaser) == 44, "FScctWorldLaser must match BridgeWorldLaser");

namespace EScctWorldMarkerKind
{
	constexpr int32 Computer = 1;         // SComputerObjectiveTrigger, a terminal to hack. State: its SkinState
	constexpr int32 BombTarget = 2;       // SBombTargetObjectiveTrigger. State: 1 while a bomb is armed on it
	constexpr int32 Flag = 3;             // SFlag, the disk to carry off. State: 1 while away from home; OwnerPawnId its carrier
	constexpr int32 FlagDropZone = 4;     // SFlagDropZone, the case a disk is brought to
	constexpr int32 ObjectiveTrigger = 5; // any other SObjectiveTrigger
	constexpr int32 Alarm = 6;            // SAlarm. State: 1 while it sounds; TimeLeft the countdown
	constexpr int32 Camera = 7;           // SCamera. State: LightState (0 locked, 1 inactive, 2 patrol, 3 alert); OwnerPawnId the pawn it sees in alert
	constexpr int32 VolumetricSensor = 8; // SVolumetricSensor. State: bLightState (0 broken, 1 off, 2 green, 3 red)
	constexpr int32 LaserTrigger = 9;     // SLaserTrigger. State: 0 working, 1 beam switched off, 2 broken
	constexpr int32 Disk = 10;            // SDisk, a Disk Hunt dog tag (hidden in the other modes)
}

namespace EScctWorldMarkerFlags
{
	constexpr int32 Active = 1;      // the objective can be taken now (bActive)
	constexpr int32 Triggering = 2;  // someone is taking it (bTriggering)
	constexpr int32 Locked = 4;      // locked (by an alarm)
	constexpr int32 Taken = 8;       // its objective has been taken (SObjective.ObjectiveTaken)
	constexpr int32 Hidden = 16;     // the actor is hidden (not part of this game mode, or not shown yet)
	constexpr int32 Secondary = 32;  // a secondary objective
	constexpr int32 Gone = 64;       // not on this client now (out of network relevancy): the values are the last known
}

/** One gameplay object a client marks in the world, as of the last tick. */
struct FScctWorldMarker
{
	int32 NameId;      // the actor's map name
	int32 Kind;        // EScctWorldMarkerKind
	int32 State;       // the kind's own state value
	int32 Flags;       // EScctWorldMarkerFlags
	int32 OwnerPawnId; // FScctBridgePawn::ActorId of the pawn that holds it (a disk's carrier), 0 when none
	float X, Y, Z;     // where it is now, cm (a carried disk follows its carrier)
	float Progress;    // 0..1 through a running capture (a hack, a bomb's fuse) or alarm; 0 when nothing runs
	float TimeLeft;    // seconds until the capture completes or the alarm stops; 0 when nothing runs
};
static_assert(sizeof(FScctWorldMarker) == 40, "FScctWorldMarker must match BridgeWorldMarker");

namespace EScctWorldEventKind
{
	/** The actor shows another static mesh (a damage stage, a triggered mesh). A: new mesh name id (0 none), B: the one before. */
	constexpr int32 MeshChanged = 1;
	/** A material slot changed (cracked screen, warning lamp). A: the slot, B: the skin's name id (0: back to the mesh's own material).
	 *  Skins an actor already wears when it is first seen raise no event: read them with GetActorSkins. */
	constexpr int32 SkinChanged = 2;
	/** A glass pane (SVitre) broke. A: 1 when X,Y,Z is where it was hit and DX,DY,DZ the unit direction of the hit; 0 when only the pane's location is known. */
	constexpr int32 GlassBroken = 3;
	/** A switched light went on or off. A: 1 on / 0 off, B: the light type now. */
	constexpr int32 LightSwitched = 4;
	/** A switched light was broken (shot out or chaffed) and flickers for half a second before going dark. */
	constexpr int32 LightBreaking = 5;
	/** A marker's state or flags changed (the Gone flag included). A: the state now, B: the flags now. */
	constexpr int32 MarkerChanged = 6;
	/** A mover started (A 1) or stopped (A 0) moving or turning. B: its key number now. X,Y,Z: where it is. */
	constexpr int32 MoverMoving = 7;
	/** An actor was shown (A 1) or hidden (A 0) by the simulation. A spawned prop that is destroyed raises A 0, B 1 and leaves the actor list. */
	constexpr int32 ShownChanged = 8;
	/** A map Emitter was triggered: each of its sub-emitters toggles or bursts. NameId: the Emitter actor's map name. X,Y,Z: where it stands. */
	constexpr int32 EmitterTriggered = 9;
}

/** One state change of the level, in the order they happened. */
struct FScctWorldEvent
{
	int32 Kind;       // EScctWorldEventKind
	int32 NameId;     // the map name of the actor or light it happened to
	int32 A, B;       // the kind's own values
	float X, Y, Z;    // where it happened when the kind says so, else the actor's location, cm
	float DX, DY, DZ; // a unit direction when the kind says so, else 0
};
static_assert(sizeof(FScctWorldEvent) == 40, "FScctWorldEvent must match BridgeWorldEvent");

namespace EScctWorldAnimatedFlags
{
	constexpr int32 Visible = 1;
	constexpr int32 Looping = 2;
}

/** One skeletal map prop as it should be drawn this frame: an SAnimatedMesh or a skeletal fish. These are not in the imported level. */
struct FScctWorldAnimated
{
	int32 NameId;     // the actor's map name
	int32 MeshId;     // the skeletal mesh, as the simulation names it (Package.Name)
	int32 SequenceId; // the sequence it plays or holds; 0 none
	int32 Flags;      // EScctWorldAnimatedFlags
	float X, Y, Z;    // the actor's Location (not pivot-corrected), cm, blended between the last two ticks
	float Pitch, Yaw, Roll; // degrees
	float ScaleX, ScaleY, ScaleZ;
	float Frame;      // where the sequence is this frame, 0 its first frame to 1 its last; -1 when only a loop is named (fish): play it at your own pace
	float Rate;       // sequence lengths per second, negative backwards, 0 when it holds a frame
};
static_assert(sizeof(FScctWorldAnimated) == 60, "FScctWorldAnimated must match BridgeWorldAnimated");

/**
 * The table. The Get* entries that fill an array return how many items there are, which can be more than Capacity
 * (only Capacity are written; a null buffer asks for the count). GetEvents is the exception: it returns how many it
 * wrote and removes them, so call it until it returns less than Capacity.
 */
struct FScctWorldApi
{
	int32 Version;

	/** How many names the table holds, id 0 (none) included: valid ids are 0 .. count - 1. It only grows. */
	int32 (__cdecl* GetNameCount)();
	/** Copies the name of Id into Buffer (cut to fit, always terminated). Returns its full length; 0 for id 0 or an unknown id. */
	int32 (__cdecl* GetName)(int32 Id, TCHAR* Buffer, int32 Capacity);
	/** The id of a name already in the table (exact case), 0 when it is not there. */
	int32 (__cdecl* FindName)(const TCHAR* Name);

	/** The followed map actors as drawn this frame. The order is stable while no spawned prop comes or goes. */
	int32 (__cdecl* GetActors)(FScctWorldActor* Actors, int32 Capacity);
	/** The fixed facts of the followed actor named NameId. Returns 1, or 0 when it is not one. */
	int32 (__cdecl* GetActorInfo)(int32 NameId, FScctWorldActorInfo* Info);
	/** Every light of the map as it shines and stands this frame, in lighting.json's order. */
	int32 (__cdecl* GetLights)(FScctWorldLight* Lights, int32 Capacity);
	/** The laser beams to draw this frame. */
	int32 (__cdecl* GetLasers)(FScctWorldLaser* Lasers, int32 Capacity);
	/** The gameplay markers as of the last tick. */
	int32 (__cdecl* GetMarkers)(FScctWorldMarker* Markers, int32 Capacity);
	/** The skeletal map props as drawn this frame. */
	int32 (__cdecl* GetAnimated)(FScctWorldAnimated* Animated, int32 Capacity);
	/** Moves the oldest waiting events into Events; returns how many were written. Each event is handed out once. */
	int32 (__cdecl* GetEvents)(FScctWorldEvent* Events, int32 Capacity);
	/**
	 * The skins the followed actor NameId wears now: one name id per material slot (the original's Actor.Skins:
	 * Skins[i] replaces the material of the mesh's section i), 0 for the mesh's own material. Returns the slot count
	 * (ScctWorldSkinSlots; only Capacity are written), 0 when NameId is not a followed actor.
	 */
	int32 (__cdecl* GetActorSkins)(int32 NameId, int32* Skins, int32 Capacity);
};

namespace FScctWorld
{
	/** The rotator of a world struct's Pitch, Yaw, Roll (the same conversion the importer used for placed actors). */
	inline FRotator ToRotator(float Pitch, float Yaw, float Roll)
	{
		return FRotator(Pitch, Yaw, Roll);
	}

	/** The level tag of a simulation actor or light name. */
	inline FName LevelTag(const TCHAR* Name)
	{
		return FName(*(FString(TEXT("Scct:")) + Name));
	}
}
