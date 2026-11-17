using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

// The dynamic state of the level as it crosses to C++. Source/OpenSCCT/Runtime/ScctWorldApi.h mirrors these structs
// field for field; change both together and bump WorldExports.ApiVersion.
//
// Names (actors, meshes, skins, bones, classes, sequences) cross as ids into the session's name table
// (WorldApi.GetName): 0 is "none", an id never changes while the session runs, and new ids are only added at the end.
// Locations are simulation units (1 unit = 1 Unreal cm, same axes); rotations are Unreal rotators in degrees.

/// <summary>
/// What never changes about a followed map actor (<c>GetActorInfo</c>): enough for C++ to find the level's actor
/// (tag "Scct:" + name) or, when the level has none, to spawn one.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeWorldActorInfo
{
    /// <summary>The simulation actor's map name (the level actor's tag is "Scct:" + this).</summary>
    public int NameId;

    /// <summary>The actor's class without its package (Mover, SVitre, SCamera...).</summary>
    public int ClassId;

    /// <summary>The static mesh the map gave the actor, as the simulation names it (Package.Group.Name); 0 when none.</summary>
    public int MeshId;

    /// <summary>
    /// The mesh of the actor's instance in render/world.glb (the same asset with the converter's variant suffix, the
    /// name the importer made the static mesh asset from); 0 when the render file has no instance of this actor.
    /// </summary>
    public int RenderMeshId;

    /// <summary><see cref="BridgeWorldActorInfoFlags"/>.</summary>
    public int Flags;

    /// <summary>DrawScale3D x DrawScale.</summary>
    public float ScaleX, ScaleY, ScaleZ;

    /// <summary>The mesh's pivot in its own units: a point of the mesh is ((local - PrePivot) x Scale) rotated + Location.</summary>
    public float PrePivotX, PrePivotY, PrePivotZ;

    /// <summary>Where the map placed the actor (its Location, not pivot-corrected).</summary>
    public float X, Y, Z;

    /// <summary>The map's rotation, degrees.</summary>
    public float Pitch, Yaw, Roll;
}

public static class BridgeWorldActorInfoFlags
{
    /// <summary>render/world.glb has a visible instance of this actor, so the imported level has an actor tagged with its name.</summary>
    public const int InLevel = 1;

    /// <summary>An Engine.Mover or a subclass (a door, a lift, a shutter).</summary>
    public const int Mover = 2;

    /// <summary>The render file has the instance but marks it hidden, so the importer placed nothing for it.</summary>
    public const int HiddenInMap = 4;

    /// <summary>Spawned during play by its owner (a camera's wall mount, a drop zone's lid, a disk's rack): never in the level.</summary>
    public const int Spawned = 8;
}

/// <summary>One followed map actor as it should be drawn this frame (<c>GetActors</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeWorldActor
{
    public int NameId;

    /// <summary><see cref="BridgeWorldActorFlags"/>.</summary>
    public int Flags;

    /// <summary>The static mesh the actor shows now, as the simulation names it; 0 when it shows none (a broken pane).</summary>
    public int MeshId;

    /// <summary>The pawn (BridgePawn.ActorId) that carries the actor on a bone, 0 when it stands in the world.</summary>
    public int AttachPawnId;

    /// <summary>The bone it hangs on (name id), 0 when not carried.</summary>
    public int AttachBoneId;

    /// <summary>
    /// Standing in the world: where the Unreal actor goes, the pivot already taken off (Location minus the rotated
    /// PrePivot x Scale), blended between the last two ticks. Carried: the attachment's RelativeLocation from the
    /// bone, in simulation units; the mesh's PrePivot of <see cref="BridgeWorldActorInfo"/> still applies.
    /// </summary>
    public float X, Y, Z;

    /// <summary>Degrees, -180 to 180. Standing: the world rotation, blended the shortest way. Carried: RelativeRotation from the bone.</summary>
    public float Pitch, Yaw, Roll;
}

public static class BridgeWorldActorFlags
{
    /// <summary>Draw it. Clear: hide the level actor (hidden by script, destroyed, or showing no mesh).</summary>
    public const int Visible = 1;

    /// <summary>
    /// The actor has left its map placement at some time (it stays set): its transform must be applied. An actor
    /// that never moved keeps the transform the importer gave it.
    /// </summary>
    public const int Moved = 2;

    /// <summary>The location or rotation changed in the last tick (a door that is opening right now).</summary>
    public const int Moving = 4;

    /// <summary>Carried on a pawn's bone: X..Roll are relative to the bone.</summary>
    public const int Attached = 8;

    /// <summary>The simulation actor is gone on this client (destroyed, or out of network relevancy).</summary>
    public const int Gone = 16;
}

