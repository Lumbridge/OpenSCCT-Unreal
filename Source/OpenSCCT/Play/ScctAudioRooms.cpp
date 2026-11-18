// Source-room and adaptive reverb for UScctAudioSubsystem: the part of the match's sound that hears a source in the
// room it is in rather than only in the listener's.
//
// The Godot client keeps the listener's EAX environment on its main reverb and gives a source in another room that
// room's reverb (ReverbBuses.SourceRooms: four preallocated room networks, the last kept for gameplay sounds over
// ambience), each turned by what the way through the openings keeps (AcousticScene.ReflectedGain), and it moves every
// reverb toward the room size a bounded ray probe measures (ReverbBuses.Geometry, ReverbGeometryProbe, SoundSystem's
// AdaptiveReverb). Here:
//   - the bridge's reverb room table (Runtime/ScctReverbRoomApi.h) says which zone the listener and each routed 3D voice
//     are in, the ZoneInfo that names the zone's reverb in the map's audio table and the way to it;
//   - the main reverb stays the engine's (UpdateReverb, the listener zone's UReverbEffect); a voice in another zone sends
//     to one of four field submixes (children of the match's effects submix, so its volume and headroom apply), each
//     with a USubmixEffectReverbPreset set from that zone's environment and an output volume that follows the way;
//   - a voice cross-fades between the two: its attenuation's own reverb send is scaled by 1 - mix and its send to the
//     field by mix, the mix easing over about a quarter of a second; when the listener walks into the voice's room the
//     voice fades back to the main reverb, whose environment fades to the new room over a second, and the field keeps
//     ringing out with nothing new sent to it, so no tail is cut;
//   - the geometry probes trace 12 rays a frame, shared round-robin by the listener and the fields in use, and every
//     0.1 s the main reverb and each field move (0.5 s time constant) toward Godot's EaxEnvironment.WithGeometry of the
//     measured room over their zone's authored environment.

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctAudioMix.h"
#include "Play/ScctAudioSubsystem.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundSubmix.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "ISubmixBufferListener.h"

#include <atomic>

namespace
{
	/** Godot's MaxSourceRooms; the last is kept for gameplay sounds, so ambience cannot take every field. */
	constexpr int32 NumRoomFields = 4;
	/** The cross-fade between the main reverb and a field, and a field's output toward its way (Godot's 0.12 s). */
	constexpr float RoomMixSeconds = 0.08f;
	constexpr float RoomFieldGainSeconds = 0.12f;
	/** A voice's routing is put on its component again only for a change at least this big. */
	constexpr float RoomMixStep = 0.04f;
	constexpr float RoomSendStep = 0.02f;
	/** The geometry probes' rays a frame (Godot: 12 a batch at 60 Hz) and the time between reverb packets. */
	constexpr int32 GeometryRaysPerFrame = 12;
	constexpr int32 GeometryRaysPerCall = 4;
	constexpr double GeometryPacketSeconds = 0.1;
	/** After the listener changes zone the main reverb fades to the new room this long before the probe moves it. */
	constexpr double ZoneFadeSeconds = 1.0;
	/** The engine's fade between two adaptive packets of the main reverb. */
	constexpr float AdaptiveFadeSeconds = 0.1f;

	/** An EAX environment in the terms the adaptive reverb moves it in: levels in millibels, times in seconds. */
	struct FScctRoomEax
	{
		float Size = 7.5f;        // metres (the cube root of the volume once measured)
		float Diffusion = 1.0f;
		float Room = -1000.0f;    // mB
		float RoomHF = -100.0f;   // mB
		float Decay = 1.49f;
		float DecayHF = 0.83f;
		float Reflections = -2602.0f; // mB
		float ReflectionsDelay = 0.007f;
		float Reverb = 200.0f;    // mB
		float ReverbDelay = 0.011f;
		float AirAbsorptionHF = 0.994f; // linear, as the reverb takes it

		static float ToMillibels(float Gain) { return Gain > 0.0f ? 2000.0f * FMath::LogX(10.0f, Gain) : -10000.0f; }
		static float FromMillibels(float Millibels) { return FMath::Pow(10.0f, Millibels / 2000.0f); }

		static FScctRoomEax From(const UReverbEffect& Effect)
		{
			FScctRoomEax Eax;
			Eax.Size = FMath::Max(FMath::Pow(FMath::Max(Effect.Density, 0.0f) * 16.0f, 1.0f / 3.0f), 1.0f);
			Eax.Diffusion = Effect.Diffusion;
			Eax.Room = ToMillibels(Effect.Gain);
			Eax.RoomHF = ToMillibels(Effect.GainHF);
			Eax.Decay = Effect.DecayTime;
			Eax.DecayHF = Effect.DecayHFRatio;
			Eax.Reflections = ToMillibels(Effect.ReflectionsGain);
			Eax.ReflectionsDelay = Effect.ReflectionsDelay;
			Eax.Reverb = ToMillibels(Effect.LateGain);
			Eax.ReverbDelay = Effect.LateDelay;
			Eax.AirAbsorptionHF = Effect.AirAbsorptionGainHF;
			return Eax;
		}

		/** Godot's EaxEnvironment.WithGeometry: the measured room's size, decay and delays over the authored levels. */
		FScctRoomEax WithGeometry(const FScctReverbGeometry& Measured) const
		{
			if (Measured.Valid == 0)
			{
				return *this;
			}
			FScctRoomEax Eax = *this;
			Eax.Size = Measured.Size;
			Eax.Diffusion = FMath::Clamp(Measured.Diffusion * 0.65f + Diffusion * 0.35f, 0.0f, 1.0f);
			Eax.Room = FMath::RoundToFloat(FMath::Clamp(-600.0f * 0.65f + Room * 0.35f + 2000.0f * FMath::LogX(10.0f, FMath::Max(Measured.WetGain, 0.00001f)), -10000.0f, 0.0f));
			Eax.Decay = Measured.Decay;
			Eax.DecayHF = Measured.HFDecayRatio;
			Eax.Reflections = FMath::RoundToFloat(FMath::Clamp(-600.0f * 0.75f + Reflections * 0.25f - 200.0f * FMath::LogX(10.0f, 1.0f + Measured.EarlyDelay * 343.0f / 6.0f), -10000.0f, 1000.0f));
			Eax.ReflectionsDelay = Measured.EarlyDelay;
			Eax.Reverb = FMath::RoundToFloat(FMath::Clamp(Reverb * 0.25f, -10000.0f, 2000.0f));
			Eax.ReverbDelay = Measured.LateDelay;
			return Eax;
		}

