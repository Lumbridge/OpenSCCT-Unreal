#include "Hub/ScctHubAudio.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Hub/ScctHubActivities.h"
#include "OpenSCCT.h"
#include "Play/ScctAudioSubsystem.h"
#include "Settings/ScctSettings.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	constexpr int32 MixRate = 22050;

	/** How far ahead of the mixer the game thread keeps each stream (a slow frame must not starve it). */
	constexpr float LeadSeconds = 0.25f;

	// HubPlayK.
	constexpr float JukeboxUseRange = 384.0f;
	constexpr float JukeboxRange = 1600.0f;
	constexpr float JukeboxLoudest = 0.55f;

	float Freq(int32 Midi)
	{
		return 440.0f * FMath::Pow(2.0f, (Midi - 69) / 12.0f);
	}

	struct FChord
	{
		int32 Offset;
		TArray<int32> Tones;
	};

	struct FPiece
	{
		float Bpm;
		int32 Root;
		TArray<FChord> Chords;
		float Brightness;
		float Swing;
	};

	const TArray<FPiece>& Pieces()
	{
		static const TArray<int32> Minor = { 0, 3, 7 };
		static const TArray<int32> Major = { 0, 4, 7 };
		static const TArray<int32> Minor7 = { 0, 3, 7, 10 };
		static const TArray<int32> Major7 = { 0, 4, 7, 11 };
		static const TArray<FPiece> All = {
			{ 92.0f, 57, { { 0, Minor }, { -4, Major }, { 3, Major }, { -2, Major } }, 0.35f, 0.0f },
			{ 104.0f, 50, { { 0, Minor7 }, { 5, Minor }, { -4, Major }, { -2, Major } }, 0.55f, 0.08f },
			{ 124.0f, 52, { { 0, Minor }, { 8, Major }, { 3, Major }, { 10, Major } }, 0.7f, 0.0f },
			{ 80.0f, 53, { { 0, Major7 }, { -1, Minor7 }, { -3, Minor7 }, { -5, Major7 } }, 0.25f, 0.12f },
		};
		return All;
	}

	float Frac(float Value)
	{
		return Value - FMath::FloorToFloat(Value);
	}
}

// ---- the synthesiser ------------------------------------------------------------------------------------------------

void FScctHubSynth::SetTrack(int32 Value)
{
	if (Value != Track)
	{
		Track = Value;
		Sample = 0;
		LastEighth = -1;
	}
}

void FScctHubSynth::Render(float* Out, int32 Frames, float Gain)
{
	const TArray<FPiece>& All = Pieces();
	const FPiece& Piece = All[((Track % All.Num()) + All.Num()) % All.Num()];
	const float Dt = 1.0f / Rate;
	const float BassDecay = FMath::Pow(0.001f, Dt / 0.45f);
	const float ArpDecay = FMath::Pow(0.001f, Dt / 0.28f);
	const float HatDecay = FMath::Pow(0.001f, Dt / 0.04f);
	const float EighthSeconds = 30.0f / Piece.Bpm;
	for (int32 I = 0; I < Frames; ++I)
	{
		const float T = Sample * Dt;
		const int32 Eighth = static_cast<int32>(T / EighthSeconds);
		const int32 Bar = Eighth / 8 % Piece.Chords.Num();
		const FChord& Chord = Piece.Chords[Bar];
		const TArray<int32>& Tones = Chord.Tones;
		const int32 Root = Piece.Root + Chord.Offset;
		if (Eighth != LastEighth)
		{
			// A swung off-beat comes a little late.
			const float Into = T - Eighth * EighthSeconds;
			const bool bOff = Eighth % 2 == 1;
			if (!bOff || Into >= Piece.Swing * EighthSeconds)
			{
				LastEighth = Eighth;
				const int32 Step = Eighth % 8;
				if (Step == 0 || Step == 4 || (Step == 7 && Bar % 2 == 1))
				{
					BassFreq = Freq(Root - 12 + (Step == 7 ? 7 : 0));
					BassEnv = 1.0f;
				}
				const int32 Order[8] = { 0, 1, 2, 1, 0, 2, 1, Tones.Num() - 1 };
				ArpFreq = Freq(Root + 12 + Tones[Order[Step] % Tones.Num()]);
				ArpEnv = 0.9f;
				if (bOff)
				{
					HatEnv = 1.0f;
				}
			}
		}

		float Pad = 0.0f;
		const float Swell = 0.75f + 0.25f * FMath::Sin(T * 0.7f);
		for (int32 V = 0; V < Tones.Num() && V < UE_ARRAY_COUNT(PadPhase); ++V)
		{
			const float F = Freq(Root + Tones[V]) * (1.0f + (V - 1) * 0.0015f);
			PadPhase[V] = Frac(PadPhase[V] + F * Dt);
			Pad += FMath::Sin(PadPhase[V] * UE_TWO_PI);
		}
		Pad *= 0.05f * Swell;
		BassPhase = Frac(BassPhase + BassFreq * Dt);
		const float Tri = 1.0f - 4.0f * FMath::Abs(BassPhase - 0.5f);
		const float Bass = Tri * BassEnv * 0.22f;
		ArpPhase = Frac(ArpPhase + ArpFreq * Dt);
		const float S1 = FMath::Sin(ArpPhase * UE_TWO_PI);
		const float S2 = FMath::Sin(ArpPhase * 2.0f * UE_TWO_PI);
		const float Arp = (S1 + Piece.Brightness * 0.5f * S2) * ArpEnv * 0.09f;
		Noise = Noise * 1664525u + 1013904223u;
		const float White = (Noise >> 9) * (2.0f / 8388608.0f) - 1.0f;
		LowPass += (White - LowPass) * 0.6f;
		const float Hat = (White - LowPass) * HatEnv * 0.05f * (0.5f + Piece.Brightness);
		BassEnv *= BassDecay;
		ArpEnv *= ArpDecay;
		HatEnv *= HatDecay;
		const float Mix = (Pad + Bass + Arp + Hat) * Gain;
		Out[I * 2] = Mix + Arp * Gain * 0.15f;
		Out[I * 2 + 1] = Mix - Arp * Gain * 0.15f;
		++Sample;
	}
}