/// <summary>One light of render/lighting.json as it should shine and stand this frame (<c>GetLights</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeWorldLight
{
    /// <summary>The light actor's name (the level light's tag is "Scct:" + this).</summary>
    public int NameId;

    /// <summary><see cref="BridgeWorldLightFlags"/>.</summary>
    public int Flags;

    /// <summary>The original's ELightType now: 0 none, 1 steady, 2 pulse, 3 blink, 4 flicker, 5 strobe, 7 subtle pulse.</summary>
    public int LightType;

    /// <summary>
    /// What to multiply the light's placed intensity by this frame: 0 when off, else the light type's curve (1 for a
    /// steady light) times any change of the actor's LightBrightness from its placed value.
    /// </summary>
    public float Scale;

    /// <summary>The light's colour at full value (linear 0..1, as lighting.json's "color"); follows a changed hue or saturation.</summary>
    public float R, G, B;

    /// <summary>
    /// Where the light is: the placed location until <see cref="BridgeWorldLightFlags.Moved"/> is set, then the
    /// simulation actor's, blended between the last two ticks (a lift's lamp, a light riding a mover). With
    /// <see cref="BridgeWorldLightFlags.OnBone"/>: the RelativeLocation from the bone.
    /// </summary>
    public float X, Y, Z;

    /// <summary>Degrees (a spot light shines along the rotation's forward axis). With OnBone: RelativeRotation from the bone.</summary>
    public float Pitch, Yaw, Roll;

    /// <summary>With OnBone: the skeletal prop (<see cref="BridgeWorldAnimated.NameId"/>) the light hangs on; else 0.</summary>
    public int AttachNameId;

    /// <summary>With OnBone: the bone's name id; else 0.</summary>
    public int AttachBoneId;
}

public static class BridgeWorldLightFlags
{
    public const int On = 1;

    /// <summary>An STriggerLight: events and broken devices switch it.</summary>
    public const int Triggered = 2;

    /// <summary>A switched light in its half-second break flicker (shot out or chaffed) before it goes dark.</summary>
    public const int Breaking = 4;

    /// <summary>The light draws its corona now (known only for Triggered lights).</summary>
    public const int Corona = 8;

    /// <summary>The light has left its placed location or rotation at some time (it stays set): apply X..Roll to the level light.</summary>
    public const int Moved = 16;

    /// <summary>
    /// The light hangs on a bone of a skeletal map prop (a beacon's lamp, a crane's flare): attach it to
    /// AttachBoneId of the prop AttachNameId with X..Roll as the relative transform.
    /// </summary>
    public const int OnBone = 32;
}

/// <summary>One laser beam to draw this frame (<c>GetLasers</c>): a map tripwire or a mine's beam.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeWorldLaser
{
    /// <summary>Stable while the beam lives: the simulation ActorId of its tripwire, or of the mine beam itself.</summary>
    public int Id;

    /// <summary>0 an SLaserTrigger's tripwire, 1 a mine's beam (SLaserBeam_MineLaser).</summary>
    public int Kind;

    /// <summary>The tripwire's map name, 0 for a mine beam.</summary>
    public int NameId;

    /// <summary><see cref="BridgeWorldLaserFlags"/>.</summary>
    public int Flags;

    public float StartX, StartY, StartZ;
    public float EndX, EndY, EndZ;

    /// <summary>The beam's HeatIntensity (how bright it is in thermal vision; 1 by default).</summary>
    public float Heat;
}

public static class BridgeWorldLaserFlags
{
    /// <summary>The beam shows only in the vision modes (thermal, EMF), not to the naked eye.</summary>
    public const int HideInNormalVision = 1;
}

/// <summary>What a marker is (<see cref="BridgeWorldMarker.Kind"/>).</summary>
public static class BridgeWorldMarkerKind
{
    /// <summary>SComputerObjectiveTrigger: a terminal to hack. State is its SkinState.</summary>
    public const int Computer = 1;

    /// <summary>SBombTargetObjectiveTrigger: where a bomb goes. State 1 while a bomb is armed on it.</summary>
    public const int BombTarget = 2;

    /// <summary>SFlag: the disk (or vial) to carry off. State 1 while it is away from home; OwnerPawnId is its carrier.</summary>
    public const int Flag = 3;

    /// <summary>SFlagDropZone: the case a disk is brought to.</summary>
    public const int FlagDropZone = 4;

    /// <summary>Any other SObjectiveTrigger.</summary>
    public const int ObjectiveTrigger = 5;

    /// <summary>SAlarm: State 1 while it sounds; TimeLeft is the countdown.</summary>
    public const int Alarm = 6;

    /// <summary>SCamera: State is its LightState (0 locked, 1 inactive, 2 patrol, 3 alert).</summary>
    public const int Camera = 7;

    /// <summary>SVolumetricSensor: State is its bLightState (0 broken, 1 off, 2 green, 3 red).</summary>
    public const int VolumetricSensor = 8;

    /// <summary>SLaserTrigger: State 0 working, 1 beam hidden (switched off), 2 broken.</summary>
    public const int LaserTrigger = 9;

    /// <summary>SDisk: a Disk Hunt dog tag (hidden in the other modes).</summary>
    public const int Disk = 10;
}

