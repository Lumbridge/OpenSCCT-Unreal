#include "Play/ScctAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctAudioMix.h"
#include "Runtime/ScctFeatures.h"

// The audio tracks of the animation sequences, as the Godot client's AnimationSequenceAudio plays them: the bridge lists
// every audio segment a sequence plays at this moment of its replicated clock (Bridge Events/SequenceAudio.cs), and
// each gets one voice, started at the segment's offset into its file, following its target, put back in step when it
// drifts and stopped when the segment is no longer listed. A client restores a sequence's clock from the server's
// replicated PlaybackTime, so a player who joins in the middle of a sequence starts its segments part-way through.

namespace
{
	/** AnimationSequenceAudio: a segment whose voice is further than this from the clock's offset is sought to it. */
	constexpr float ScctSequenceAudioMaxDrift = 0.25f;
	/**
	 * The minimum distance of a 3D segment: AnimationSequenceAudio gives its players UnitSize 1000 (metres) and no maximum
	 * distance, whatever the sound's radius, so a scripted sequence's soundtrack is heard at full volume across a map and
	 * only placed (panned, in its room's reverb) by its target. 1000 m is 100000 uu.
	 */
	constexpr float ScctSequenceAudioUnitSize = 100000.0f;
	/** The voice pitch range the engine plays (the segment's rate is its pitch). */
	constexpr float ScctSequenceAudioMinPitch = 0.4f;
	constexpr float ScctSequenceAudioMaxPitch = 2.0f;
	/** A segment that is no longer listed fades out over this (a cut, not a tail: the sequence stopped it). */
	constexpr float ScctSequenceAudioStopFade = 0.1f;
	/** -ScctSequenceAudioDrift slips a voice this long after it starts. */
	constexpr float ScctSequenceAudioDriftAfter = 2.0f;
	constexpr int32 ScctSequenceAudioCapacity = 64;
}

void UScctAudioSubsystem::ReadSequenceSwitches()
{
	FString Error;
	SequenceApi = ScctFeature<FScctSequenceAudioApi>(&Error);
	UE_CLOG(SequenceApi == nullptr, LogOpenSCCT, Warning, TEXT("Audio: animation sequences play no sound this match, the bridge's sequence audio table is missing: %s"), *Error);
	SequenceBuffer.SetNumUninitialized(ScctSequenceAudioCapacity);
	SequenceSegments.Reset();
	SequenceClock = 0.0f;

	SequenceTriggerTag.Reset();
	SequenceTriggerIn = -1.0f;
	FString Trigger;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctSequenceAudioTrigger="), Trigger, false))
	{
		FString At;
		SequenceTriggerIn = 2.0f;
		if (Trigger.Split(TEXT("@"), &Trigger, &At))
		{
			SequenceTriggerIn = FMath::Max(FCString::Atof(*At), 0.0f);
		}
		SequenceTriggerTag = Trigger.TrimStartAndEnd();
	}
	SequenceJoinAt = 0.0f;
	FParse::Value(FCommandLine::Get(), TEXT("ScctSequenceAudioJoinAt="), SequenceJoinAt);
	SequenceDrift = 0.0f;
	FParse::Value(FCommandLine::Get(), TEXT("ScctSequenceAudioDrift="), SequenceDrift);
	UE_CLOG(SequenceJoinAt > 0.0f, LogOpenSCCT, Log, TEXT("ScctAudio: sequence audio is read from %.1f s into the match only, as for a player who joins then"), SequenceJoinAt);
	UE_CLOG(SequenceDrift != 0.0f, LogOpenSCCT, Log, TEXT("ScctAudio: every sequence voice slips %.2f s once, %.0f s after it starts"), SequenceDrift, ScctSequenceAudioDriftAfter);
}

