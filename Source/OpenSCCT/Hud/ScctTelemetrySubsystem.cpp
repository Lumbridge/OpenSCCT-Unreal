#include "Hud/ScctTelemetrySubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudSubsystem.h"
#include "Hud/ScctTelemetryView.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctSessionSubsystem.h"
#include "Settings/ScctSettings.h"
#include "Settings/ScctSettingsForced.h"

#define LOCTEXT_NAMESPACE "ScctTelemetry"

namespace
{
	const FName FpsModeKey(TEXT("Hud.FpsMode"));
	const FName NetModeKey(TEXT("Hud.NetMode"));
	const FName TelemetryPositionKey(TEXT("Hud.TelemetryPosition"));
	const FName TelemetryScaleKey(TEXT("Hud.TelemetryScale"));
	const FName TelemetryOpacityKey(TEXT("Hud.TelemetryOpacity"));
	const FName TelemetryGraphKey(TEXT("Hud.TelemetryGraph"));
	const FName TelemetryHoldKey(TEXT("Hud.TelemetryIssueHoldSeconds"));

	// Godot's HudFps and TelemetrySettings defaults.
	constexpr float FpsWindowSeconds = 5.0f;
	constexpr float FpsRefreshSeconds = 0.25f;
	constexpr float FpsGraphSampleSeconds = 0.05f;
	constexpr float TelemetryGraphSeconds = 5.0f;
	constexpr float FrameTimeThresholdMs = 33.3f;
	// Godot's NetworkTelemetrySettings defaults.
	constexpr float PingThresholdMs = 150.0f;
	constexpr float JitterThresholdMs = 30.0f;
	constexpr float LossThresholdPercent = 2.0f;
	constexpr float NetWindowSeconds = 1.0f;
	constexpr float LogEverySeconds = 3.0f;

	namespace ENetIssue
	{
		enum : int32
		{
			Ping = 1,
			Jitter = 2,
			Loss = 4,
			Stale = 8,
			Underrun = 16,
			Prediction = 32,
			State = 64,
		};
	}