/// <summary>One gameplay object a client marks in the world (<c>GetMarkers</c>).</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeWorldMarker
{
    /// <summary>The actor's map name.</summary>
    public int NameId;

    /// <summary><see cref="BridgeWorldMarkerKind"/>.</summary>
    public int Kind;

    /// <summary>The kind's own state value (see the kind).</summary>
    public int State;

    /// <summary><see cref="BridgeWorldMarkerFlags"/>.</summary>
    public int Flags;

    /// <summary>The pawn (BridgePawn.ActorId) that holds it: a disk's carrier, or the pawn a camera sees while in alert; 0 when none.</summary>
    public int OwnerPawnId;

    /// <summary>Where it is now (a carried disk follows its carrier).</summary>
    public float X, Y, Z;

    /// <summary>0..1 through a running capture (a hack, a bomb's fuse); 0 when nothing runs.</summary>
    public float Progress;

    /// <summary>Seconds until the running capture completes or the alarm stops; 0 when nothing runs.</summary>
    public float TimeLeft;
}

public static class BridgeWorldMarkerFlags
{
    /// <summary>The objective can be taken now (bActive).</summary>
    public const int Active = 1;

    /// <summary>Someone is taking it (bTriggering).</summary>
    public const int Triggering = 2;

    /// <summary>Locked (by an alarm).</summary>
    public const int Locked = 4;

    /// <summary>Its objective has been taken (SObjective.ObjectiveTaken).</summary>
    public const int Taken = 8;

    /// <summary>The actor is hidden.</summary>
    public const int Hidden = 16;

    /// <summary>A secondary objective (bSecondaryObjective).</summary>
    public const int Secondary = 32;

    /// <summary>The actor is not on this client now (out of network relevancy): the values are the last known.</summary>
    public const int Gone = 64;
}

/// <summary>What happened (<see cref="BridgeWorldEvent.Kind"/>).</summary>
public static class BridgeWorldEventKind
{
    /// <summary>The actor shows another static mesh (a damage stage, a triggered mesh): A new mesh id (0 none), B the one before.</summary>
    public const int MeshChanged = 1;

    /// <summary>
    /// A material slot changed (cracked screen, camera LED): A the slot, B the skin's name id (0: back to the mesh's
    /// own). The skins an actor already wears when it is first seen raise no event: read them with GetActorSkins.
    /// </summary>
    public const int SkinChanged = 2;

    /// <summary>
    /// A glass pane (SVitre) broke: X,Y,Z where it was hit and DX,DY,DZ the unit direction of the hit, both zero
    /// when the simulation did not say (A is 1 when they are given).
    /// </summary>
    public const int GlassBroken = 3;

    /// <summary>A switched light went on or off: A 1 on / 0 off, B the light type now.</summary>
    public const int LightSwitched = 4;

    /// <summary>A switched light was broken (shot out or chaffed) and flickers for half a second before going dark.</summary>
    public const int LightBreaking = 5;

    /// <summary>A marker's state or flags changed (going out of network relevancy included: the Gone flag): A the state now, B the flags now.</summary>
    public const int MarkerChanged = 6;

    /// <summary>A mover started (A 1) or stopped (A 0) moving or turning; B is its key number now. X,Y,Z is where it is.</summary>
    public const int MoverMoving = 7;

    /// <summary>
    /// An actor was hidden (A 0) or shown (A 1) by the simulation. A spawned prop that is destroyed raises A 0, B 1
    /// and leaves the actor list.
    /// </summary>
    public const int ShownChanged = 8;
}

/// <summary>One state change of the level (<c>GetEvents</c>), in the order they happened.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeWorldEvent
{
    /// <summary><see cref="BridgeWorldEventKind"/>.</summary>
    public int Kind;

    /// <summary>The map name of the actor or light it happened to.</summary>
    public int NameId;

    /// <summary>The kind's own values (see the kind).</summary>
    public int A, B;

    /// <summary>Where it happened, when the kind says so; else the actor's location.</summary>
    public float X, Y, Z;

    public float DX, DY, DZ;
}

/// <summary>One skeletal map prop as it should be drawn this frame (<c>GetAnimated</c>): an SAnimatedMesh or a skeletal fish.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeWorldAnimated
{
    /// <summary>The actor's map name.</summary>
    public int NameId;

    /// <summary>The skeletal mesh, as the simulation names it (Package.Name).</summary>
    public int MeshId;

    /// <summary>The sequence it plays or holds; 0 when none.</summary>
    public int SequenceId;

    /// <summary><see cref="BridgeWorldAnimatedFlags"/>.</summary>
    public int Flags;

    /// <summary>The actor's Location (not pivot-corrected), blended between the last two ticks.</summary>
    public float X, Y, Z;

    /// <summary>Degrees.</summary>
    public float Pitch, Yaw, Roll;

    public float ScaleX, ScaleY, ScaleZ;

    /// <summary>
    /// Where the sequence is this frame, 0 at its first frame to 1 at its last; -1 when the simulation only names
    /// a loop (fish), which the client then plays at its own pace.
    /// </summary>
    public float Frame;

    /// <summary>Sequence lengths per second, negative when it plays backwards, 0 when it holds a frame.</summary>
    public float Rate;
}

public static class BridgeWorldAnimatedFlags
{
    public const int Visible = 1;
    public const int Looping = 2;
}
