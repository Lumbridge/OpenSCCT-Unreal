#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctDemoPlayApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctDemoPlayback.generated.h"

class SScctDemoHud;
class SWidget;
class UScctSessionSubsystem;

/**
 * Watching a demo (an OpenSCCT feature, docs/DEMOS.md; the Godot client's DemoLibrary.Watch and PlayRoot's demo
 * presentation): the recording plays in its map's imported level, through the bridge's main session slot like an
 * online round (Bridge/OpenScct.UnrealBridge/Demo/DemoBridge.cs), with the playback bar on top (Demo/SScctDemoHud.h).
 *
 *   1. Watch(Path), from the Demos page: the bridge reads and checks the file (game version, map installed) and the
 *      demo's level is opened with ?ScctDemo=1.
 *   2. AScctGameMode::StartPlay asks ClaimLevel: a demo level starts no match; PresentIn has the level's session
 *      subsystem follow the bridge, starts the playback in the main slot and puts the playback bar up.
 *   3. Every frame, before the simulation's ticks, the bridge runs a seek's fast-forward and the camera (Frame).
 *   4. Leave (the bar's Back to demos, Escape) stops the playback and returns to the front end on its Demos page.
 *
 * Switches, for unattended checks (the Godot client's --svm-demo):
 *   -ScctDemo=<file>          the first level of the run plays that demo (the right level is opened when it is another)
 *   -ScctDemoSpeed=S          playback speed: 0.25, 0.5, 1, 2 or 4
 *   -ScctDemoCamera=C         first, chase (third person), free or asplayed
 *   -ScctDemoSeek=S           seek S seconds into the recording once it plays
 *   -ScctDemoPlayer=N         watch the Nth player (1 based)
 *   -ScctDemoPause=S          pause S seconds after it starts playing
 *   -ScctDemoLeaveAfter=S     press Back to demos after S seconds
 */
UCLASS()
class OPENSCCT_API UScctDemoPlayback : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The URL option of a level opened to play a demo. */
	static const TCHAR* const DemoOption;

	static UScctDemoPlayback* Get(const UObject* WorldContext);

	/** The bridge's demo table, or null when the bridge is not up. */
	static const FScctDemoPlayApi* Api();

	/**
	 * Opens a demo and its level. False, with the reason in GetError(), when the file cannot be read, was recorded on
	 * another game version, or is on a map that is not installed or not imported.
	 */
	bool Watch(const FString& Path);

	/** Why the last Watch or start failed, as a player reads it. */
	const FString& GetError() const { return Error; }

	/**
	 * From AScctGameMode::StartPlay: true when this level is a demo's (opened by Watch, or the run's -ScctDemo), so it
	 * starts no match. A -ScctDemo on another map's level opens that map's level instead.
	 */
	bool ClaimLevel(UWorld* World, const FString& LevelMap, const FString& Options);

	/** Plays the opened demo in this level (when it is the demo's map's) and puts the playback bar up. */
	void PresentIn(UScctSessionSubsystem* Session, const FString& LevelMap);

	/** Stops watching and returns to the front end's Demos page. */
	void Leave();

	/** The front end asks once when it is shown: true when it should open the Demos page (back from a demo). */
	bool TakeReturnToDemos();

	/** A text of the playback (EScctDemoPlayText). */
	static FString PlayText(EScctDemoPlayText Kind);

	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleBeforeTicks(float DeltaSeconds);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void ApplyStartOptions(const FScctDemoPlayInfo& Info);
	void ShowHud(UWorld* World);
	void HideHud();
	void Fail(const FString& Reason);

	FString Error;
	FString OpenedMap;
	FString CommandLineDemo;
	bool bCommandLineUsed = false;
	bool bReturnToDemos = false;
	bool bLeaving = false;

	/** The world presenting the playback, and its session subsystem's frame hook. */
	TWeakObjectPtr<UWorld> PresentingWorld;
	TWeakObjectPtr<UScctSessionSubsystem> PresentingSession;
	FDelegateHandle BeforeTicksHandle;
	FDelegateHandle CleanupHandle;

	TSharedPtr<SScctDemoHud> Hud;
	TSharedPtr<SWidget> HudRoot;

	/** The switches still to apply once the playback runs. */
	float StartSpeed = 0.0f;
	int32 StartCamera = -1;
	float StartSeek = -1.0f;
	int32 StartPlayer = 0;
	float PauseAfter = -1.0f;
	float LeaveAfter = -1.0f;
	float PlayingSeconds = 0.0f;
	bool bStartApplied = false;
	int32 LastLoggedSecond = -1;
};
