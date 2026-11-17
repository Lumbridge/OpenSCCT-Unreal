#pragma once

#include "CoreMinimal.h"

struct FScctTestContext;

// ---------------------------------------------------------------------------------------------------------------
// FRAME RECORDING FOR THE NAMED TESTS
//
// What the frame-time tests (frame-rate, smooth, play-hitch, first-use in Test/ScctFrameTests.cpp) measure: one row
// per engine frame, taken from the test's Step, which the runner calls on the core ticker after the frame's world
// tick, so every row is the frame that has just been simulated and handed to the renderer. A row has the frame's
// wall time (from one Step to the next, the whole frame including the frame limiter's wait), the engine's delta time,
// the game, render, RHI and GPU thread times of the last frame the renderer finished (the figures 'stat unit' shows),
// the garbage collections that ran in the frame and how long they took, the pipeline states the renderer had to create
// on the spot and how many of those were slow, the shader compile jobs still waiting (editor builds compile shaders
// as they are first needed), the simulation ticks the frame ran, the drawn camera, the local player's drawn body and
// how far the first-person view model moved in the camera's frame. Tests mark rows with a phase name. The rows go to
// Saved/Test/frames-<test>.csv for a look in a spreadsheet.
//
// The Godot client's SmoothRecorder, FrameRateRecorder and FrameMotion (game/Scripts/Smoke) are the reference.
//
// Console:
//   scct.TestStall <ms> [frames]   sleeps the game thread that long in each of the next frames (1 by default): an
//                                  artificial hitch, to see play-hitch fail
// ---------------------------------------------------------------------------------------------------------------

struct FScctFrameSample
{
	/** Seconds since recording began, at the end of the frame. */
	double Time = 0.0;

	/** The whole frame as the wall clock saw it, from the last row to this one (ms). */
	float FrameMs = 0.0f;

	/** The engine's delta time for the frame (ms): what the game advanced by. */
	float DeltaMs = 0.0f;

	/** The last finished frame's thread times (ms), as 'stat unit' shows them; 0 where the platform gives none. */
	float GameMs = 0.0f;
	float RenderMs = 0.0f;
	float RhiMs = 0.0f;
	float GpuMs = 0.0f;

	/** Garbage collections that ran in the frame and their time (ms). */
	int32 Collections = 0;
	float CollectMs = 0.0f;

	/** Pipeline states the renderer created on its critical path in the frame, and those that took too long. */
	int32 PsoCreated = 0;
	int32 PsoSlow = 0;

	/** Shader compile jobs waiting at the end of the frame (editor builds only), and the change since the last row. */
	int32 ShaderJobs = 0;
	int32 ShadersDone = 0;

	/** Simulation ticks the session ran in the frame. */
	int32 Ticks = 0;

	/** The drawn camera. */
	bool bCamera = false;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	float Fov = 0.0f;

	/** The local player's drawn body. */
	bool bBody = false;
	FVector BodyLocation = FVector::ZeroVector;

	/** The first-person view model is drawn; how far its bones moved in the camera's frame since the last row (uu, summed). */
	bool bViewModel = false;
	float ViewModelStep = 0.0f;

	/** The test's phase when the row was taken. */
	FName Phase;
};

/** The frame times of a stretch of rows. */
struct FScctFrameTimeStats
{
	int32 Frames = 0;
	double MeanMs = 0.0;
	double MedianMs = 0.0;
	double P99Ms = 0.0;
	double MaxMs = 0.0;

	/** The 99th percentile of each frame's difference from the stretch's mean frame time, as a share of it. */
	double P99Jitter = 0.0;

	/** Frames per second from the mean frame time (what the player gets over the stretch). */
	double Fps() const { return MeanMs > 0.0 ? 1000.0 / MeanMs : 0.0; }
};

/**
 * How evenly something drawn moves from frame to frame: per frame its speed (the step over the engine's delta time)
 * against the moving average of its own speed over the frames around it. Frames where it should have moved (the
 * moving average says it is moving) but held still are counted apart: that is a value advancing at the 60 Hz tick
 * rate instead of every frame. Starts and stops (a walk into a wall) are left out: a frame is judged only when every
 * other frame of its window moves at a third of the stretch's typical speed or more (not for the view model's clips,
 * whose pace rises and falls by itself).
 */