void UScctAudioSubsystem::UpdateSequenceAudio(float DeltaSeconds)
{
	if (SequenceApi == nullptr)
	{
		return;
	}

	SequenceClock += DeltaSeconds;
	if (SequenceTriggerIn >= 0.0f && SequenceClock >= SequenceTriggerIn)
	{
		SequenceTriggerIn = -1.0f;
		const int32 Queued = SequenceApi->TriggerForCheck(*SequenceTriggerTag);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: -ScctSequenceAudioTrigger: '%s' %s at %.1f s"), *SequenceTriggerTag,
			Queued != 0 ? TEXT("fired on the local server") : TEXT("not fired (no local server)"), SequenceClock);
	}
	if (SequenceClock < SequenceJoinAt)
	{
		// -ScctSequenceAudioJoinAt: this player has not arrived yet.
		return;
	}

	for (TPair<int32, FSequenceSegment>& Pair : SequenceSegments)
	{
		Pair.Value.bSeen = false;
	}

	const int32 Count = SequenceApi->GetSounds(SequenceBuffer.GetData(), SequenceBuffer.Num());
	PeakSequenceSegments = FMath::Max(PeakSequenceSegments, Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctSequenceSound& Record = SequenceBuffer[Index];
		FSequenceSegment& Segment = SequenceSegments.FindOrAdd(Record.Key);
		Segment.Last = Record;
		Segment.bSeen = true;
		if (Segment.bEnded)
		{
			continue;
		}

		FVoice* Voice = Segment.VoiceIndex != INDEX_NONE && Voices.IsValidIndex(Segment.VoiceIndex) && Voices[Segment.VoiceIndex].SequenceKey == Record.Key
			&& Voices[Segment.VoiceIndex].State != EVoiceState::Fading ? &Voices[Segment.VoiceIndex] : nullptr;
		if (Voice == nullptr)
		{
			if (Segment.VoiceIndex != INDEX_NONE)
			{
				// Its voice is over while the segment still runs: the file ended (the segment is longer than what is
				// left of it), it failed to load or had nothing left to play. It is not started again.
				Segment.VoiceIndex = INDEX_NONE;
				Segment.bEnded = true;
				++NumSequenceEnded;
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: sequence seg=%s voice over before its segment (offset now %.2f s)"), *GetName(Record.NameId), Record.Offset);
				continue;
			}
			if (Record.FileId == 0)
			{
				Segment.bEnded = true;
				++NumSequenceNoFile;
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctAudio: sequence seg=%s sound '%s' has no file%s: the segment is silent"), *GetName(Record.NameId), *GetName(Record.SoundId),
					(Record.Flags & EScctSequenceSoundFlags::Unknown) != 0 ? TEXT(" (the library does not know it)") : TEXT(""));
				continue;
			}

			FVoice New;
			New.SequenceKey = Record.Key;
			New.ActorId = Record.ActorId;
			New.SoundId = Record.SoundId;
			New.LeafId = Record.SoundId;
			New.FileId = Record.FileId;
			New.Location = FVector(Record.X, Record.Y, Record.Z);
			New.Volume = FMath::Clamp(Record.Volume, 0.0f, 1.0f);
			New.Radius = Record.Radius;
			New.MinDistance = FMath::Max(Record.Radius, ScctSequenceAudioUnitSize);
			// AnimationSequenceAudio: the segment's rate is its player's pitch.
			New.Pitch = FMath::Clamp(Record.Rate, ScctSequenceAudioMinPitch, ScctSequenceAudioMaxPitch);
			New.bPositional = (Record.Flags & EScctSequenceSoundFlags::Positional) != 0;
			New.Category = static_cast<uint8>(EScctAudioCategory::World);
			New.LifeLeft = -1.0f;
			New.SequenceOffset = Record.Offset;
			Segment.VoiceIndex = Voices.Add(New);
			FScctPcmPtr Pcm;
			Cache.Request(Record.FileId, GetFilePath(Record.FileId), Pcm);
			continue;
		}

		// The segment's place and loudness follow the bridge; its offset is where the clock says it should be.
		if (Voice->bPositional)
		{
			Voice->Location = FVector(Record.X, Record.Y, Record.Z);
		}
		Voice->Volume = FMath::Clamp(Record.Volume, 0.0f, 1.0f);
		Voice->SequenceOffset = Record.Offset;
		if (Voice->State != EVoiceState::Playing)
		{
			continue;
		}

		// Where this voice is in its file by its own playback: the engine renders it in real time at its pitch, so the
		// wall clock since it started (or was last sought) says where, however long the game's frames are.
		const double Played = FPlatformTime::Seconds() - Voice->SequenceStartedAt;
		Voice->SequencePosition = Voice->StartOffset + static_cast<float>(Played) * Voice->Pitch + Voice->SequenceSlip;
		if (SequenceDrift != 0.0f && !Voice->bSequenceDrifted && Played >= ScctSequenceAudioDriftAfter)
		{
			// -ScctSequenceAudioDrift: as if its playback had slipped (a hitch, a clock correction from the server).
			Voice->bSequenceDrifted = true;
			Voice->SequenceSlip = SequenceDrift;
			Voice->SequencePosition += SequenceDrift;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: sequence seg=%s -ScctSequenceAudioDrift: playback slipped %.2f s (now %.2f s, the clock says %.2f s)"),
				*GetName(Record.NameId), SequenceDrift, Voice->SequencePosition, Record.Offset);
		}
		const float Drift = Voice->SequencePosition - Record.Offset;
		if (FMath::Abs(Drift) > ScctSequenceAudioMaxDrift)
		{
			// AnimationSequenceAudio: Seek(offset) when the playback is more than 0.25 s off.
			++NumSequenceSeeks;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: sequence seg=%s seek from %.2f s to %.2f s (drift %+.2f s, clock %.2f)"),
				*GetName(Record.NameId), Voice->SequencePosition, Record.Offset, Drift, Record.Clock);
			if (Record.Offset >= Voice->FileSeconds)
			{
				FadeOutVoice(*Voice, ScctSequenceAudioStopFade, TEXT("sequence segment past its file's end"));
				continue;
			}
			Voice->SequencePosition = Record.Offset;
			Voice->StartOffset = Record.Offset;
			Voice->SequenceStartedAt = FPlatformTime::Seconds();
			Voice->SequenceSlip = 0.0f;
			Voice->LifeLeft = (Voice->FileSeconds - Record.Offset) / Voice->Pitch + 0.1f;
			if (UAudioComponent* Component = Voice->Component.Get())
			{
				Component->Play(Record.Offset);
			}
		}
	}

	// A segment no longer listed (its time is over, its sequence stopped or restarted) stops.
	for (auto It = SequenceSegments.CreateIterator(); It; ++It)
	{
		FSequenceSegment& Segment = It.Value();
		if (Segment.bSeen)
		{
			continue;
		}
		if (Segment.VoiceIndex != INDEX_NONE && Voices.IsValidIndex(Segment.VoiceIndex) && Voices[Segment.VoiceIndex].SequenceKey == It.Key())
		{
			FVoice& Voice = Voices[Segment.VoiceIndex];
			if (Voice.State != EVoiceState::Fading)
			{
				++NumSequenceStopped;
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: sequence seg=%s stopped: no longer listed (last offset %.2f s, its voice at %.2f s)"),
					*GetName(Segment.Last.NameId), Segment.Last.Offset, Voice.SequencePosition);
				FadeOutVoice(Voice, ScctSequenceAudioStopFade, TEXT("sequence segment over"));
			}
		}
		It.RemoveCurrent();
	}
}

