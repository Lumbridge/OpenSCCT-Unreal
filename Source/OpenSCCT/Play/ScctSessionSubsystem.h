#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctBridgeApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctSessionSubsystem.generated.h"

class AScctPawnProxy;

DECLARE_MULTICAST_DELEGATE(FScctMatchEvent);
DECLARE_MULTICAST_DELEGATE_OneParam(FScctFrameEvent, float /*DeltaSeconds*/);

/**
 * Runs the OpenSCCT match of a game world. Each frame it hands the player's input to the simulation, runs the
 * ticks that are due, and moves the Unreal actors that stand for the simulation's pawns and camera.
 */
UCLASS()
class OPENSCCT_API UScctSessionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Hosts Map locally (the dedicated server's code, with bots) and joins it. Blocks while the map loads. */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT")
	bool StartLocalMatch(const FString& Map, const FString& Mode, bool bMerc, int32 Bots);

	UFUNCTION(BlueprintCallable, Category = "OpenSCCT")
	void StopMatch();

	UFUNCTION(BlueprintPure, Category = "OpenSCCT")
	bool IsPlaying() const { return bPlaying; }

	/** Why the match stopped or did not start; empty while all is well. */
	UFUNCTION(BlueprintPure, Category = "OpenSCCT")
	const FString& GetFailure() const { return Failure; }

	/** The input the next simulation ticks take. The player controller fills it every frame. */
	void SetInput(const FScctBridgeInput& Input);

	/** Queues one of the simulation's exec commands (Jump, SwitchHeadset...). */
	void SendCommand(const TCHAR* Name, int32 Arg = 0);

	/**
	 * Raised when a match has started and when it is about to stop. Feature subsystems (sound, level state,
	 * HUD...) fetch their bridge tables and reset their state on these.
	 */
	FScctMatchEvent OnMatchStarted;
	FScctMatchEvent OnMatchStopped;

	/**
	 * Raised once a frame while a match plays, after the frame's simulation ticks have run and the pawns and
	 * camera are placed. Feature subsystems read their per-frame bridge state here.
	 */
	FScctFrameEvent OnFrame;

	/** The Unreal body of a simulation pawn by its actor id, or null. */
	AScctPawnProxy* FindProxy(int32 ActorId) const;

	/** The bridge's core entry points while a match plays, else null. */
	const FScctBridgeApi* GetBridge() const { return bPlaying ? Api : nullptr; }

	/** The local camera this frame; Valid is 0 before the player has a view. */
	const FScctBridgeView& GetView() const { return View; }

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Fail(const FString& Reason);
	void ApplyViewToPlayer();

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
	void UpdatePawns();

	const FScctBridgeApi* Api = nullptr;
	bool bPlaying = false;
	FString Failure;
	FScctBridgeView View = {};
	TArray<FScctBridgePawn> PawnBuffer;
	TArray<FScctBridgeBone> PoseBuffer;

	/** The bridge's bone lists by pawn kind (0 spy, 1 merc), read once per match. */
	TArray<FScctBridgeBoneInfo> Skeletons[2];

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<AScctPawnProxy>> Proxies;
};
