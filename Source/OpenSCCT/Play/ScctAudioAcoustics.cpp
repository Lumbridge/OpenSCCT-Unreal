// Sound through walls and openings for UScctAudioSubsystem: the part of the match's sound that asks the bridge's
// acoustic table (Runtime/ScctAcousticApi.h) about every 3D voice each frame and puts the answer on the voice.
//
// The bridge does what the Godot client's SoundSystem does between a voice and its filter (SoundSystem.Transmission,
// SoundSystem.Acoustics, AcousticScene, CornerPathSearch and rule 18 of SoundSystem.cs): it traces each voice's line
// to the listener at about 10 Hz, eases the levels every frame and finds the opening a blocked sound comes through.
// Here the answer becomes three things Unreal has on an audio component: the volume multiplier (the voice's own
// volume times the scale the bridge gives, which also carries the longer way round and rule 18's shrunk minimum
// distance), the low-pass filter (the high-frequency gain the wall and the way leave, as a cut-off) and the place the
// component stands at (the opening, when the sound is heard through one).

#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctAudioSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"

#define LOCTEXT_NAMESPACE "ScctAudioAcoustics"

namespace
{
	const FName WallSoundKey(TEXT("Audio.WallSound"));

	/** scct.AudioOcclusion: log every 3D voice's acoustics once a second. */
	int32 GAudioOcclusionLog = 0;
	FAutoConsoleVariableRef CVarAudioOcclusion(TEXT("scct.AudioOcclusion"), GAudioOcclusionLog,
		TEXT("1 logs every 3D voice's sound through walls once a second (occlusion, low-pass, volume scale, the place it is heard from and the way it takes), and at once when a wall comes between it and the listener or goes; 0 stops."));

	/** The engine's filter is not set again for a change smaller than this (a ratio of the cut-off). */
	constexpr float LowPassStep = 0.03f;
	constexpr float NoLowPass = 20000.0f;

	FString FlagsText(int32 Flags)
	{
		FString Text;
		Text += (Flags & EScctAcousticResultFlags::Blocked) != 0 ? TEXT("blocked") : TEXT("in sight");
		if ((Flags & EScctAcousticResultFlags::Indirect) != 0)
		{
			Text += (Flags & EScctAcousticResultFlags::Corner) != 0 ? TEXT(", round a corner") : TEXT(", through an opening");
		}
		return Text;
	}
}

void UScctAudioSubsystem::RegisterAcousticSettings()
{
	FScctSettingDesc Desc;
	Desc.Key = WallSoundKey;
	Desc.Page = TEXT("Audio");
	Desc.Group = LOCTEXT("Group", "Sound in the world");
	Desc.Label = LOCTEXT("WallSound", "Sound through walls");
	Desc.Tooltip = LOCTEXT("WallSoundTip",
		"Material: a wall muffles what is behind it by its material, and a sound with a way round is heard from the door or opening it comes through (the Godot client's default). "
		"Original: as the 2005 game without EAX, a wall only makes a sound quieter. Off: walls change nothing.");
	Desc.Type = EScctSettingType::Choice;
	Desc.Choices = { LOCTEXT("Material", "Material"), LOCTEXT("Original", "Original"), LOCTEXT("Off", "Off") };
	Desc.Default = TEXT("0");
	Desc.Min = 0.0f;
	Desc.Max = 2.0f;
	Desc.Order = 100;
	FScctSettings::Register(Desc);
}

void UScctAudioSubsystem::ReadAcousticSwitches()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("ScctAudioOcclusion")))
	{
		GAudioOcclusionLog = 1;
	}
	int32 Mode = INDEX_NONE;
	ForcedAcousticMode = FParse::Value(FCommandLine::Get(), TEXT("ScctAudioWallSound="), Mode) ? FMath::Clamp(Mode, 0, 2) : INDEX_NONE;

	ProbePlaces.Reset();
	ProbeCalls.Reset();
	FString Places;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctAudioProbe="), Places, false))
	{
		TArray<FString> Points;
		Places.ParseIntoArray(Points, TEXT(";"));
		for (const FString& Point : Points)
		{
			TArray<FString> Parts;
			if (Point.ParseIntoArray(Parts, TEXT(",")) == 3)
			{
				ProbePlaces.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])));
			}
		}
		ProbeSound = TEXT("Interface.Objectifs.Alarme");
		FParse::Value(FCommandLine::Get(), TEXT("ScctAudioProbeSound="), ProbeSound);
		ProbeIn = 2.0f;
		FParse::Value(FCommandLine::Get(), TEXT("ScctAudioProbeAt="), ProbeIn);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: -ScctAudioProbe: %d test emitters of '%s' in %.1f s"), ProbePlaces.Num(), *ProbeSound, ProbeIn);
	}
	if (ForcedAcousticMode != INDEX_NONE)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: -ScctAudioWallSound: sound through walls is mode %d for this run"), ForcedAcousticMode);
	}
}

