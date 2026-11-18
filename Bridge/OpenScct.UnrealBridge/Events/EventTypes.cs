using System.Runtime.InteropServices;

namespace OpenScct.UnrealBridge;

// The records of the events feature. They cross to C++ as they are: Source/OpenSCCT/Runtime/ScctEventsApi.h mirrors
// them field for field; change both together and bump EventsExports.ApiVersion. Every field is 4 bytes, so the
// layouts have no padding. Locations are in the simulation's units (1 unit = 1 Unreal cm, same axes), angles in
// degrees, names are ids of the events name table (EventNames; 0 is "none").

/// <summary>
/// One presentation event of the simulation as it raised it (OpenSCCT's <c>SimEvent</c>, src/OpenScct.Core/SimEvents.cs).
/// Sounds are also delivered resolved (<see cref="BridgeSound"/>); this stream is for everything else a client
/// reacts to (notifies, SpawnEmitter) and for checks.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeSimEvent
{
    /// <summary>SimEventKind: 1 PlaySound, 2 SpawnEmitter, 3 Notify, 4 StopSound, 5 PlayMusic, 6 StopMusic, 7 SetSwitch.</summary>
    public int Kind;

    /// <summary>The client tick the event is presented with (a late event belongs to the tick about to run).</summary>
    public int Tick;

    /// <summary>Source or owner actor, 0 when none.</summary>
    public int ActorId;

    /// <summary>Name id: the sound's object path, the emitter class or the notify name.</summary>
    public int WhatId;

    /// <summary>
    /// Where the source was when the event was raised. A Notify raised through CombatFx.Notify carries its three
    /// parameters (A, B, C) here instead.
    /// </summary>
    public float X, Y, Z;

    /// <summary>The source's rotation, degrees.</summary>
    public float Pitch, Yaw, Roll;

    /// <summary>The sound slot (ESoundSlot), the notify's parameter, or SetSwitch's surface type.</summary>
    public int Slot;

    /// <summary>PlaySound: the call's volume (before the Sound's own gain).</summary>
    public float Volume;

    /// <summary>PlaySound: the call's radius in units (0: none given). PlayMusic and StopMusic: the fade in seconds.</summary>
    public float Radius;

    /// <summary>PlaySound: the call's pitch factor.</summary>
    public float PitchScale;

    /// <summary>Name id of the bone of the source's mesh a sound follows, 0 for none.</summary>
    public int BoneId;

    /// <summary><see cref="BridgeSimEventFlags"/>.</summary>
    public int Flags;
}

public static class BridgeSimEventFlags
{
    /// <summary>PlaySound's bNoOverride.</summary>
    public const int NoOverride = 1;

    /// <summary>PlaySound called with Attenuate false.</summary>
    public const int NoAttenuate = 2;

    /// <summary>Raised by PlayOwnedSound.</summary>
    public const int Owned = 4;

    /// <summary>The server's call (ClientHearSound), not one this machine's simulation made.</summary>
    public const int Heard = 8;

    /// <summary>Plays without 3D (an unattenuated call of the viewed actor).</summary>
    public const int Flat = 16;

    /// <summary>Raised between two ticks (a network call, an input command), taken before the next tick.</summary>
    public const int Late = 32;

    /// <summary>
    /// The event gave no sound record. A PlaySound that was not played: the other machine's copy of a call that already
    /// played (the client's own simulation and the server both raise footsteps and gear sounds), or out of hearing
    /// reach. A StopSound for a sound nothing can still be playing on its actor (idle cameras raise these every tick).
    /// </summary>
    public const int Dropped = 64;
}

/// <summary>What a <see cref="BridgeSound"/> asks for.</summary>
public static class BridgeSoundOp
{
    /// <summary>Start the file. A sequence sound gives several records with one CallId.</summary>
    public const int Play = 1;

    /// <summary>StopSound: stop the voices of ActorId that play SoundId (see the flags for the wildcards).</summary>
    public const int Stop = 2;

    /// <summary>Start the file as the one music track, fading in over FadeSeconds; the previous track fades out.</summary>
    public const int PlayMusic = 3;

    /// <summary>Stop the music track, fading out over FadeSeconds (0: at once).</summary>
    public const int StopMusic = 4;

    /// <summary>Stop the voices started with CallId: a looping sound whose actor is gone.</summary>
    public const int StopCall = 5;
}

public static class BridgeSoundFlags
{
    /// <summary>A 3D source: pan and attenuate it by distance. Clear: play it flat (2D).</summary>
    public const int Positional = 1;

