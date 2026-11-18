#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctEffectsWarmup.generated.h"

class AActor;
class UScctSessionSubsystem;

/**
 * The first grenade, the first pair of goggles and the first broken pane of a match no longer stall the picture: the
 * Godot client draws every gameplay effect once in a private world while the play scene loads (GameplayFxWarmup), and
 * this does the same on the level itself before the player sees it.
 *
 * When a level's first match starts (inside the level's BeginPlay, while the movie player still shows the loading
 * page) the loading subsystem keeps the same page over the picture (UScctLoadingSubsystem::BeginCover). Behind it, on
 * the first frames of the match, every look of the effects subsystem is drawn once a little in front of the camera
 * (UScctEffectsSubsystem::SpawnWarmupSamples: each particle style, timed lights with and without shadows, the
 * bullet-mark and projection decals, a casing, a tracer and every imported effect mesh), a piece of each of the map's
 * glass materials as the shards draw them (a procedural mesh), and each of the four goggle modes is held for a few
 * frames (UScctVisionSubsystem::HoldModeForWarmup) so its post-process and its thermal and EMF marks are drawn. That
 * creates the materials' render proxies, which asks the engine to precompile their pipeline states (PSO precaching);
 * the page stays up until those requests and any shader compilation have finished (waiting 20 s at the most once every sample and mode has been drawn, and never covering the running match for more
 * than 30 s from its first frame, however slow the frames are: the match does not wait for the page), and then
 * everything drawn is taken away and the page goes. The engine's PSO cache (Config/DefaultEngine.ini) is switched to
 * its fast batches while this runs.
 *
 * Logged always: 'Warm-up: done in X ms' with what was drawn and what was still compiling.
 *
 * Unattended switches (off by default):
 *   -ScctNoWarmup          no warm-up: the loading page goes as soon as the level has begun play, as before
 *   -ScctWarmupLog         log every frame slower than 50 ms in the first 30 s of play and then the worst frame of
 *                          those 30 s, so runs with and without the warm-up can be compared
 *   -ScctWarmupGoggleTest  with the log: hold thermal, night, motion and EMF in turn from 10 s into play (1.5 s each)
 *                          and log the worst of the four frames after each switch
 */
UCLASS()
class OPENSCCT_API UScctEffectsWarmupSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** True while the warm-up holds the loading page up. */
	bool IsWarming() const { return Phase == EPhase::Showing; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	enum class EPhase : uint8
	{
		Idle,     // no match has started in this world yet
		Showing,  // the page is up and the samples are drawn behind it
		Done,     // finished (or skipped) for this world
	};

	void HandleMatchStarted();
	void HandleSessionChanged(int32 OldId, int32 NewId);
	void HandleFrame(float DeltaSeconds);

	/** One frame of the warm-up; true once it is finished. */
	bool StepWarmup();
	/** Takes everything away, drops the page and starts the play measurement. */
	void Finish(const TCHAR* Why);
	/** A piece of each glass material of the map's panes, drawn as the shards are (procedural meshes). */
	int32 SpawnGlassSamples(const FVector& At, const FVector& Forward);

	/** -ScctWarmupLog: the frame times of the first 30 s of play. */
	void MeasurePlayFrame(double FrameMs);
	void StepGoggleTest();

	UPROPERTY(Transient)
	TObjectPtr<UScctSessionSubsystem> Session;

	/** The glass samples' actor, destroyed at the end. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> GlassHost;

	EPhase Phase = EPhase::Idle;
	bool bDisabled = false;
	bool bLog = false;
	bool bGoggleTest = false;

	// The warm-up.
	double StartSeconds = 0.0;
	int32 Frames = 0;
	int32 SpawnFrame = INDEX_NONE;
	/** When the samples went up: the compilation wait is capped from here. */
	double SpawnSeconds = 0.0;
	int32 NumMeshes = 0;
	int32 NumGlass = 0;
	int32 VisionStep = 0;
	uint32 PeakPrecacheRequests = 0;
	int32 PeakShaderJobs = 0;

	// The play measurement.
	double LastFrameSeconds = 0.0;
	bool bMeasuring = false;
	double PlaySeconds = 0.0;
	double MatchSeconds = 0.0;
	double WorstMs = 0.0;
	double WorstAt = 0.0;
	int32 NumOver33 = 0;
	int32 NumOver50 = 0;
	int32 NumOver100 = 0;
	int32 NumPlayFrames = 0;

	// The goggle test.
	int32 GoggleStep = 0;
	int32 GoggleFramesLeft = 0;
	/** The mode the goggle test holds now. */
	int32 GoggleMode = 0;
	double GoggleWorstMs = 0.0;
};