		/** Godot's EaxEnvironment.Approach: every value eases toward Target with a 0.5 s time constant. */
		FScctRoomEax Approach(const FScctRoomEax& Target, float Seconds) const
		{
			if (!(Seconds > 0.0f))
			{
				return *this;
			}
			const float T = 1.0f - FMath::Exp(-Seconds / 0.5f);
			FScctRoomEax Eax;
			const auto Ease = [T](float From, float To) { return From + (To - From) * T; };
			Eax.Size = Ease(Size, Target.Size);
			Eax.Diffusion = Ease(Diffusion, Target.Diffusion);
			Eax.Room = FMath::RoundToFloat(Ease(Room, Target.Room));
			Eax.RoomHF = FMath::RoundToFloat(Ease(RoomHF, Target.RoomHF));
			Eax.Decay = Ease(Decay, Target.Decay);
			Eax.DecayHF = Ease(DecayHF, Target.DecayHF);
			Eax.Reflections = FMath::RoundToFloat(Ease(Reflections, Target.Reflections));
			Eax.ReflectionsDelay = Ease(ReflectionsDelay, Target.ReflectionsDelay);
			Eax.Reverb = FMath::RoundToFloat(Ease(Reverb, Target.Reverb));
			Eax.ReverbDelay = Ease(ReverbDelay, Target.ReverbDelay);
			Eax.AirAbsorptionHF = Ease(AirAbsorptionHF, Target.AirAbsorptionHF);
			return Eax;
		}

		bool Near(const FScctRoomEax& Other) const
		{
			return FMath::Abs(Size - Other.Size) < 0.01f && FMath::Abs(Diffusion - Other.Diffusion) < 0.002f && FMath::Abs(Room - Other.Room) < 1.0f
				&& FMath::Abs(Decay - Other.Decay) < 0.005f && FMath::Abs(DecayHF - Other.DecayHF) < 0.002f && FMath::Abs(Reflections - Other.Reflections) < 1.0f
				&& FMath::Abs(ReflectionsDelay - Other.ReflectionsDelay) < 0.0005f && FMath::Abs(Reverb - Other.Reverb) < 1.0f
				&& FMath::Abs(ReverbDelay - Other.ReverbDelay) < 0.0005f;
		}

		/** Into Unreal's reverb, which keeps the EAX model; HF scales the room's high frequencies by what the way keeps. */
		void Put(UReverbEffect& Effect, float HF = 1.0f) const
		{
			Effect.Density = FMath::Clamp(Size * Size * Size / 16.0f, 0.0f, 1.0f);
			Effect.Diffusion = FMath::Clamp(Diffusion, 0.0f, 1.0f);
			Effect.Gain = FMath::Clamp(FromMillibels(Room), 0.0f, 1.0f);
			Effect.GainHF = FMath::Clamp(FromMillibels(RoomHF) * FMath::Clamp(HF, 0.0f, 1.0f), 0.0f, 1.0f);
			Effect.DecayTime = FMath::Clamp(Decay, 0.1f, 20.0f);
			Effect.DecayHFRatio = FMath::Clamp(DecayHF, 0.1f, 2.0f);
			Effect.ReflectionsGain = FMath::Clamp(FromMillibels(Reflections), 0.0f, 3.16f);
			Effect.ReflectionsDelay = FMath::Clamp(ReflectionsDelay, 0.0f, 0.3f);
			Effect.LateGain = FMath::Clamp(FromMillibels(Reverb), 0.0f, 10.0f);
			Effect.LateDelay = FMath::Clamp(ReverbDelay, 0.0f, 0.1f);
			Effect.AirAbsorptionGainHF = FMath::Clamp(AirAbsorptionHF, 0.0f, 1.0f);
		}
	};

	/** The adaptive state the header does not see: each field's and the main reverb's environment as last applied. */
	struct FScctRoomAdaptive
	{
		FScctRoomEax Main;
		int32 MainZone = -2;
		bool bMainSet = false;
		FScctRoomEax Fields[NumRoomFields];
		bool bFieldSet[NumRoomFields] = {};
		/** Each field submix's largest output sample over the last second (-1: not measured). */
		float FieldPeaks[NumRoomFields] = { -1.0f, -1.0f, -1.0f, -1.0f };
		float PeakTimer = 1.0f;
	};

	TMap<const UObject*, FScctRoomAdaptive>& RoomAdaptiveStates()
	{
		static TMap<const UObject*, FScctRoomAdaptive> States;
		return States;
	}

	/**
	 * The largest sample a field submix rendered (its reverb's wet output after the field's volume), for the log of
	 * -ScctAudioRooms=log and the probe emitters: written on the audio render thread, read and cleared on the game thread.
	 */
	class FScctRoomFieldPeak : public ISubmixBufferListener
	{
	public:
		explicit FScctRoomFieldPeak(FString InName) : Name(MoveTemp(InName)) {}

		virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override
		{
			float Largest = 0.0f;
			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				Largest = FMath::Max(Largest, FMath::Abs(AudioData[Index]));
			}
			float Known = Peak.load(std::memory_order_relaxed);
			while (Largest > Known && !Peak.compare_exchange_weak(Known, Largest, std::memory_order_relaxed))
			{
			}
		}

		virtual const FString& GetListenerName() const override { return Name; }

		float Take() { return Peak.exchange(0.0f, std::memory_order_relaxed); }

	private:
		FString Name;
		std::atomic<float> Peak{ 0.0f };
	};

	TMap<const UObject*, TArray<TSharedRef<FScctRoomFieldPeak, ESPMode::ThreadSafe>>>& RoomFieldPeaks()
	{
		static TMap<const UObject*, TArray<TSharedRef<FScctRoomFieldPeak, ESPMode::ThreadSafe>>> Peaks;
		return Peaks;
	}

	/** The attenuation's linear wet level at Distance (FindOrCreateAttenuation: 0.12 at the minimum distance to 0.5). */
	float RoomWetAt(const FSoundAttenuationSettings& Settings, float Distance)
	{
		const float Span = FMath::Max(Settings.ReverbDistanceMax - Settings.ReverbDistanceMin, 1.0f);
		const float Alpha = FMath::Clamp((Distance - Settings.ReverbDistanceMin) / Span, 0.0f, 1.0f);
		return FMath::Lerp(Settings.ReverbWetLevelMin, Settings.ReverbWetLevelMax, Alpha);
	}
}