// ---- the chimes -----------------------------------------------------------------------------------------------------

void FScctHubChimes::Play(const FString& Cue)
{
	struct FAt
	{
		int32 Midi;
		float At;
	};
	TArray<FAt> Run;
	if (Cue == TEXT("start")) Run = { { 72, 0.0f }, { 79, 0.12f } };
	else if (Cue == TEXT("cp")) Run = { { 84, 0.0f } };
	else if (Cue == TEXT("finish")) Run = { { 76, 0.0f }, { 81, 0.14f } };
	else if (Cue == TEXT("best") || Cue == TEXT("fanfare")) Run = { { 72, 0.0f }, { 76, 0.1f }, { 79, 0.2f }, { 84, 0.3f }, { 88, 0.42f } };
	else if (Cue == TEXT("hit")) Run = { { 91, 0.0f } };
	else if (Cue == TEXT("head")) Run = { { 96, 0.0f }, { 91, 0.06f } };
	else if (Cue == TEXT("end")) Run = { { 79, 0.0f }, { 76, 0.14f }, { 72, 0.28f } };
	else if (Cue == TEXT("fall")) Run = { { 67, 0.0f }, { 60, 0.12f } };
	else Run = { { 84, 0.0f } };
	const float Gain = Cue == TEXT("hit") || Cue == TEXT("head") ? 0.18f : 0.28f;
	for (const FAt& Note : Run)
	{
		Notes.Add({ Freq(Note.Midi), Sample + static_cast<int64>(Note.At * Rate), Gain });
	}
	++NumPlayed;
}

void FScctHubChimes::Render(float* Out, int32 Frames)
{
	const float Dt = 1.0f / Rate;
	for (int32 I = 0; I < Frames; ++I)
	{
		float Mix = 0.0f;
		for (int32 N = Notes.Num() - 1; N >= 0; --N)
		{
			const FNote& Note = Notes[N];
			if (Sample < Note.Start)
			{
				continue;
			}
			const float Age = (Sample - Note.Start) * Dt;
			if (Age > 0.9f)
			{
				Notes.RemoveAtSwap(N, EAllowShrinking::No);
				continue;
			}
			const float Env = FMath::Exp(-Age * 6.0f) * FMath::Min(1.0f, Age * 400.0f);
			const float Phase = Note.Freq * Age * UE_TWO_PI;
			Mix += (FMath::Sin(Phase) + 0.35f * FMath::Sin(Phase * 2.0f)) * Env * Note.Gain;
		}
		Out[I * 2] = Mix;
		Out[I * 2 + 1] = Mix;
		++Sample;
	}
}

// ---- the hub's sounds -----------------------------------------------------------------------------------------------

const TCHAR* const FScctHubAudio::JukeboxVolumeSetting = TEXT("Audio.HubJukebox");
const TCHAR* const FScctHubAudio::ChimesVolumeSetting = TEXT("Audio.HubChimes");

