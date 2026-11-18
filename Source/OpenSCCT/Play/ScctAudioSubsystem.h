#pragma once

#include "CoreMinimal.h"
#include "AudioDeviceHandle.h"
#include "Play/ScctSoundData.h"
#include "Runtime/ScctAcousticApi.h"
#include "Runtime/ScctAudioViewApi.h"
#include "Runtime/ScctEventsApi.h"
#include "Runtime/ScctReverbRoomApi.h"
#include "Runtime/ScctSequenceAudioApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctAudioSubsystem.generated.h"

class AScctPawnProxy;
class UAudioComponent;
class UReverbEffect;
class UScctAudioMix;
class UScctSessionSubsystem;
class UScctSoundWave;
class USoundAttenuation;
class USoundSubmix;
class USubmixEffectReverbPreset;

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
 *     (svmap/<Map>/audio/audio.json "zones"), or a small neutral room where the zone has none;
 *   - a 3D voice behind a wall is muffled by the wall's material and heard from the opening its sound comes through
 *     (Play/ScctAudioAcoustics.cpp, the bridge's acoustic table), or, with the Audio page's "Sound through walls" set
 *     to the original, only quieter by rule 18.
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
 *   -ScctAudioOcclusion    log every 3D voice's acoustics once a second (as the console's scct.AudioOcclusion 1)
 *   -ScctAudioWallSound=M  "Sound through walls" for this run without saving it: 0 material transmission, 1 the
 *                          original's rule 18, 2 off
 *   -ScctAudioProbe=x,y,z[;x,y,z...]  a looping test emitter at each place, started 2 s into the match (or
 *                          -ScctAudioProbeAt=S), playing -ScctAudioProbeSound=Name (default Interface.Objectifs.Alarme);
 *                          their acoustics are logged once a second whatever scct.AudioOcclusion says
 *   -ScctAudioZoom=F       force the scope's audio zoom factor F for the run (the scope's steps are 0.33, 0.66 and 1)
 *                          and log every 3D voice's zoom gain once a second
 *   -ScctAudioDoppler      log the Doppler pitch of moving 3D voices, and the pitch 1 of the listener-bound ones
 *   -ScctAudioListener=x,y,z[;x,y,z...]  hold the listener (for every sound rule, not the engine's panning) at each place
 *                          in turn, -ScctAudioListenerEvery=S seconds each (default 8), from the match's start
 *   -ScctAudioRooms=0|log  0: no source-room or adaptive reverb (one listener reverb, as before); log: log every 3D
 *                          voice's room routing once a second (the -ScctAudioProbe emitters always are)
 *   -ScctSequenceAudioTrigger=Tag[@S]  fire the event Tag on the local server S seconds into the match (default 2), which
 *                          starts the animation controllers of that slot (Tools/Test/SequenceAudioPack has ScctSequenceAudioCheck)
 *   -ScctSequenceAudioJoinAt=S  read no sequence audio before S seconds into the match, as a player who joins then
 *   -ScctSequenceAudioDrift=S  every sequence voice slips S seconds once, 2 s after it starts, so it is put back in step
 * Play/ScctAudioMix has the volume switches (-ScctAudioVolume, -ScctAudioPeaks).
 *
 * Animation sequences (Play/ScctAudioSequences.cpp): every audio segment an animation sequence plays now is one voice,
 * started at the segment's offset on the sequence's replicated clock (so a player who joins late hears it in step),
 * following its target actor and put back in step when it drifts more than 0.25 s (the Godot client's
 * AnimationSequenceAudio).
 *
 * Rooms (Play/ScctAudioRooms.cpp): a 3D voice whose source is in another zone than the listener's sends its reverb to
 * that zone's own reverb (one of four source-room submixes, Godot's ReverbBuses.SourceRooms), turned by what the way
 * through the openings keeps; the listener's zone keeps the main reverb, and a voice cross-fades between the two when
 * either moves, so tails carry across zone changes. The main reverb and each field also move toward the room size the
 * bridge's geometry probe measures (Godot's AdaptiveReverb, ReverbBuses.Geometry).
 *
 * The listener's view (Play/ScctAudioView.cpp): through a scope or binoculars every 3D voice near the aim line gets
 * louder and the rest quieter (the original's SetAudioZoom), and every 3D voice's pitch carries the Doppler shift of
 * its own and the listener's movement along the line between them (the Godot client's rule 17).
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

	/** Voices started since the match began (counted with or without an audio device), and those whose file failed to load. */
	int32 GetNumStarted() const { return NumStarted; }
	int32 GetNumFailedFiles() const { return NumFailedFiles; }

	/** Voices started since the match began in one volume category (EScctAudioCategory as a number). */
	int32 GetNumStartedIn(int32 Category) const { return Category >= 0 && Category < UE_ARRAY_COUNT(NumStartedByCategory) ? NumStartedByCategory[Category] : 0; }

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
		float SeekFadeOut = 0.0f;

		// Ambient voices: the key they are matched by between frames (0: not ambient) and the repeat of a one-shot.
		int64 AmbientId = 0;
		bool bEquipmentLoop = false; // A gadget can use AmbientSound for its repeating warning.
		/** Ambient voices: the UpdateAmbient pass that made the voice (Play/ScctAudioAmbientLoops.cpp). */
		int32 AmbientPass = 0;
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

		// Sound through walls (Play/ScctAudioAcoustics.cpp): the voice's handle in the bridge's acoustic table, the
		// volume multiplier and low-pass it gets this frame and how far the place it is heard from lies from its own.
		int32 AcousticKey = 0;
		int32 AcousticFlags = 0;
		float AcousticScale = 1.0f;
		float AcousticLowPass = 20000.0f;
		float AppliedLowPass = 20000.0f;
		FVector AcousticOffset = FVector::ZeroVector;
		float AcousticLogIn = 0.0f;
		bool bProbe = false;    // a -ScctAudioProbe emitter: always logged

		// The listener's view (Play/ScctAudioView.cpp): the scope's or binoculars' audio zoom gain on its volume and
		// the Doppler factor on its pitch this frame, from its place last frame and the pitch its component has.
		float ZoomGain = 1.0f;
		float DopplerPitch = 1.0f;
		float AppliedPitch = -1.0f;
		FVector LastLocation = FVector::ZeroVector;
		bool bHasLastLocation = false;
		bool bViewLogged = false;
		float ZoomLogIn = 0.0f;
		float DopplerLogIn = 0.0f;

		// Source-room reverb (Play/ScctAudioRooms.cpp): the BSP zone its source is in, the source-room field it sends to
		// (INDEX_NONE: the main reverb only), how far it has cross-faded from the main reverb to that field, and what its
		// component was last given (the main reverb's share and the send to the field).
		int32 RoomZone = 0;
		/** Where its reverb is going: a field, INDEX_NONE for the main reverb, -2 before its first routing. */
		int32 RoomTarget = -2;
		int32 AppliedRoomField = INDEX_NONE;
		int32 RoomField = INDEX_NONE;
		float RoomMix = 0.0f;
		float AppliedRoomMix = 0.0f;
		float AppliedRoomSend = 0.0f;
		float RoomSend = 0.0f;
		float RoomLogIn = 0.0f;

		// Animation sequence audio (Play/ScctAudioSequences.cpp): the segment key it plays (0: none), where in its file
		// the bridge says the segment is now, where this voice is by its own playback (from the wall clock since it started,
		// as the engine renders it in real time, however long a game frame takes), where it started and the file's length.
		int32 SequenceKey = 0;
		float SequenceOffset = 0.0f;
		float SequencePosition = 0.0f;
		float StartOffset = 0.0f;
		float FileSeconds = 0.0f;
		double SequenceStartedAt = 0.0;
		/** -ScctSequenceAudioDrift: the slip put on its playback (once). */
		float SequenceSlip = 0.0f;
		bool bSequenceDrifted = false;
	};

	/** One audio segment of an animation sequence, as the bridge last listed it (Play/ScctAudioSequences.cpp). */
	struct FSequenceSegment
	{
		FScctSequenceSound Last = {};
		/** The voice that plays it (an index into Voices whose SequenceKey is the segment's), INDEX_NONE before it has one. */
		int32 VoiceIndex = INDEX_NONE;
		/** Its voice is over (its file ended, failed or had nothing left to play): it stays silent until it is no longer listed. */
		bool bEnded = false;
		bool bSeen = false;
	};

	/**
	 * A source-room reverb field (Play/ScctAudioRooms.cpp, Godot's ReverbBuses.SourceRoomField): a submix of its own
	 * with a reverb set from one zone's EAX environment, which the voices whose source is in that zone (and not in the
	 * listener's) send to; its output is turned by what the way from the room to the listener keeps.
	 */
	struct FRoomField
	{
		/** The BSP zone it carries (0: free). */
		int32 ZoneIndex = 0;
		/** Its reverb: an index into Zones, INDEX_NONE for the default room. */
		int32 Zone = INDEX_NONE;
		double LastUsed = -1000.0;
		double ClaimedAt = -1000.0;
		float Gain = 0.0f;
		float HF = 1.0f;
		float TailSeconds = 2.0f;
		bool bReservedForGameplay = false;
		/** Where its geometry probe stands: the first source that claimed it, moved to the nearest source of each frame. */
		FVector Anchor = FVector::ZeroVector;
		FVector Candidate = FVector::ZeroVector;
		bool bCandidate = false;
		/** The adaptive reverb's latest estimate and whether the field's reverb follows it yet. */
		FScctReverbGeometry Geometry = {};
		bool bAdapted = false;
		float LogIn = 0.0f;
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

	// The flat (2D) ambient loops of actors that are no zones: one voice per actor, and the voices add up
	// (Play/ScctAudioAmbientLoops.cpp).
	static constexpr int64 FlatZoneLoopBit = 1ll << 40;
	static constexpr int64 FlatActorLoopBit = 1ll << 41;
	static constexpr float FlatLoopSilence = 0.05f;
	static constexpr float FlatLoopFadeSeconds = 0.4f;
	int32 BeginFlatLoopPass(int32 Count);
	bool UpdateFlatActorLoop(const FScctAmbientSound& Source, float Distance, int64& OutId, float& OutVolume);
	bool HandFlatLoopToZone(const FScctAmbientSound& Source, int64 ZoneId);
	void UpdateVoices(float DeltaSeconds);
	void UpdateReverb();

	/**
	 * Sound through walls and openings (Play/ScctAudioAcoustics.cpp): every waiting or playing 3D voice is asked about
	 * in one call to the bridge's acoustic table, and its volume, low-pass and the place it is heard from follow.
	 */
	void UpdateAcoustics(float DeltaSeconds);
	/** The player's volume of a voice's kind (Menu/ScctUserSettings.h); the master volume is the engine's own. */
	static float SettingsGainOf(const FVoice& Voice);
	/** Puts a voice's acoustics on its component (volume x kind's gain x scale, low-pass, place); Force applies even unchanged values. */
	void ApplyAcoustics(FVoice& Voice, bool bForce);
	/** Reads -ScctAudioOcclusion, -ScctAudioWallSound and the -ScctAudioProbe switches at the match's start. */
	void ReadAcousticSwitches();
	/** -ScctAudioProbe: starts the test emitters when their time comes. */
	void UpdateAudioProbe(float DeltaSeconds);
	void LogAcoustics(const FVoice& Voice, const FScctAcousticResult& Result) const;
	void LogAcousticSummary() const;

	/**
	 * The listener's view (Play/ScctAudioView.cpp): reads the bridge's audio view table (the scope's or binoculars'
	 * audio zoom, the FOV, the view actor and the local pawn) and the listener's velocity from its movement this frame.
	 */
	void UpdateAudioView(float DeltaSeconds);
	/** A voice's audio zoom gain and Doppler pitch this frame, from its movement since the last (as the Godot client's rule 17). */
	void UpdateZoomAndDoppler(FVoice& Voice, float DeltaSeconds);
	/** True for a voice that moves with the listener: the local pawn's own 3D sounds and the view actor's ambient loop. */
	bool IsListenerBound(const FVoice& Voice) const;
	/** The volume a voice's component gets: its volume (raised by the audio zoom, never above full) x its kind's gain x the wall's scale. */
	float HeardVolumeOf(const FVoice& Voice) const;
	/** Reads -ScctAudioZoom and -ScctAudioDoppler and fetches the audio view table at the match's start. */
	void ReadAudioViewSwitches();
	void LogAudioViewSummary() const;

	/**
	 * Source-room and adaptive reverb (Play/ScctAudioRooms.cpp): finds the zone of the listener and of every routed 3D
	 * voice through the bridge's reverb room table, gives a voice in another room a source-room field and cross-fades its
	 * reverb between the main one and that field, turns each field by the way to it, and moves the main reverb and the
	 * fields toward the room size the geometry probes measure.
	 */
	void UpdateRooms(float DeltaSeconds);
	/** Makes the source-room fields' submixes and reverbs on the world's audio device (none without one). */
	void CreateRoomFields();
	void ReleaseRoomFields();
	/** Reads -ScctAudioListener, -ScctAudioListenerEvery and -ScctAudioRooms at the match's start. */
	void ReadRoomSwitches();
	/** -ScctAudioListener: puts the listener at the forced place of this moment. */
	void UpdateForcedListener(float DeltaSeconds);
	/** The field that carries ZoneIndex (claiming a free one if needed), or INDEX_NONE. */
	int32 FindRoomField(int32 ZoneIndex, int32 Zone, bool bAmbient, bool bPathValid, float Reflected, const FVector& Anchor);
	/** Puts a voice's main reverb share and its field send on its component. */
	void ApplyRoomRouting(FVoice& Voice, float HeardDistance, float InputDistance);
	/** The adaptive reverb's packets: the main reverb and each field toward its room's measured geometry. */
	void UpdateRoomGeometry(float DeltaSeconds);
	/** The index into Zones of a located room (INDEX_NONE: a zone without its own reverb). */
	int32 ZoneOfRoom(const FScctReverbRoom& Room) const;
	FString RoomName(int32 Zone) const;
	void LogRoomSummary() const;

	/**
	 * Animation sequence audio (Play/ScctAudioSequences.cpp, the Godot client's AnimationSequenceAudio): one 3D voice per
	 * audio segment the bridge lists, started at the segment's offset on the replicated sequence clock, following its
	 * target, put back in step when it drifts more than 0.25 s and stopped when its segment is no longer listed.
	 */
	void UpdateSequenceAudio(float DeltaSeconds);
	/** Reads the -ScctSequenceAudio switches and fetches the sequence audio table at the match's start. */
	void ReadSequenceSwitches();
	/** The start of a sequence voice: where in its file it starts (the segment's latest offset); false when nothing is left to play. */
	bool PrepareSequenceStart(FVoice& Voice, float FileSeconds);
	void LogSequenceSummary() const;

public:
	/** Puts "Sound through walls" on the Audio page (called by Play/ScctAudioMix with the other audio options). */
	static void RegisterAcousticSettings();

private:
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

	/** One attenuation per (minimum distance, radius, HRTF or panning) in use: a map has a few dozen. */
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
	float PlaybackRate = 1.0f;
	FScctSoundCache Cache;
	TSparseArray<FVoice> Voices;
	TMap<int64, int32> AmbientVoices;
	TMap<int32, FString> Names;
	mutable TMap<int64, FName> DrawnBones;
	TArray<FScctSound> SoundBuffer;
	TArray<FScctAmbientSound> AmbientBuffer;
	/** How many of AmbientBuffer's records are this frame's, the count of UpdateAmbient passes and the sounds the listener's zone plays. */
	int32 AmbientCount = 0;
	int32 AmbientPass = 0;
	TArray<int32> ListenerZoneSounds;
	/** For the log: the flat sources that are no zones listed in this pass and at the last line about them. */
	int32 NumFlatActorSources = 0;
	int32 LoggedFlatActorSources = INDEX_NONE;
	/** -ScctAudioAmbientAfter=<seconds>: no ambient source is read before this world time (a check: all become audible in one update). */
	float AmbientAfterSeconds = 0.0f;
	TArray<FZone> Zones;
	FString ContentRoot;

	FVector Listener = FVector::ZeroVector;
	bool bListenerValid = false;
	bool bHasDevice = false;
	bool bLogVoices = false;

	/** The zone whose reverb is active: an index into Zones, INDEX_NONE for the default room, -2 before the first. */
	int32 ActiveZone = -2;
	int32 ListenerZone = INDEX_NONE;

	// Sound through walls (Play/ScctAudioAcoustics.cpp).
	const FScctAcousticApi* Acoustics = nullptr;
	TArray<FScctAcousticSource> AcousticSources;
	TArray<FScctAcousticResult> AcousticResults;
	TArray<int32> AcousticVoiceIndices;
	int32 NextAcousticKey = 0;
	/** -ScctAudioWallSound: the mode forced for the run (INDEX_NONE: the player's option). */
	int32 ForcedAcousticMode = INDEX_NONE;

	/** -ScctAudioProbe: the places of the test emitters, the sound they play and the seconds until they start. */
	TArray<FVector> ProbePlaces;
	TArray<int32> ProbeCalls;
	FString ProbeSound;
	float ProbeIn = -1.0f;

	// The listener's view (Play/ScctAudioView.cpp).
	const FScctAudioViewApi* AudioViewApi = nullptr;
	float AudioZoom = 0.0f;
	float AudioZoomFov = 90.0f;
	int32 ViewActorId = 0;
	int32 LocalPawnId = 0;
	FRotator ListenerRotation = FRotator::ZeroRotator;
	FVector ListenerVelocity = FVector::ZeroVector;
	FVector LastListener = FVector::ZeroVector;
	bool bLastListenerSet = false;
	/** -ScctAudioZoom=F: the audio zoom forced for the run (negative: the player's scope or binoculars); it also logs every 3D voice's zoom gain. */
	float ForcedAudioZoom = -1.0f;
	/** -ScctAudioDoppler: log the Doppler pitch of moving 3D voices and the pitch 1 of the listener-bound ones. */
	bool bLogDoppler = false;
	/** The zoom last said in the log, so a change of scope step is said once. */
	float LoggedAudioZoom = -1.0f;
	int32 NumDopplerShifted = 0;
	float PeakDopplerPitch = 1.0f;
	float LowDopplerPitch = 1.0f;

	// Source-room and adaptive reverb (Play/ScctAudioRooms.cpp).
	const FScctReverbRoomApi* RoomApi = nullptr;
	FAudioDeviceHandle RoomDevice;
	TArray<FRoomField> RoomFields;
	TArray<FVector> RoomPoints;
	TArray<FScctReverbRoom> RoomResults;
	TArray<int32> RoomVoiceIndices;
	/** The field submixes, their reverb presets and the reverb settings each is given (one per field, on the world's device). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundSubmix>> RoomSubmixes;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USubmixEffectReverbPreset>> RoomPresets;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UReverbEffect>> RoomEffects;
	/** The adaptive main reverb's two settings, used in turn so the engine sees each packet as a change. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UReverbEffect>> AdaptiveMainEffects;
	int32 NextAdaptiveMain = 0;
	/** The listener's BSP zone (kept through zone 0, as rule 23 keeps it) and when it last changed (RoomClock). */
	int32 ListenerZoneIndex = 0;
	/** The listener's zone's reverb as this table found it (an index into Zones, INDEX_NONE for the default room). */
	int32 ListenerRoomZone = INDEX_NONE;
	double ListenerZoneChangedAt = -1000.0;
	FScctReverbGeometry ListenerGeometry = {};
	bool bListenerAdapted = false;
	double RoomClock = 0.0;
	double NextGeometryPacket = 0.0;
	double LastGeometryPacket = 0.0;
	int32 NextGeometrySlot = 0;
	/** False with -ScctAudioRooms=0: every voice keeps the main reverb and nothing adapts (as before this was ported). */
	bool bRoomsEnabled = true;
	bool bAdaptiveEnabled = true;
	/** -ScctAudioRooms=log: log every voice's room routing once a second (probe emitters always are). */
	bool bLogRooms = false;
	/** -ScctAudioListener: forced listener places, how long each is held and the time into the match. */
	TArray<FVector> ForcedListenerPlaces;
	float ForcedListenerEvery = 8.0f;
	float ForcedListenerClock = 0.0f;
	int32 ForcedListenerAt = INDEX_NONE;
	// Counters for the summary line.
	int32 NumRoomCrossfades = 0;
	int32 NumRoomFieldClaims = 0;
	int32 NumRoomRefused = 0;
	int32 NumGeometryPackets = 0;

	// Animation sequence audio (Play/ScctAudioSequences.cpp).
	const FScctSequenceAudioApi* SequenceApi = nullptr;
	TArray<FScctSequenceSound> SequenceBuffer;
	TMap<int32, FSequenceSegment> SequenceSegments;
	float SequenceClock = 0.0f;
	/** -ScctSequenceAudioTrigger=Tag@S: the event fired on the local server S seconds into the match (empty: none). */
	FString SequenceTriggerTag;
	float SequenceTriggerIn = -1.0f;
	/** -ScctSequenceAudioJoinAt=S: nothing is read before S seconds into the match, as for a player who arrives then. */
	float SequenceJoinAt = 0.0f;
	/** -ScctSequenceAudioDrift=S: each sequence voice slips S seconds once, 2 s after it starts, so it is put back in step. */
	float SequenceDrift = 0.0f;
	int32 NumSequenceStarted = 0;
	int32 NumSequenceSeeks = 0;
	int32 NumSequenceStopped = 0;
	int32 NumSequenceEnded = 0;
	int32 NumSequenceNoFile = 0;
	int32 PeakSequenceSegments = 0;

	/** -ScctAudioMusic: the song to start and the seconds into the match it starts at (negative: done). */
	FString CheckSong;
	float CheckSongIn = -1.0f;

	float SummaryEvery = 10.0f;
	float SummaryTimer = 0.0f;
	float TrimTimer = 0.0f;

	// Counters for the summary line (since the match started).
	int32 NumStarted = 0;
	int32 NumStartedByCategory[8] = {};
	int32 NumStopped = 0;
	int32 NumOutOfReach = 0;
	int32 NumRefused = 0;
	int32 NumStolen = 0;
	int32 NumFailedFiles = 0;
	int32 NumObjectiveSkipped = 0;
	int32 PeakVoices = 0;
	/** 3D voices started rendered by HRTF (Headphones, Play/ScctAudioOutput.h) and panned. */
	int32 NumHrtfStarted = 0;
	int32 NumPannedStarted = 0;
	/** The spatialization the playing voices were started with; a change starts the looping 3D voices again (UpdateVoices). */
	bool bOutputModeHrtf = false;
};
