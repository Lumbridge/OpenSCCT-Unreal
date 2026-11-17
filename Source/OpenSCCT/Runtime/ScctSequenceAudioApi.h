#pragma once

#include "CoreMinimal.h"

// The C# bridge's sequence audio table: the audio segments the client world's animation sequences play at this moment
// of their replicated clocks, read each frame like the ambient sounds. Mirrors
// Bridge/OpenScct.UnrealBridge/Events/SequenceAudioExports.cs; change both together and bump the version. Fetched with
// ScctFeature<FScctSequenceAudioApi>() (Runtime/ScctFeatures.h).

constexpr int32 ScctSequenceAudioApiVersion = 1;

namespace EScctSequenceSoundFlags
{
	/** The segment's sound is a 3D sound; otherwise it plays flat. */
	constexpr int32 Positional = 1;
	/** The track's target actor is not in the map: the sound comes from the controller's own place. */
	constexpr int32 AtController = 2;
	/** The library does not know the segment's sound (FileId is 0). */
	constexpr int32 Unknown = 4;
}

struct FScctSequenceSound
{
	/** The segment instance's key (its controller, the sequence's generation and the segment), never reused in a session. */
	int32 Key;
	/** The segment instance's id text (FScctEventsApi::GetName), for the log. */
	int32 NameId;
	/** The sound's name id (FScctEventsApi::GetName). */
	int32 SoundId;
	/** The file it plays (the events table's file ids), 0 when the sound has none. */
	int32 FileId;
	/** The track's target actor, 0 when it plays from the controller. */
	int32 ActorId;
	/** EScctSequenceSoundFlags. */
	int32 Flags;
	/** The AnimationController that plays the sequence. */
	int32 ControllerId;
	/** Where it sounds from now (the target actor, else the controller). */
	float X;
	float Y;
	float Z;
	/** How far into its file the segment is now, in the file's seconds. */
	float Offset;
	/** The segment's playback rate (its voice's pitch). */
	float Rate;
	/** Its volume (the sound's and its file's gain), 0..1. */
	float Volume;
	/** The sound's radius (its minimum distance), 0 for the default. */
	float Radius;
	/** The controller's replicated sequence clock (PlaybackTime as last published by the server), seconds. */
	float Clock;
	/** When the segment starts in the sequence and where in its file it starts, seconds (for the log). */
	float SegmentStart;
	float SegmentFileOffset;
};

static_assert(sizeof(FScctSequenceSound) == 68, "FScctSequenceSound must match BridgeSequenceSound");

struct FScctSequenceAudioApi
{
	int32 Version;

	/** Fills up to Capacity segments sounding now. Returns the count (0 with no session or no sound library). */
	int32 (__cdecl* GetSounds)(FScctSequenceSound* OutSounds, int32 Capacity);

	/** For a check: fires the event Tag on the local match's server, which starts the controllers of that slot. 1 when queued. */
	int32 (__cdecl* TriggerForCheck)(const TCHAR* Tag);

	/** Distinct segment sounds the library did not know, since the session started. */
	int32 (__cdecl* GetUnknownCount)();
};
