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

constexpr int32 ScctVoiceApiVersion = 4;

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
	// Version 4.
	/** How he reaches the local player: EScctVoiceLink (all or team for a mate; hacked, trace, downed or grab for an enemy). */
	int32 Link;
};

/**
 * How a talker reaches the local player (FScctVoiceTalker::Link), or how the local player reaches an enemy
 * (FScctVoiceContext::TalkLink): the special cases of the server's voice routing (OpenSCCT VoiceRouting rules 8 to 14),
 * named by the bridge from the client's copy of the round, since the packets carry only the all or team channel.
 */
enum class EScctVoiceLink : int32
{
	All = 0,
	Team = 1,
	/** An enemy tagged by the local player's team, heard through Hack Comm (the key, or auto-hack). */
	Hacked = 2,
	/** The enemy the scope's or the binoculars' voice trace found. */
	Trace = 3,
	/** Downed chat: an enemy talking to the local player while he lies on the ground; for TalkLink, talking to a downed enemy. */
	Downed = 4,
	/** Grab talk: the spy and the merc he holds, while the spy holds the Hack Comm key. */
	Grab = 5,
};

/** The link's name as the log writes it: All, Team, Hacked, Trace, Downed, Grab. */
inline const TCHAR* ScctVoiceLinkName(int32 Link)
{
	switch (static_cast<EScctVoiceLink>(Link))
	{
	case EScctVoiceLink::All: return TEXT("All");
	case EScctVoiceLink::Team: return TEXT("Team");
	case EScctVoiceLink::Hacked: return TEXT("Hacked");
	case EScctVoiceLink::Trace: return TEXT("Trace");
	case EScctVoiceLink::Downed: return TEXT("Downed");
	case EScctVoiceLink::Grab: return TEXT("Grab");
	default: return TEXT("?");
	}
}

/** One player as the lobby shows him (GetPlayerVoice, by name): Godot's speaking mark, "Voice volume" and "Mute". */
struct FScctVoicePlayer
{
	/** An online visit is up (without one only Volume and Muted mean anything). */
	int32 Visit;
	/** His talker id on the current map, 0 when he is not on it. */
	int32 Talker;
	/** He is the local player. */
	int32 Local;
	/** Being heard now; for the local player, transmitting. */
	int32 Speaking;
	/** Muted by the local player (kept by name for the rest of the run). */
	int32 Muted;
	/** The local player's volume for him, 0 to 2 (kept by name in the volume file). */
	float Volume;
	/** RMS of the last frame heard from him (after the volume), or of the microphone for the local player. */
	float Level;
};

/** Where the visit is (GetContext): what the speaker overlay needs beyond the talker rows. */
struct FScctVoiceContext
{
	int32 Visit;
	/** In a round, not the lobby. */
	int32 InRound;
	/** 0 spies, 1 mercs, 255 spectators (lobby), -1 not known. */
	int32 LocalTeam;
	int32 LocalTalker;
	// Version 4: the special cases of a round.
	/** EScctVoiceLink::Downed or Grab while the local player answers the HUD's Speak prompt with the Hack Comm key (the
	 *  microphone then talks on the all channel), else 0. */
	int32 TalkLink;
	/** Hack Comm is listening: the key is held or auto-hack is on. */
	int32 HackComm;
	/** The talker id of the enemy the scope's or the binoculars' voice trace holds, 0 for none. */
	int32 TraceTalker;
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

	// Version 2: the Voice page's level meter and the lobby's "Restrict Mic to Team".

	/** The Voice page's level meter (1 on): outside a visit and the echo test, the microphone goes through the player's
	 *  clean-up and gain into an unconnected client, so GetState's InputLevel reads the microphone. Sends nothing. */
	void (__cdecl* SetMeter)(int32 On);
	/** Sends "Restrict Mic to Team" on the online visit (OnlineClient.SetTeamVoiceOnly); 1 sent, 0 no visit. */
	int32 (__cdecl* SetTeamVoiceOnly)(int32 Only);
	/** The local player's "Restrict Mic to Team" as the server has it: -1 not known (no visit yet), else 0 or 1. */
	int32 (__cdecl* GetTeamVoiceOnly)();

	// Version 3: per-player choices by name, the lobby's per-player row, the host's voice ban, the overlay's context.

	/** Keeps the per-player volumes in this file (JSON of name to volume): loaded now, written whenever one changes. */
	void (__cdecl* SetVolumeFile)(const TCHAR* Path);
	/** The local player's volume for a player by name, 0 to 2: at once on the current map, and saved. */
	void (__cdecl* SetPlayerVolume)(const TCHAR* Name, float Volume);
	/** Mutes or unmutes a player by name for the rest of the run (the server stops sending him while muted). */
	void (__cdecl* SetPlayerMuted)(const TCHAR* Name, int32 Muted);
	/** One player's speaking, level, volume and mute by name; returns 1 while an online visit is up. */
	int32 (__cdecl* GetPlayerVoice)(const TCHAR* Name, FScctVoicePlayer* OutPlayer);
	/** The host's Voice Ban of a lobby player id (OnlineClient.SetVoiceBanned; the server checks who asks); 1 sent, 0 no visit. */
	int32 (__cdecl* SetVoiceBanned)(int32 PlayerId, int32 Banned);
	/** Lobby or round, and the local player's team. */
	void (__cdecl* GetContext)(FScctVoiceContext* OutContext);
};

static_assert(sizeof(FScctVoiceSettings) == 48, "FScctVoiceSettings does not match VoiceSettingsBlock");
static_assert(sizeof(FScctVoiceStatus) == 64, "FScctVoiceStatus does not match VoiceStatus");
static_assert(sizeof(FScctVoiceTalker) == 184, "FScctVoiceTalker does not match VoiceTalkerInfo");
static_assert(sizeof(FScctVoicePlayer) == 28, "FScctVoicePlayer does not match VoicePlayerInfo");
static_assert(sizeof(FScctVoiceContext) == 28, "FScctVoiceContext does not match VoiceContext");
static_assert(sizeof(FScctVoiceApi) == 8 + 24 * sizeof(void*), "FScctVoiceApi does not match VoiceApi");