void UScctAudioSubsystem::ReadRoomSwitches()
{
	FString Rooms;
	bRoomsEnabled = true;
	bAdaptiveEnabled = true;
	bLogRooms = false;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctAudioRooms="), Rooms))
	{
		bRoomsEnabled = bAdaptiveEnabled = Rooms != TEXT("0");
		bLogRooms = Rooms.Equals(TEXT("log"), ESearchCase::IgnoreCase);
	}

	ForcedListenerPlaces.Reset();
	ForcedListenerAt = INDEX_NONE;
	ForcedListenerClock = 0.0f;
	FString Places;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctAudioListener="), Places, false))
	{
		TArray<FString> Points;
		Places.ParseIntoArray(Points, TEXT(";"));
		for (const FString& Point : Points)
		{
			TArray<FString> Parts;
			if (Point.ParseIntoArray(Parts, TEXT(",")) == 3)
			{
				ForcedListenerPlaces.Add(FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])));
			}
		}
		ForcedListenerEvery = 8.0f;
		FParse::Value(FCommandLine::Get(), TEXT("ScctAudioListenerEvery="), ForcedListenerEvery);
		ForcedListenerEvery = FMath::Max(ForcedListenerEvery, 0.5f);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: -ScctAudioListener: the listener is held at %d place(s), %.1f s each"), ForcedListenerPlaces.Num(), ForcedListenerEvery);
	}

	FString Error;
	RoomApi = ScctFeature<FScctReverbRoomApi>(&Error);
	UE_CLOG(RoomApi == nullptr, LogOpenSCCT, Warning, TEXT("Audio: every voice keeps the listener's reverb this match, the bridge's reverb room table is missing: %s"), *Error);
	UE_CLOG(!bRoomsEnabled, LogOpenSCCT, Log, TEXT("ScctAudioRooms: -ScctAudioRooms=0: no source-room or adaptive reverb this match"));
}

void UScctAudioSubsystem::UpdateForcedListener(float DeltaSeconds)
{
	if (ForcedListenerPlaces.Num() == 0)
	{
		return;
	}
	ForcedListenerClock += DeltaSeconds;
	const int32 At = FMath::Min(FMath::FloorToInt32(ForcedListenerClock / ForcedListenerEvery), ForcedListenerPlaces.Num() - 1);
	if (At != ForcedListenerAt)
	{
		ForcedListenerAt = At;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: -ScctAudioListener: the listener is now held at (%.0f, %.0f, %.0f) (place %d of %d, %.1f s into the match; the camera stays %s)"),
			ForcedListenerPlaces[At].X, ForcedListenerPlaces[At].Y, ForcedListenerPlaces[At].Z, At + 1, ForcedListenerPlaces.Num(), ForcedListenerClock,
			bListenerValid ? *FString::Printf(TEXT("at (%.0f, %.0f, %.0f)"), Listener.X, Listener.Y, Listener.Z) : TEXT("where it is"));
	}
	Listener = ForcedListenerPlaces[At];
	bListenerValid = true;
}

void UScctAudioSubsystem::CreateRoomFields()
{
	ReleaseRoomFields();
	RoomFields.SetNum(NumRoomFields);
	for (int32 Index = 0; Index < NumRoomFields; ++Index)
	{
		RoomFields[Index] = FRoomField();
		RoomFields[Index].bReservedForGameplay = Index == NumRoomFields - 1;
	}
	RoomAdaptiveStates().Add(this, FScctRoomAdaptive());

	RoomDevice = GetWorld()->GetAudioDevice();
	FAudioDevice* Device = RoomDevice.GetAudioDevice();
	USoundSubmix* World = Mix != nullptr ? Mix->GetSubmix(EScctAudioCategory::World) : nullptr;
	if (Device == nullptr || !bRoomsEnabled)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: %d source-room fields kept %s"), NumRoomFields,
			!bRoomsEnabled ? TEXT("off (-ScctAudioRooms=0)") : TEXT("as bookkeeping only (no audio device)"));
		return;
	}

	for (int32 Index = 0; Index < NumRoomFields; ++Index)
	{
		UReverbEffect* Effect = NewObject<UReverbEffect>(this);
		Effect->Density = DefaultReverb->Density;
		FScctRoomEax::From(*DefaultReverb).Put(*Effect);
		// The mixer keeps registered submixes rooted until its audio-thread unregister runs.
		// Neither the submix nor its retained preset may own the departing world during travel.
		UObject* Outer = GetTransientPackage();
		const FName RoomName(*FString::Printf(TEXT("ScctSourceRoom%d"), Index));
		USoundSubmix* Submix = NewObject<USoundSubmix>(Outer, MakeUniqueObjectName(Outer, USoundSubmix::StaticClass(), RoomName), RF_Transient);
		USubmixEffectReverbPreset* Preset = NewObject<USubmixEffectReverbPreset>(Submix);
		Preset->SetSettingsWithReverbEffect(Effect, 1.0f, 0.0f);
		// A child of the match's effects submix, so its headroom and the device's master apply to the room as to the
		// voices it comes from (Godot's source-room buses send to SvmWorld).
		Submix->ParentSubmix = World;
		Submix->SubmixEffectChain.Add(Preset);
		Device->RegisterSoundSubmix(Submix, true);
		Device->SetSubmixOutputVolume(Submix, 0.0f);
		RoomEffects.Add(Effect);
		RoomPresets.Add(Preset);
		RoomSubmixes.Add(Submix);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AdaptiveMainEffects.Add(NewObject<UReverbEffect>(this));
	}
	if (bLogRooms || ProbePlaces.Num() > 0)
	{
		// What each field's reverb puts out, said in the routing log once a second.
		TArray<TSharedRef<FScctRoomFieldPeak, ESPMode::ThreadSafe>>& Peaks = RoomFieldPeaks().FindOrAdd(this);
		for (int32 Index = 0; Index < RoomSubmixes.Num(); ++Index)
		{
			Peaks.Add(MakeShared<FScctRoomFieldPeak, ESPMode::ThreadSafe>(FString::Printf(TEXT("ScctRoomFieldPeak%d"), Index)));
			Device->RegisterSubmixBufferListener(Peaks.Last(), *RoomSubmixes[Index]);
		}
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: %d source-room reverb submixes registered under %s (the last kept for gameplay sounds); adaptive reverb %s"),
		RoomSubmixes.Num(), World != nullptr ? *World->GetName() : TEXT("the main submix"), bAdaptiveEnabled ? TEXT("on") : TEXT("off"));
}