struct FScctMotionStats
{
	/** Frames looked at, and those among them where it was moving. */
	int32 Frames = 0;
	int32 Moving = 0;

	/** Moving frames on which it did not move at all. */
	int32 Held = 0;

	/** The mean step while moving (in the value's units). */
	double MeanStep = 0.0;

	/** The mean and 99th percentile of |speed - moving average| / moving average over the moving frames. */
	double MeanJitter = 0.0;
	double P99Jitter = 0.0;

	double HeldShare() const { return Moving > 0 ? static_cast<double>(Held) / Moving : 0.0; }
	FString ToString() const;
};

class OPENSCCT_API FScctFrameRecorder
{
public:
	FScctFrameRecorder();
	~FScctFrameRecorder();

	FScctFrameRecorder(const FScctFrameRecorder&) = delete;
	FScctFrameRecorder& operator=(const FScctFrameRecorder&) = delete;

	/** Takes this frame's row (call once a frame from the test's Step). The first call only starts the clock. */
	void Sample(const FScctTestContext& Context);

	/** The phase name given to the rows that follow. */
	void SetPhase(FName InPhase) { Phase = InPhase; }
	FName GetPhase() const { return Phase; }

	const TArray<FScctFrameSample>& GetSamples() const { return Samples; }
	const FScctFrameSample* GetLast() const { return Samples.Num() > 0 ? &Samples.Last() : nullptr; }

	/** Row indices [First, End) of one phase (the first run of rows with that name), or false when it has none. */
	bool FindPhase(FName InPhase, int32& OutFirst, int32& OutEnd) const;

	/** Frame-time statistics of the rows [First, End). */
	FScctFrameTimeStats FrameTimes(int32 First, int32 End) const;

	/** The camera's turn (yaw, degrees), the camera's move, the body's move and the view model's own steps. */
	FScctMotionStats CameraTurn(int32 First, int32 End) const;
	FScctMotionStats CameraMove(int32 First, int32 End) const;
	FScctMotionStats BodyMove(int32 First, int32 End) const;
	FScctMotionStats ViewModel(int32 First, int32 End) const;

	/** Writes every row to Saved/Test/frames-<Name>.csv; returns the file's path, or empty when it could not be written. */
	FString WriteCsv(const FString& Name) const;

	/** One line describing a row's frame: its time and what ran in it (for the hitch lists). */
	static FString Describe(const FScctFrameSample& Sample);

	/** The median of some values (0 for none). */
	static double Median(TArray<double> Values);

	/** The q-quantile (0..1) of some values (0 for none). */
	static double Quantile(TArray<double> Values, double Q);

	/**
	 * The motion statistics of per-frame steps against the delta times of the same frames. StillEpsilon: a step no
	 * larger is a frame that held still. MinSpeed: a moving average below it (per second) is not moving. bSkipStops leaves
	 * out the windows around starts and stops (off for a value whose pace rises and falls by itself).
	 */
	static FScctMotionStats Motion(const TArray<double>& Steps, const TArray<double>& DeltaSeconds, double StillEpsilon, double MinSpeed, bool bSkipStops = true);

private:
	void OnPreCollect();
	void OnPostCollect();
	void FindViewModel(const FScctTestContext& Context, FScctFrameSample& Row);

	TArray<FScctFrameSample> Samples;
	FName Phase;
	double StartSeconds = -1.0;
	double LastSeconds = 0.0;

	FDelegateHandle PreCollectHandle;
	FDelegateHandle PostCollectHandle;
	double CollectStart = 0.0;
	int32 CollectionsSince = 0;
	double CollectSecondsSince = 0.0;

	uint32 LastPsoCreated = 0;
	uint32 LastPsoSlow = 0;
	int32 LastShaderJobs = 0;

	/** The view model's bones in the camera's frame last row, to take this row's step from. */
	TArray<FVector> LastViewModelBones;
};
