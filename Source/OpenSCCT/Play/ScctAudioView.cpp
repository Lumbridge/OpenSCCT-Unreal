#include "Play/ScctAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

// The listener's view and the match's sound: the scope's and binoculars' audio zoom (the original's SetAudioZoom, as
// the Godot client's SoundSystem.SetAudioZoom and SoundRules.AudioZoomGain) and the Doppler shift of every 3D voice
// (the Godot client's rule 17, SoundSystem.Doppler and SoundRules.DopplerPitch).

namespace
{
	// SoundRules.AudioZoomGain (AudioK): a sound's off-axis measure is 112.5 / FOV x its sideways share, held at 1;
	// its gain is (3 - 3.9 r) x zoom + 1.
	constexpr float ScctAudioViewZoomFovScale = 112.5f;
	constexpr float ScctAudioViewZoomCentre = 3.0f;
	constexpr float ScctAudioViewZoomEdge = 3.9f;
	constexpr float ScctAudioViewDefaultFov = 90.0f;

	// SoundRules.DopplerPitch (AudioK): DirectSound's model with the listener's Doppler factor 0.5, the speed of sound
	// 343 m/s over the distance factor 0.0127 m per unit; a source faster than 300 uu/s counts as still; the pitch
	// stays within the voice pitch range 0.5 to 2.
	constexpr float ScctAudioViewDopplerFactor = 0.5f;
	constexpr float ScctAudioViewSpeedOfSound = 343.0f / 0.0127f;
	constexpr float ScctAudioViewMaxSourceSpeed = 300.0f;
	constexpr float ScctAudioViewMinPitch = 0.5f;
	constexpr float ScctAudioViewMaxPitch = 2.0f;

	// -ScctAudioDoppler logs a voice moving faster than this, at most this often.
	constexpr float ScctAudioViewDopplerLogSpeed = 20.0f;
	constexpr float ScctAudioViewDopplerLogEvery = 0.2f;

	/** The audio zoom gain of a sound Forward uu ahead of the view and Sideways uu off its axis. */
	float ScctAudioViewZoomGain(float Zoom, float Fov, float Forward, float Sideways)
	{
		if (Zoom <= 0.0f || Fov <= 0.0f)
		{
			return 1.0f;
		}
		const float Distance = FMath::Sqrt(Forward * Forward + Sideways * Sideways);
		const float R = Forward <= 0.0f || Distance <= 0.0f ? 1.0f : FMath::Min(1.0f, ScctAudioViewZoomFovScale / Fov * (Sideways / Distance));
		// The scope's steps never take it below 0.1; a factor forced past 1 (-ScctAudioZoom) could, and a volume is not negative.
		return FMath::Max((ScctAudioViewZoomCentre - R * ScctAudioViewZoomEdge) * Zoom + 1.0f, 0.0f);
	}

	/** The Doppler pitch factor of a source from its own and the listener's velocities (uu/s) along the line between them. */
	float ScctAudioViewDopplerPitch(const FVector& Listener, const FVector& ListenerVelocity, const FVector& Source, FVector SourceVelocity)
	{
		const FVector ToListener = Listener - Source;
		const double D = ToListener.Size();
		if (D <= 0.0)
		{
			return 1.0f;
		}
		if (SourceVelocity.SizeSquared() > FMath::Square(ScctAudioViewMaxSourceSpeed))
		{
			SourceVelocity = FVector::ZeroVector;
		}
		const float Vl = static_cast<float>(FVector::DotProduct(ToListener, ListenerVelocity) / D);
		const float Vs = static_cast<float>(FVector::DotProduct(ToListener, SourceVelocity) / D);
		const float Below = ScctAudioViewSpeedOfSound - ScctAudioViewDopplerFactor * Vs;
		if (Below <= 0.0f)
		{
			return ScctAudioViewMaxPitch;
		}
		return FMath::Clamp((ScctAudioViewSpeedOfSound - ScctAudioViewDopplerFactor * Vl) / Below, ScctAudioViewMinPitch, ScctAudioViewMaxPitch);
	}
}