void UScctAudioSubsystem::ReleaseRoomFields()
{
	if (FAudioDevice* Device = RoomDevice.GetAudioDevice())
	{
		Device->DeactivateReverbEffect(TEXT("ScctZone"));
		const TArray<TSharedRef<FScctRoomFieldPeak, ESPMode::ThreadSafe>>* Peaks = RoomFieldPeaks().Find(this);
		for (int32 Index = 0; Index < RoomSubmixes.Num(); ++Index)
		{
			if (RoomSubmixes[Index] == nullptr)
			{
				continue;
			}
			if (Peaks != nullptr && Peaks->IsValidIndex(Index))
			{
				Device->UnregisterSubmixBufferListener((*Peaks)[Index], *RoomSubmixes[Index]);
			}
			Device->UnregisterSoundSubmix(RoomSubmixes[Index], false);
		}
	}
	RoomFieldPeaks().Remove(this);
	RoomDevice.Reset();
	RoomSubmixes.Reset();
	RoomPresets.Reset();
	RoomEffects.Reset();
	AdaptiveMainEffects.Reset();
	RoomFields.Reset();
	RoomAdaptiveStates().Remove(this);
	if (RoomApi != nullptr)
	{
		RoomApi->Reset(-1);
	}
	ListenerZoneIndex = 0;
	ListenerRoomZone = INDEX_NONE;
	ListenerZoneChangedAt = -1000.0;
	ListenerGeometry = {};
	bListenerAdapted = false;
	RoomClock = NextGeometryPacket = LastGeometryPacket = 0.0;
	NumRoomCrossfades = NumRoomFieldClaims = NumRoomRefused = NumGeometryPackets = 0;
}

int32 UScctAudioSubsystem::ZoneOfRoom(const FScctReverbRoom& Room) const
{
	if ((Room.Flags & EScctReverbRoomFlags::HasZoneInfo) == 0)
	{
		return INDEX_NONE;
	}
	const FVector Place(Room.ZoneX, Room.ZoneY, Room.ZoneZ);
	for (int32 Zone = 0; Zone < Zones.Num(); ++Zone)
	{
		if (Zones[Zone].Location.Equals(Place, 1.0))
		{
			return Zone;
		}
	}
	return INDEX_NONE;
}

FString UScctAudioSubsystem::RoomName(int32 Zone) const
{
	if (!Zones.IsValidIndex(Zone))
	{
		return TEXT("the default room");
	}
	FString Effect = Zones[Zone].EffectClass;
	Effect.RemoveFromStart(TEXT("SBase."));
	return FString::Printf(TEXT("%s %s, decay %.2f s"), *Zones[Zone].Id, *Effect, Zones[Zone].DecayTime);
}

int32 UScctAudioSubsystem::FindRoomField(int32 ZoneIndex, int32 Zone, bool bAmbient, bool bPathValid, float Reflected, const FVector& Anchor)
{
	int32 Available = INDEX_NONE;
	for (int32 Index = 0; Index < RoomFields.Num(); ++Index)
	{
		FRoomField& Field = RoomFields[Index];
		if (bAmbient && Field.bReservedForGameplay)
		{
			continue;
		}
		if (Field.ZoneIndex == ZoneIndex)
		{
			Field.LastUsed = RoomClock;
			if (!Field.bCandidate || FVector::DistSquared(Anchor, Field.Anchor) < FVector::DistSquared(Field.Candidate, Field.Anchor))
			{
				Field.Candidate = Anchor;
				Field.bCandidate = true;
			}
			return Index;
		}
		// A field is free once its last tail has rung out.
		if (Field.ZoneIndex == 0 || RoomClock - Field.LastUsed > FMath::Max(2.0f, Field.TailSeconds) + 0.5)
		{
			Available = Index;
		}
	}
	// A passage closed now silences an existing field; it never hands a new room the listener's reverb.
	if (!bPathValid)
	{
		++NumRoomRefused;
		return INDEX_NONE;
	}
	bool bTaken = false;
	if (Available == INDEX_NONE)
	{
		// Every field busy. Godot then leaves the new room on the listener's reverb; here a room whose way keeps at least
		// twice what the faintest field's way keeps takes that field over (it has held its room a second at least), so a
		// near room is not kept off by ambience far away. That field's voices go back to the main reverb at once.
		int32 Weakest = INDEX_NONE;
		for (int32 Index = 0; Index < RoomFields.Num(); ++Index)
		{
			const FRoomField& Field = RoomFields[Index];
			if ((bAmbient && Field.bReservedForGameplay) || RoomClock - Field.ClaimedAt < 1.0)
			{
				continue;
			}
			if (Weakest == INDEX_NONE || Field.Gain < RoomFields[Weakest].Gain)
			{
				Weakest = Index;
			}
		}
		if (Weakest == INDEX_NONE || RoomFields[Weakest].Gain * 2.0f > Reflected)
		{
			++NumRoomRefused;
			return INDEX_NONE;
		}
		Available = Weakest;
		bTaken = true;
		for (FVoice& Voice : Voices)
		{
			if (Voice.RoomField == Available)
			{
				Voice.RoomField = INDEX_NONE;
				Voice.RoomMix = 0.0f;
				Voice.RoomTarget = INDEX_NONE;
			}
		}
	}

	FRoomField& Field = RoomFields[Available];
	const int32 OldZone = Field.ZoneIndex;
	const float OldGain = Field.Gain;
	Field.ClaimedAt = RoomClock;
	// A one-shot excites its room from its first block (Godot applies the way's level at once on a claim).
	Field.Gain = Reflected;
	Field.ZoneIndex = ZoneIndex;
	Field.Zone = Zone;
	Field.LastUsed = RoomClock;
	Field.Anchor = Field.Candidate = Anchor;
	Field.bCandidate = true;
	Field.Geometry = {};
	Field.bAdapted = false;
	Field.LogIn = 0.0f;
	const UReverbEffect* Authored = Zones.IsValidIndex(Zone) ? ZoneEffects[Zones[Zone].Effect].Get() : DefaultReverb.Get();
	// EAX's tail: twice the longer of the decays plus the delays (Godot's EaxEnvironment.TailSeconds).
	Field.TailSeconds = 2.0f * Authored->DecayTime * FMath::Max(1.0f, Authored->DecayHFRatio) + Authored->ReflectionsDelay + Authored->LateDelay;
	if (FScctRoomAdaptive* State = RoomAdaptiveStates().Find(this))
	{
		State->Fields[Available] = FScctRoomEax::From(*Authored);
		State->bFieldSet[Available] = true;
	}
	if (RoomApi != nullptr)
	{
		RoomApi->Reset(Available + 1);
	}
	if (RoomPresets.IsValidIndex(Available))
	{
		FScctRoomEax::From(*Authored).Put(*RoomEffects[Available], Field.HF);
		RoomPresets[Available]->SetSettingsWithReverbEffect(RoomEffects[Available], 1.0f, 0.0f);
	}
	++NumRoomFieldClaims;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: field %d now carries zone %d (%s), whose way keeps %.2f%s"), Available, ZoneIndex, *RoomName(Zone), Reflected,
		bTaken ? *FString::Printf(TEXT(", taken from zone %d whose way kept only %.2f"), OldZone, OldGain)
			: OldZone != 0 ? *FString::Printf(TEXT(", after zone %d had rung out"), OldZone) : TEXT(""));
	return Available;
}

