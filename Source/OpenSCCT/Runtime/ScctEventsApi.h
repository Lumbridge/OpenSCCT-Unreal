#pragma once

#include "CoreMinimal.h"

// The flat interface of the C# bridge's events feature: what a client needs to play sound and trigger effects.
// These mirror Bridge/OpenScct.UnrealBridge/Events/EventTypes.cs and EventsExports.cs field for field; change both
// together and bump the version.
//
// Fetch the table once, next to the main bridge table:
//   Host.GetFunction(TEXT("OpenScct.UnrealBridge.EventsExports"), TEXT("GetApi"), Error)
// with the signature int32 (__stdcall*)(FScctEventsApi*), as OpenSCCT.cpp does for Exports.GetApi, and check the
// returned version against ScctEventsApiVersion.
//
// Conventions: every field is 4 bytes (no padding). Locations are simulation units = Unreal centimetres, same
// axes. Angles are degrees. "Id" fields ending in Id that name text (WhatId, SoundId, FileId, ClassId, BoneId...)
// are ids of the events name table: 0 is none, GetName gives the text, ids never change within a process (cache
// them). Actor ids are the simulation's (the same as FScctBridgePawn::ActorId), 0 is none.
//
// Three streams are DRAINED (each record is delivered once: call until fewer than Capacity come back, every frame
// after Update): raw events, sounds, shots. Three lists are STATE (the whole current list every call): effect
// actors, impact marks, ambient sounds.

constexpr int32 ScctEventsApiVersion = 1;

// ---- raw events ---------------------------------------------------------------------------------------------

/** SimEventKind of the simulation (src/OpenScct.Core/SimEvents.cs). */
namespace EScctSimEventKind
{
	constexpr int32 PlaySound = 1;    // also delivered resolved as FScctSound
	constexpr int32 SpawnEmitter = 2; // for information only: do not draw from it (see FScctSimEvent)
	constexpr int32 Notify = 3;       // WhatId names it: Vibration_*, ClientDamageHudEffect, HudShutDown, SpawnSFX...
	constexpr int32 StopSound = 4;    // also delivered as FScctSound Stop
	constexpr int32 PlayMusic = 5;    // also delivered as FScctSound PlayMusic
	constexpr int32 StopMusic = 6;    // also delivered as FScctSound StopMusic
	constexpr int32 SetSwitch = 7;    // the surface type the next switch sound plays; already applied by the bridge
}

namespace EScctSimEventFlags
{
	constexpr int32 NoOverride = 1;   // PlaySound's bNoOverride
	constexpr int32 NoAttenuate = 2;  // PlaySound called with Attenuate false
	constexpr int32 Owned = 4;        // raised by PlayOwnedSound
	constexpr int32 Heard = 8;        // the server's call (ClientHearSound), not this machine's own simulation
	constexpr int32 Flat = 16;        // plays without 3D
	constexpr int32 Late = 32;        // raised between two ticks (a network call, an input command)
	constexpr int32 Dropped = 64;     // gave no FScctSound: a duplicate copy, out of hearing reach, or a stop of nothing
}

/**
 * One presentation event as the simulation raised it. Use it for notifies; sounds come resolved in FScctSound.
 * Do NOT start an effect from a SpawnEmitter event, or it is drawn twice (the Godot client ignores them in play):
 * an impact raises one for its spark and also fills an impact mark with the same spark class (draw the mark's), and
 * a device's Shocker burst is turned into an SBase.SSparkShocker actor on the client, which arrives as an
 * FScctEffectActor.
 */
struct FScctSimEvent
{
	int32 Kind;        // EScctSimEventKind
	int32 Tick;        // client tick the event is presented with
	int32 ActorId;     // source or owner actor
	int32 WhatId;      // name id: sound object path, emitter class, or notify name
	float X, Y, Z;     // where the source was; for a CombatFx notify its three parameters (A, B, C) instead
	float Pitch, Yaw, Roll; // the source's rotation, degrees
	int32 Slot;        // sound slot (ESoundSlot), the notify's parameter, or SetSwitch's surface type
	float Volume;      // PlaySound: the call's volume, before the Sound's own gain
	float Radius;      // PlaySound: the call's radius in cm (0: none given); PlayMusic/StopMusic: fade seconds
	float PitchScale;  // PlaySound: the call's pitch factor
	int32 BoneId;      // name id of the bone a sound follows, 0 none
	int32 Flags;       // EScctSimEventFlags
};

