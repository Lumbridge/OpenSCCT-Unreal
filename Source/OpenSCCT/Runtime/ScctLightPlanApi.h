#pragma once

#include "CoreMinimal.h"

// The lighting plan's part of the C# bridge's flat interface (scct.Light.Plan, Play/ScctLightPlan.h): the facts of the
// map's lights and zones the plan classifies and dresses a level by, the actor light rig (which lights the original's
// renderer would light an actor with now, by the simulation's own rules), the own spy's shadow darkness and the
// merc's squint. These mirror Bridge/OpenScct.UnrealBridge/LightPlan/LightPlanExports.cs field for field; change both
// together and bump the version. Every field is 4 bytes wide, so the structs have no padding.
//
// Fetch the table through ScctFeature<FScctLightPlanApi>() (Runtime/ScctFeatures.h), on the game thread, while a
// match plays. Simulation units are Unreal centimetres in Unreal's axes; rotations are degrees as FRotator takes them.
// Name ids are the world table's (FScctWorldApi::GetName).
//
// THE TERM. All colours here are in the display units of the original's light term: 1 is a white texture drawn at
// full white, and the term saturates at 2. A surface of an actor with normal N shows
//   min(1, texture x min(2, min(2, ambient + sum over the rig's lights of colour x max(0, N.L) [+ colour if point
//   ambient]) + projectors))
// as a display value (0 to 1). Under the plan's tone curve a display value v is scene light v^2.2 at exposure 1.

constexpr int32 ScctLightPlanApiVersion = 2;

/** What the lighting plan does with a map light. */
namespace EScctLightClass
{
	/** No static, in-game or dynamic flag: it lights nothing. */
	constexpr int32 None = 0;
	/** LightEffect 22 (LE_ESpotShadow): a projected, shadowed patch on world and characters. */
	constexpr int32 Projector = 1;
	/** bDynamicLight: a live light on the world, the only world light that goes when switched or shot. */
	constexpr int32 Dynamic = 2;
	/** bApplyToStaticLighting and not dynamic: its light is in the lightmaps and vertex colours already. */
	constexpr int32 Baked = 3;
	/** bApplyToInGameLighting only: it lights characters and other actor-lit things, never the world. */
	constexpr int32 CharactersOnly = 4;
	/** bSpecialLit: it lights special-lit actors only. */
	constexpr int32 SpecialLit = 5;
	constexpr int32 Count = 6;
}

namespace EScctLightInfoFlags
{
	constexpr int32 ApplyStatic = 1;
	constexpr int32 ApplyInGame = 2;
	constexpr int32 Dynamic = 4;
	constexpr int32 OwnZoneOnly = 8;
	constexpr int32 SpecialLit = 16;
	constexpr int32 ActorShadows = 32;
	/** An STriggerLight: events and broken devices switch it. */
	constexpr int32 Triggered = 64;
	constexpr int32 InitiallyOn = 128;
	/** bHidden: the importer placed no level light for it. */
	constexpr int32 Hidden = 256;
	/** The light actor is bStatic. */
	constexpr int32 Static = 512;
	constexpr int32 Corona = 1024;
	/** In the actor light set (kept for run time, with a light type, not a projector): a rig may choose it. */
	constexpr int32 LightsActors = 2048;
	/** UsesLightBeam. */
	constexpr int32 Beam = 4096;
	/** A cone light (LE_StaticSpot or LE_Spotlight). */
	constexpr int32 Spot = 8192;
	/** LE_PointAmbient: it also adds its attenuated colour whatever the normal. */
	constexpr int32 PointAmbient = 16384;
	/** LE_Sunlight. */
	constexpr int32 Sun = 32768;
}

/** The map's light data in short. The class counts leave hidden lights out, as no level light stands for them. */
struct FScctLightLevel
{
	/** 1 when the map's render/lighting.json was read. */
	int32 HasData;
	/** Lights in the file, and how many of them are hidden. */
	int32 Lights, Hidden;
	/** Lights that are not hidden, by EScctLightClass. */
	int32 Projectors, Dynamic, Baked, CharactersOnly, SpecialLit, Unused;
	int32 Zones;
	/** LevelInfo.GlowThreshold (0 to 255) and whether the map's glow is on. */
	int32 GlowThreshold, GlowEnabled;
	/** The sky zone's index, -1 none. */
	int32 SkyZone;
	/** LevelInfo.ShadowDarknessScale (1.7 unless the map says otherwise). */
	float ShadowDarknessScale;
};
static_assert(sizeof(FScctLightLevel) == 56, "FScctLightLevel must match BridgeLightLevel");