bool UScctAudioSubsystem::PrepareSequenceStart(FVoice& Voice, float FileSeconds)
{
	const FSequenceSegment* Segment = SequenceSegments.Find(Voice.SequenceKey);
	const FScctSequenceSound Last = Segment != nullptr ? Segment->Last : FScctSequenceSound{};
	// The clock moved on while the file was decoded: the segment starts where it is now.
	const float Offset = Segment != nullptr ? Last.Offset : Voice.SequenceOffset;
	Voice.FileSeconds = FileSeconds;
	if (Offset >= FileSeconds)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: sequence seg=%s not started: offset %.2f s is past its file's %.2f s"), *GetName(Last.NameId), Offset, FileSeconds);
		return false;
	}

	Voice.StartOffset = FMath::Max(Offset, 0.0f);
	Voice.SequencePosition = Voice.StartOffset;
	Voice.SequenceStartedAt = FPlatformTime::Seconds();
	Voice.SequenceSlip = 0.0f;
	++NumSequenceStarted;
	// The clock's own offset (the segment's file offset plus its time so far on the replicated clock x its rate), which
	// the start offset matches within the few ticks a published clock lags.
	const float ByClock = Last.SegmentFileOffset + (Last.Clock - Last.SegmentStart) * Last.Rate;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: sequence seg=%s start offset=%.2f s of %.2f s (sequence clock %.2f s: segment from %.2f s at file %.2f s, rate %.2f, so %.2f s by the clock) '%s' %s at (%.0f, %.0f, %.0f)%s, %.0f s into the match"),
		*GetName(Last.NameId), Voice.StartOffset, FileSeconds, Last.Clock, Last.SegmentStart, Last.SegmentFileOffset, Last.Rate, ByClock, *GetName(Voice.SoundId),
		Voice.bPositional ? TEXT("3D") : TEXT("flat"), Voice.Location.X, Voice.Location.Y, Voice.Location.Z,
		(Last.Flags & EScctSequenceSoundFlags::AtController) != 0 ? TEXT(" (the controller's place: no target actor)") : *FString::Printf(TEXT(" on actor %d"), Last.ActorId), SequenceClock);
	return true;
}

void UScctAudioSubsystem::LogSequenceSummary() const
{
	if (SequenceApi == nullptr)
	{
		return;
	}
	int32 Sounding = 0;
	for (const FVoice& Voice : Voices)
	{
		Sounding += Voice.SequenceKey != 0 && Voice.State == EVoiceState::Playing ? 1 : 0;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Audio summary: animation sequences: %d segments listed now (peak %d), %d voices sounding; since the match began %d started, %d sought back in step, %d stopped with their segment, %d over before it, %d without a file, %d sounds unknown"),
		SequenceSegments.Num(), PeakSequenceSegments, Sounding, NumSequenceStarted, NumSequenceSeeks, NumSequenceStopped, NumSequenceEnded, NumSequenceNoFile,
		SequenceApi->GetUnknownCount != nullptr ? SequenceApi->GetUnknownCount() : 0);
}