	FLinearColor TelemetrySrgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B));
	}

	int32 TelemetryModeOf(const FString& Text)
	{
		if (Text.Equals(TEXT("detailed"), ESearchCase::IgnoreCase) || Text == TEXT("2"))
		{
			return 2;
		}
		if (Text.Equals(TEXT("simple"), ESearchCase::IgnoreCase) || Text == TEXT("1"))
		{
			return 1;
		}
		if (Text.Equals(TEXT("off"), ESearchCase::IgnoreCase) || Text == TEXT("0"))
		{
			return 0;
		}
		return -1;
	}

	const TCHAR* TelemetryModeName(int32 Mode)
	{
		return Mode >= 2 ? TEXT("detailed") : Mode == 1 ? TEXT("simple") : TEXT("off");
	}

	/** Packet loss each way between two totals, in percent (HudNet.Loss). */
	void NetLoss(const FScctHudNetState& From, const FScctHudNetState& To, float& OutIn, float& OutOut)
	{
		const int64 Received = FMath::Max<int64>(0, To.InPackets - From.InPackets);
		const int64 InLost = FMath::Max<int64>(0, To.InPacketsLost - From.InPacketsLost);
		const int64 Sent = FMath::Max<int64>(0, To.OutPackets - From.OutPackets);
		const int64 OutLost = FMath::Max<int64>(0, To.OutPacketsLost - From.OutPacketsLost);
		OutIn = Received + InLost > 0 ? FMath::Clamp(100.0f * InLost / (Received + InLost), 0.0f, 100.0f) : 0.0f;
		OutOut = Sent > 0 ? FMath::Clamp(100.0f * OutLost / Sent, 0.0f, 100.0f) : 0.0f;
	}

	bool NetCountersReset(const FScctHudNetState& From, const FScctHudNetState& To)
	{
		return To.InPackets < From.InPackets || To.OutPackets < From.OutPackets || To.InBytes < From.InBytes
			|| To.OutBytes < From.OutBytes || To.InPacketsLost < From.InPacketsLost || To.OutPacketsLost < From.OutPacketsLost
			|| To.Corrections < From.Corrections || To.BufferUnderflows < From.BufferUnderflows
			|| To.PredictionResets < From.PredictionResets || To.StateGaps < From.StateGaps;
	}

	int32 NetCounterIssues(const FScctHudNetState& From, const FScctHudNetState& To)
	{
		return (To.BufferUnderflows > From.BufferUnderflows ? ENetIssue::Underrun : 0)
			| (To.PredictionResets > From.PredictionResets ? ENetIssue::Prediction : 0)
			| (To.StateGaps > From.StateGaps ? ENetIssue::State : 0);
	}

	FString NetPingText(const FScctHudNetState& S)
	{
		if (S.HasRtt != 0)
		{
			return FString::Printf(TEXT("ping %lld ms  jitter %lld ms"), S.RttMicros / 1000, S.JitterMicros / 1000);
		}
		// A listen host's own player is on the loopback: no round trip to measure.
		return S.IsHost != 0 ? FString(TEXT("ping 0 ms (local host)")) : FString(TEXT("ping -  jitter -"));
	}

	/** The host's line: the other players with a ping, their best and worst, and those without one. */
	FString NetHostText(const FScctHudNetState& S)
	{
		if (S.IsHost == 0)
		{
			return FString();
		}
		FString Text = S.Clients > 0
			? FString::Printf(TEXT("%d client%s  best %d ms  worst %d ms"), S.Clients, S.Clients == 1 ? TEXT("") : TEXT("s"), S.BestClientPingMs, S.WorstClientPingMs)
			: FString(TEXT("no remote clients"));
		if (S.Unmeasured > 0)
		{
			Text += FString::Printf(TEXT("  %d without ping (bots)"), S.Unmeasured);
		}
		return Text;
	}

	void SetTelemetryModes(int32 Mode, bool bFps, bool bNet, bool bSave)
	{
		if (bFps)
		{
			FScctSettings::SetInt(FpsModeKey, Mode);
		}
		if (bNet)
		{
			FScctSettings::SetInt(NetModeKey, Mode);
		}
		if (bSave)
		{
			FScctSettings::Save();
		}
	}

	FAutoConsoleCommand TelemetryConsoleCommand(
		TEXT("scct.Telemetry"),
		TEXT("scct.Telemetry <off|simple|detailed> [fps|net]: shows the frame-rate and network readouts (Hud.FpsMode, Hud.NetMode) and saves the choice."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const int32 Mode = Args.Num() > 0 ? TelemetryModeOf(Args[0]) : -1;
			if (Mode < 0)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("scct.Telemetry: give off, simple or detailed, and optionally fps or net"));
				return;
			}
			const FString Which = Args.Num() > 1 ? Args[1] : FString();
			const bool bFps = Which.IsEmpty() || Which.Equals(TEXT("fps"), ESearchCase::IgnoreCase);
			const bool bNet = Which.IsEmpty() || Which.Equals(TEXT("net"), ESearchCase::IgnoreCase);
			SetTelemetryModes(Mode, bFps, bNet, true);
			UE_LOG(LogOpenSCCT, Log, TEXT("scct.Telemetry: frame rate %s, network %s"),
				TelemetryModeName(FScctSettings::GetInt(FpsModeKey)), TelemetryModeName(FScctSettings::GetInt(NetModeKey)));
		}));
}