// ---- sounds -------------------------------------------------------------------------------------------------

namespace EScctSoundOp
{
	constexpr int32 Play = 1;      // start FileId; a sequence sound gives several records with one CallId
	constexpr int32 Stop = 2;      // StopSound: see FScctSound for the matching rule
	constexpr int32 PlayMusic = 3; // start FileId as the one music track, fading in over FadeSeconds
	constexpr int32 StopMusic = 4; // stop the music track, fading out over FadeSeconds (0: at once)
	constexpr int32 StopCall = 5;  // stop the voices started with CallId (a looping sound whose actor is gone)
}

namespace EScctSoundFlags
{
	constexpr int32 Positional = 1;       // a 3D source (pan, distance); clear: play flat (2D)
	constexpr int32 Loop = 2;             // loops until stopped (or for PlaySeconds when that is set)
	constexpr int32 AtListener = 4;       // a sound of the local player's own pawn: play at the listener for its whole life
	constexpr int32 Music = 8;
	constexpr int32 NoOverride = 16;      // does not replace a sound playing in its slot
	constexpr int32 Objective = 32;       // an objective jingle (own volume slider in OpenSCCT)
	constexpr int32 Heard = 64;           // the server's copy of the call
	constexpr int32 Stream = 128;         // FileId is an Ogg Vorbis stream of the map's bank, else a PCM wave
	constexpr int32 AllSounds = 256;      // Stop: every sound of the actor (StopSound with None)
	constexpr int32 LocalPawnVoices = 512; // Stop: ActorId is the local controller, which also owns its pawn's AtListener voices
}

/**
 * One thing for the sound engine to do, resolved as the Godot client's SoundSystem resolves a call: random and
 * switch lists are already picked, a sequence is laid out in time (Delay), volume/radius/pitch rules are applied.
 *
 * Play: open FileId (relative to the content root), start it after Delay seconds at X,Y,Z; while it plays move it
 * with FollowActorId (GetActorLocation; for a pawn with FollowBoneId, that bone of the drawn skeleton) unless
 * AtListener. Gain by distance d for a Positional source: 1 inside MinDistance, else
 * MinDistance / (MinDistance + 0.5 * (d - MinDistance)); nothing is audible beyond 100 * Radius.
 * Keep per voice: CallId, ActorId, SoundId, LeafId, Slot (for Stop, StopCall and the slot rule).
 *
 * Stop: stop every voice V where (ActorId == 0 ? (!AllSounds || !V.Music) : (V.ActorId == ActorId ||
 * (LocalPawnVoices && V.AtListener))) and (AllSounds || V.SoundId == SoundId || V.LeafId == SoundId).
 *
 * Slot rule of the original (32 voices): a new call takes a free voice; when none is free it replaces the voice
 * with the same ActorId and Slot (unless the new call is NoOverride, then it is not played), else the voice with
 * the lowest priority. Slot 0 (SLOT_None) never matches another call.
 *
 * PlayMusic: one track at a time, 2D, Volume 1 (music ignores the Sound's gain). The same SoundId already playing:
 * ignore. Otherwise the previous track fades out over max(FadeSeconds, 2) while the new one fades in.
 */
struct FScctSound
{
	int32 Op;            // EScctSoundOp
	int32 Tick;          // client tick of the event
	int32 CallId;        // identifies the PlaySound call; the entries of a sequence share it. 0 for Stop and music
	int32 Segment;       // index within the call (0 unless the sound is a sequence)
	int32 ActorId;       // the actor the call was made on; owns the voice
	int32 FollowActorId; // actor the source follows while playing, 0: stays put (or at the listener)
	int32 FollowBoneId;  // name id of the bone of that actor's mesh to follow, 0: the actor itself
	int32 SoundId;       // name id of the sound the call named (object path)
	int32 LeafId;        // name id of the wave or stream that plays
	int32 FileId;        // name id of the file: "shared/audio/wav/Pkg/Group/Name.wav" or "svmap/Map/audio/streams/Pkg/Name.ogg"
	int32 Slot;          // ESoundSlot 0..7
	int32 Flags;         // EScctSoundFlags
	float X, Y, Z;       // where the source starts
	float Volume;        // linear source volume 0..1 (all gains applied, clamped)
	float Radius;        // cm
	float MinDistance;   // cm, see the gain formula above
	float Pitch;         // 0.5 .. 2
	float Delay;         // seconds from the event before this record starts
	float PlaySeconds;   // > 0: stop after this many seconds of real time (a timed loop of a sequence); 0: play to the end
	float Duration;      // file length in seconds at pitch 1 (0 unknown)
	float FadeSeconds;   // PlayMusic: fade in; StopMusic: fade out
	int32 LoopStart;     // loop points of a looping wave in sample frames, -1: the whole file
	int32 LoopEnd;
};

