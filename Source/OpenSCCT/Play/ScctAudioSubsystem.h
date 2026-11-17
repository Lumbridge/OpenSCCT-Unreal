#pragma once

#include "CoreMinimal.h"
#include "Play/ScctSoundData.h"
#include "Runtime/ScctEventsApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctAudioSubsystem.generated.h"

class AScctPawnProxy;
class UAudioComponent;
class UReverbEffect;
class UScctAudioMix;
class UScctSessionSubsystem;
class UScctSoundWave;
class USoundAttenuation;

/**
 * Plays the match's sound. The simulation decides what sounds and the bridge resolves each call to a file, a
 * place, a volume and a reach (Runtime/ScctEventsApi.h, FScctSound and FScctAmbientSound); this subsystem turns
 * those records into Unreal voices each frame, on the session subsystem's OnFrame:
 *
 *   - the files are the player's loose .wav and .ogg files, decoded on worker tasks and cached (FScctSoundCache);
 *   - every 3D voice is attenuated with the original's distance curve (full volume inside the minimum distance,
 *     MinDistance / (MinDistance + 0.5 x (d - MinDistance)) beyond it, with no cut-off: the bridge has already
 *     tested each call against the original's reach of 100 x its radius), follows its actor or its bone on the
 *     drawn pawn, and sends to the reverb more the further away it is;
 *   - the local player's own sounds play without 3D and send a fixed share to the room's reverb; flat sounds play
 *     without 3D and dry; music is one cross-faded track;
 *   - every voice plays in its category's sound class (Play/ScctAudioMix: the match's effects, the round's music,
 *     the objective jingles), which routes it to its submix; its gain is the player's volume of its kind
 *     (UScctUserSettings::GetGain: effects, ambience, music, objective), set on the voice; at objective volume 0 a
 *     jingle is not played;
 *   - the ambient loops of the map play while they are in reach (3D sources), while the listener is in their
 *     zone (zone beds, one voice per sound) or by distance (other flat sources, the loudest of each sound);
 *   - the reverb is the EAX environment of the zone the listener stands in, read from the map's audio table
 *     (svmap/<Map>/audio/audio.json "zones"), or a small neutral room where the zone has none.
 *
 * Without an audio device (-nosound) everything still runs except the Unreal voices themselves, so the
 * bookkeeping can be checked in a log.
 *
 * Unattended switches (off by default):
 *   -ScctAudioLog          log every voice that starts or stops, with file, place, distance and gains
 *   -ScctAudioSummary=S    log the voice counts every S seconds (default 10; 0 never)
 *   -ScctAudioMusic=Song[@S]  start Song as the round's music S seconds into the match (default 3), through the
 *                          bridge's PlayMusic path, so the music's volume can be checked on a map whose script
 *                          plays none early ("Amb_Aqua.Ambiances.aqua_musicA")
 * Play/ScctAudioMix has the volume switches (-ScctAudioVolume, -ScctAudioPeaks).
 */