/** One light of render/lighting.json: its flags, its raw values and the plan's class. */
struct FScctLightInfo
{
	/** The light actor's name (the level light's tag is "Scct:" + it), 0 when it has none. */
	int32 NameId;
	/** EScctLightClass */
	int32 Class;
	/** EScctLightInfoFlags */
	int32 Flags;
	/** The original's ELightEffect (8 and 12 cone lights, 19 sun, 20 quadratic, 21 point ambient, 22 projector). */
	int32 Effect;
	/** The placed ELightType (0 none, 1 steady, 2 pulse, 3 blink, 4 flicker, 5 strobe, 7 subtle pulse). */
	int32 LightType;
	/** LightBrightness, LightHue, LightSaturation (255 is white), LightRadius and LightCone as the map has them. */
	float Brightness;
	int32 Hue, Saturation;
	float LightRadius;
	int32 Cone;
	/** How far the light reaches (cm): 25 x (LightRadius + 1); for a projector its EchelonRange. */
	float WorldRadius;
	/** A cone light's full outer angle in degrees; the inner one is 0.05 of it, linear in the cosine between. */
	float ConeDegrees;
	/** The zone index of the light's zone, -1 unknown. */
	int32 Zone;
	/** Where the map placed it, and its rotation. */
	float X, Y, Z;
	float Pitch, Yaw, Roll;
	/** The colour in the term's display units before the gain: FGetHSV x brightness / 255 (a projector: / 510, brightest channel at most 1). */
	float R, G, B;
	/** What the term multiplies the colour by at the light: 2 (points, cones, projectors), 1.75 (suns). Peak term = Gain x colour. */
	float Gain;
	/**
	 * A projector's frustum: it reaches EchelonRange along its axis and lights +-SpotWidth x +-SpotHeight there; the
	 * apex sits ApexDistance behind the light (-1: parallel rays). NormalInfluence 0 lights every surface fully, 1 by
	 * N.L. Attenuation 0 none, 1 linear (1 - depth / range), 2 late (1 until 0.9 of the range).
	 */
	float EchelonRange, SpotWidth, SpotHeight, NearPlaneRatio, NormalInfluence, ApexDistance;
	int32 Attenuation;
	/** The projected texture's file in the map's render part (a name id), 0 none. */
	int32 CookieNameId;
	float Period, Phase;
};
static_assert(sizeof(FScctLightInfo) == 132, "FScctLightInfo must match BridgeLightInfo");

struct FScctLightZone
{
	int32 ZoneIndex;
	/** The zone actor's name and the place name players see (name ids). */
	int32 NameId, PlaceNameId;
	/** 1 the sky zone, 2 fogged. */
	int32 Flags;
	/** The zone ambient in display units (m_RefAmbientColor / 255): what every actor in the zone is lit with at least. */
	float R, G, B;
	float FogR, FogG, FogB, FogStart, FogEnd;
};
static_assert(sizeof(FScctLightZone) == 48, "FScctLightZone must match BridgeLightZone");

namespace EScctLightRigOptions
{
	/** Map lights only: no flare, muzzle flash, flash grenade, flag or torch. */
	constexpr int32 NoRuntimeLights = 1;
	/** Every map light where the map placed it, not where its actor rides now. */
	constexpr int32 PlacedOnly = 2;
	/** Do not trace the occluded flag of the chosen lights. */
	constexpr int32 NoOcclusion = 4;
	/** Leave special-lit lights out of the set (the simulation's field does not). */
	constexpr int32 NoSpecialLit = 8;
	/** Only ordinary lamps supplied by the pawn material. Unreal draws live lights separately. */
	constexpr int32 MaterialLamps = 16;
}

namespace EScctRigLightFlags
{
	/** LE_PointAmbient: the colour is also added whatever the normal. */
	constexpr int32 PointAmbient = 1;
	/** A collision line from the light to the actor's centre is blocked. The original lights the actor all the same. */
	constexpr int32 Occluded = 2;
	/** A sun: X, Y, Z is the unit direction towards the light, not a place. */
	constexpr int32 Sun = 4;
	/** Not a map light: a flare, a muzzle flash, a flash grenade, a flag (the id is minus the actor id). */
	constexpr int32 Runtime = 8;
	/** A cone light; the cone's edge is already in the colour (taken at the actor's centre). */
	constexpr int32 Spot = 16;
}

struct FScctRigLight
{
	/** Stable over frames: the light's index in GetLights for a map light, minus the actor id for a runtime light. */
	int32 Id;
	/** EScctRigLightFlags */
	int32 Flags;
	/** Where the light is now; for a sun the unit direction towards it. */
	float X, Y, Z;
	/** The colour as it reaches the actor's centre, in the term's display units (gain, light type, falloff and cone included). */
	float R, G, B;
};
static_assert(sizeof(FScctRigLight) == 32, "FScctRigLight must match BridgeRigLight");

constexpr int32 ScctLightRigLights = 4;