// ---- shots --------------------------------------------------------------------------------------------------

namespace EScctShotKind
{
	constexpr int32 MuzzleSticky = 1;      // the spy pistol (Shocker): SBase.SMuzzleSticky mesh, draw scale 0.8..1.3, blue light (hue 148, sat 127)
	constexpr int32 ThirdPersonMuzzle = 2; // a merc gun seen from outside
	constexpr int32 FirstPersonMuzzle = 3; // the local merc's own gun in first person
	constexpr int32 Tracer = 4;            // a streak from X,Y,Z to Impact at Speed
}

namespace EScctShotFlags
{
	constexpr int32 Local = 1;       // the shooter is the local player's pawn
	constexpr int32 Suppressed = 2;  // no SMuzzleGenerique flash, weak light
	constexpr int32 Pistol = 4;      // SMuzzleGenerique at draw scale 0.6
	constexpr int32 Revolver = 8;    // no shell casing
	constexpr int32 Shotgun = 16;    // also start SBase.SShotGunEmitter (the spray), moving with the shooter
	constexpr int32 Uzi = 32;
	constexpr int32 Casing = 64;     // start a shell casing SBase.SDouilleEmitter (first person: at the gun's FX_DOUILLE bone)
	constexpr int32 BarrelFlash = 128; // also draw SBase.sMuzzleCanon
}

/**
 * A muzzle flash or a tracer to show once. A merc flash in the original: the SMuzzleGenerique mesh (draw scale
 * 1.5..3.5, random roll; in first person stretched 3.5..5.5 along the barrel) plus sMuzzleCanon when BarrelFlash,
 * for 0.1 s, with a warm light (hue 40, saturation 190) of 50 cm radius.
 */
struct FScctShot
{
	int32 Kind;          // EScctShotKind
	int32 Tick;
	int32 SourceActorId; // the weapon actor (flash) or the SFireTrace actor (tracer)
	int32 ShooterId;     // the pawn that fired
	int32 WeaponClassId; // name id of the weapon's class path, 0 for a tracer
	int32 Flags;         // EScctShotFlags
	float X, Y, Z;       // the muzzle as the simulation estimates it; prefer the drawn weapon's "FX" bone. Tracer: start
	float DirX, DirY, DirZ; // the aim (unit)
	float ImpactX, ImpactY, ImpactZ; // tracer: where the bullet hit
	float Speed;         // tracer: cm/s (7000)
	float Life;          // seconds: 0.1 for a flash, 2 for a tracer
	float HideDistance;  // tracer: hidden for the last this many cm before the impact (502)
};

// ---- effect actors ------------------------------------------------------------------------------------------

/** The kind of an effect class (shared/fx/effects.json). */
namespace EScctEffectKind
{
	constexpr int32 Emitter = 1;    // run the particle system of ClassId at the actor (SSmokeEmitter, SGrenadeExplosion...)
	constexpr int32 Projectile = 2; // a thrown object drawn as MeshId (grenades...)
	constexpr int32 Mine = 3;       // drawn as MeshId
	constexpr int32 Gadget = 4;     // drawn as MeshId (sticky cam, gas mask, backpack...)
	constexpr int32 Beam = 5;       // MeshId stretched by Scale along its forward axis (laser, taser)
	constexpr int32 Decal = 6;      // a sprinkler's puddle
	constexpr int32 Light = 7;      // only lights (the explosion flash lights)
}