    /// <summary>Loops until stopped (or for PlaySeconds when that is set).</summary>
    public const int Loop = 2;

    /// <summary>
    /// A sound of the pawn this machine controls: the original plays it at the listener for its whole life (no
    /// distance, no pan), whatever <see cref="Positional"/> says.
    /// </summary>
    public const int AtListener = 4;

    public const int Music = 8;

    /// <summary>bNoOverride: the call does not replace a sound playing in its slot.</summary>
    public const int NoOverride = 16;

    /// <summary>An objective jingle (the Jingl sounds of Interface.Objectifs): OpenSCCT gives these their own volume slider.</summary>
    public const int Objective = 32;

    /// <summary>The server's copy of the call (ClientHearSound).</summary>
    public const int Heard = 64;

    /// <summary>The file is a streamed Ogg Vorbis file of the map's bank, not a wave of the shared library.</summary>
    public const int Stream = 128;

    /// <summary>Stop: every sound of the actor (StopSound with None).</summary>
    public const int AllSounds = 256;

    /// <summary>Stop: ActorId is the local player's controller, which also owns the AtListener voices of its pawn.</summary>
    public const int LocalPawnVoices = 512;
}

/// <summary>
/// One thing for the sound engine to do, already resolved as the Godot client's SoundSystem resolves a call
/// (game/Scripts/Audio/SoundSystem.cs: random and switch lists picked, a sequence laid out in time, the volume,
/// radius and pitch rules applied). C++ plays <see cref="FileId"/> and applies the distance model.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeSound
{
    /// <summary><see cref="BridgeSoundOp"/>.</summary>
    public int Op;

    /// <summary>Client tick of the event.</summary>
    public int Tick;

    /// <summary>
    /// Identifies the PlaySound call. The entries of a sequence sound share it and differ in <see cref="Segment"/>.
    /// 0 for Stop, PlayMusic and StopMusic.
    /// </summary>
    public int CallId;

    /// <summary>Index of this record within its call (0 unless the sound is a sequence).</summary>
    public int Segment;

    /// <summary>The actor the call was made on (0: none). Owns the voice for Stop and for the slot rule.</summary>
    public int ActorId;

    /// <summary>The actor whose location the source follows while it plays, 0 when it stays at X, Y, Z or at the listener.</summary>
    public int FollowActorId;

    /// <summary>Name id of the bone of that actor's mesh the source follows, 0 for the actor itself.</summary>
    public int FollowBoneId;

    /// <summary>Name id of the sound the call named (its object path, e.g. Sound_Persos.FootSteps.SW_FootRun).</summary>
    public int SoundId;

    /// <summary>Name id of the wave or stream that plays (the leaf the lists resolved to).</summary>
    public int LeafId;

    /// <summary>
    /// Name id of the audio file, relative to the content root with forward slashes
    /// (shared/audio/wav/Package/Group/Name.wav or svmap/Map/audio/streams/Package/Name.ogg).
    /// </summary>
    public int FileId;

    /// <summary>ESoundSlot of the call (0 SLOT_None ... 7).</summary>
    public int Slot;

    /// <summary><see cref="BridgeSoundFlags"/>.</summary>
    public int Flags;

    /// <summary>Where the source starts.</summary>
    public float X, Y, Z;

    /// <summary>Linear source volume, 0 to 1: the call's volume times every gain on the way, clamped.</summary>
    public float Volume;

    /// <summary>The source's radius in units: the Sound's own when it has one, else the call's (80 when the call gave none).</summary>
    public float Radius;

    /// <summary>
    /// Full volume inside this distance; beyond it the original's gain is
    /// <c>MinDistance / (MinDistance + 0.5 x (d - MinDistance))</c>. The sound is not heard at all beyond 100 x Radius.
    /// </summary>
    public float MinDistance;

    /// <summary>Pitch factor, 0.5 to 2.</summary>
    public float Pitch;

    /// <summary>Seconds from the event before this record starts (the earlier entries of its sequence).</summary>
    public float Delay;

    /// <summary>When above 0: stop after this many seconds (a timed loop entry of a sequence). 0: play to the end.</summary>
    public float PlaySeconds;

    /// <summary>Length of the file in seconds at pitch 1 (0 when the content does not say).</summary>
    public float Duration;

    /// <summary>PlayMusic: fade in. StopMusic: fade out. Seconds.</summary>
    public float FadeSeconds;

    /// <summary>Loop points of a looping wave in sample frames (its smpl chunk), -1 for the whole file.</summary>
    public int LoopStart, LoopEnd;
    public float SeekOffset, SeekFadeOut;
}