UCLASS()
class OPENSCCT_API UScctAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Voices that sound or wait for their start, the ambient ones included. */
	int32 GetNumVoices() const { return Voices.Num(); }

	/** The original's gain of a 3D source at Distance: 1 inside MinDistance, falling with distance beyond it (Radius is not used: there is no cut-off). */
	static float DistanceGain(float Distance, float MinDistance, float Radius);

	/** Asked for notes of sounds played outside this subsystem (the hub's jukebox), appended to every audio summary. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSummary, TArray<FString>& /*Notes*/);
	static FOnSummary& OnSummary();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	enum class EVoiceState : uint8
	{
		Waiting,  // its delay runs or its file is still being decoded
		Playing,
		Dormant,  // an ambient one-shot that has played and waits for its repeat
		Fading,   // on its way out; gone when FadeLeft runs out
	};

	struct FVoice
	{
		// What the bridge said (see FScctSound).
		int32 CallId = 0;
		int32 Segment = 0;
		int32 ActorId = 0;
		int32 FollowActorId = 0;
		int32 FollowBoneId = 0;
		int32 SoundId = 0;
		int32 LeafId = 0;
		int32 FileId = 0;
		int32 Slot = 0;
		FVector Location = FVector::ZeroVector;
		float Volume = 1.0f;
		float Radius = 0.0f;
		float MinDistance = 0.0f;
		float Pitch = 1.0f;
		int32 LoopStart = INDEX_NONE;
		int32 LoopEnd = INDEX_NONE;
		float FadeIn = 0.0f;

		// Ambient voices: the key they are matched by between frames (0: not ambient) and the repeat of a one-shot.
		int64 AmbientId = 0;
		float RepeatInterval = 0.0f;
		float RepeatLeft = 0.0f;

		// Running state.
		float StartIn = 0.0f;   // seconds until it starts
		float LifeLeft = 0.0f;  // seconds until it ends by itself; negative: never (a loop)
		float FadeLeft = 0.0f;
		float AppliedVolume = -1.0f;
		EVoiceState State = EVoiceState::Waiting;
		bool bPositional = false;
		bool bAtListener = false;
		bool bLoop = false;
		bool bMusic = false;
		bool bNoOverride = false;
		/** Its volume category (EScctAudioCategory): the sound class and submix it plays through. */
		uint8 Category = 0;
		bool bSeen = false;     // ambient: listed and audible this frame
		TWeakObjectPtr<UAudioComponent> Component;
	};

	/** A zone of the map's audio table that has a reverb. */
	struct FZone
	{
		FString Id;
		FString EffectClass;
		FVector Location = FVector::ZeroVector;
		int32 Effect = INDEX_NONE; // index into ZoneEffects
		float DecayTime = 0.0f;
	};

	void HandleSessionChanged(int32 OldId, int32 NewId);
	void HandleMatchStarted();
	void HandleFrame(float DeltaSeconds);

	void DrainSounds();
	void Play(const FScctSound& Record);
	void Stop(const FScctSound& Record);
	void PlayMusic(const FScctSound& Record);
	void UpdateAmbient();
	void UpdateVoices(float DeltaSeconds);
	void UpdateReverb();

	/** Starts a waiting voice whose file is decoded. False when it cannot play (the voice is then to be removed). */
	bool StartVoice(FVoice& Voice, const FScctPcmPtr& Pcm);

	/** Lets a voice end over Seconds (0: at once). The voice is removed by UpdateVoices. */
	void FadeOutVoice(FVoice& Voice, float Seconds, const TCHAR* Why);
	void ReleaseVoice(FVoice& Voice);
	void StopAllVoices();

	/** Makes room for one more event voice by the original's slot rule. False: the new call is not played. */
	bool MakeRoom(const FScctSound& Record);

	/** Where a 3D voice is this frame (its actor, its bone on the drawn pawn, or where it started). */
	FVector FindVoiceLocation(const FVoice& Voice) const;

	void LoadZones(const FString& Map);
	void LogVoice(const TCHAR* What, const FVoice& Voice, const TCHAR* Extra = TEXT("")) const;
	void LogSummary() const;

	const FString& GetName(int32 Id);
	FString GetFilePath(int32 FileId);

	/** The drawn body's own name for the bone the bridge names BoneId; looked up once per bone and pawn kind. */
	FName FindDrawnBone(const AScctPawnProxy& Proxy, int32 BoneId) const;
	UScctSoundWave* FindOrCreateWave(const FVoice& Voice, const FScctPcmPtr& Pcm);
	USoundAttenuation* FindOrCreateAttenuation(float MinDistance, float Radius);

	/** The settings of the local player's own (flat) sounds: no distance model, only their send to the room's reverb. */
	USoundAttenuation* FindOrCreateOwnSoundSettings();
	UAudioComponent* AcquireComponent();

	UPROPERTY(Transient)
	TObjectPtr<UScctSessionSubsystem> Session;

	/** The volume categories' classes and gains (Play/ScctAudioMix); null outside a game instance. */
	UPROPERTY(Transient)
	TObjectPtr<UScctAudioMix> Mix;

	/** Playable waves by file id and loop setting; they share the cache's samples. */
	UPROPERTY(Transient)
	TMap<int64, TObjectPtr<UScctSoundWave>> Waves;

	/** One attenuation per (minimum distance, radius) pair in use: a map has a few dozen. */
	UPROPERTY(Transient)
	TMap<uint64, TObjectPtr<USoundAttenuation>> Attenuations;

	/** Every audio component made so far; the ones no voice uses are in FreeComponents. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> Components;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> FreeComponents;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> OwnSoundSettings;

	UPROPERTY(Transient)
	TObjectPtr<UReverbEffect> DefaultReverb;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UReverbEffect>> ZoneEffects;

	const FScctEventsApi* Events = nullptr;
	FScctSoundCache Cache;
	TSparseArray<FVoice> Voices;
	TMap<int64, int32> AmbientVoices;
	TMap<int32, FString> Names;
	mutable TMap<int64, FName> DrawnBones;
	TArray<FScctSound> SoundBuffer;
	TArray<FScctAmbientSound> AmbientBuffer;
	TArray<FZone> Zones;
	FString ContentRoot;

	FVector Listener = FVector::ZeroVector;
	bool bListenerValid = false;
	bool bHasDevice = false;
	bool bLogVoices = false;

	/** The zone whose reverb is active: an index into Zones, INDEX_NONE for the default room, -2 before the first. */
	int32 ActiveZone = -2;
	int32 ListenerZone = INDEX_NONE;

	/** -ScctAudioMusic: the song to start and the seconds into the match it starts at (negative: done). */
	FString CheckSong;
	float CheckSongIn = -1.0f;

	float SummaryEvery = 10.0f;
	float SummaryTimer = 0.0f;
	float TrimTimer = 0.0f;

	// Counters for the summary line (since the match started).
	int32 NumStarted = 0;
	int32 NumStopped = 0;
	int32 NumOutOfReach = 0;
	int32 NumRefused = 0;
	int32 NumStolen = 0;
	int32 NumFailedFiles = 0;
	int32 NumObjectiveSkipped = 0;
	int32 PeakVoices = 0;
};