void UScctAudioSubsystem::UpdateAudioProbe(float DeltaSeconds)
{
	if (ProbeIn < 0.0f || ProbePlaces.Num() == 0)
	{
		return;
	}
	ProbeIn -= DeltaSeconds;
	if (ProbeIn >= 0.0f)
	{
		return;
	}

	const FScctAudioCheckApi* Check = ScctFeature<FScctAudioCheckApi>();
	for (const FVector& Place : ProbePlaces)
	{
		// Through the bridge, so the emitter is an ordinary Play record with the sound's own radius and file.
		const int32 Call = Check != nullptr ? Check->PlaySoundAt(*ProbeSound, Place.X, Place.Y, Place.Z, 0.0f) : 0;
		if (Call != 0)
		{
			ProbeCalls.Add(Call);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioProbe: '%s' at (%.0f, %.0f, %.0f) %s; listener at (%.0f, %.0f, %.0f), %.0f uu away"), *ProbeSound,
			Place.X, Place.Y, Place.Z, Call != 0 ? *FString::Printf(TEXT("started as call %d"), Call) : Check == nullptr ? TEXT("not started: the bridge has no audio check table") : TEXT("not started: unknown sound or no file"),
			Listener.X, Listener.Y, Listener.Z, FVector::Dist(Place, Listener));
	}
}

void UScctAudioSubsystem::UpdateAcoustics(float DeltaSeconds)
{
	const int32 Mode = ForcedAcousticMode != INDEX_NONE ? ForcedAcousticMode : FMath::Clamp(FScctSettings::GetInt(WallSoundKey), 0, 2);
	AcousticSources.Reset();
	AcousticVoiceIndices.Reset();
	for (auto It = Voices.CreateIterator(); It; ++It)
	{
		FVoice& Voice = *It;
		if (!Voice.bPositional || Voice.bMusic || (Voice.State != EVoiceState::Waiting && Voice.State != EVoiceState::Playing))
		{
			continue;
		}
		if (Voice.AcousticKey == 0)
		{
			// Unique for the run, so a voice never inherits the state of an earlier one in its slot.
			NextAcousticKey = NextAcousticKey == MAX_int32 ? 1 : NextAcousticKey + 1;
			Voice.AcousticKey = NextAcousticKey;
		}
		FScctAcousticSource& Source = AcousticSources.AddDefaulted_GetRef();
		Source.Key = Voice.AcousticKey;
		Source.ActorId = Voice.ActorId;
		Source.Flags = Voice.bAtListener ? EScctAcousticSourceFlags::NeverOccluded : 0;
		Source.X = Voice.Location.X;
		Source.Y = Voice.Location.Y;
		Source.Z = Voice.Location.Z;
		Source.MinDistance = Voice.MinDistance;
		Source.Radius = Voice.Radius;
		AcousticVoiceIndices.Add(It.GetIndex());
	}

	// Asked even with no voice, so the bridge forgets the voices that ended.
	AcousticResults.SetNumUninitialized(AcousticSources.Num());
	const float ListenerPoint[3] = { static_cast<float>(Listener.X), static_cast<float>(Listener.Y), static_cast<float>(Listener.Z) };
	const int32 Count = Acoustics != nullptr && bListenerValid
		? Acoustics->Update(ListenerPoint, DeltaSeconds, Mode, AcousticSources.GetData(), AcousticSources.Num(), AcousticResults.GetData())
		: 0;
	if (Count != AcousticSources.Num())
	{
		// No answer (no table, no listener): every voice sounds as in the open.
		for (const int32 Index : AcousticVoiceIndices)
		{
			FVoice& Voice = Voices[Index];
			Voice.AcousticScale = 1.0f;
			Voice.AcousticLowPass = NoLowPass;
			Voice.AcousticOffset = FVector::ZeroVector;
			Voice.AcousticFlags = 0;
		}
		return;
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FScctAcousticResult& Result = AcousticResults[Index];
		FVoice& Voice = Voices[AcousticVoiceIndices[Index]];
		const int32 Changed = (Voice.AcousticFlags ^ Result.Flags) & (EScctAcousticResultFlags::Blocked | EScctAcousticResultFlags::Indirect);
		Voice.AcousticScale = FMath::Clamp(Result.VolumeScale, 0.0f, 1.0f);
		Voice.AcousticLowPass = FMath::Clamp(Result.LowPassHz, 20.0f, NoLowPass);
		Voice.AcousticOffset = FVector(Result.X, Result.Y, Result.Z) - Voice.Location;
		Voice.AcousticFlags = Result.Flags;

		Voice.AcousticLogIn -= DeltaSeconds;
		if ((GAudioOcclusionLog != 0 || Voice.bProbe) && (Voice.AcousticLogIn <= 0.0f || Changed != 0))
		{
			Voice.AcousticLogIn = 1.0f;
			LogAcoustics(Voice, Result);
		}
	}
}