/** What the original's renderer lights one actor with now. */
struct FScctLightRig
{
	/** The zone ambient at the actor. */
	float AmbientR, AmbientG, AmbientB;
	/** The summed term of the projectors and torches that reach the actor's centre with an open line, every side facing. */
	float SpotR, SpotG, SpotB;
	/** The simulation's level for the actor lit from every side, animated lights at their mean (0 black, at most 2). */
	float Level;
	/** The same on the side that faces the viewer, now: what the bots' sight uses. */
	float LevelFacing;
	int32 Zone;
	/** How many of the four lights are filled, and how many projectors and torches are in the Spot term. */
	int32 Count, SpotCount;
	/** How many of the filled lights have a blocked line to the centre. */
	int32 Occluded;
	FScctRigLight Lights[ScctLightRigLights];
};
static_assert(sizeof(FScctLightRig) == 176, "FScctLightRig must match BridgeLightRig");

/** The own spy's projected shadow as the original's light cache has it. */
struct FScctShadowLighting
{
	/** 1 while the local player's pawn is a living spy. */
	int32 Valid;
	/** The shadow's darkness, 0 (no shadow) to 255. */
	int32 Darkness;
	/** 1 when the sticky camera's second view solve runs this tick by the original's rule (the darkness before was not 0). */
	int32 ViewUpdate;
	/** 1 when the session's sticky camera follows that rule (SetStickyCamShadow). */
	int32 Feeds;
	/** The unit direction from the shadow's origin towards the weighted place of the lights, and the origin. */
	float DirX, DirY, DirZ;
	float OriginX, OriginY, OriginZ;
};
static_assert(sizeof(FScctShadowLighting) == 40, "FScctShadowLighting must match BridgeShadowLighting");

struct FScctLightSquint
{
	/** 1 with a local player controller. */
	int32 Valid;
	/** SquintingFactor, 0 to 0.4 (a merc standing still with the torch off). */
	float Factor;
	/** What the original adds to every light term in normal view for it, at most 10 / 255. */
	float Term;
};
static_assert(sizeof(FScctLightSquint) == 12, "FScctLightSquint must match BridgeLightSquint");

/** One living pawn's light by the simulation. */
struct FScctLightAuditRow
{
	int32 ActorId;
	/** 0 spy, 1 merc. */
	int32 Kind;
	/** 1 the local player's pawn. */
	int32 Flags;
	int32 Zone;
	float X, Y, Z;
	float Level, LevelFacing;
	/** Luminance of the zone ambient and of the projector and torch term. */
	float Ambient, Spot;
	/** Lights in the rig, and how many of them have a blocked line. */
	int32 Lights, Occluded;
};
static_assert(sizeof(FScctLightAuditRow) == 52, "FScctLightAuditRow must match BridgeLightAudit");

struct FScctLightPlanApi
{
	int32 Version;

	/** Writes the map's light data in short; 1 when written. */
	int32 (__cdecl* GetLevel)(FScctLightLevel* OutLevel);

	/** Writes the map's lights in the file's order (a rig's light id is the index here). Returns how many there are; null asks for the count. */
	int32 (__cdecl* GetLights)(FScctLightInfo* OutLights, int32 Capacity);

	/** Writes the map's zones. Returns how many there are. */
	int32 (__cdecl* GetZones)(FScctLightZone* OutZones, int32 Capacity);

	/** The zone index of a point by the collision's BSP, -1 unknown. */
	int32 (__cdecl* ZoneAt)(float X, float Y, float Z);

	/** The rig of the pawn with this simulation actor id (FScctPawn::ActorId), seen from the viewer; 1 when written, 0 without such a pawn. */
	int32 (__cdecl* GetActorRig)(int32 ActorId, float ViewerX, float ViewerY, float ViewerZ, int32 Options, FScctLightRig* OutRig);

	/** The rig of anything actor-lit whose bounding sphere is ((X, Y, Z), Radius): a view model, a mover, a prop, a probe; 1 when written. */
	int32 (__cdecl* GetPointRig)(float X, float Y, float Z, float Radius, float ViewerX, float ViewerY, float ViewerZ, int32 Options, FScctLightRig* OutRig);

	/** Writes the own spy's shadow darkness as of the last tick; 1 when written. */
	int32 (__cdecl* GetShadowLighting)(FScctShadowLighting* OutShadow);

	/** Writes the local merc's squint; 1 when written. */
	int32 (__cdecl* GetSquint)(FScctLightSquint* OutSquint);

	/** Writes one row per living pawn, its facing level towards the viewer (the camera). Returns how many pawns there are. */
	int32 (__cdecl* GetAudit)(float ViewerX, float ViewerY, float ViewerZ, FScctLightAuditRow* OutRows, int32 Capacity);

	/**
	 * 1: the sticky camera's fly-in follows the original's shadow rule (solved twice a tick only while the own spy's
	 * shadow is drawn). 0: the session's default (always twice). Returns what is set now.
	 */
	int32 (__cdecl* SetStickyCamShadow)(int32 bOn);
};