/// <summary>What a <see cref="BridgeShot"/> is.</summary>
public static class BridgeShotKind
{
    /// <summary>The spy pistol's shot (the Shocker): SMuzzleSticky at the pistol's muzzle.</summary>
    public const int MuzzleSticky = 1;

    /// <summary>A merc gun seen from outside (SWeaponDefenseDummy.ClientFireMuzzle).</summary>
    public const int ThirdPersonMuzzle = 2;

    /// <summary>The local merc's own gun in first person (SWeaponDefense.SpawnSFX).</summary>
    public const int FirstPersonMuzzle = 3;

    /// <summary>A rifle tracer (an SFireTrace actor): a streak from X, Y, Z to the impact at Speed.</summary>
    public const int Tracer = 4;
}

public static class BridgeShotFlags
{
    /// <summary>The shooter is the pawn this machine controls.</summary>
    public const int Local = 1;

    /// <summary>A suppressed gun: no SMuzzleGenerique flash, no barrel flash, a weak light.</summary>
    public const int Suppressed = 2;

    /// <summary>A sidearm: the flash is SMuzzleGenerique at draw scale 0.6, no sMuzzleCanon.</summary>
    public const int Pistol = 4;

    /// <summary>A revolver: no shell casing.</summary>
    public const int Revolver = 8;

    /// <summary>A shotgun: also start SBase.SShotGunEmitter (the spray) at the muzzle, moving with the shooter.</summary>
    public const int Shotgun = 16;

    public const int Uzi = 32;

    /// <summary>Start a shell casing (SBase.SDouilleEmitter) at the gun's FX_DOUILLE bone (else at the muzzle).</summary>
    public const int Casing = 64;

    /// <summary>Draw the barrel flash sMuzzleCanon as well as SMuzzleGenerique.</summary>
    public const int BarrelFlash = 128;
}

/// <summary>A muzzle flash or a tracer to show once. Drained like the events.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeShot
{
    /// <summary><see cref="BridgeShotKind"/>.</summary>
    public int Kind;

    /// <summary>Client tick it was seen on.</summary>
    public int Tick;

    /// <summary>The weapon actor (a flash) or the SFireTrace actor (a tracer), 0 when unknown.</summary>
    public int SourceActorId;

    /// <summary>The pawn that fired, 0 when unknown.</summary>
    public int ShooterId;

    /// <summary>Name id of the weapon's class path (SBase.SWeaponDefense_Shotgun...), 0 for a tracer.</summary>
    public int WeaponClassId;

    /// <summary><see cref="BridgeShotFlags"/>.</summary>
    public int Flags;

    /// <summary>
    /// The muzzle as the simulation estimates it (it has no skeleton): prefer the drawn weapon's "FX" bone, else
    /// the drawn "B R Hand" plus 24 units along the aim. For a tracer: where it starts.
    /// </summary>
    public float X, Y, Z;

    /// <summary>The aim, a unit vector.</summary>
    public float DirX, DirY, DirZ;

    /// <summary>Tracer: where the bullet hit.</summary>
    public float ImpactX, ImpactY, ImpactZ;

    /// <summary>Tracer: units a second (SFireTrace.FTSpeed, 7000).</summary>
    public float Speed;

    /// <summary>Seconds the effect lives: 0.1 for a flash (SMuzzleFlash Lifetime), the tracer's LifeSpan (2).</summary>
    public float Life;

    /// <summary>Tracer: it is hidden for the last this many units before the impact (502 in the original).</summary>
    public float HideDistance;
}

/// <summary>The kind of an effect class (shared/fx/effects.json "kind").</summary>
public static class BridgeEffectKind
{
    /// <summary>An Emitter actor: run the particle system of its class at the actor.</summary>
    public const int Emitter = 1;

    /// <summary>A thrown or fired object drawn as its static mesh (grenades, the sticky cam in flight...).</summary>
    public const int Projectile = 2;

    public const int Mine = 3;

    /// <summary>A placed gadget drawn as its static mesh (sticky cam, backpack, gas mask, noise emitter...).</summary>
    public const int Gadget = 4;

    /// <summary>A beam drawn as a stretched static mesh (laser, taser).</summary>
    public const int Beam = 5;

    /// <summary>A puddle a sprinkler made (SFlaque).</summary>
    public const int Decal = 6;

    /// <summary>A light actor (the flash lights of explosions): it only lights, nothing is drawn.</summary>
    public const int Light = 7;
}

public static class BridgeEffectFlags
{
    /// <summary>
    /// Not drawn now: bHidden (light actors still light), or a gas mask or backpack that is not on its wearer's bone
    /// yet (EffectViews.Draw hides those until the attachment has arrived, and the mask while its wearer is dead).
    /// </summary>
    public const int Hidden = 1;