void UScctAudioSubsystem::ApplyAcoustics(FVoice& Voice, bool bForce)
{
	UAudioComponent* Component = Voice.Component.Get();
	if (Component == nullptr)
	{
		return;
	}

	// The map's volume of the voice (raised or lowered by the scope's audio zoom), the player's volume of its kind
	// (Menu/ScctUserSettings.h) and the wall's scale.
	const float Volume = HeardVolumeOf(Voice);
	if (bForce || !FMath::IsNearlyEqual(Voice.AppliedVolume, Volume, 0.005f))
	{
		Voice.AppliedVolume = Volume;
		Component->SetVolumeMultiplier(Volume);
	}

	// Its pitch carries the Doppler shift of this frame (Play/ScctAudioView.cpp).
	const float Pitch = Voice.Pitch * Voice.DopplerPitch;
	if (bForce || !FMath::IsNearlyEqual(Voice.AppliedPitch, Pitch, 0.0005f))
	{
		Voice.AppliedPitch = Pitch;
		Component->SetPitchMultiplier(Pitch);
	}

	const float LowPass = Voice.AcousticLowPass;
	const bool bWasOn = Voice.AppliedLowPass < NoLowPass - 1.0f;
	const bool bOn = LowPass < NoLowPass - 1.0f;
	if (bForce || bWasOn != bOn || FMath::Abs(LowPass - Voice.AppliedLowPass) > LowPassStep * Voice.AppliedLowPass)
	{
		// The setters reach the playing engine voice only; the properties are kept too, for its next play (an ambient
		// one-shot plays again on its component) and for the log to read back.
		Voice.AppliedLowPass = LowPass;
		Component->bEnableLowPassFilter = bOn;
		Component->LowPassFilterFrequency = LowPass;
		Component->SetLowPassFilterEnabled(bOn);
		Component->SetLowPassFilterFrequency(LowPass);
	}
}

void UScctAudioSubsystem::LogAcoustics(const FVoice& Voice, const FScctAcousticResult& Result) const
{
	UScctAudioSubsystem* Self = const_cast<UScctAudioSubsystem*>(this);
	const FVector HeardFrom(Result.X, Result.Y, Result.Z);
	const UAudioComponent* Component = Voice.Component.Get();
	// What the engine's voice has, read back from its component (none without an audio device).
	const FString Engine = Component != nullptr
		? FString::Printf(TEXT("component at (%.0f, %.0f, %.0f), volume x%.2f, low-pass %s"), Component->GetComponentLocation().X, Component->GetComponentLocation().Y,
			Component->GetComponentLocation().Z, Component->VolumeMultiplier, Component->bEnableLowPassFilter ? *FString::Printf(TEXT("%.0f Hz"), Component->LowPassFilterFrequency) : TEXT("off"))
		: FString(TEXT("no engine voice"));
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioOcclusion: %s'%s' call %d actor %d at (%.0f, %.0f, %.0f), %.0f uu from the listener: %s, occlusion %.2f (rule 18 min distance %.0f of %.0f), gain %.2f HF %.2f -> LPF %.0f Hz, volume x%.2f, heard from (%.0f, %.0f, %.0f) %.0f uu from the source by a way of %.0f uu, %d opening(s), mix %.2f, zone %d; %s"),
		Voice.bProbe ? TEXT("probe ") : TEXT(""), *Self->GetName(Voice.FileId), Voice.CallId, Voice.ActorId, Voice.Location.X, Voice.Location.Y, Voice.Location.Z,
		FVector::Dist(Voice.Location, Listener), *FlagsText(Result.Flags), Result.Occlusion, Result.OccludedMinDistance, Voice.MinDistance, Result.DirectGain, Result.DirectHF,
		Result.LowPassHz, Result.VolumeScale, HeardFrom.X, HeardFrom.Y, HeardFrom.Z, FVector::Dist(HeardFrom, Voice.Location), Result.Distance, Result.Hops,
		Result.PropagationMix, Result.SourceZone, *Engine);
}

void UScctAudioSubsystem::LogAcousticSummary() const
{
	FScctAcousticInfo Info = {};
	if (Acoustics == nullptr || Acoustics->GetInfo(&Info) == 0)
	{
		return;
	}
	int32 Blocked = 0, Indirect = 0;
	for (const FVoice& Voice : Voices)
	{
		Blocked += (Voice.AcousticFlags & EScctAcousticResultFlags::Blocked) != 0 ? 1 : 0;
		Indirect += (Voice.AcousticFlags & EScctAcousticResultFlags::Indirect) != 0 ? 1 : 0;
	}
	const int32 Mode = ForcedAcousticMode != INDEX_NONE ? ForcedAcousticMode : FMath::Clamp(FScctSettings::GetInt(WallSoundKey), 0, 2);
	UE_LOG(LogOpenSCCT, Log, TEXT("Audio walls: mode %s, %d openings, %d zones, listener zone %d; %d voices tracked, %d behind a wall, %d heard by a way round; %lld line checks, %lld opening probes; %lld calls had their reach shrunk by a wall, %lld of them put out of reach"),
		Mode == 0 ? TEXT("material") : Mode == 1 ? TEXT("original") : TEXT("off"), Info.Portals, Info.Zones, Info.ListenerZone, Info.TrackedVoices, Blocked, Indirect,
		Info.LineChecks, Info.PortalProbes, Info.ReachShrunk, Info.ReachDropped);
}

#undef LOCTEXT_NAMESPACE