bool UScctTelemetrySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UScctTelemetrySubsystem::RegisterSettings()
{
	const FText Group = LOCTEXT("Group", "Telemetry");
	const TArray<FText> Modes = { LOCTEXT("Off", "Off"), LOCTEXT("Simple", "Simple"), LOCTEXT("Detailed", "Detailed") };

	FScctSettingDesc Fps;
	Fps.Key = FpsModeKey;
	Fps.Page = TEXT("Hud");
	Fps.Group = Group;
	Fps.Label = LOCTEXT("FpsLabel", "Frame rate readout");
	Fps.Tooltip = LOCTEXT("FpsTip", "Simple: frames a second and the frame time. Detailed: also the slowest and fastest 1% of frames, the average, the longest frame and a frame-time graph.");
	Fps.Type = EScctSettingType::Choice;
	Fps.Choices = Modes;
	Fps.Default = TEXT("0");
	Fps.Order = 60;
	FScctSettings::Register(Fps);

	FScctSettingDesc Net;
	Net.Key = NetModeKey;
	Net.Page = TEXT("Hud");
	Net.Group = Group;
	Net.Label = LOCTEXT("NetLabel", "Network readout");
	Net.Tooltip = LOCTEXT("NetTip", "Simple: ping, jitter and the server's send rate, with any problem named. Detailed: also traffic and packet loss each way, the interpolation buffer, corrections, replays, resets and ping, jitter and loss graphs.");
	Net.Type = EScctSettingType::Choice;
	Net.Choices = Modes;
	Net.Default = TEXT("0");
	Net.Order = 61;
	FScctSettings::Register(Net);

	FScctSettingDesc Position;
	Position.Key = TelemetryPositionKey;
	Position.Page = TEXT("Hud");
	Position.Group = Group;
	Position.Label = LOCTEXT("PositionLabel", "Readout corner");
	Position.Tooltip = LOCTEXT("PositionTip", "The corner of the screen the readouts stack in. At the top right the kill feed moves down below them; at the top left they stand beside the radar, at the bottom above your status or equipment.");
	Position.Type = EScctSettingType::Choice;
	Position.Choices = { LOCTEXT("TopRight", "Top right"), LOCTEXT("TopLeft", "Top left"), LOCTEXT("BottomRight", "Bottom right"), LOCTEXT("BottomLeft", "Bottom left") };
	Position.Default = TEXT("0");
	Position.Order = 62;
	FScctSettings::Register(Position);

	FScctSettingDesc Scale;
	Scale.Key = TelemetryScaleKey;
	Scale.Page = TEXT("Hud");
	Scale.Group = Group;
	Scale.Label = LOCTEXT("ScaleLabel", "Readout size");
	Scale.Tooltip = LOCTEXT("ScaleTip", "How large the readouts are drawn.");
	Scale.Type = EScctSettingType::Float;
	Scale.Default = TEXT("1.0");
	Scale.Min = 0.5f;
	Scale.Max = 2.0f;
	Scale.Step = 0.05f;
	Scale.Order = 63;
	Scale.FormatValue = [](float Value) { return FString::Printf(TEXT("%.0f%%"), Value * 100.0f); };
	FScctSettings::Register(Scale);

	FScctSettingDesc Opacity;
	Opacity.Key = TelemetryOpacityKey;
	Opacity.Page = TEXT("Hud");
	Opacity.Group = Group;
	Opacity.Label = LOCTEXT("OpacityLabel", "Readout opacity");
	Opacity.Tooltip = LOCTEXT("OpacityTip", "How solid the readouts are drawn over the picture.");
	Opacity.Type = EScctSettingType::Float;
	Opacity.Default = TEXT("1.0");
	Opacity.Min = 0.1f;
	Opacity.Max = 1.0f;
	Opacity.Step = 0.05f;
	Opacity.Order = 64;
	Opacity.FormatValue = [](float Value) { return FString::Printf(TEXT("%.0f%%"), Value * 100.0f); };
	FScctSettings::Register(Opacity);

	FScctSettingDesc Graph;
	Graph.Key = TelemetryGraphKey;
	Graph.Page = TEXT("Hud");
	Graph.Group = Group;
	Graph.Label = LOCTEXT("GraphLabel", "Readout graphs");
	Graph.Tooltip = LOCTEXT("GraphTip", "Under a detailed readout, graphs of the last 5 seconds: frame times, and ping, jitter and packet loss.");
	Graph.Type = EScctSettingType::Bool;
	Graph.Default = TEXT("true");
	Graph.Order = 65;
	FScctSettings::Register(Graph);

	FScctSettingDesc Hold;
	Hold.Key = TelemetryHoldKey;
	Hold.Page = TEXT("Hud");
	Hold.Group = Group;
	Hold.Label = LOCTEXT("HoldLabel", "Keep problems shown for");
	Hold.Tooltip = LOCTEXT("HoldTip", "Seconds a long frame, a ping or jitter spike, packet loss or a buffer underrun stays highlighted after it is over.");
	Hold.Type = EScctSettingType::Float;
	Hold.Default = TEXT("3.0");
	Hold.Min = 0.0f;
	Hold.Max = 10.0f;
	Hold.Step = 0.5f;
	Hold.Order = 66;
	Hold.FormatValue = [](float Value) { return FString::Printf(TEXT("%.1f s"), Value); };
	FScctSettings::Register(Hold);
}

void UScctTelemetrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSettings();

	Collection.InitializeDependency<UScctHudSubsystem>();
	if (UScctSessionSubsystem* Session = Collection.InitializeDependency<UScctSessionSubsystem>())
	{
		Session->OnSessionChanged.AddUObject(this, &UScctTelemetrySubsystem::HandleSessionChanged);
		Session->OnFrame.AddUObject(this, &UScctTelemetrySubsystem::HandleFrame);
	}

	FString Debug;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctTelemetry="), Debug))
	{
		const int32 Mode = TelemetryModeOf(Debug.TrimStartAndEnd());
		if (Mode < 0)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctTelemetry: '%s' is not off, simple or detailed"), *Debug);
		}
		else
		{
			FScctSettingsForced::Force(FpsModeKey, FString::FromInt(Mode));
			FScctSettingsForced::Force(NetModeKey, FString::FromInt(Mode));
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctTelemetry: both readouts %s for this run (not saved)"), TelemetryModeName(Mode));
		}
	}
}