    /// <summary>
    /// The actor went this tick. The record is given once more so its effect can be ended, then never again.
    /// </summary>
    public const int Deleted = 2;

    /// <summary>With Deleted: Emitter.Kill was called, so spawning stops and the live particles die out in place.</summary>
    public const int Killed = 4;

    /// <summary>bDynamicLight: the actor also lights its surroundings with the light fields.</summary>
    public const int DynamicLight = 8;

    /// <summary>
    /// Attached to a bone of a pawn (BaseActorId is the pawn, BoneId, Rel, RelRot): place it at that bone of the drawn
    /// skeleton. Not set for an effect on a gun's bone (the barrel smoke): its location is already the bone's place.
    /// </summary>
    public const int OnBone = 16;

    /// <summary>bUnlit.</summary>
    public const int Unlit = 32;

    /// <summary>
    /// A laser beam the Godot client leaves to a view of its own (the spy pistol's and merc laser's beam from the
    /// drawn hand, the mines' and tripwires' beams): its simulated place starts in mid-air.
    /// </summary>
    public const int OwnView = 64;

    /// <summary>The location was not blended from the tick before (first tick, or the actor was placed).</summary>
    public const int Snapped = 128;

    /// <summary>bHideInNormalVision: drawn only in the night and heat vision modes (EffectViews.Draw, rendMap 36 or 38).</summary>
    public const int VisionOnly = 256;
    public const int RestoreAge = 512;
    public const int HistoricalEmitter = 1024;
}

/// <summary>
/// One transient effect actor of the client world as it should be drawn this frame, found the way the Godot
/// client's EffectViews finds them (game/Scripts/Fx/EffectViews.cs).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeEffectActor
{
    public int ActorId;

    /// <summary>Name id of the class path (SBase.SSmokeEmitter, SBase.SProjFrag...).</summary>
    public int ClassId;

    /// <summary><see cref="BridgeEffectKind"/>.</summary>
    public int Kind;

    /// <summary><see cref="BridgeEffectFlags"/>.</summary>
    public int Flags;

    /// <summary>Name id of the actor's StaticMesh object path (SGameplayObjects.Objects.sticky_cam), 0 when it draws none.</summary>
    public int MeshId;

    /// <summary>
    /// Name id of the launch trail class script spawns with the actor on every drawing machine and that follows
    /// it (SBase.SSmokeLaunchEmitter, SBase.SFragLaunchEmitter), 0 for none. The frag's trail ends with the frag;
    /// the others are left to die out where the grenade went.
    /// </summary>
    public int TrailClassId;

    /// <summary>The actor it is attached to (its Base, else its Owner), 0 for none.</summary>
    public int BaseActorId;

    /// <summary>Name id of the bone it is attached to, 0 for none.</summary>
    public int BoneId;

    /// <summary>The pawn that caused it (Instigator), 0 for none.</summary>
    public int InstigatorId;

    /// <summary>
    /// Only for an SBase.SSpawnableEmitter (the damage effects of the map's breakable objects): name id of the object
    /// path of the map's particle emitter it runs (its Emitters[0]; EffectViews.StartSpawnable), which the class
    /// alone does not say. 0 for every other actor.
    /// </summary>
    public int ParticleId;

    /// <summary>The simulation's location, blended between the last two ticks.</summary>
    public float X, Y, Z;

    /// <summary>Degrees.</summary>
    public float Pitch, Yaw, Roll;

    /// <summary>DrawScale3D times DrawScale.</summary>
    public float ScaleX, ScaleY, ScaleZ;

    /// <summary>PrePivot: the mesh is drawn moved by minus this, in its own frame.</summary>
    public float PrePivotX, PrePivotY, PrePivotZ;

    /// <summary>Units a second.</summary>
    public float VelX, VelY, VelZ;

    /// <summary>RelativeLocation: with OnBone, the offset in the bone's frame.</summary>
    public float RelX, RelY, RelZ;

    /// <summary>RelativeRotation in degrees: with OnBone, the turn in the bone's frame (EffectViews.BoneMeshFrame).</summary>
    public float RelPitch, RelYaw, RelRoll;

    /// <summary>Seconds since the actor was first seen.</summary>
    public float Age;

    /// <summary>The actor's remaining LifeSpan in seconds, 0 for none.</summary>
    public float LifeSpan;

    /// <summary>
    /// The light of a Light actor or a DynamicLight emitter, as EffectViews.PlaceLight reads it. Brightness 0 to 255,
    /// already 0 for LightType LT_None and already faded for the frag's flash (SFlashLight_FragGrenade: 255 down to 0
    /// over its fTempsDisparition). Hue and saturation are the UE2 bytes, 0 to 255 (saturation 0 is full colour in
    /// UE2's light model, 255 is white). Radius is the reach in world units: 25 x (LightRadius + 1).
    /// </summary>
    public float LightBrightness, LightHue, LightSaturation, LightRadius;
}

