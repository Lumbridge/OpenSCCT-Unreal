#pragma once

#include "CoreMinimal.h"

class FScctHubActivities;
class UScctHubSubsystem;
class UWorld;

/** How one other player's voice is to be played to this listener (UScctHubSubsystem::GetVoiceSpeakers). */
struct FScctHubVoiceSpeaker
{
	int32 PlayerId = 0;
	FString Name;
	/** His mouth (UE units): where his voice sounds from. */
	FVector Location = FVector::ZeroVector;
	float Distance = 0.0f;
	/** The proximity gain, 0 to 1 (full to 400 uu, nothing past 3000 uu, x0.45 through a wall), smoothed. */
	float ProximityGain = 0.0f;
	/** The player list's volume for him (0 to 2) and his mute. */
	float Volume = 1.0f;
	bool bMuted = false;
	/** What his voice is played at: ProximityGain x Volume, 0 when muted. */
	float Gain = 0.0f;
	/** The wall's muffle, 0 in the open to 1 behind a wall (the low-pass at 20000 Hz to 1200 Hz), smoothed. */
	float Muffle = 0.0f;
	float LowPassHz = 20000.0f;
	bool bOccluded = false;
};

/**
 * The hub's proximity voice, the mixing half (an OpenSCCT addition; port of the gains of the Godot client's
 * game/Scripts/Hub/HubVoiceChat.cs over the shared ProximityVoice). Ten times a second it asks the bridge (the activity
 * table's GetVoiceMixes) how every other player's voice is heard from the camera: where his mouth is, how far, the
 * proximity gain and the wall muffle, the occlusion traced through the simulation's own collision (the imported level's
 * meshes have none), and multiplies in the player list's volume and mute for him.
 *
 * There is no voice transport in this client: nothing captures the microphone, encodes or relays a voice, or plays one
 * (the Godot client's HubClient.Voice and VoiceClient have no Unreal counterpart yet). The gains are worked out and
 * kept for that transport to apply (UScctHubSubsystem::GetVoiceSpeakers), and with the hub's status lines on they are
 * logged per speaker.
 */
class FScctHubVoice : public TSharedFromThis<FScctHubVoice>
{
public:
	FScctHubVoice(UScctHubSubsystem* InOwner, TSharedPtr<FScctHubActivities> InActivities);

	void Tick(UWorld* World, float DeltaSeconds, bool bVerbose);
	void Stop();

	const TArray<FScctHubVoiceSpeaker>& GetSpeakers() const { return Speakers; }

private:
	UScctHubSubsystem* Owner = nullptr;
	TSharedPtr<FScctHubActivities> Activities;
	TArray<FScctHubVoiceSpeaker> Speakers;
	float SinceUpdate = 0.0f;
	float LogTimer = 0.0f;
	bool bToldNoTransport = false;
};