void UScctAudioSubsystem::UpdateRooms(float DeltaSeconds)
{
	if (RoomApi == nullptr || !bRoomsEnabled || !bListenerValid || RoomFields.Num() == 0)
	{
		return;
	}
	RoomClock += DeltaSeconds;

	// The listener first, then every 3D voice that is heard in a room (not the local pawn's own or the view actor's
	// sounds, which move with the listener, nor music).
	RoomPoints.Reset();
	RoomVoiceIndices.Reset();
	RoomPoints.Add(Listener);
	for (auto It = Voices.CreateIterator(); It; ++It)
	{
		const FVoice& Voice = *It;
		if (Voice.bPositional && !Voice.bMusic && !Voice.bAtListener && Voice.State == EVoiceState::Playing && !IsListenerBound(Voice))
		{
			RoomPoints.Add(Voice.Location);
			RoomVoiceIndices.Add(It.GetIndex());
		}
	}
	TArray<float, TInlineAllocator<192>> Floats;
	Floats.SetNumUninitialized(RoomPoints.Num() * 3);
	for (int32 Index = 0; Index < RoomPoints.Num(); ++Index)
	{
		Floats[Index * 3] = RoomPoints[Index].X;
		Floats[Index * 3 + 1] = RoomPoints[Index].Y;
		Floats[Index * 3 + 2] = RoomPoints[Index].Z;
	}
	RoomResults.SetNumUninitialized(RoomPoints.Num());
	const float ListenerPoint[3] = { static_cast<float>(Listener.X), static_cast<float>(Listener.Y), static_cast<float>(Listener.Z) };
	// The acoustic table keeps the ways up to date only with Sound through walls on Material; otherwise this call does.
	const int32 WallMode = ForcedAcousticMode != INDEX_NONE ? ForcedAcousticMode : FMath::Clamp(FScctSettings::GetInt(FName(TEXT("Audio.WallSound"))), 0, 2);
	const bool bUpdateScene = Acoustics == nullptr || WallMode != 0;
	if (RoomApi->Locate(ListenerPoint, DeltaSeconds, bUpdateScene ? 1 : 0, Floats.GetData(), RoomPoints.Num(), RoomResults.GetData()) != RoomPoints.Num())
	{
		return;
	}

	// The listener's zone: zone 0 (solid space, a doorway's edge) keeps the last one, as rule 23 keeps it.
	const FScctReverbRoom& Heard = RoomResults[0];
	int32 SwitchedFrom = INDEX_NONE;
	if (Heard.ZoneIndex != 0 && Heard.ZoneIndex != ListenerZoneIndex)
	{
		SwitchedFrom = ListenerZoneIndex;
		ListenerZoneIndex = Heard.ZoneIndex;
		ListenerZoneChangedAt = RoomClock;
		ListenerGeometry = {};
		bListenerAdapted = false;
		RoomApi->Reset(0);
	}
	if (Heard.ZoneIndex != 0)
	{
		ListenerRoomZone = ZoneOfRoom(Heard);
	}
	// The main reverb (UpdateReverb) is the zone this table finds, which follows a forced listener too.
	ListenerZone = ListenerRoomZone;

	if (FScctRoomAdaptive* State = RoomAdaptiveStates().Find(this))
	{
		State->PeakTimer -= DeltaSeconds;
		if (State->PeakTimer <= 0.0f)
		{
			State->PeakTimer = 1.0f;
			if (TArray<TSharedRef<FScctRoomFieldPeak, ESPMode::ThreadSafe>>* Peaks = RoomFieldPeaks().Find(this))
			{
				for (int32 Index = 0; Index < Peaks->Num() && Index < NumRoomFields; ++Index)
				{
					State->FieldPeaks[Index] = (*Peaks)[Index]->Take();
				}
			}
		}
	}

	int32 ToMain = 0, ToField = 0;
	for (FRoomField& Field : RoomFields)
	{
		Field.bCandidate = false;
	}
	for (int32 Index = 0; Index < RoomVoiceIndices.Num(); ++Index)
	{
		FVoice& Voice = Voices[RoomVoiceIndices[Index]];
		const FScctReverbRoom& Room = RoomResults[Index + 1];
		const int32 OldTarget = Voice.RoomTarget;
		// A source in solid space or the default zone counts as in the listener's room.
		const bool bOtherRoom = Room.ZoneIndex != 0 && Room.ZoneIndex != ListenerZoneIndex;
		Voice.RoomZone = Room.ZoneIndex;
		int32 Target = INDEX_NONE;
		if (bOtherRoom)
		{
			Target = FindRoomField(Room.ZoneIndex, ZoneOfRoom(Room), Voice.AmbientId != 0, (Room.Flags & EScctReverbRoomFlags::PathValid) != 0,
				Room.ReflectedGain, Voice.Location);
		}

		// The cross-fade: towards the target field, through the main reverb when the voice moves from one field to another.
		// A voice routed for the first time starts where it belongs (a shot excites its room from its first block, as
		// Godot's reflected input does on a new room).
		const float Ease = OldTarget == -2 ? 1.0f : DeltaSeconds > 0.0f ? 1.0f - FMath::Exp(-DeltaSeconds / RoomMixSeconds) : 0.0f;
		Voice.RoomTarget = Target;
		if (Target != INDEX_NONE && Target == Voice.RoomField)
		{
			Voice.RoomMix = Voice.RoomMix + (1.0f - Voice.RoomMix) * Ease;
			Voice.RoomMix = Voice.RoomMix > 0.99f ? 1.0f : Voice.RoomMix;
		}
		else if (Voice.RoomField != INDEX_NONE)
		{
			Voice.RoomMix = Voice.RoomMix * (1.0f - Ease);
			if (Voice.RoomMix < 0.01f)
			{
				Voice.RoomMix = 0.0f;
				Voice.RoomField = Target;
			}
		}
		else if (Target != INDEX_NONE)
		{
			Voice.RoomField = Target;
			Voice.RoomMix = Ease;
		}

		if (OldTarget != Target)
		{
			const bool bGoesToField = Target != INDEX_NONE;
			const TCHAR* From = OldTarget == -2 ? TEXT("starts") : TEXT("cross-fades");
			FString How;
			if (OldTarget == -2)
			{
				How = bGoesToField ? FString::Printf(TEXT("on source-room field %d"), Target) : FString(TEXT("on the main reverb"));
			}
			else
			{
				++NumRoomCrossfades;
				(bGoesToField ? ToField : ToMain) += 1;
				How = FString::Printf(TEXT("from %s to %s over about %.2f s"),
					OldTarget == INDEX_NONE ? TEXT("the main reverb") : *FString::Printf(TEXT("source-room field %d"), OldTarget),
					bGoesToField ? *FString::Printf(TEXT("source-room field %d"), Target) : TEXT("the main reverb (the field rings out)"), RoomMixSeconds * 4.6f);
			}
			UE_CLOG(bLogRooms || Voice.bProbe || (SwitchedFrom != INDEX_NONE && OldTarget != -2), LogOpenSCCT, Log,
				TEXT("ScctAudioRooms: %s'%s' call %d in zone %d (%s) %s %s; the listener is in zone %d (%s)"),
				Voice.bProbe ? TEXT("probe ") : TEXT(""), *GetName(Voice.FileId), Voice.CallId, Room.ZoneIndex, *RoomName(ZoneOfRoom(Room)), From, *How,
				ListenerZoneIndex, *RoomName(ListenerZone));
		}

		// The distances the sends are measured at: where the voice is heard from (its attenuation's input), and from the
		// source to the opening its room's sound leaves by (Godot's inputDistance of the reflected room).
		const float HeardDistance = FVector::Dist(Voice.Location + Voice.AcousticOffset, Listener);
		const FVector Opening(Room.OpeningX, Room.OpeningY, Room.OpeningZ);
		const float InputDistance = (Room.Flags & EScctReverbRoomFlags::PathValid) != 0 && Room.Hops > 0 ? FVector::Dist(Voice.Location, Opening) : HeardDistance;
		ApplyRoomRouting(Voice, HeardDistance, InputDistance);

		Voice.RoomLogIn -= DeltaSeconds;
		if ((bLogRooms || Voice.bProbe) && Voice.RoomLogIn <= 0.0f)
		{
			Voice.RoomLogIn = 1.0f;
			const FRoomField* Field = RoomFields.IsValidIndex(Voice.RoomField) ? &RoomFields[Voice.RoomField] : nullptr;
			// What the engine has: the component's reverb wet levels (its own attenuation's, or this voice's override) and
			// the field submix's output over the last second.
			const UAudioComponent* Component = Voice.Component.Get();
			const FScctRoomAdaptive* State = RoomAdaptiveStates().Find(this);
			FString Engine = TEXT("no engine voice");
			if (Component != nullptr && Component->AttenuationSettings != nullptr)
			{
				const FSoundAttenuationSettings& Used = Component->bOverrideAttenuation ? Component->AttenuationOverrides : Component->AttenuationSettings->Attenuation;
				Engine = FString::Printf(TEXT("engine: main reverb wet %.3f..%.3f (%s), field %s"), Used.ReverbWetLevelMin, Used.ReverbWetLevelMax,
					Component->bOverrideAttenuation ? TEXT("this voice's override") : TEXT("its attenuation's"),
					Field != nullptr && State != nullptr && State->FieldPeaks[Voice.RoomField] >= 0.0f
						? *FString::Printf(TEXT("submix peak %.5f in the last second"), State->FieldPeaks[Voice.RoomField]) : TEXT("not measured"));
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: %s'%s' call %d at (%.0f, %.0f, %.0f) in zone %d (%s): %s; main reverb share %.2f, field send %.2f; way to the listener %s; listener at (%.0f, %.0f, %.0f) in zone %d (%s) on the main reverb; %s"),
				Voice.bProbe ? TEXT("probe ") : TEXT(""), *GetName(Voice.FileId), Voice.CallId, Voice.Location.X, Voice.Location.Y, Voice.Location.Z, Room.ZoneIndex,
				*RoomName(ZoneOfRoom(Room)),
				Field != nullptr ? *FString::Printf(TEXT("sends to source-room field %d (zone %d, %s, field output %.2f, HF %.2f) at mix %.2f"), Voice.RoomField, Field->ZoneIndex,
					*RoomName(Field->Zone), Field->Gain, Field->HF, Voice.RoomMix)
					: (bOtherRoom ? TEXT("in another room but no field could take it (no way to it, or every field busy): the main reverb") : TEXT("in the listener's room: the main reverb")),
				1.0f - Voice.RoomMix, Voice.RoomSend,
				(Room.Flags & EScctReverbRoomFlags::PathValid) != 0 ? *FString::Printf(TEXT("%.0f uu through %d opening(s), gain %.2f HF %.2f, reflected gain %.2f"), Room.PathDistance, Room.Hops,
					Room.PathGain, Room.PathHF, Room.ReflectedGain) : TEXT("none known"),
				Listener.X, Listener.Y, Listener.Z, ListenerZoneIndex, *RoomName(ListenerZone), *Engine);
		}
	}

	if (SwitchedFrom != INDEX_NONE)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: the listener goes from zone %d to zone %d (%s): the main reverb cross-fades to it over %.1f s with its tail kept; %d voice(s) cross-fade back to the main reverb, %d to a source-room field; no reverb is cut"),
			SwitchedFrom, ListenerZoneIndex, *RoomName(ListenerZone), ZoneFadeSeconds, ToMain, ToField);
	}

	// Each field's output follows what the way from its room keeps, and goes quiet once nothing has used it for its tail.
	const float GainEase = DeltaSeconds > 0.0f ? 1.0f - FMath::Exp(-DeltaSeconds / RoomFieldGainSeconds) : 0.0f;
	FAudioDevice* Device = GetWorld()->GetAudioDeviceRaw();
	for (int32 Index = 0; Index < RoomFields.Num(); ++Index)
	{
		FRoomField& Field = RoomFields[Index];
		if (Field.ZoneIndex == 0)
		{
			continue;
		}
		// The way of a voice in its room this frame; a passage closed now silences the field (Godot's UpdateSourceRooms).
		// While no voice of its room is listed (between one-shots) the field keeps its way's last level and rings out.
		float TargetGain = Field.Gain, TargetHF = Field.HF;
		for (int32 Point = 1; Point < RoomResults.Num(); ++Point)
		{
			if (RoomResults[Point].ZoneIndex == Field.ZoneIndex)
			{
				const bool bWay = (RoomResults[Point].Flags & EScctReverbRoomFlags::PathValid) != 0;
				TargetGain = bWay ? RoomResults[Point].ReflectedGain : 0.0f;
				TargetHF = bWay ? RoomResults[Point].PathHF : 0.01f;
				break;
			}
		}
		if (RoomClock - Field.LastUsed > FMath::Max(2.0f, Field.TailSeconds))
		{
			TargetGain = 0.0f;
		}
		Field.Gain += (TargetGain - Field.Gain) * GainEase;
		Field.HF += (TargetHF - Field.HF) * GainEase;
		if (Device != nullptr && RoomSubmixes.IsValidIndex(Index))
		{
			Device->SetSubmixOutputVolume(RoomSubmixes[Index], Field.Gain);
		}
	}

	if (bAdaptiveEnabled)
	{
		UpdateRoomGeometry(DeltaSeconds);
	}
}