FScctHubAudio::FScctHubAudio(TSharedPtr<FScctHubActivities> InActivities)
	: Activities(MoveTemp(InActivities))
	, Synth(MixRate)
	, Chimes(MixRate)
{
	bAtJukebox = FParse::Param(FCommandLine::Get(), TEXT("ScctHubJukebox"));

	FScctSettingDesc Jukebox;
	Jukebox.Key = JukeboxVolumeSetting;
	Jukebox.Page = TEXT("Audio");
	Jukebox.Group = NSLOCTEXT("ScctHub", "HubAudioGroup", "Hub");
	Jukebox.Label = NSLOCTEXT("ScctHub", "HubJukebox", "Jukebox music");
	Jukebox.Tooltip = NSLOCTEXT("ScctHub", "HubJukeboxTip", "How loud the hub's jukebox plays when you stand near it.");
	Jukebox.Type = EScctSettingType::Float;
	Jukebox.Default = TEXT("1");
	Jukebox.Min = 0.0f;
	Jukebox.Max = 1.0f;
	Jukebox.Step = 0.05f;
	FScctSettings::Register(Jukebox);

	FScctSettingDesc ChimesDesc = Jukebox;
	ChimesDesc.Key = ChimesVolumeSetting;
	ChimesDesc.Label = NSLOCTEXT("ScctHub", "HubChimes", "Activity chimes");
	ChimesDesc.Tooltip = NSLOCTEXT("ScctHub", "HubChimesTip", "How loud the chimes of the hub's courses, aim range and ball game play.");
	FScctSettings::Register(ChimesDesc);

	SummaryHandle = UScctAudioSubsystem::OnSummary().AddRaw(this, &FScctHubAudio::AddSummaryNotes);
}

FScctHubAudio::~FScctHubAudio()
{
	UScctAudioSubsystem::OnSummary().Remove(SummaryHandle);
}

void FScctHubAudio::Stop()
{
	for (FStream* Stream : { &Music, &Chime })
	{
		if (UAudioComponent* Component = Stream->Component.Get())
		{
			Component->Stop();
			Component->DestroyComponent();
		}
		Stream->Component.Reset();
		Stream->Wave.Reset();
		Stream->QueuedFrames = 0;
	}
	StreamWorld.Reset();
	bNoDevice = false;
	Gain = TargetGain = 0.0f;
	JukeboxDistance = -1.0f;
	bHasJukebox = false;
}

void FScctHubAudio::MakeStream(UWorld* World, FStream& Stream, bool bMusic, const TCHAR* Name)
{
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage(), Name);
	Wave->SetSampleRate(MixRate);
	Wave->NumChannels = 2;
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->bCanProcessAsync = true;

	UAudioComponent* Component = NewObject<UAudioComponent>(World->GetWorldSettings(), Name);
	Component->bAutoActivate = false;
	Component->bAutoDestroy = false;
	Component->bStopWhenOwnerDestroyed = true;
	Component->bIsMusic = bMusic;
	Component->bAllowSpatialization = bMusic;
	if (bMusic)
	{
		// Panned from the jukebox by the engine; the fall-off is the Godot client's own curve, applied as the volume.
		Component->bOverrideAttenuation = true;
		Component->AttenuationOverrides.bAttenuate = false;
		Component->AttenuationOverrides.bSpatialize = true;
		Component->AttenuationOverrides.StereoSpread = 120.0f;
	}
	Component->RegisterComponentWithWorld(World);
	Component->SetSound(Wave);
	Component->SetVolumeMultiplier(0.0f);
	Component->Play();

	Stream.Wave.Reset(Wave);
	Stream.Component = Component;
	Stream.QueuedFrames = 0;
}

bool FScctHubAudio::EnsureStreams(UWorld* World)
{
	if (StreamWorld.Get() != World)
	{
		Stop();
		StreamWorld = World;
	}
	if (bNoDevice)
	{
		return false;
	}
	if (Music.Component.IsValid() && Chime.Component.IsValid())
	{
		return true;
	}
	if (World->GetAudioDeviceRaw() == nullptr)
	{
		bNoDevice = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubAudio: no audio device (-nosound): the jukebox and the chimes are worked out but not heard"));
		return false;
	}
	MakeStream(World, Music, true, TEXT("ScctHubJukebox"));
	MakeStream(World, Chime, false, TEXT("ScctHubChimes"));
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubAudio: the jukebox and the chimes play from procedural waves (%d Hz stereo, %.2f s ahead)%s"), MixRate, LeadSeconds,
		bAtJukebox ? TEXT("; -ScctHubJukebox: heard as if at the jukebox") : TEXT(""));
	return true;
}

