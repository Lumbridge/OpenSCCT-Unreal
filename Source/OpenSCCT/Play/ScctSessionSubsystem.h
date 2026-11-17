#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctBridgeApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctSessionSubsystem.generated.h"

class AScctPawnProxy;

// ---------------------------------------------------------------------------------------------------------------
// THE ORDER OF A FRAME
//
// The session subsystem ticks as a tickable world subsystem: after every actor tick group up to post-physics
// (so after the player controller's PlayerTick has read the devices) and before the engine caches the camera.
// Every frame it does this, in this order, and the feature subsystems hang off the delegates named here:
//
//   1. OnBeforeTicks(DeltaSeconds)   Raised every frame the bridge is connected, match or no match. What must
//                                    reach the bridge before the simulation runs goes here: the controls feed
//                                    their input (Controls.Frame), the online visit runs its Update (which may
//                                    put a round in the bridge's session slot or take one out).
//   2. Session check                 GetSession. A session id other than the one this world presents is a new
//                                    match: see "A session changes" below.
//   3. Simulation ticks              UpdateSession(id) runs the client ticks that are due (none, one or several;
//                                    GetTicksThisFrame says how many). A session that failed in them is noticed
//                                    here and ends as in "A session changes".
//   4. View                          The bridge's GetView, then ViewOverride if bound (the controls replace the
//                                    camera with their look-ahead view), then the camera is moved. The engine
//                                    caches the camera right after this tick, so it is never a frame behind.
//   5. Pawns                         GetPawns and each pawn's pose: proxies are spawned, placed, posed and, for
//                                    pawns that are gone, destroyed.
//                                    A session that failed while steps 4 and 5 read it ends here as in "A
//                                    session changes", and step 6 is not reached.
//   6. OnFrame(DeltaSeconds)         Raised only while a match plays. Everything above is this frame's: features
//                                    read their per-frame bridge state here and may use FindProxy, GetPawns,
//                                    GetView and GetSessionInfo.
//
// A session changes (step 2 or 3, or StartMatch / StopMatch), always in this order:
//   a. OnMatchStopped                if a match was playing. The old session may already be gone in the bridge
//                                    (replaced or failed), so only drop Unreal-side things here.
//   b. the pawn proxies are destroyed (and with them everything attached to them)
//   c. OnSessionChanged(OldId, NewId) every time the id changes, NewId 0 when nothing plays now. Features reset
//                                    whatever belongs to one match here: voices, effects, HUD text, name caches.
//   d. OnMatchStarted                if a session plays now; the bridge is ready to be read for the new match.
//
// Which sessions a world presents: the local matches it started itself, and, once FollowBridgeSessions(true) was
// called (the online flow does that), whatever session the bridge's slot holds, which is how online rounds arrive.
// A world's clean-up stops only a match it started, so it cannot end another world's match or leave a server.
// ---------------------------------------------------------------------------------------------------------------

DECLARE_MULTICAST_DELEGATE(FScctMatchEvent);
DECLARE_MULTICAST_DELEGATE_OneParam(FScctFrameEvent, float /*DeltaSeconds*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FScctSessionChangedEvent, int32 /*OldSessionId*/, int32 /*NewSessionId*/);
DECLARE_DELEGATE_OneParam(FScctViewOverride, FScctBridgeView& /*InOutView*/);

/** What a local match is started with. The game mode fills it from the level's URL options. */
USTRUCT(BlueprintType)
struct FScctLocalMatchOptions
{
	GENERATED_BODY()