public static class BridgeImpactFlags
{
    /// <summary>Draw the decal (false when its surface is gone or hidden, or the decal is suppressed).</summary>
    public const int DecalShown = 1;

    public const int Unlit = 2;

    /// <summary>A Shocker hit (SImpactEffectShocker, SImpactEffectShockerMetal).</summary>
    public const int Shocker = 4;
}

/// <summary>
/// One live mark of the level's impact manager (50 slots, each lives 15 s; src/OpenScct.Game/Equipment/ImpactManager.cs).
/// A slot whose <see cref="Serial"/> changed holds a new impact: draw its decal and start its spark emitter once.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeImpactMark
{
    /// <summary>The slot, 0 to 49.</summary>
    public int Slot;

    /// <summary>Counts the impacts written to the slot.</summary>
    public int Serial;

    /// <summary>Name id of the impact class (SImpactEffectBeton...).</summary>
    public int TypeId;

    /// <summary>Name id of the decal's material object path (sfx.Impacts.decal_beton), 0 for no decal.</summary>
    public int MaterialId;

    /// <summary>Name id of the spark emitter's class path (SBase.SSparkBeton), 0 for none.</summary>
    public int SparkClassId;

    /// <summary>Name id of the impact's static mesh object path (the bullet hole), 0 for none.</summary>
    public int MeshId;

    /// <summary>Name id of the impact sound's object path (it also arrives as a sound event), 0 for none.</summary>
    public int SoundId;

    /// <summary>The gameplay actor the decal lies on (it moves with it), 0 for the level's geometry.</summary>
    public int SurfaceActorId;

    /// <summary><see cref="BridgeImpactFlags"/>.</summary>
    public int Flags;

    /// <summary>Where the effect sits.</summary>
    public float X, Y, Z;

    /// <summary>Degrees; the forward axis points out of the surface.</summary>
    public float Pitch, Yaw, Roll;

    /// <summary>The decal projector's place: 10 units out of the surface.</summary>
    public float DecalX, DecalY, DecalZ;

    public float DrawScale;
}

public static class BridgeAmbientFlags
{
    /// <summary>A 3D loop: heard within Reach, attenuated like any 3D source.</summary>
    public const int Positional = 1;

    /// <summary>A zone's ambience (ZoneInfo.AmbientSound): a 2D bed that plays while the listener is in the zone.</summary>
    public const int Zone = 2;

    /// <summary>With Zone: the listener is in this zone now.</summary>
    public const int ListenerInZone = 4;

    /// <summary>Loops. Clear: plays once each time it becomes audible (and again every RepeatInterval when that is set).</summary>
    public const int Loop = 8;

    /// <summary>The file is an Ogg Vorbis stream of the map's bank.</summary>
    public const int Stream = 16;

    /// <summary>The sound is a sequence: FileId is its first entry only.</summary>
    public const int Sequence = 32;
}

/// <summary>
/// One actor whose AmbientSound plays now (spec/native/audio.md rules 19 to 22): zones, AmbientSound actors, ambient
/// triggers, movers, running conveyors, armed gadgets.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct BridgeAmbientSound
{
    /// <summary>The actor id; negative for an actor only the map's audio table has (minus its index, minus one).</summary>
    public int Key;

    /// <summary>Name id of the sound the actor names.</summary>
    public int SoundId;

    /// <summary>Name id of the file to loop, relative to the content root; 0 when the sound has no file.</summary>
    public int FileId;

    /// <summary><see cref="BridgeAmbientFlags"/>.</summary>
    public int Flags;

    public float X, Y, Z;

    /// <summary>Linear source volume, 0 to 1 (SoundVolume / 255, times LightBrightness / 255 on a light, times the Sound's gain).</summary>
    public float Volume;

    /// <summary>Units: the Sound's own radius when it has one, else the actor's SoundRadius.</summary>
    public float Radius;

    /// <summary>Pitch factor (SoundPitch / 64, held to 0.5 .. 2).</summary>
    public float Pitch;

    /// <summary>Seconds between plays of a repeating one-shot, 0 for none.</summary>
    public float RepeatInterval;
}
