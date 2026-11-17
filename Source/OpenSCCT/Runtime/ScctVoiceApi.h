#pragma once

#include "CoreMinimal.h"

// The C# bridge's voice chat table (Bridge/OpenScct.UnrealBridge/Voice: VoiceBridge, VoiceExports, VoiceTypes): one
// OpenSCCT VoiceClient at 48 kHz on the online visit (the same Opus wire, relay and jitter buffer as the Godot client
// and the server), and the Voice page's microphone test against a relay in the bridge. Unreal hands the microphone
// in, reads each talker's decoded frames and plays them, and sets the keys and the options. Change both sides
// together and bump the version. Fetched with ScctFeature<FScctVoiceApi>() (Runtime/ScctFeatures.h).
//
// Every entry is for the game thread only. Microphone samples captured on an audio thread are queued there and handed
// to SubmitCapture from the game thread; decoded frames are read on the game thread and queued for the audio thread.
// The visit's voice is ticked by the online table's Update (after the visit's own ticks); Update here only runs the
// microphone test.

constexpr int32 ScctVoiceApiVersion = 1;

/** Samples in one voice frame: 20 ms at 48 kHz, what ReadFrame writes. */
constexpr int32 ScctVoiceFrameSamples = 960;
constexpr int32 ScctVoiceSampleRate = 48000;

/** The player's voice options (OpenSCCT VoiceSettings). Flags are 0 or 1. */
struct FScctVoiceSettings
{
	int32 Enabled;
	/** Transmit only while a talk key is held; off, the activation gate decides. */
	int32 PushToTalk;
	/** Self mute: nothing is sent, everyone else is still heard. */
	int32 MicMuted;
	/** Gate threshold on a frame's RMS, 0 (always open) to 1 (never). */
	float ActivationThreshold;
	/** Milliseconds the gate stays open after the voice drops (0 to 5000). */
	int32 ActivationHoldMs;
	/** Microphone gain, 0 to 8 (not used while AutoGain is on). */
	float InputGain;
	/** Volume of the other players, 0 to 1: kept for the read back only; the voice chat submix applies it, not ReadFrame. */
	float OutputVolume;
	/** Jitter buffer depth in 20 ms frames, 1 to 12. */
	int32 JitterFrames;
	int32 NoiseSuppression;
	/** 15, 30, 45 or 60 dB. */
	int32 NoiseSuppressionDecibels;
	int32 AutoGain;
	/** Against what SubmitPlayback hands back (experimental, as in the Godot client). */
	int32 EchoCancellation;
};

enum class EScctVoiceEchoPhase : int32
{
	Off = 0,
	/** The microphone goes through the encoder, a relay in the bridge and a listening client; what it decodes is kept. */
	Recording = 1,
	/** The recording comes back through ReadFrame(ScctVoiceEchoTalker). */
	Playing = 2,
	Done = 3,
	/** The recording time passed and nothing came through: no microphone samples arrived. */
	NothingCaptured = 4,
	/** Live loopback: each decoded frame comes straight back through ReadFrame(ScctVoiceEchoTalker). */
	Live = 5,
};

/** The talker id the microphone test comes back on. */
constexpr int32 ScctVoiceEchoTalker = 1;

/** The voice chat in one read, cheap enough for every frame. */
struct FScctVoiceStatus
{
	/** The voice client has a server to send to (the visit's map, or the microphone test's relay). */
	int32 Connected;
	int32 Transmitting;
	/** 0 all, 1 team. */
	int32 Channel;
	int32 MicMuted;
	/** The local player's talker id on the current map (0 until known). */
	int32 LocalTalker;
	/** Remote talkers heard so far (GetTalkers). */
	int32 TalkerCount;
	/** EScctVoiceEchoPhase. */
	int32 EchoPhase;
	int32 Enabled;
	/** RMS of the last microphone frame after gain, 0 to 1 (the level meter). */
	float InputLevel;
	/** The automatic gain in use (1 while it is off). */
	float AutoGain;
	int64 FramesCaptured;
	int64 FramesSent;
	int64 PacketsReceived;
};