void UScctAudioSubsystem::ReadAudioViewSwitches()
{
	FString Error;
	AudioViewApi = ScctFeature<FScctAudioViewApi>(&Error);
	UE_CLOG(AudioViewApi == nullptr, LogOpenSCCT, Warning, TEXT("Audio: no audio zoom through the scope or binoculars this match, the bridge's audio view table is missing: %s"), *Error);

	ForcedAudioZoom = -1.0f;
	float Forced = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctAudioZoom="), Forced))
	{
		ForcedAudioZoom = FMath::Max(Forced, 0.0f);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioZoom: the audio zoom is held at %.2f for this run (the scope's steps are 0.33, 0.66 and 1); every 3D voice's zoom gain is logged once a second"), ForcedAudioZoom);
	}
	bLogDoppler = FParse::Param(FCommandLine::Get(), TEXT("ScctAudioDoppler"));
	bLastListenerSet = false;
	LoggedAudioZoom = -1.0f;
	NumDopplerShifted = 0;
	PeakDopplerPitch = LowDopplerPitch = 1.0f;
}

void UScctAudioSubsystem::UpdateAudioView(float DeltaSeconds)
{
	// The listener's velocity is its movement over this frame (SoundSystem.Update). A listener that jumps (a camera
	// cut) gives one frame of an extreme velocity, which the pitch range holds.
	const FScctBridgeView& View = Session->GetView();
	ListenerRotation = FRotator(View.Pitch, View.Yaw, View.Roll);
	ListenerVelocity = bLastListenerSet && bListenerValid && DeltaSeconds > 0.0f ? (Listener - LastListener) / DeltaSeconds : FVector::ZeroVector;
	LastListener = Listener;
	bLastListenerSet = bListenerValid;

	// PlayAudio: SetAudioZoom(pc.AudioZoomFactor, pc.FovAngle) every frame, and the actors bound to the listener.
	FScctAudioView Out{};
	Out.Fov = ScctAudioViewDefaultFov;
	if (AudioViewApi != nullptr && AudioViewApi->GetView(&Out) != 0 && Out.Valid != 0)
	{
		AudioZoom = FMath::Max(Out.Zoom, 0.0f);
		AudioZoomFov = Out.Fov > 0.0f ? Out.Fov : ScctAudioViewDefaultFov;
		ViewActorId = Out.ViewActorId;
		LocalPawnId = Out.LocalPawnId;
	}
	else
	{
		AudioZoom = 0.0f;
		AudioZoomFov = View.Fov > 0.0f ? View.Fov : ScctAudioViewDefaultFov;
		ViewActorId = 0;
		LocalPawnId = 0;
	}
	if (ForcedAudioZoom >= 0.0f)
	{
		AudioZoom = ForcedAudioZoom;
	}
	if (!FMath::IsNearlyEqual(AudioZoom, LoggedAudioZoom, 0.001f))
	{
		LoggedAudioZoom = AudioZoom;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudio: audio zoom %.2f at FOV %.1f%s"), AudioZoom, AudioZoomFov,
			AudioZoom > 0.0f ? *FString::Printf(TEXT(": 3D sounds on the aim line x%.2f, at the edge of the view or behind it x%.2f"),
				ScctAudioViewZoomGain(AudioZoom, AudioZoomFov, 1.0f, 0.0f), ScctAudioViewZoomGain(AudioZoom, AudioZoomFov, 0.0f, 1.0f))
			: TEXT(" (off: every voice at zoom gain 1)"));
	}
}

bool UScctAudioSubsystem::IsListenerBound(const FVoice& Voice) const
{
	return (LocalPawnId != 0 && (Voice.ActorId == LocalPawnId || Voice.FollowActorId == LocalPawnId))
		|| (Voice.AmbientId != 0 && ViewActorId != 0 && Voice.ActorId == ViewActorId);
}

float UScctAudioSubsystem::HeardVolumeOf(const FVoice& Voice) const
{
	// The audio zoom scales the source volume, which DirectSound cannot raise above full (SoundSystem.ApplyVoice).
	const float Source = Voice.bPositional && !FMath::IsNearlyEqual(Voice.ZoomGain, 1.0f) ? FMath::Min(1.0f, Voice.Volume * Voice.ZoomGain) : Voice.Volume;
	return Source * SettingsGainOf(Voice) * Voice.AcousticScale;
}