void UScctTelemetrySubsystem::Deinitialize()
{
	if (UScctSessionSubsystem* Session = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UScctSessionSubsystem>() : nullptr)
	{
		Session->OnSessionChanged.RemoveAll(this);
		Session->OnFrame.RemoveAll(this);
	}
	Super::Deinitialize();
}

bool UScctTelemetrySubsystem::RunCommand(const FString& Name, int32 Argument)
{
	if (!Name.Equals(TEXT("Telemetry"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	const int32 Mode = FMath::Clamp(Argument, 0, 2);
	FScctSettingsForced::Force(FpsModeKey, FString::FromInt(Mode));
	FScctSettingsForced::Force(NetModeKey, FString::FromInt(Mode));
	UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHudCommands: Telemetry %d: both readouts %s from now (for this run)"), Argument, TelemetryModeName(Mode));
	LogTimer = 0.5f;
	return true;
}

void UScctTelemetrySubsystem::HandleSessionChanged(int32 OldSessionId, int32 NewSessionId)
{
	// The bridge's totals belong to one connection; the frame times carry on.
	ResetNet();
}

// --- Frame rate ------------------------------------------------------------------------------------------------

void UScctTelemetrySubsystem::AddFrame(float Seconds)
{
	if (!FMath::IsFinite(Seconds) || Seconds <= 0.0f)
	{
		return;
	}
	if (FrameCount == MaxFrames)
	{
		FrameSum -= Frames[FrameStart];
		FrameStart = (FrameStart + 1) % MaxFrames;
		--FrameCount;
	}
	Frames[(FrameStart + FrameCount) % MaxFrames] = Seconds;
	++FrameCount;
	FrameSum += Seconds;
	while (FrameCount > 1 && FrameSum - Frames[FrameStart] >= FpsWindowSeconds)
	{
		FrameSum -= Frames[FrameStart];
		FrameStart = (FrameStart + 1) % MaxFrames;
		--FrameCount;
	}
	LastFrame = Seconds;

	// Peaks, not averages, so short hitches survive the graph's width.
	BucketElapsed += Seconds;
	BucketPeak = FMath::Max(BucketPeak, Seconds * 1000.0f);
	if (BucketElapsed >= FpsGraphSampleSeconds)
	{
		if (GraphCount == GraphSamples)
		{
			GraphStart = (GraphStart + 1) % GraphSamples;
			--GraphCount;
		}
		const int32 Index = (GraphStart + GraphCount++) % GraphSamples;
		GraphTimes[Index] = Clock;
		GraphDurations[Index] = BucketElapsed;
		GraphPeaks[Index] = BucketPeak;
		BucketElapsed = 0.0f;
		BucketPeak = 0.0f;
	}
}

void UScctTelemetrySubsystem::MeasureFrames()
{
	if (FrameCount == 0)
	{
		return;
	}
	TArray<float, TInlineAllocator<MaxFrames>> Sorted;
	Sorted.SetNumUninitialized(FrameCount);
	for (int32 Index = 0; Index < FrameCount; ++Index)
	{
		Sorted[Index] = Frames[(FrameStart + Index) % MaxFrames];
	}
	Sorted.Sort();
	const int32 OnePercent = FMath::Max(1, FrameCount / 100);
	float Slow = 0.0f, Fast = 0.0f;
	for (int32 Index = 0; Index < OnePercent; ++Index)
	{
		Fast += Sorted[Index];
		Slow += Sorted[FrameCount - 1 - Index];
	}
	RateNow = 1.0f / LastFrame;
	RateLow = OnePercent / Slow;
	RateHigh = OnePercent / Fast;
	RateAverage = FrameCount / FMath::Max(FrameSum, 0.0001f);
	PeakFrameMs = Sorted[FrameCount - 1] * 1000.0f;
	ShownFrameMs = LastFrame * 1000.0f;
}

void UScctTelemetrySubsystem::BuildFpsPanel(FScctTelemetryPanel& Panel, int32 Mode, bool bGraph) const
{
	Panel.bShown = FrameCount > 0;
	Panel.bIssue = ShownFrameMs > FrameTimeThresholdMs || FpsIssueRemaining > 0.0f;
	FScctTelemetryRow& Main = Panel.Rows.AddDefaulted_GetRef();
	Main.Value = FString::Printf(TEXT("%.0f fps  %.1f ms"), RateNow, ShownFrameMs);
	Main.bAlert = Panel.bIssue;
	if (Mode < 2)
	{
		return;
	}
	Panel.Rows.Add({ FString(), FString::Printf(TEXT("1%% low %.0f  1%% high %.0f"), RateLow, RateHigh), false });
	Panel.Rows.Add({ FString(), FString::Printf(TEXT("avg %.0f fps  peak %.1f ms"), RateAverage, PeakFrameMs), PeakFrameMs > FrameTimeThresholdMs });
	if (!bGraph)
	{
		return;
	}

	// HudFps.FillGraph: each 0.05 s peak over the columns it covers.
	FScctTelemetryGraph& Graph = Panel.Graphs.AddDefaulted_GetRef();
	Graph.Values.SetNumZeroed(GraphColumns);
	const double Begin = Clock - TelemetryGraphSeconds;
	float Maximum = 0.0f;
	for (int32 Index = 0; Index < GraphCount; ++Index)
	{
		const int32 At = (GraphStart + Index) % GraphSamples;
		if (GraphTimes[At] < Begin)
		{
			continue;
		}
		const int32 First = FMath::Clamp(static_cast<int32>((GraphTimes[At] - GraphDurations[At] - Begin) / TelemetryGraphSeconds * GraphColumns), 0, GraphColumns - 1);
		const int32 Last = FMath::Clamp(static_cast<int32>((GraphTimes[At] - Begin) / TelemetryGraphSeconds * GraphColumns), 0, GraphColumns - 1);
		for (int32 Column = First; Column <= Last; ++Column)
		{
			Graph.Values[Column] = FMath::Max(Graph.Values[Column], GraphPeaks[At]);
		}
		Maximum = FMath::Max(Maximum, GraphPeaks[At]);
	}
	Graph.Values.Last() = FMath::Max(Graph.Values.Last(), BucketPeak);
	Maximum = FMath::Max(Maximum, BucketPeak);
	// A floor keeps small changes honest; a hitch widens the scale.
	Graph.Maximum = FMath::Max(FMath::Max(16.7f, FrameTimeThresholdMs), Maximum);
	Graph.Threshold = FrameTimeThresholdMs;
	Graph.Fill = 1.0f;
	Graph.Colour = TelemetrySrgb(128, 204, 242);
	Graph.Caption = FString::Printf(TEXT("FRAME TIME  %.0fs  0-%.1f ms"), TelemetryGraphSeconds, Graph.Maximum);
}

// --- Network ---------------------------------------------------------------------------------------------------

void UScctTelemetrySubsystem::ResetNet()
{
	NetFrom = SampleFrom = SampleTo = FScctHudNetState{};
	bHasNetFrom = false;
	NetElapsed = SampleSeconds = LossIn = LossOut = NetIssueRemaining = NetHistoryElapsed = 0.0f;
	IntervalIssues = ShownIssues = 0;
	bNetIssueNow = false;
	NetHistoryStart = NetHistoryCount = 0;
}

int32 UScctTelemetrySubsystem::LinkIssues(const FScctHudNetState& Now) const
{
	int32 Issues = 0;
	if ((Now.HasRtt != 0 && Now.RttMicros / 1000.0f > PingThresholdMs) || (Now.IsHost != 0 && Now.Clients > 0 && Now.WorstClientPingMs > PingThresholdMs))
	{
		Issues |= ENetIssue::Ping;
	}
	if (Now.HasRtt != 0 && Now.JitterMicros / 1000.0f > JitterThresholdMs)
	{
		Issues |= ENetIssue::Jitter;
	}
	if (Now.BufferMs > 0.0f && Now.SnapshotAgeMs > FMath::Max(250.0f, Now.BufferMs * 2.0f))
	{
		Issues |= ENetIssue::Stale;
	}
	return Issues;
}

void UScctTelemetrySubsystem::AdvanceNet(const FScctHudNetState& Now, float DeltaSeconds, float HoldSeconds)
{
	// A new connection restarts the counts.
	if (!bHasNetFrom || NetCountersReset(NetFrom, Now))
	{
		ResetNet();
		NetFrom = Now;
		bHasNetFrom = true;
	}
	NetElapsed += DeltaSeconds;
	NetIssueRemaining = FMath::Max(0.0f, NetIssueRemaining - DeltaSeconds);
	if (NetElapsed >= NetWindowSeconds)
	{
		SampleFrom = NetFrom;
		SampleTo = Now;
		SampleSeconds = NetElapsed;
		NetLoss(NetFrom, Now, LossIn, LossOut);
		IntervalIssues = NetCounterIssues(NetFrom, Now) | (FMath::Max(LossIn, LossOut) > LossThresholdPercent ? ENetIssue::Loss : 0);
		NetFrom = Now;
		NetElapsed = 0.0f;
	}
	const int32 Active = IntervalIssues | LinkIssues(Now) | NetCounterIssues(NetFrom, Now);
	bNetIssueNow = Active != 0;
	if (bNetIssueNow)
	{
		NetIssueRemaining = HoldSeconds;
		ShownIssues = Active;
	}
	else if (NetIssueRemaining <= 0.0f)
	{
		ShownIssues = 0;
	}

	// A history sample every 0.1 s, kept for 30 s.
	NetHistoryElapsed += DeltaSeconds;
	if (NetHistoryCount == 0 || NetHistoryElapsed >= 0.1f)
	{
		NetHistoryElapsed = FMath::Fmod(NetHistoryElapsed, 0.1f);
		if (NetHistoryCount == NetHistoryCapacity)
		{
			NetHistoryStart = (NetHistoryStart + 1) % NetHistoryCapacity;
			--NetHistoryCount;
		}
		FNetSample& Sample = NetHistory[(NetHistoryStart + NetHistoryCount++) % NetHistoryCapacity];
		Sample.Time = Clock;
		Sample.Ping = Now.HasRtt != 0 ? FMath::Max(0.0f, Now.RttMicros / 1000.0f) : (Now.IsHost != 0 ? static_cast<float>(Now.WorstClientPingMs) : 0.0f);
		Sample.Jitter = Now.HasRtt != 0 ? FMath::Max(0.0f, Now.JitterMicros / 1000.0f) : 0.0f;
		Sample.Loss = FMath::Max(LossIn, LossOut);
		while (NetHistoryCount > 1 && Clock - NetHistory[NetHistoryStart].Time > 30.0)
		{
			NetHistoryStart = (NetHistoryStart + 1) % NetHistoryCapacity;
			--NetHistoryCount;
		}
	}
}

void UScctTelemetrySubsystem::BuildNetPanel(FScctTelemetryPanel& Panel, const FScctHudNetState& Now, int32 Mode, bool bGraph) const
{
	Panel.bShown = true;
	Panel.Header = TEXT("NETWORK");
	Panel.Tag = SampleSeconds > 0.0f ? TEXT("1s sample") : TEXT("live link");
	Panel.bIssue = bNetIssueNow || NetIssueRemaining > 0.0f;

	Panel.Rows.Add({ TEXT("LINK"), FString::Printf(TEXT("%s  %d Hz"), *NetPingText(Now), Now.ServerNetRate), LinkIssues(Now) != 0 });
	if (ShownIssues != 0)
	{
		TArray<FString> Reasons;
		if (ShownIssues & ENetIssue::Ping) Reasons.Add(TEXT("high ping"));
		if (ShownIssues & ENetIssue::Jitter) Reasons.Add(TEXT("high jitter"));
		if (ShownIssues & ENetIssue::Loss) Reasons.Add(TEXT("packet loss"));
		if (ShownIssues & ENetIssue::Stale) Reasons.Add(TEXT("stale snapshots"));
		if (ShownIssues & ENetIssue::Underrun) Reasons.Add(TEXT("buffer underrun"));
		if (ShownIssues & ENetIssue::Prediction) Reasons.Add(TEXT("prediction reset"));
		if (ShownIssues & ENetIssue::State) Reasons.Add(TEXT("state gap"));
		Panel.Rows.Add({ TEXT("ISSUE"), (bNetIssueNow ? FString() : FString(TEXT("recent: "))) + FString::Join(Reasons, TEXT("  ")), true });
	}

	if (Mode >= 2 && SampleSeconds > 0.0f)
	{
		// HudNet.FormatRows over the last whole second.
		const FScctHudNetState& From = SampleFrom;
		const FScctHudNetState& To = SampleTo;
		const float Seconds = FMath::Max(SampleSeconds, 0.001f);
		const float Down = (To.InBytes - From.InBytes) / 1024.0f / Seconds;
		const float Up = (To.OutBytes - From.OutBytes) / 1024.0f / Seconds;
		const float Corrections = (To.Corrections - From.Corrections) / Seconds;
		const int64 InLost = To.InPacketsLost - From.InPacketsLost;
		const int64 OutLost = To.OutPacketsLost - From.OutPacketsLost;
		Panel.Rows.Add({ TEXT("TRAFFIC"), FString::Printf(TEXT("down %.1f KB/s  up %.1f KB/s"), Down, Up), false });
		Panel.Rows.Add({ TEXT("LOSS"), FString::Printf(TEXT("in %.1f%%  out %.1f%%"), LossIn, LossOut), InLost > 0 || OutLost > 0 });
		if (To.BufferMs > 0.0f)
		{
			Panel.Rows.Add({ TEXT("BUFFER"), FString::Printf(TEXT("%.0f ms  age %.0f ms  underruns %lld"), To.BufferMs, To.SnapshotAgeMs, To.BufferUnderflows - From.BufferUnderflows),
				To.BufferUnderflows > From.BufferUnderflows });
		}
		Panel.Rows.Add({ TEXT("SYNC"), FString::Printf(TEXT("corr %.1f/s  adjust %.1f uu"), Corrections, To.CorrectionDistance), false });
		if (To.BufferMs > 0.0f)
		{
			Panel.Rows.Add({ TEXT("REPLAY"), FString::Printf(TEXT("%lld moves  anim gaps %lld  state gaps %lld"), To.MovesReplayed - From.MovesReplayed,
				To.AnimationGaps - From.AnimationGaps, To.StateGaps - From.StateGaps), To.AnimationGaps > From.AnimationGaps || To.StateGaps > From.StateGaps });
			Panel.Rows.Add({ TEXT("RESETS"), FString::Printf(TEXT("pose %lld  owner %lld"), To.PoseResets - From.PoseResets, To.PredictionResets - From.PredictionResets),
				To.PoseResets > From.PoseResets || To.PredictionResets > From.PredictionResets });
		}
	}

	const FString Host = NetHostText(Now);
	if (!Host.IsEmpty())
	{
		Panel.Rows.Add({ TEXT("HOST"), Host, Now.WorstClientPingMs > PingThresholdMs });
	}

	if (Mode < 2 || !bGraph || NetHistoryCount == 0)
	{
		return;
	}
	// The last 5 s of history, oldest first; a short history covers only its share of the width.
	const double Begin = Clock - TelemetryGraphSeconds;
	int32 First = 0;
	while (First < NetHistoryCount - 1 && NetHistory[(NetHistoryStart + First) % NetHistoryCapacity].Time < Begin)
	{
		++First;
	}
	const double Oldest = NetHistory[(NetHistoryStart + First) % NetHistoryCapacity].Time;
	const float Fill = FMath::Clamp(static_cast<float>((Clock - Oldest) / TelemetryGraphSeconds), 0.02f, 1.0f);
	struct FMetric
	{
		const TCHAR* Caption;
		float Floor;
		float Threshold;
		FLinearColor Colour;
	};
	const FMetric Metrics[] = {
		{ TEXT("PING ms"), FMath::Max(50.0f, PingThresholdMs), PingThresholdMs, TelemetrySrgb(89, 217, 255) },
		{ TEXT("JITTER ms"), FMath::Max(10.0f, JitterThresholdMs), JitterThresholdMs, TelemetrySrgb(242, 191, 77) },
		{ TEXT("LOSS % (worst direction)"), FMath::Max(5.0f, LossThresholdPercent), LossThresholdPercent, TelemetrySrgb(255, 115, 102) },
	};
	for (int32 Metric = 0; Metric < UE_ARRAY_COUNT(Metrics); ++Metric)
	{
		FScctTelemetryGraph& Graph = Panel.Graphs.AddDefaulted_GetRef();
		float Peak = 0.0f;
		for (int32 Index = First; Index < NetHistoryCount; ++Index)
		{
			const FNetSample& Sample = NetHistory[(NetHistoryStart + Index) % NetHistoryCapacity];
			const float Value = Metric == 0 ? Sample.Ping : Metric == 1 ? Sample.Jitter : Sample.Loss;
			Graph.Values.Add(Value);
			Peak = FMath::Max(Peak, Value);
		}
		Graph.Maximum = FMath::Max(Metrics[Metric].Floor, Peak);
		Graph.Threshold = Metrics[Metric].Threshold;
		Graph.Fill = Fill;
		Graph.Colour = Metrics[Metric].Colour;
		Graph.Caption = FString::Printf(TEXT("%s  0-%.0f  / %.0fs"), Metrics[Metric].Caption, Graph.Maximum, TelemetryGraphSeconds);
	}
}

// --- Each frame ------------------------------------------------------------------------------------------------

void UScctTelemetrySubsystem::HandleFrame(float DeltaSeconds)
{
	// Unreal's own frame time, not the world's (which time dilation and pauses change).
	const float FrameSeconds = static_cast<float>(FApp::GetDeltaTime());
	Clock += FMath::Max(0.0f, FrameSeconds);
	AddFrame(FrameSeconds);

	const float HoldSeconds = FMath::Clamp(FScctSettings::GetFloat(TelemetryHoldKey), 0.0f, 10.0f);
	FpsIssueRemaining = FMath::Max(0.0f, FpsIssueRemaining - FrameSeconds);
	if (FrameSeconds * 1000.0f > FrameTimeThresholdMs)
	{
		FpsIssueRemaining = HoldSeconds;
	}
	FpsRefresh -= FrameSeconds;
	if (FpsRefresh <= 0.0f)
	{
		FpsRefresh = FpsRefreshSeconds;
		MeasureFrames();
	}

	FScctHudNetState Net = {};
	const FScctHudNetApi* Api = ScctFeature<FScctHudNetApi>();
	const bool bNet = Api != nullptr && Api->GetState(&Net) == 1;
	if (Api == nullptr && !bTableMissingLogged)
	{
		bTableMissingLogged = true;
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctTelemetry: the bridge has no network readout table; only the frame rate can be shown"));
	}
	if (bNet)
	{
		AdvanceNet(Net, FrameSeconds, HoldSeconds);
	}
	else
	{
		ResetNet();
	}

	const int32 FpsMode = FMath::Clamp(FScctSettings::GetInt(FpsModeKey), 0, 2);
	const int32 NetMode = FMath::Clamp(FScctSettings::GetInt(NetModeKey), 0, 2);
	const bool bGraph = FScctSettings::GetBool(TelemetryGraphKey);

	if (UScctHudSubsystem* Hud = GetWorld()->GetSubsystem<UScctHudSubsystem>())
	{
		FScctTelemetryView& View = Hud->GetMutableModel().Telemetry;
		View = FScctTelemetryView();
		View.Corner = FMath::Clamp(FScctSettings::GetInt(TelemetryPositionKey), 0, 3);
		View.Scale = FMath::Clamp(FScctSettings::GetFloat(TelemetryScaleKey), 0.5f, 2.0f);
		View.Opacity = FMath::Clamp(FScctSettings::GetFloat(TelemetryOpacityKey), 0.1f, 1.0f);
		if (FpsMode > 0)
		{
			BuildFpsPanel(View.Fps, FpsMode, bGraph);
		}
		if (NetMode > 0 && bNet)
		{
			BuildNetPanel(View.Net, Net, NetMode, bGraph);
		}
	}

	if (FpsMode > 0 || NetMode > 0)
	{
		LogTimer -= FrameSeconds;
		if (LogTimer <= 0.0f)
		{
			LogTimer = LogEverySeconds;
			LogLine(FpsMode, NetMode, bNet, Net);
		}
	}
}

void UScctTelemetrySubsystem::LogLine(int32 FpsMode, int32 NetMode, bool bNet, const FScctHudNetState& Net) const
{
	FString NetText = TEXT("no connection");
	if (bNet)
	{
		const float Seconds = FMath::Max(SampleSeconds, 0.001f);
		NetText = FString::Printf(TEXT("%s  %d Hz | down %.1f KB/s up %.1f KB/s loss in %.1f%% out %.1f%% | buffer %.0f ms age %.0f ms | corr %.1f/s adjust %.1f uu | totals: underruns %lld replays %lld anim gaps %lld state gaps %lld resets pose %lld owner %lld | host %d clients %d (best %d worst %d ms) without ping %d | issues 0x%x%s"),
			*NetPingText(Net), Net.ServerNetRate,
			SampleSeconds > 0.0f ? (SampleTo.InBytes - SampleFrom.InBytes) / 1024.0f / Seconds : 0.0f,
			SampleSeconds > 0.0f ? (SampleTo.OutBytes - SampleFrom.OutBytes) / 1024.0f / Seconds : 0.0f,
			LossIn, LossOut, Net.BufferMs, Net.SnapshotAgeMs,
			SampleSeconds > 0.0f ? (SampleTo.Corrections - SampleFrom.Corrections) / Seconds : 0.0f, Net.CorrectionDistance,
			Net.BufferUnderflows, Net.MovesReplayed, Net.AnimationGaps, Net.StateGaps, Net.PoseResets, Net.PredictionResets,
			Net.IsHost, Net.Clients, Net.BestClientPingMs, Net.WorstClientPingMs, Net.Unmeasured,
			ShownIssues, bNetIssueNow ? TEXT(" now") : (ShownIssues != 0 ? TEXT(" held") : TEXT("")));
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctTelemetry: fps %.0f (%.1f ms, 1%% low %.0f, 1%% high %.0f, avg %.0f, peak %.1f ms, %d frames in 5 s) | net %s | shown fps %s net %s corner %d scale %.2f opacity %.2f graphs %d hold %.1f s"),
		RateNow, ShownFrameMs, RateLow, RateHigh, RateAverage, PeakFrameMs, FrameCount, *NetText,
		TelemetryModeName(FpsMode), TelemetryModeName(NetMode), FScctSettings::GetInt(TelemetryPositionKey),
		FScctSettings::GetFloat(TelemetryScaleKey), FScctSettings::GetFloat(TelemetryOpacityKey),
		FScctSettings::GetBool(TelemetryGraphKey) ? 1 : 0, FScctSettings::GetFloat(TelemetryHoldKey));
}

#undef LOCTEXT_NAMESPACE
