#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FScctHubActivities;
class UAudioComponent;
class USoundWaveProcedural;
class UWorld;

/**
 * The jukebox's music: four procedural loops of our own (a pad, a bass line, an arpeggio and a soft hi-hat over a
 * four-chord progression), rendered sample by sample. A port of the Godot client's HubSynth (game/Scripts/Hub/HubAudio.cs),
 * note for note: the server's track number picks the piece (HubJukeboxTracks names them).
 */
class FScctHubSynth
{
public:
	explicit FScctHubSynth(int32 InRate) : Rate(InRate) {}

	int32 GetTrack() const { return Track; }
	void SetTrack(int32 Value);

	/** Frames of interleaved stereo into Out (2 x Frames floats), at Gain. */
	void Render(float* Out, int32 Frames, float Gain);

private:
	int32 Rate;
	float PadPhase[4] = {};
	float BassPhase = 0.0f, ArpPhase = 0.0f;
	float BassEnv = 0.0f, ArpEnv = 0.0f, HatEnv = 0.0f;
	float BassFreq = 0.0f, ArpFreq = 0.0f;
	uint32 Noise = 0x1234567u;
	int64 Sample = 0;
	int32 LastEighth = -1;
	int32 Track = 0;
	float LowPass = 0.0f;
};

/** The activities' chimes: short runs of bell-like notes (a sine and its octave with a quick decay). Port of HubChimes. */
class FScctHubChimes
{
public:
	explicit FScctHubChimes(int32 InRate) : Rate(InRate) {}

	/** A chime for an activity cue ("start", "cp", "fall", "finish", "best", "hit", "head", "end", "fanfare"; anything else a single note). */
	void Play(const FString& Cue);

	void Render(float* Out, int32 Frames);

	int32 GetNumPlayed() const { return NumPlayed; }

private:
	struct FNote
	{
		float Freq;
		int64 Start;
		float Gain;
	};
	int32 Rate;
	TArray<FNote> Notes;
	int64 Sample = 0;
	int32 NumPlayed = 0;
};

/**
 * The hub's own sounds (an OpenSCCT addition; port of the Godot client's game/Scripts/Hub/HubAudio.cs): the jukebox's
 * music and the activities' chimes, all made by the small synthesiser above (no recorded audio, no samples, nothing
 * from the game). Each plays through a USoundWaveProcedural the game thread keeps about a quarter of a second ahead
 * of the mixer (no call into the bridge from the audio thread):
 *   - the music sounds from the jukebox (a 3D source there, panned by the engine, its distance fall-off ours): full
 *     from HubPlayK.JukeboxUseRange (384 uu), squared down to nothing at HubPlayK.JukeboxRange (1600 uu), at most 0.55,
 *     eased in and out as in Godot; the track is the one the server plays (the jukebox's Use changes it);
 *   - the chimes play flat for the activity cues the bridge raised this frame (gates, hits, the end of a run or a round,
 *     a fanfare for a record).
 * Volumes: the Audio page's "Jukebox music" and "Activity chimes" (Audio.HubJukebox, Audio.HubChimes).
 * Every audio summary (UScctAudioSubsystem, -ScctAudioSummary) gets a note of how much of each the mixer has played.
 *
 * Unattended switch: -ScctHubJukebox   the music is heard as if the ear stood at the jukebox (full volume wherever
 *                                     the camera is), so a run anywhere in the hub plays it
 */
class FScctHubAudio : public TSharedFromThis<FScctHubAudio>
{
public:
	explicit FScctHubAudio(TSharedPtr<FScctHubActivities> InActivities);
	~FScctHubAudio();

	void Tick(UWorld* World, float DeltaSeconds, bool bVerbose);
	void Stop();

private:
	struct FStream
	{
		TStrongObjectPtr<USoundWaveProcedural> Wave;
		TWeakObjectPtr<UAudioComponent> Component;
		int64 QueuedFrames = 0;
	};

	bool EnsureStreams(UWorld* World);
	void MakeStream(UWorld* World, FStream& Stream, bool bMusic, const TCHAR* Name);

	/** Tops the stream up to the lead and returns the frames the mixer has taken from it so far. */
	int64 Fill(FStream& Stream, TFunctionRef<void(float*, int32)> Render);

	void AddSummaryNotes(TArray<FString>& Notes) const;

	TSharedPtr<FScctHubActivities> Activities;
	FScctHubSynth Synth;
	FScctHubChimes Chimes;

	TWeakObjectPtr<UWorld> StreamWorld;
	bool bNoDevice = false;
	FStream Music;
	FStream Chime;

	float Gain = 0.0f;
	float TargetGain = 0.0f;
	float JukeboxDistance = -1.0f;
	bool bAtJukebox = false;
	FVector JukeboxAt = FVector::ZeroVector;
	bool bHasJukebox = false;

	int64 MusicPlayedFrames = 0;
	int64 ChimePlayedFrames = 0;
	int32 Underruns = 0;

	TArray<float> Scratch;
	TArray<int16> Pcm;
	FDelegateHandle SummaryHandle;
	float LogTimer = 0.0f;
};