void UScctAudioSubsystem::UpdateZoomAndDoppler(FVoice& Voice, float DeltaSeconds)
{
	// The source's velocity is its movement since the last frame (SoundSystem.Doppler).
	const FVector Velocity = Voice.bHasLastLocation && DeltaSeconds > 0.0f ? (Voice.Location - Voice.LastLocation) / DeltaSeconds : FVector::ZeroVector;
	Voice.LastLocation = Voice.Location;
	Voice.bHasLastLocation = true;
	Voice.ZoomGain = 1.0f;
	Voice.DopplerPitch = 1.0f;
	Voice.ZoomLogIn -= DeltaSeconds;
	Voice.DopplerLogIn -= DeltaSeconds;

	if (!Voice.bPositional || Voice.bMusic || !bListenerValid)
	{
		if (bLogDoppler && !Voice.bViewLogged && Voice.bAtListener)
		{
			Voice.bViewLogged = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioDoppler: '%s' actor %d plays at the listener: pitch %.2f x Doppler 1.0000 (listener-bound)"),
				*GetName(Voice.FileId), Voice.ActorId, Voice.Pitch);
		}
		return;
	}

	const FVector ToSource = Voice.Location - Listener;
	const FVector Forward = ListenerRotation.Vector();
	const float Ahead = static_cast<float>(FVector::DotProduct(ToSource, Forward));
	const float Sideways = FMath::Sqrt(FMath::Max(static_cast<float>(ToSource.SizeSquared()) - Ahead * Ahead, 0.0f));
	if (AudioZoom > 0.0f)
	{
		Voice.ZoomGain = ScctAudioViewZoomGain(AudioZoom, AudioZoomFov, Ahead, Sideways);
	}

	const bool bBound = IsListenerBound(Voice);
	if (!bBound)
	{
		// The listener-bound sounds move with the listener: nothing between them changes and they keep pitch 1.
		Voice.DopplerPitch = ScctAudioViewDopplerPitch(Listener, ListenerVelocity, Voice.Location, Velocity);
	}
	if (!FMath::IsNearlyEqual(Voice.DopplerPitch, 1.0f, 1.0e-4f))
	{
		++NumDopplerShifted;
		PeakDopplerPitch = FMath::Max(PeakDopplerPitch, Voice.DopplerPitch);
		LowDopplerPitch = FMath::Min(LowDopplerPitch, Voice.DopplerPitch);
	}

	if (ForcedAudioZoom >= 0.0f && Voice.ZoomLogIn <= 0.0f)
	{
		Voice.ZoomLogIn = 1.0f;
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Sideways, Ahead));
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioZoom: '%s' actor %d %.0f uu ahead and %.0f uu off the aim line (%.0f deg): zoom gain %.2f, volume %.2f -> %.2f"),
			*GetName(Voice.FileId), Voice.ActorId, Ahead, Sideways, Angle, Voice.ZoomGain, Voice.Volume, FMath::Min(1.0f, Voice.Volume * Voice.ZoomGain));
	}

	if (bLogDoppler)
	{
		const double Distance = FVector::Dist(Voice.Location, Listener);
		if (bBound && !Voice.bViewLogged)
		{
			Voice.bViewLogged = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioDoppler: '%s' actor %d moves with the listener (%s): pitch %.2f x Doppler 1.0000 (listener-bound)"),
				*GetName(Voice.FileId), Voice.ActorId, Voice.AmbientId != 0 ? TEXT("the view actor's ambient sound") : TEXT("the local pawn's own sound"), Voice.Pitch);
		}
		else if (!bBound && Voice.DopplerLogIn <= 0.0f && Velocity.Size() >= ScctAudioViewDopplerLogSpeed && Distance > 0.0)
		{
			Voice.DopplerLogIn = ScctAudioViewDopplerLogEvery;
			const double Toward = FVector::DotProduct(-ToSource, Velocity) / Distance;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctAudioDoppler: '%s' actor %d %.0f uu away, source %.0f uu/s (%.0f toward the listener)%s, listener %.0f uu/s: pitch %.2f x Doppler %.4f"),
				*GetName(Voice.FileId), Voice.ActorId, Distance, Velocity.Size(), Toward,
				Velocity.Size() > ScctAudioViewMaxSourceSpeed ? TEXT(" counted as still (over 300 uu/s)") : TEXT(""), ListenerVelocity.Size(), Voice.Pitch, Voice.DopplerPitch);
		}
	}
}

void UScctAudioSubsystem::LogAudioViewSummary() const
{
	UE_LOG(LogOpenSCCT, Log, TEXT("Audio view: audio zoom %.2f at FOV %.1f%s, view actor %d, local pawn %d, listener moving %.0f uu/s; Doppler since the match began: %d voice frames shifted, pitch from %.4f to %.4f"),
		AudioZoom, AudioZoomFov, AudioZoom > 0.0f ? TEXT("") : TEXT(" (off: zoom gain 1)"), ViewActorId, LocalPawnId, ListenerVelocity.Size(),
		NumDopplerShifted, LowDopplerPitch, PeakDopplerPitch);
}
