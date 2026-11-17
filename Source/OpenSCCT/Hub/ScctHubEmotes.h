#pragma once

#include "CoreMinimal.h"

class FScctHubActivities;
class UWorld;

/**
 * The hub's emotes on the drawn bodies (an OpenSCCT addition; port of the Godot client's game/Scripts/Hub/HubEmotePoses.cs
 * and Views/PawnPoseOverlay.cs). The emote itself is the server's: G then 1 to 8 on the hub HUD (Hub/ScctHubHud.h), or
 * "/wave" in the chat, asks for it and every client hears who plays what. The pose is laid over the body by the bridge:
 * fetching the activity table installs HubEmotePoses as the pose overlay of every drawn pawn (Pose/PawnPoser.cs), which
 * turns the arms, the spine, the head and the hips of whoever plays an emote (wave, salute, cheer, dance, sit, point,
 * laugh, clap) and lowers the hips to sit, on top of the animation the body plays. This class only reads it back for
 * the log, and plays an emote for unattended runs.
 *
 * Unattended switch: -ScctHubEmote=<1-8>[@seconds]   the local player plays that emote (3 cheer) that long after he
 *                                                     stands in the hub (3 s when no time is given), and again just
 *                                                     before it ends, so a screenshot at any later moment shows it
 */
class FScctHubEmotes : public TSharedFromThis<FScctHubEmotes>
{
public:
	explicit FScctHubEmotes(TSharedPtr<FScctHubActivities> InActivities);

	void Tick(UWorld* World, float DeltaSeconds, bool bVerbose);
	void Stop();

private:
	TSharedPtr<FScctHubActivities> Activities;

	int32 AutoEmote = 0;
	float AutoDelay = 3.0f;
	float InHubSeconds = 0.0f;
	float RepeatIn = 0.0f;
	int32 NumPlayed = 0;
	int32 LoggedPoses = 0;
	float LogTimer = 0.0f;
};