void UScctAudioSubsystem::ApplyRoomRouting(FVoice& Voice, float HeardDistance, float InputDistance)
{
	UAudioComponent* Component = Voice.Component.Get();
	const USoundAttenuation* Attenuation = Component != nullptr ? Component->AttenuationSettings.Get() : nullptr;
	const FSoundAttenuationSettings* Base = Attenuation != nullptr ? &Attenuation->Attenuation : nullptr;

	// The field send: the room's wet level at the source's distance from its room's opening, over what the voice's own
	// distance curve and wall scale already took off its signal before the send (Unreal sends after the attenuation).
	float Send = 0.0f;
	if (Voice.RoomField != INDEX_NONE && Voice.RoomMix > 0.0f && Base != nullptr)
	{
		const float Taken = FMath::Max(DistanceGain(HeardDistance, Voice.MinDistance, Voice.Radius) * Voice.AcousticScale, 0.05f);
		Send = FMath::Clamp(Voice.RoomMix * RoomWetAt(*Base, InputDistance) / Taken, 0.0f, 1.0f);
	}
	Voice.RoomSend = Send;
	if (Component == nullptr || Base == nullptr || !Component->IsPlaying())
	{
		return;
	}

	// The main reverb's share: the attenuation's own reverb send, scaled for this voice alone.
	const bool bMixEnds = (Voice.RoomMix == 0.0f || Voice.RoomMix == 1.0f) && Voice.RoomMix != Voice.AppliedRoomMix;
	if (bMixEnds || FMath::Abs(Voice.RoomMix - Voice.AppliedRoomMix) >= RoomMixStep)
	{
		FSoundAttenuationSettings Settings = *Base;
		Settings.ReverbWetLevelMin *= 1.0f - Voice.RoomMix;
		Settings.ReverbWetLevelMax *= 1.0f - Voice.RoomMix;
		Component->AdjustAttenuation(Settings);
		Voice.AppliedRoomMix = Voice.RoomMix;
	}

	// Nothing left on a field the voice no longer uses, then the send to its own.
	if (Voice.AppliedRoomField != Voice.RoomField)
	{
		if (RoomSubmixes.IsValidIndex(Voice.AppliedRoomField))
		{
			Component->SetSubmixSend(RoomSubmixes[Voice.AppliedRoomField], 0.0f);
		}
		Voice.AppliedRoomField = Voice.RoomField;
		Voice.AppliedRoomSend = 0.0f;
	}
	if (RoomSubmixes.IsValidIndex(Voice.RoomField)
		&& (FMath::Abs(Send - Voice.AppliedRoomSend) >= RoomSendStep || (Send == 0.0f) != (Voice.AppliedRoomSend == 0.0f)))
	{
		Component->SetSubmixSend(RoomSubmixes[Voice.RoomField], Send);
		Voice.AppliedRoomSend = Send;
	}
}

