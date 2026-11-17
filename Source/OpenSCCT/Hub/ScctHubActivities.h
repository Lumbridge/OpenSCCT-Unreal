#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctHubActivityApi.h"

class UScctHubSubsystem;
class UWorld;

/**
 * The hub's activities as the bridge keeps them (an OpenSCCT addition; the reading half of the Godot client's
 * game/Scripts/Hub/HubHud.Activities.cs and HubProps.cs). It belongs to the hub subsystem and reads the bridge's
 * activity table (Runtime/ScctHubActivityApi.h) once a frame while the hub level presents the visit, for the others:
 *   - the hub HUD draws the clock of the course run or the range round (top middle), the notices under it and the
 *     nearest score board that is not a world sign (a panel on the right);
 *   - the props (Hub/ScctHubProps.h) draw the marks, the targets and the ball;
 *   - the hub's audio (Hub/ScctHubAudio.h) plays the chime cues taken this frame and the jukebox's record;
 *   - the emotes (Hub/ScctHubEmotes.h) read the local player's emote; fetching the table installs the emote poses
 *     the bridge lays over every drawn pawn.
 * With the hub subsystem's status lines on (unattended runs) it logs what it read every few seconds.
 */
class FScctHubActivities : public TSharedFromThis<FScctHubActivities>
{
public:
	explicit FScctHubActivities(UScctHubSubsystem* InOwner);

	/** The table, fetched on first use; null when the bridge has none. */
	const FScctHubActivityApi* GetApi();

	/** One frame of the drawn hub: the state, marks, targets, texts and the cues raised since the last frame. */
	void Tick(UWorld* World, float DeltaSeconds, bool bVerbose);

	/** The hub is no longer drawn. */
	void Stop();

	bool IsValid() const { return State.Valid != 0 && (State.Flags & EScctHubActivityFlags::InHub) != 0; }
	const FScctHubActivityState& GetState() const { return State; }
	const TArray<FScctHubActivityMark>& GetMarks() const { return Marks; }
	const TArray<FScctHubActivityTarget>& GetTargets() const { return Targets; }

	/** The chime cues the activities raised since the last frame ("start", "cp", "hit"...). */
	const TArray<FString>& GetCues() const { return Cues; }

	/** The first mark of a kind, false when there is none. */
	bool FindMark(EScctHubMarkKind Kind, FVector& OutAt) const;

	/** The camera this frame (where the texts were read from). */
	const FVector& GetEye() const { return Eye; }

	const FString& GetActivity() const { return Activity; }
	const FString& GetNotices() const { return Notices; }
	const FString& GetBoardTitle() const { return BoardTitle; }
	const FString& GetBoardLines() const { return BoardLines; }
	const FString& GetTrackTitle() const { return TrackTitle; }
	const FString& GetLocalEmote() const { return LocalEmote; }

private:
	FString ReadText(EScctHubActivityText Kind) const;
	void LogLine(float DeltaSeconds);

	UScctHubSubsystem* Owner = nullptr;
	const FScctHubActivityApi* Api = nullptr;
	bool bReportedNoApi = false;

	FScctHubActivityState State = {};
	TArray<FScctHubActivityMark> Marks;
	TArray<FScctHubActivityTarget> Targets;
	TArray<FString> Cues;
	FVector Eye = FVector::ZeroVector;
	FString Activity, Notices, BoardTitle, BoardLines, TrackTitle, LocalEmote;

	float LogTimer = 0.0f;
	int32 LoggedEmotePoses = 0;
	int32 CuesSinceLog = 0;
};
