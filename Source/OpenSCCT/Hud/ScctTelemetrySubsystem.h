#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctHudNetApi.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctTelemetrySubsystem.generated.h"

struct FScctTelemetryPanel;

/**
 * The frame-rate and network readouts of a match level (Godot's HudFps and HudNet with Options/TelemetrySettings).
 *
 * FRAME RATE. Unreal's frame times (FApp::GetDeltaTime) over the last 5 s, at most 2048 frames, are sampled every frame
 * even while the readout is hidden. Simple shows frames a second and the frame time; Detailed adds the frame rate of the
 * slowest and the fastest 1 % of frames, the average and the longest frame, and (Hud.TelemetryGraph) a frame-time graph
 * over 5 s with a line at 33.3 ms. A frame over 33.3 ms colours the readout for the issue hold time.
 *
 * NETWORK. The bridge's running totals of the local player's link (Runtime/ScctHudNetApi.h), turned into rates over a
 * one-second window as Godot's HudNet does. Simple shows the round trip, jitter and the server's send rate, the issue line
 * and the host line; Detailed adds traffic each way, packet loss each way, the interpolation buffer with the snapshot age
 * and underruns, corrections a second and the last correction's size, replayed moves with animation and state gaps,
 * pose and owner resets, and (Hud.TelemetryGraph) ping, jitter and loss graphs over 5 s. A listen host's own link is the
 * loopback (no round trip): its host line counts the other players with their best and worst ping, and those the host
 * measures none for (bots). A ping over 150 ms, jitter over 30 ms, loss over 2 %, stale snapshots, an underrun, a
 * prediction reset or a state gap is named on the issue line and stays there for Hud.TelemetryIssueHoldSeconds.
 *
 * OPTIONS (page Hud, group Telemetry): Hud.FpsMode and Hud.NetMode (Off, Simple, Detailed), Hud.TelemetryPosition (a
 * corner; at the top right the kill feed moves down below the panels, at the top left they stand beside the radar, at
 * the bottom above the status block or the equipment), Hud.TelemetryScale, Hud.TelemetryOpacity,
 * Hud.TelemetryGraph, Hud.TelemetryIssueHoldSeconds. They are read every frame, so a change applies at once.
 *
 * Console: scct.Telemetry <off|simple|detailed> [fps|net] sets the options (and saves them, as the settings page does).
 * Unattended switches (off by default):
 *   -ScctTelemetry=<off|simple|detailed>   forces both readouts for the run (not saved; the settings page's change wins)
 *   -ScctHudCommands "Telemetry N"          forces both options to N (0 off, 1 simple, 2 detailed) from that moment, for
 *                                           the run only, as a mid-match change of the option
 * While a readout is shown a "ScctTelemetry:" line with the numbers goes to the log every 3 s.
 */
UCLASS()
class OPENSCCT_API UScctTelemetrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** A timed HUD command of the readouts (Telemetry); false for a name that is not one. */
	bool RunCommand(const FString& Name, int32 Argument);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static void RegisterSettings();

	void HandleSessionChanged(int32 OldSessionId, int32 NewSessionId);
	void HandleFrame(float DeltaSeconds);

	// --- Frame rate (Godot's HudFps) ---
	void AddFrame(float Seconds);
	void MeasureFrames();
	void BuildFpsPanel(FScctTelemetryPanel& Panel, int32 Mode, bool bGraph) const;

	// --- Network (Godot's HudNet) ---
	void ResetNet();
	void AdvanceNet(const FScctHudNetState& Now, float DeltaSeconds, float HoldSeconds);
	void BuildNetPanel(FScctTelemetryPanel& Panel, const FScctHudNetState& Now, int32 Mode, bool bGraph) const;
	int32 LinkIssues(const FScctHudNetState& Now) const;

	void LogLine(int32 FpsMode, int32 NetMode, bool bNet, const FScctHudNetState& Net) const;

	static constexpr int32 MaxFrames = 2048;
	static constexpr int32 GraphSamples = 602;
	static constexpr int32 GraphColumns = 120;
	static constexpr int32 NetHistoryCapacity = 301;

	double Clock = 0.0;

	// Frame times over the last 5 s.
	float Frames[MaxFrames] = {};
	int32 FrameStart = 0;
	int32 FrameCount = 0;
	float FrameSum = 0.0f;
	float LastFrame = 0.0f;
	float FpsIssueRemaining = 0.0f;
	float FpsRefresh = 0.0f;
	// Frame-time peaks per 0.05 s, for the graph.
	double GraphTimes[GraphSamples] = {};
	float GraphDurations[GraphSamples] = {};
	float GraphPeaks[GraphSamples] = {};
	int32 GraphStart = 0;
	int32 GraphCount = 0;
	float BucketElapsed = 0.0f;
	float BucketPeak = 0.0f;
	// The last measure (refreshed every 0.25 s).
	float RateNow = 0.0f;
	float RateLow = 0.0f;
	float RateHigh = 0.0f;
	float RateAverage = 0.0f;
	float PeakFrameMs = 0.0f;
	float ShownFrameMs = 0.0f;

	// The network window.
	FScctHudNetState NetFrom = {};
	FScctHudNetState SampleFrom = {};
	FScctHudNetState SampleTo = {};
	bool bHasNetFrom = false;
	float NetElapsed = 0.0f;
	float SampleSeconds = 0.0f;
	float LossIn = 0.0f;
	float LossOut = 0.0f;
	float NetIssueRemaining = 0.0f;
	int32 IntervalIssues = 0;
	int32 ShownIssues = 0;
	bool bNetIssueNow = false;
	struct FNetSample
	{
		double Time = 0.0;
		float Ping = 0.0f;
		float Jitter = 0.0f;
		float Loss = 0.0f;
	};
	FNetSample NetHistory[NetHistoryCapacity];
	int32 NetHistoryStart = 0;
	int32 NetHistoryCount = 0;
	float NetHistoryElapsed = 0.0f;

	float LogTimer = 0.0f;
	bool bTableMissingLogged = false;
};