namespace EScctEffectFlags
{
	constexpr int32 Hidden = 1;       // do not draw it now: bHidden (light actors are hidden and still light), or a gas mask / backpack not yet on its wearer
	constexpr int32 Deleted = 2;      // the actor went: this is its last record, end the effect
	constexpr int32 Killed = 4;       // with Deleted: stop spawning and let the live particles die out in place
	constexpr int32 DynamicLight = 8; // also lights its surroundings with the Light fields
	constexpr int32 OnBone = 16;      // attached to a pawn: place it at bone BoneId of the drawn skeleton of pawn BaseActorId, moved by Rel and turned by RelPitch/Yaw/Roll in the bone's frame
	constexpr int32 Unlit = 32;
	constexpr int32 OwnView = 64;     // a laser beam whose simulated start is in mid-air: draw it from the drawn hand / device instead
	constexpr int32 Snapped = 128;    // X,Y,Z was not blended from the tick before
	constexpr int32 VisionOnly = 256; // bHideInNormalVision: draw it only in the night and heat vision modes
}

/**
 * One transient effect actor as it should be drawn this frame. Match records between frames by ActorId: a new id
 * is a new effect, a record with Deleted (or an id that is no longer listed) ends it.
 */
struct FScctEffectActor
{
	int32 ActorId;
	int32 ClassId;       // name id of the class path ("SBase.SProjFrag")
	int32 Kind;          // EScctEffectKind
	int32 Flags;         // EScctEffectFlags
	int32 MeshId;        // name id of the StaticMesh object path, 0 when it draws none
	int32 TrailClassId;  // name id of the launch trail emitter to run following this actor, 0 none
	int32 BaseActorId;   // its Base, else its Owner; an attachment only with OnBone (else just who it belongs to)
	int32 BoneId;        // name id of the bone it is attached to (without OnBone: a gun's bone, already in X,Y,Z)
	int32 InstigatorId;  // the pawn that caused it
	int32 ParticleId;    // only for SBase.SSpawnableEmitter: name id of the object path of the map's particle emitter it runs, else 0
	float X, Y, Z;       // blended between the last two ticks
	float Pitch, Yaw, Roll;
	float ScaleX, ScaleY, ScaleZ; // DrawScale3D * DrawScale
	float PrePivotX, PrePivotY, PrePivotZ;
	float VelX, VelY, VelZ; // cm/s
	float RelX, RelY, RelZ; // RelativeLocation, cm in the bone's frame (with OnBone)
	float RelPitch, RelYaw, RelRoll; // RelativeRotation, degrees in the bone's frame (with OnBone)
	float Age;           // seconds since first seen
	float LifeSpan;      // the actor's LifeSpan in seconds, 0 none
	float LightBrightness; // 0..255; already 0 when the actor's light is off and already faded for the frag grenade's flash
	float LightHue;        // 0..255 (the UE2 byte)
	float LightSaturation; // 0..255, the UE2 byte as stored (in UE2's light model 255 is white, 0 the full colour)
	float LightRadius;     // reach in cm: 25 x (the UE2 LightRadius + 1), as the Godot client lights it
};

// ---- impact marks -------------------------------------------------------------------------------------------

namespace EScctImpactFlags
{
	constexpr int32 DecalShown = 1; // draw the decal
	constexpr int32 Unlit = 2;
	constexpr int32 Shocker = 4;    // a Shocker hit: the original shows no decal for these
}

/**
 * One live mark of the impact manager (50 slots, each lives 15 s). A slot whose Serial changed holds a new
 * impact: draw its decal (MaterialId, about 12 cm, facing along the rotation's forward axis out of the surface),
 * start SparkClassId once at X,Y,Z with the same rotation, show MeshId when set. A slot no longer listed expired.
 */
struct FScctImpactMark
{
	int32 Slot;           // 0..49
	int32 Serial;         // counts the impacts written to the slot
	int32 TypeId;         // name id of the impact class ("SImpactEffectBeton")
	int32 MaterialId;     // name id of the decal material ("sfx.Impacts.decal_beton"), 0 none
	int32 SparkClassId;   // name id of the spark emitter class ("SBase.SSparkBeton"), 0 none
	int32 MeshId;         // name id of the impact's static mesh, 0 none
	int32 SoundId;        // name id of the impact sound (it also arrives as a sound), 0 none
	int32 SurfaceActorId; // the actor the decal lies on (moves with it), 0: level geometry
	int32 Flags;          // EScctImpactFlags
	float X, Y, Z;
	float Pitch, Yaw, Roll;
	float DecalX, DecalY, DecalZ; // the decal projector's place, 10 cm out of the surface
	float DrawScale;
};