	/** The OpenSCCT map's folder name ("AquaE"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	FString Map;

	/** Story, DiskHunt, Deathmatch or MercDeathMatch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	FString Mode = TEXT("Story");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	bool bMerc = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	int32 Bots = 3;

	/** The bots' skill: 0 default (normal), 1 easy, 2 normal, 3 hard, 4 expert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	int32 Difficulty = 0;

	/** The mercs carry OpenSCCT's sidearms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	bool bSidearms = false;

	/** The mercs may pick OpenSCCT's additional rifles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	bool bRifles = false;

	/** Practice rules: no time limit, unlimited lives, 5 s respawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	bool bOfflineRules = false;

	/** Empty: "Player". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT")
	FString PlayerName;
};

/**
 * Presents the OpenSCCT match of a game world. Each frame it runs the simulation ticks that are due and moves
 * the Unreal actors that stand for the simulation's pawns and camera. The order of a frame is at the top of
 * this file.
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard, used with -RenderOffscreen):
 *   -ScctShots=N -ScctShotEvery=S -ScctShotDir=<folder>   save N screenshots S seconds apart, then quit
 *   -ScctCams="x,y,z,yaw,pitch;..."                       take each screenshot from a fixed point instead
 *   -ScctCheckFeatures                                    fetch all six feature tables when a match starts and
 *                                                         log each one's version (or why it is missing)
 *   -ScctShowLocalBody                                    draw the local player's body even when the view is from
 *                                                         its own eyes (for tour cameras that look at the player)
 *   -ScctRestartAfter=S                                   S seconds into the first match, start it again once
 *                                                         (a second match in the same world: a new session id)
 */
UCLASS()
class OPENSCCT_API UScctSessionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Hosts a match locally (the dedicated server's code, with bots) and joins it. Blocks while the map loads. */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT")
	bool StartMatch(const FScctLocalMatchOptions& Options);

	/** The same with the options every match needs and the rest left at their defaults. */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT")
	bool StartLocalMatch(const FString& Map, const FString& Mode, bool bMerc, int32 Bots);

	/**
	 * Ends the session this world presents, whoever started it (for an online round that means leaving the
	 * server), and leaves the bridge idle.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT")
	void StopMatch();

	/**
	 * Connects to the bridge without starting a match, so OnBeforeTicks runs. With bFollow set this world also
	 * presents sessions it did not start: the rounds the online visit puts in the bridge's slot. Returns false
	 * (GetFailure has the reason) when the bridge cannot be loaded.
	 */
	bool FollowBridgeSessions(bool bFollow);

	UFUNCTION(BlueprintPure, Category = "OpenSCCT")
	bool IsPlaying() const { return SessionId != 0; }

	/** Why the match stopped or did not start; empty while all is well. Set once per failure, cleared by the next start. */
	UFUNCTION(BlueprintPure, Category = "OpenSCCT")
	const FString& GetFailure() const { return Failure; }

	/** The id of the session this world presents; 0 when none. A new number for every match and online round. */
	UFUNCTION(BlueprintPure, Category = "OpenSCCT")
	int32 GetSessionId() const { return SessionId; }

	/** The bridge's description of that session as of this frame (map, Remote and Merc flags, ticks run). */
	const FScctBridgeSessionInfo& GetSessionInfo() const { return SessionInfo; }

	/** The session is a round on a remote server, not a local match. */
	bool IsRemoteSession() const { return SessionId != 0 && (SessionInfo.Flags & EScctSessionFlags::Remote) != 0; }

	/** How many simulation ticks step 3 of this frame ran. */
	int32 GetTicksThisFrame() const { return TicksThisFrame; }

	/** The input the next simulation ticks take. Not to be mixed with the controls table's Frame. */
	void SetInput(const FScctBridgeInput& Input);

	/** Queues one of the simulation's exec commands (Jump, SwitchHeadset...). */
	void SendCommand(const TCHAR* Name, int32 Arg = 0);

	/** See "The order of a frame" at the top of this file for when each of these is raised. */
	FScctFrameEvent OnBeforeTicks;
	FScctMatchEvent OnMatchStarted;
	FScctMatchEvent OnMatchStopped;
	FScctSessionChangedEvent OnSessionChanged;
	FScctFrameEvent OnFrame;

	/**
	 * Bound by the one feature that owns the camera (the controls): called in step 4 with the bridge's view of
	 * the frame, to replace what it knows better (the look-ahead position and angles, the field of view). Fov is
	 * the horizontal angle of a 4:3 picture. Leave HideLocalBody as it is.
	 */
	FScctViewOverride ViewOverride;

	/** The Unreal body of a simulation pawn by its actor id, or null. */
	AScctPawnProxy* FindProxy(int32 ActorId) const;

	/** The body of the player at this machine, or null while they have no pawn. */
	AScctPawnProxy* FindLocalProxy() const { return FindProxy(LocalActorId); }

	/** Every pawn body there is this frame, by actor id. */
	const TMap<int32, TObjectPtr<AScctPawnProxy>>& GetProxies() const { return Proxies; }

	/** The pawns of this frame as the bridge listed them in step 5. */
	TConstArrayView<FScctBridgePawn> GetPawns() const { return TConstArrayView<FScctBridgePawn>(PawnBuffer.GetData(), PawnCount); }

	/** The bridge's bone list of a pawn kind (0 spy, 1 merc), in the order of every pose; empty for other kinds. */
	TConstArrayView<FScctBridgeBoneInfo> GetSkeleton(int32 Kind) const;

	/** The bridge's core entry points while a match plays, else null. */
	const FScctBridgeApi* GetBridge() const { return SessionId != 0 ? Api : nullptr; }

	/** The camera of this frame as it was applied in step 4; Valid is 0 before the player has a view. */
	const FScctBridgeView& GetView() const { return View; }

	/**
	 * The camera is on a screenshot tour (-ScctCams), not at the player's view: GetView is the tour point, and
	 * what belongs to the player's own eyes (a first-person view model) is not drawn.
	 */
	bool IsTourView() const { return bTourView; }

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Fetches the bridge's table; false with the failure recorded when it cannot be loaded. */
	bool ConnectBridge();

	/** Step 2: reads the bridge's session and makes this world present the right one. */
	void CheckSession();

	/** Steps a to d of "A session changes". NewInfo describes the new session (Id 0: none). */
	void ChangeSession(const FScctBridgeSessionInfo& NewInfo, bool bStartedHere);

	/** Records and shows a failure once. */
	void Fail(const FString& Reason);

	void DestroyProxies();
	void UpdatePawns();
	void ApplyViewToPlayer();
	void LogFeatureTables() const;

	/**
	 * Unattended look check: -ScctShots=N saves N screenshots, -ScctShotEvery=S seconds apart (default 5), into
	 * -ScctShotDir= (default Saved/ScctShots), then quits. Used with -RenderOffscreen so no window appears.
	 */
	void TickAutoShots(float DeltaTime);

	/** Moves the camera to the tour point of the next screenshot, if -ScctCams= gave one. */
	void ApplyTourCamera();

	/** -ScctCams="x,y,z,yaw,pitch;...": where each screenshot is taken from, instead of the player's view. */
	TArray<FTransform> TourCameras;
	bool bTourView = false;

	/** The tour point the camera is at; it moves on the tick after a screenshot was requested there. */
	int32 TourIndex = 0;
	bool bAdvanceTour = false;
	int32 ShotsLeft = -1;
	int32 ShotIndex = 0;
	float ShotTimer = 0.0f;
	float QuitTimer = -1.0f;

	const FScctBridgeApi* Api = nullptr;

	/** The session this world presents, 0 for none, and what the bridge last said about it. */
	int32 SessionId = 0;
	FScctBridgeSessionInfo SessionInfo = {};

	/** This world started the session (a local match): its clean-up may stop it. */
	bool bStartedHere = false;

	/** Present sessions this world did not start (online rounds). */
	bool bFollowBridge = false;

	/** What the last StartMatch was asked for. */
	FScctLocalMatchOptions LastOptions;

	/** -ScctRestartAfter: seconds until the one restart; negative when none is due. */
	float RestartTimer = -1.0f;
	bool bRestarted = false;

	int32 TicksThisFrame = 0;
	FString Failure;
	FScctBridgeView View = {};
	TArray<FScctBridgePawn> PawnBuffer;
	int32 PawnCount = 0;
	int32 LocalActorId = 0;
	TArray<FScctBridgeBone> PoseBuffer;

	/** The bridge's bone lists by pawn kind (0 spy, 1 merc), read once per session. */
	TArray<FScctBridgeBoneInfo> Skeletons[2];

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<AScctPawnProxy>> Proxies;
};