int64 FScctHubAudio::Fill(FStream& Stream, TFunctionRef<void(float*, int32)> Render)
{
	USoundWaveProcedural* Wave = Stream.Wave.Get();
	if (Wave == nullptr)
	{
		return 0;
	}
	const int32 Waiting = Wave->GetAvailableAudioByteCount() / (2 * static_cast<int32>(sizeof(int16)));
	const int64 Played = Stream.QueuedFrames - Waiting;
	const int32 Want = static_cast<int32>(MixRate * LeadSeconds) - Waiting;
	if (Want > 0)
	{
		if (Waiting == 0 && Stream.QueuedFrames > 0)
		{
			++Underruns;
		}
		Scratch.SetNumUninitialized(Want * 2, EAllowShrinking::No);
		Pcm.SetNumUninitialized(Want * 2, EAllowShrinking::No);
		Render(Scratch.GetData(), Want);
		for (int32 I = 0; I < Want * 2; ++I)
		{
			Pcm[I] = static_cast<int16>(FMath::Clamp(Scratch[I], -1.0f, 1.0f) * 32767.0f);
		}
		Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Want * 2 * sizeof(int16));
		Stream.QueuedFrames += Want;
	}
	return Played;
}

void FScctHubAudio::Tick(UWorld* World, float DeltaSeconds, bool bVerbose)
{
	if (World == nullptr || !Activities.IsValid() || !Activities->IsValid())
	{
		return;
	}

	for (const FString& Cue : Activities->GetCues())
	{
		Chimes.Play(Cue);
	}

	// HubAudio._Process: the server's track; full near the jukebox, squared down to nothing at its range.
	const FScctHubActivityState& State = Activities->GetState();
	Synth.SetTrack(FMath::Max(0, State.Track));
	bHasJukebox = Activities->FindMark(EScctHubMarkKind::Jukebox, JukeboxAt);
	TargetGain = 0.0f;
	JukeboxDistance = -1.0f;
	if (State.Track >= 0 && bHasJukebox)
	{
		JukeboxDistance = bAtJukebox ? 0.0f : static_cast<float>(FVector::Dist(Activities->GetEye(), JukeboxAt));
		const float T = FMath::Clamp(1.0f - (JukeboxDistance - JukeboxUseRange) / (JukeboxRange - JukeboxUseRange), 0.0f, 1.0f);
		TargetGain = T * T * JukeboxLoudest;
	}
	Gain = FMath::Lerp(Gain, TargetGain, FMath::Clamp(DeltaSeconds * 3.0f, 0.0f, 1.0f));

	if (EnsureStreams(World))
	{
		if (UAudioComponent* Component = Music.Component.Get())
		{
			// -ScctHubJukebox puts the sound at the ear too, so it is heard wherever the camera is.
			Component->SetWorldLocation(bAtJukebox ? Activities->GetEye() : JukeboxAt + FVector(0.0, 0.0, 96.0));
			Component->SetVolumeMultiplier(FScctSettings::GetFloat(JukeboxVolumeSetting));
		}
		if (UAudioComponent* Component = Chime.Component.Get())
		{
			Component->SetVolumeMultiplier(FScctSettings::GetFloat(ChimesVolumeSetting));
		}
		const float MusicGain = Gain;
		MusicPlayedFrames = Fill(Music, [this, MusicGain](float* Out, int32 Frames) { Synth.Render(Out, Frames, MusicGain); });
		ChimePlayedFrames = Fill(Chime, [this](float* Out, int32 Frames) { Chimes.Render(Out, Frames); });
	}

	LogTimer -= DeltaSeconds;
	if (bVerbose && LogTimer <= 0.0f)
	{
		LogTimer = 4.0f;
		const UAudioComponent* Component = Music.Component.Get();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubAudio: jukebox track %d '%s', %s, gain %.2f (aiming at %.2f); component %s, %.1f s of music and %.1f s of chimes mixed, %d chimes, %d underruns"),
			State.Track, *Activities->GetTrackTitle(),
			bHasJukebox ? *FString::Printf(TEXT("%.0f uu from the jukebox%s"), JukeboxDistance, bAtJukebox ? TEXT(" (-ScctHubJukebox)") : TEXT("")) : TEXT("no jukebox mark"),
			Gain, TargetGain, Component == nullptr ? TEXT("none") : Component->IsPlaying() ? TEXT("playing") : TEXT("stopped"),
			MusicPlayedFrames / static_cast<float>(MixRate), ChimePlayedFrames / static_cast<float>(MixRate), Chimes.GetNumPlayed(), Underruns);
	}
}

void FScctHubAudio::AddSummaryNotes(TArray<FString>& Notes) const
{
	const UAudioComponent* Component = Music.Component.Get();
	if (Component == nullptr)
	{
		if (bNoDevice)
		{
			Notes.Add(TEXT("hub jukebox not heard (no audio device)"));
		}
		return;
	}
	Notes.Add(FString::Printf(TEXT("hub jukebox voice %s (track %d, gain %.2f, %.1f s mixed), hub chimes %d played (%.1f s mixed)"),
		Component->IsPlaying() ? TEXT("playing") : TEXT("stopped"), Synth.GetTrack(), Gain, MusicPlayedFrames / static_cast<float>(MixRate),
		Chimes.GetNumPlayed(), ChimePlayedFrames / static_cast<float>(MixRate)));
}
