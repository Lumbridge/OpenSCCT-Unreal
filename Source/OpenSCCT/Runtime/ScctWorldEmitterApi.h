#pragma once

#include "CoreMinimal.h"

// The map's placed Emitters that ride another actor: a mover's rider (AttachTag) or one hung on a bone of a skeletal
// prop (ActorsToAttach: the Club's falling fuse boxes, the Missile base's doors, the Orphanage's generator) or of a
// pawn. The level state moves the importer's particle actors of each with it. Mirrors
// Bridge/OpenScct.UnrealBridge/Devices/WorldEmitterExports.cs; change both together.

constexpr int32 ScctWorldEmitterApiVersion = 1;

namespace EScctWorldEmitterFlags
{
	/** Based on a mover or another actor that is not world geometry: X..Roll is its place now. */
	constexpr int32 Rides = 1;
	/** Hung on bone AttachBoneId of the skeletal prop AttachNameId (FScctWorldAnimated::NameId): X..Roll relative to the drawn bone. */
	constexpr int32 OnBone = 2;
	/** Hung on bone AttachBoneId of pawn AttachPawnId: X..Roll relative to the drawn bone. */
	constexpr int32 OnPawn = 4;
}

struct FScctWorldEmitter
{
	int32 NameId;          // the Emitter's map name in the world table (its particle actors carry "Scct:" + it)
	int32 Flags;           // EScctWorldEmitterFlags
	float X, Y, Z;         // its place blended between the last two ticks; with OnBone/OnPawn its RelativeLocation
	float Pitch, Yaw, Roll; // degrees; with OnBone/OnPawn its RelativeRotation
	float PlacedX, PlacedY, PlacedZ; // where the map placed it (the frame its particle actors were imported in)
	float PlacedPitch, PlacedYaw, PlacedRoll;
	int32 AttachNameId;    // world table name id of what it rides (mover or prop), 0 when that actor is not followed
	int32 AttachBoneId;    // with OnBone/OnPawn: name id of the bone
	int32 AttachPawnId;    // with OnPawn: FScctBridgePawn::ActorId
};

static_assert(sizeof(FScctWorldEmitter) == 68, "FScctWorldEmitter must match BridgeWorldEmitter");

struct FScctWorldEmitterApi
{
	int32 Version;

	/** Writes the Emitters riding another actor this frame; returns how many there are (only Capacity written). */
	int32 (__cdecl* GetAttachedEmitters)(FScctWorldEmitter* OutEmitters, int32 Capacity);
};