void UScctAudioSubsystem::UpdateRoomGeometry(float DeltaSeconds)
{
	// The probes: 12 rays a frame, four at a time round-robin over the listener and every field still ringing.
	int32 Budget = GeometryRaysPerFrame;
	for (int32 Step = 0; Step < NumRoomFields + 1 && Budget > 0; ++Step)
	{
		const int32 Slot = NextGeometrySlot;
		NextGeometrySlot = (NextGeometrySlot + 1) % (NumRoomFields + 1);
		FScctReverbGeometry Geometry = {};
		if (Slot == 0)
		{
			const float Origin[3] = { static_cast<float>(Listener.X), static_cast<float>(Listener.Y), static_cast<float>(Listener.Z) };
			const int32 Rays = RoomApi->Probe(0, Origin, ListenerZoneIndex, GeometryRaysPerCall, &Geometry);
			Budget -= FMath::Max(Rays, 0);
			ListenerGeometry = Geometry;
			continue;
		}
		FRoomField& Field = RoomFields[Slot - 1];
		if (Field.ZoneIndex == 0 || RoomClock - Field.LastUsed > FMath::Max(2.0f, Field.TailSeconds))
		{
			continue;
		}
		const FVector At = Field.bCandidate ? Field.Candidate : Field.Anchor;
		Field.Anchor = At;
		const float Origin[3] = { static_cast<float>(At.X), static_cast<float>(At.Y), static_cast<float>(At.Z) };
		const int32 Rays = RoomApi->Probe(Slot, Origin, Field.ZoneIndex, GeometryRaysPerCall, &Geometry);
		Budget -= FMath::Max(Rays, 0);
		Field.Geometry = Geometry;
	}

	if (RoomClock < NextGeometryPacket)
	{
		return;
	}
	const float Elapsed = static_cast<float>(RoomClock - LastGeometryPacket);
	LastGeometryPacket = RoomClock;
	NextGeometryPacket = RoomClock + GeometryPacketSeconds;
	FScctRoomAdaptive* State = RoomAdaptiveStates().Find(this);
	if (State == nullptr)
	{
		return;
	}
	FAudioDevice* Device = GetWorld()->GetAudioDeviceRaw();

	// The main reverb: once the zone change's own fade is over, toward the measured room over the zone's environment.
	const UReverbEffect* ListenerAuthored = Zones.IsValidIndex(ListenerZone) ? ZoneEffects[Zones[ListenerZone].Effect].Get() : DefaultReverb.Get();
	if (!State->bMainSet || State->MainZone != ListenerZone)
	{
		// A new zone's environment, which UpdateReverb fades the main reverb to: the packets move on from there.
		State->Main = FScctRoomEax::From(*ListenerAuthored);
		State->MainZone = ListenerZone;
		State->bMainSet = true;
	}
	if (RoomClock - ListenerZoneChangedAt >= ZoneFadeSeconds && ListenerGeometry.Valid != 0)
	{
		const FScctRoomEax Target = FScctRoomEax::From(*ListenerAuthored).WithGeometry(ListenerGeometry);
		const FScctRoomEax Next = State->Main.Approach(Target, Elapsed);
		if (!State->Main.Near(Next))
		{
			State->Main = Next;
			++NumGeometryPackets;
			if (Device != nullptr && AdaptiveMainEffects.Num() == 2)
			{
				// Two settings in turn: the engine applies a reverb only when the effect it is given changes.
				UReverbEffect* Effect = AdaptiveMainEffects[NextAdaptiveMain];
				NextAdaptiveMain ^= 1;
				Next.Put(*Effect);
				Device->ActivateReverbEffect(Effect, FName(TEXT("ScctZone")), 1.0f, 0.5f, AdaptiveFadeSeconds);
			}
		}
		if (!bListenerAdapted)
		{
			bListenerAdapted = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: adaptive reverb of the listener's zone %d (%s): measured %.1f m across, openness %.2f, decay %.2f s (HF x%.2f), early %.3f s, late %.3f s, wet %.2f after %d sweep(s) of %d rays; the main reverb moves from decay %.2f s toward %.2f s"),
				ListenerZoneIndex, *RoomName(ListenerZone), ListenerGeometry.Size, ListenerGeometry.Openness, ListenerGeometry.Decay, ListenerGeometry.HFDecayRatio,
				ListenerGeometry.EarlyDelay, ListenerGeometry.LateDelay, ListenerGeometry.WetGain, ListenerGeometry.Sweeps, ListenerGeometry.Rays, ListenerAuthored->DecayTime, Target.Decay);
		}
	}

	for (int32 Index = 0; Index < RoomFields.Num(); ++Index)
	{
		FRoomField& Field = RoomFields[Index];
		if (Field.ZoneIndex == 0 || !State->bFieldSet[Index])
		{
			continue;
		}
		const UReverbEffect* Authored = Zones.IsValidIndex(Field.Zone) ? ZoneEffects[Zones[Field.Zone].Effect].Get() : DefaultReverb.Get();
		const FScctRoomEax Target = Field.Geometry.Valid != 0 ? FScctRoomEax::From(*Authored).WithGeometry(Field.Geometry) : State->Fields[Index];
		const FScctRoomEax Next = State->Fields[Index].Approach(Target, Elapsed);
		const bool bChanged = !State->Fields[Index].Near(Next);
		State->Fields[Index] = Next;
		if (bChanged || Field.LogIn <= 0.0f)
		{
			// The way's high frequencies go with the packet too (a field's preset is set at most ten times a second).
			if (RoomPresets.IsValidIndex(Index))
			{
				Next.Put(*RoomEffects[Index], Field.HF);
				RoomPresets[Index]->SetSettingsWithReverbEffect(RoomEffects[Index], 1.0f, 0.0f);
			}
			NumGeometryPackets += bChanged ? 1 : 0;
		}
		Field.LogIn -= Elapsed;
		if (Field.Geometry.Valid != 0 && !Field.bAdapted)
		{
			Field.bAdapted = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioRooms: adaptive reverb of field %d (zone %d, %s) measured at (%.0f, %.0f, %.0f): %.1f m across, openness %.2f, decay %.2f s, wet %.2f; the field's reverb moves from decay %.2f s toward %.2f s"),
				Index, Field.ZoneIndex, *RoomName(Field.Zone), Field.Anchor.X, Field.Anchor.Y, Field.Anchor.Z, Field.Geometry.Size, Field.Geometry.Openness, Field.Geometry.Decay,
				Field.Geometry.WetGain, Authored->DecayTime, Target.Decay);
		}
		if (Field.LogIn <= 0.0f)
		{
			Field.LogIn = 5.0f;
		}
	}
}