// ---- ambient sounds -----------------------------------------------------------------------------------------

namespace EScctAmbientFlags
{
	constexpr int32 Positional = 1;     // a 3D loop: audible within 100 * Radius, attenuated like any 3D source
	constexpr int32 Zone = 2;           // a zone's ambience: a 2D bed that plays while the listener is in the zone
	constexpr int32 ListenerInZone = 4; // with Zone: the listener is in this zone now
	constexpr int32 Loop = 8;           // loops; clear: plays once when it becomes audible (again every RepeatInterval)
	constexpr int32 Stream = 16;        // FileId is an Ogg Vorbis stream
	constexpr int32 Sequence = 32;      // the sound is a sequence: FileId is only its first entry
}

/**
 * One actor whose AmbientSound plays now. Match between frames by Key. The original's rules: a Positional source
 * plays within 100 * Radius; a Zone source plays (2D) only while ListenerInZone, and several zones naming one
 * sound share one voice; any other 2D source plays at Volume inside Radius and Volume / (1 + 0.5 * (d - R) / R)
 * beyond it, silent below 0.05.
 */
struct FScctAmbientSound
{
	int32 Key;      // the actor id; negative for an actor only the map's audio table has
	int32 SoundId;  // name id of the sound the actor names
	int32 FileId;   // name id of the file to play, 0 when the sound has none
	int32 Flags;    // EScctAmbientFlags
	float X, Y, Z;
	float Volume;   // linear 0..1
	float Radius;   // cm
	float Pitch;    // 0.5 .. 2
	float RepeatInterval; // seconds between plays of a repeating one-shot, 0 none
};

// ---- the table ----------------------------------------------------------------------------------------------

struct FScctEventsApi
{
	int32 Version;

	/** Starts the feature for the running session. Call right after StartLocal succeeds: events raised before it are not seen. Returns 1 when a session runs. */
	int32 (__cdecl* Begin)();

	/** Takes waiting raw events, oldest first. Returns the count written; call again while it equals Capacity. */
	int32 (__cdecl* DrainEvents)(FScctSimEvent* Events, int32 Capacity);

	/** Takes waiting sound records, oldest first. */
	int32 (__cdecl* DrainSounds)(FScctSound* Sounds, int32 Capacity);

	/** Takes waiting muzzle flashes and tracers, oldest first. */
	int32 (__cdecl* DrainShots)(FScctShot* Shots, int32 Capacity);

	/** Writes the effect actors as drawn this frame, the Deleted ones first. Returns the count written (use a buffer of 256 or more). */
	int32 (__cdecl* GetEffectActors)(FScctEffectActor* Actors, int32 Capacity);

	/** Writes the live impact marks (at most 50). */
	int32 (__cdecl* GetImpactMarks)(FScctImpactMark* Marks, int32 Capacity);

	/** Writes the ambient sound sources of this frame (a map has 40 to 100; use a buffer of 512). */
	int32 (__cdecl* GetAmbientSounds)(FScctAmbientSound* Sounds, int32 Capacity);

	/** Copies the text of a name id (zero terminated). Returns its full length in characters, 0 for an unknown id. Works without a session. */
	int32 (__cdecl* GetName)(int32 Id, TCHAR* Buffer, int32 Capacity);

	/** Writes an actor's current location (3 floats, cm). Returns 1, or 0 when the actor is gone. */
	int32 (__cdecl* GetActorLocation)(int32 ActorId, float* Location);
};

static_assert(sizeof(FScctSimEvent) == 64, "FScctSimEvent must match BridgeSimEvent");
static_assert(sizeof(FScctSound) == 100, "FScctSound must match BridgeSound");
static_assert(sizeof(FScctShot) == 72, "FScctShot must match BridgeShot");
static_assert(sizeof(FScctEffectActor) == 148, "FScctEffectActor must match BridgeEffectActor");
static_assert(sizeof(FScctImpactMark) == 76, "FScctImpactMark must match BridgeImpactMark");
static_assert(sizeof(FScctAmbientSound) == 44, "FScctAmbientSound must match BridgeAmbientSound");