/** One remote talker heard on the current map. */
struct FScctVoiceTalker
{
	static constexpr int32 NameCapacity = 64;

	/** The id ReadFrame, SetMuted and SetVolume take. */
	int32 Talker;
	/** 0 spies, 1 mercs, 255 spectators (lobby), -1 not known. */
	int32 Team;
	/** Being heard now. */
	int32 Speaking;
	/** Of the latest packet: 0 all, 1 team. */
	int32 Channel;
	/** Muted by the local player. */
	int32 Muted;
	/** The local player's volume for this talker, 0 to 2. */
	float Volume;
	/** RMS of the last frame read, after the volume. */
	float Level;
	/** Frames waiting in the jitter buffer. */
	int32 QueuedFrames;
	int64 FramesDecoded;
	int64 DecodeErrors;
	/** The player's name, or "Player n" while unknown. UTF-16, zero terminated. */
	TCHAR Name[NameCapacity];
};

struct FScctVoiceApi
{
	int32 Version;
	/** sizeof the bridge's talker row, checked against FScctVoiceTalker. */
	int32 TalkerInfoSize;

	/** Once a frame: runs the microphone test while it is on. */
	void (__cdecl* Update)();
	/** The rate of the samples SubmitCapture receives (8000 to 192000). */
	void (__cdecl* SetCaptureRate)(int32 SampleRate);
	/** Microphone samples: mono, -1 to 1, at the capture rate. Dropped while nothing listens. */
	void (__cdecl* SubmitCapture)(const float* Samples, int32 Count);
	/** What the voice output played (48 kHz mono, every talker mixed at the output volume): the echo reference. */
	void (__cdecl* SubmitPlayback)(const float* Samples, int32 Count);
	/** The push to talk keys held: Talk the all channel, Team the team channel. */
	void (__cdecl* SetKeys)(int32 Talk, int32 Team);
	void (__cdecl* ApplySettings)(const FScctVoiceSettings* Settings);
	void (__cdecl* GetSettings)(FScctVoiceSettings* OutSettings);
	void (__cdecl* GetState)(FScctVoiceStatus* OutStatus);
	/** Fills up to Capacity rows; returns how many talkers there are (may exceed Capacity). */
	int32 (__cdecl* GetTalkers)(FScctVoiceTalker* OutRows, int32 Capacity);
	/** The talker's next 20 ms into ScctVoiceFrameSamples floats at 48 kHz; 1 when written, 0 when none is due. */
	int32 (__cdecl* ReadFrame)(int32 Talker, float* OutFrame);
	/** Mutes a talker for the local player (by name when named, so it holds on later maps). */
	void (__cdecl* SetMuted)(int32 Talker, int32 Muted);
	/** The local player's volume for a talker, 0 to 2. */
	void (__cdecl* SetVolume)(int32 Talker, float Volume);
	/** Starts the microphone test: seconds recorded then played back, 0 for live loopback. 0 when refused (online). */
	int32 (__cdecl* StartEchoTest)(int32 RecordSeconds);
	void (__cdecl* StopEchoTest)();
	/** The last failure inside a voice entry into the buffer; returns its full length (0: none). */
	int32 (__cdecl* GetError)(TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctVoiceSettings) == 48, "FScctVoiceSettings does not match VoiceSettingsBlock");
static_assert(sizeof(FScctVoiceStatus) == 64, "FScctVoiceStatus does not match VoiceStatus");
static_assert(sizeof(FScctVoiceTalker) == 176, "FScctVoiceTalker does not match VoiceTalkerInfo");
static_assert(sizeof(FScctVoiceApi) == 8 + 15 * sizeof(void*), "FScctVoiceApi does not match VoiceApi");
