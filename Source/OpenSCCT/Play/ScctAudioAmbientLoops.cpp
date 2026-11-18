// The flat (2D) ambient loops of actors that are no zones, for UScctAudioSubsystem: the original's rule 22 as the
// Godot client plays it (SoundSystem.Update2DActorLoop, read from the original hosting MoonD: all seven park speakers
// hold a voice of the organ, each at its own distance volume, and the voices add up).
//
//   - An actor whose loop plays keeps it, at the actor's own volume inside the radius and falling off beyond it, until
//     that is under 0.05.
//   - An actor without a loop is a candidate when the unguarded formula (which rises towards the actor inside the
//     radius) gives 0.05. If a flat loop of the same sound already played before this update, the candidate takes it
//     over when it is louder than that loop's actor, and otherwise does nothing; if none played, it starts its own.
//   - Candidates are only compared with loops that played before this update, so the actors that become audible
//     together each start one.
//   - The loop of the zone the listener stands in is never taken by an actor; that zone takes over an actor's loop of
//     its sound, and the loop of a zone the listener left is taken over by a candidate. A loop handed over carries on
//     from where it was.
//
// A voice is matched to its actor by the actor's key and to "the same sound" by the sound the actors name, never by
// the file: a random list is picked per source, so two sources of one sound may hold different files, and a voice
// handed over keeps the file it plays. Matching by file made such sources take the voice from each other every frame.
//
// Not as Godot: the record's volume already has the gain of the file picked (Godot's candidate test uses the gain of
// the sound named), and a candidate whose playing volume would be under 0.05 does not start (Godot starts it and
// stops it again on the next update).

#include "OpenSCCT.h"
#include "Play/ScctAudioSubsystem.h"

namespace
{
	/** The original's Rolloff (rule 22). */
	constexpr float ScctFlatLoopRolloff = 0.5f;

	/** The volume of a playing flat loop: the source's own inside its radius, falling off beyond it. */
	float ScctFlatLoopPlayingVolume(float Volume, float Distance, float Radius)
	{
		return Radius > 0.0f && Distance > Radius ? Volume / (((Distance - Radius) / Radius) * ScctFlatLoopRolloff + 1.0f) : Volume;
	}

	/** The volume the start and takeover tests use: the same formula without the guard, so it rises inside the radius. */
	float ScctFlatLoopCandidateVolume(float Volume, float Distance, float Radius)
	{
		return Radius > 0.0f ? Volume / (((Distance - Radius) / Radius) * ScctFlatLoopRolloff + 1.0f) : Volume;
	}

	bool ScctIsFlatZoneLoop(int64 AmbientId)
	{
		return (AmbientId >> 32) == (1ll << 8);
	}

	bool ScctIsFlatActorLoop(int64 AmbientId)
	{
		return (AmbientId >> 32) == (1ll << 9);
	}
}

static_assert((1ll << 40) >> 32 == (1ll << 8) && (1ll << 41) >> 32 == (1ll << 9), "the flat loop keys' bits");

int32 UScctAudioSubsystem::BeginFlatLoopPass(int32 Count)
{
	if (bLogVoices && NumFlatActorSources != LoggedFlatActorSources)
	{
		int32 Held = 0;
		for (const TPair<int64, int32>& Pair : AmbientVoices)
		{
			Held += ScctIsFlatActorLoop(Pair.Key) ? 1 : 0;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Audio: %d flat ambient sources that are no zones are listed (%d before), %d hold a voice of their own"), NumFlatActorSources,
			FMath::Max(LoggedFlatActorSources, 0), Held);
		LoggedFlatActorSources = NumFlatActorSources;
	}
	NumFlatActorSources = 0;
	++AmbientPass;
	ListenerZoneSounds.Reset();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctAmbientSound& Source = AmbientBuffer[Index];
		const int32 InZone = EScctAmbientFlags::Zone | EScctAmbientFlags::ListenerInZone;
		if ((Source.Flags & InZone) == InZone && (Source.Flags & EScctAmbientFlags::Positional) == 0 && Source.FileId != 0 && Source.Volume > 0.0f)
		{
			ListenerZoneSounds.AddUnique(Source.SoundId);
		}
	}
	return Count;
}