void UScctAudioSubsystem::LogRoomSummary() const
{
	if (RoomFields.Num() == 0)
	{
		return;
	}
	int32 Routed = 0, InUse = 0;
	for (const FVoice& Voice : Voices)
	{
		Routed += Voice.RoomField != INDEX_NONE ? 1 : 0;
	}
	FString Fields;
	for (int32 Index = 0; Index < RoomFields.Num(); ++Index)
	{
		const FRoomField& Field = RoomFields[Index];
		if (Field.ZoneIndex == 0)
		{
			continue;
		}
		++InUse;
		Fields += FString::Printf(TEXT("%s%d: zone %d (%s) output %.2f%s"), Fields.IsEmpty() ? TEXT("") : TEXT(", "), Index, Field.ZoneIndex, *RoomName(Field.Zone), Field.Gain,
			Field.Geometry.Valid != 0 ? *FString::Printf(TEXT(", measured %.1f m decay %.2f s"), Field.Geometry.Size, Field.Geometry.Decay) : TEXT(""));
	}
	const FScctRoomAdaptive* State = RoomAdaptiveStates().Find(this);
	UE_LOG(LogOpenSCCT, Log, TEXT("Audio rooms: listener in zone %d (%s)%s; %d of %d source-room fields in use%s%s; %d voices on a field now; %d cross-fades, %d field claims, %d frame requests for a field refused (no way to the room, or every field busy and none faint enough to take over), %d adaptive packets"),
		ListenerZoneIndex, *RoomName(ListenerZone),
		ListenerGeometry.Valid != 0 && State != nullptr ? *FString::Printf(TEXT(", measured %.1f m, main reverb decay %.2f s"), ListenerGeometry.Size, State->Main.Decay) : TEXT(""),
		InUse, RoomFields.Num(), Fields.IsEmpty() ? TEXT("") : TEXT(" ("), *(Fields.IsEmpty() ? FString() : Fields + TEXT(")")), Routed, NumRoomCrossfades, NumRoomFieldClaims,
		NumRoomRefused, NumGeometryPackets);
}