bool UScctAudioSubsystem::UpdateFlatActorLoop(const FScctAmbientSound& Source, float Distance, int64& OutId, float& OutVolume)
{
	const int64 Id = FlatActorLoopBit | static_cast<uint32>(Source.Key);
	const float Playing = ScctFlatLoopPlayingVolume(Source.Volume, Distance, Source.Radius);
	OutId = Id;
	OutVolume = Playing;
	++NumFlatActorSources;

	if (const int32* Found = AmbientVoices.Find(Id))
	{
		FVoice& Own = Voices[*Found];
		if (Own.SoundId == Source.SoundId && Own.State != EVoiceState::Fading)
		{
			if (Playing >= FlatLoopSilence)
			{
				Own.Volume = Playing;
				Own.Location = FVector(Source.X, Source.Y, Source.Z);
				Own.bSeen = true;
			}
			// Else it is not seen this pass, and the pass's end fades it out.
			return false;
		}
		// The actor now names another sound: its loop starts afresh.
		FadeOutVoice(Own, FlatLoopFadeSeconds, TEXT("ambient source changed"));
		Own.AmbientId = 0;
		AmbientVoices.Remove(Id);
	}

	const float Candidate = ScctFlatLoopCandidateVolume(Source.Volume, Distance, Source.Radius);
	if (Candidate < FlatLoopSilence || Playing < FlatLoopSilence)
	{
		return false;
	}

	// A flat loop of the same sound that already played before this update.
	int64 OtherId = 0;
	int32 OtherIndex = INDEX_NONE;
	for (const TPair<int64, int32>& Pair : AmbientVoices)
	{
		const FVoice& Other = Voices[Pair.Value];
		if (Pair.Key != Id && !Other.bPositional && (ScctIsFlatActorLoop(Pair.Key) || ScctIsFlatZoneLoop(Pair.Key)) && Other.SoundId == Source.SoundId &&
			Other.AmbientPass != AmbientPass && (Other.State == EVoiceState::Playing || Other.State == EVoiceState::Waiting))
		{
			OtherId = Pair.Key;
			OtherIndex = Pair.Value;
			break;
		}
	}
	if (OtherIndex == INDEX_NONE)
	{
		// None plays: the actor starts its own.
		return true;
	}

	FVoice& Other = Voices[OtherIndex];
	if (ScctIsFlatZoneLoop(OtherId))
	{
		if (ListenerZoneSounds.Contains(Source.SoundId))
		{
			// The listener's zone keeps a sound it plays.
			return false;
		}
	}
	else
	{
		for (int32 Index = 0; Index < AmbientCount; ++Index)
		{
			const FScctAmbientSound& Theirs = AmbientBuffer[Index];
			if (Theirs.Key == Other.ActorId && Theirs.SoundId == Source.SoundId && (Theirs.Flags & (EScctAmbientFlags::Positional | EScctAmbientFlags::Zone)) == 0)
			{
				const float TheirDistance = FVector::Dist(FVector(Theirs.X, Theirs.Y, Theirs.Z), Listener);
				if (Candidate <= ScctFlatLoopCandidateVolume(Theirs.Volume, TheirDistance, Theirs.Radius))
				{
					return false;
				}
				break;
			}
		}
	}

	// The loop carries on for this actor from where it was.
	AmbientVoices.Remove(OtherId);
	if (bLogVoices)
	{
		LogVoice(TEXT("handed over"), Other, *FString::Printf(TEXT(" (to actor %d, volume %.2f)"), Source.Key, Playing));
	}
	Other.AmbientId = Id;
	Other.ActorId = Source.Key;
	Other.Location = FVector(Source.X, Source.Y, Source.Z);
	Other.Volume = Playing;
	Other.Radius = Source.Radius;
	Other.bSeen = true;
	AmbientVoices.Add(Id, OtherIndex);
	return false;
}

bool UScctAudioSubsystem::HandFlatLoopToZone(const FScctAmbientSound& Source, int64 ZoneId)
{
	int64 OtherId = 0;
	int32 OtherIndex = INDEX_NONE;
	for (const TPair<int64, int32>& Pair : AmbientVoices)
	{
		const FVoice& Other = Voices[Pair.Value];
		if (ScctIsFlatActorLoop(Pair.Key) && !Other.bPositional && Other.SoundId == Source.SoundId && Other.AmbientPass != AmbientPass &&
			(Other.State == EVoiceState::Playing || Other.State == EVoiceState::Waiting))
		{
			OtherId = Pair.Key;
			OtherIndex = Pair.Value;
			break;
		}
	}
	if (OtherIndex == INDEX_NONE)
	{
		return false;
	}

	FVoice& Other = Voices[OtherIndex];
	AmbientVoices.Remove(OtherId);
	if (bLogVoices)
	{
		LogVoice(TEXT("handed over"), Other, TEXT(" (to the listener's zone)"));
	}
	Other.AmbientId = ZoneId;
	Other.ActorId = Source.Key;
	Other.Location = FVector(Source.X, Source.Y, Source.Z);
	Other.Volume = Source.Volume;
	Other.Radius = Source.Radius;
	Other.bSeen = true;
	AmbientVoices.Add(ZoneId, OtherIndex);
	return true;
}
