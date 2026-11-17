#include "Test/ScctFrameRecorder.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PoseableMeshComponent.h"
#include "DynamicRHI.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PipelineStateCache.h"
#include "Play/ScctFirstPersonView.h"
#include "Play/ScctPawnProxy.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "RenderTimer.h"
#include "Test/ScctTest.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
	/** Frames on each side of a frame that make its moving average (17 frames in all). */
	constexpr int32 MotionHalfWindow = 8;

	/** View-model bones followed: spread over the skeleton, enough to see any clip move. */
	constexpr int32 ViewModelBones = 16;

	float CyclesToMs(uint32 Cycles)
	{
		return static_cast<float>(FPlatformTime::ToMilliseconds(Cycles));
	}

	// -- scct.TestStall ---------------------------------------------------------------------------------------------

	/** The stall still to come: its length and the frames left. Game thread only. */
	float StallMs = 0.0f;
	int32 StallFrames = 0;
	FTSTicker::FDelegateHandle StallTicker;

	bool TickStall(float)
	{
		if (StallFrames <= 0)
		{
			StallTicker.Reset();
			return false;
		}
		--StallFrames;
		UE_LOG(LogScctTest, Log, TEXT("scct.TestStall: holding the game thread %.0f ms (%d more frame(s))"), StallMs, StallFrames);
		FPlatformProcess::SleepNoStats(StallMs / 1000.0f);
		return StallFrames > 0;
	}

	FAutoConsoleCommand TestStallCommand(
		TEXT("scct.TestStall"),
		TEXT("scct.TestStall <ms> [frames]: sleeps the game thread that long in each of the next frames (1 by default), an artificial hitch for the play-hitch test."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const float Ms = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 100.0f;
			const int32 Frames = Args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*Args[1])) : 1;
			if (Ms <= 0.0f)
			{
				UE_LOG(LogScctTest, Warning, TEXT("scct.TestStall: give the stall in milliseconds (scct.TestStall 100)"));
				return;
			}
			StallMs = FMath::Min(Ms, 5000.0f);
			StallFrames = Frames;
			UE_LOG(LogScctTest, Log, TEXT("scct.TestStall: %.0f ms in each of the next %d frame(s)"), StallMs, StallFrames);
			if (!StallTicker.IsValid())
			{
				StallTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickStall));
			}
		}));
}

FString FScctMotionStats::ToString() const
{
	return FString::Printf(TEXT("frames=%d moving=%d held=%d (%.1f%%) step=%.3f jitter mean=%.3f p99=%.3f"),
		Frames, Moving, Held, HeldShare() * 100.0, MeanStep, MeanJitter, P99Jitter);
}

FScctFrameRecorder::FScctFrameRecorder()
{
	PreCollectHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FScctFrameRecorder::OnPreCollect);
	PostCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &FScctFrameRecorder::OnPostCollect);
	Samples.Reserve(1 << 14);
}

FScctFrameRecorder::~FScctFrameRecorder()
{
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(PreCollectHandle);
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostCollectHandle);
}

void FScctFrameRecorder::OnPreCollect()
{
	CollectStart = FPlatformTime::Seconds();
}

void FScctFrameRecorder::OnPostCollect()
{
	++CollectionsSince;
	if (CollectStart > 0.0)
	{
		CollectSecondsSince += FPlatformTime::Seconds() - CollectStart;
		CollectStart = 0.0;
	}
}

void FScctFrameRecorder::Sample(const FScctTestContext& Context)
{
	const double Now = FPlatformTime::Seconds();
	const FPSORuntimeCreationStats Pso = PipelineStateCache::GetPSORuntimeCreationStats();
	const uint32 PsoSlow = Pso.GraphicsPSOHitches + Pso.ComputePSOHitches;
	int32 ShaderJobs = 0;
#if WITH_EDITOR
	ShaderJobs = GShaderCompilingManager != nullptr ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#endif

	if (StartSeconds < 0.0)
	{
		// The first call only starts the clock: there is no frame before it to measure.
		StartSeconds = Now;
		LastSeconds = Now;
		LastPsoCreated = Pso.TotalPSOCreations;
		LastPsoSlow = PsoSlow;
		LastShaderJobs = ShaderJobs;
		CollectionsSince = 0;
		CollectSecondsSince = 0.0;
		return;
	}

	FScctFrameSample& Row = Samples.AddDefaulted_GetRef();
	Row.Time = Now - StartSeconds;
	Row.FrameMs = static_cast<float>((Now - LastSeconds) * 1000.0);
	Row.DeltaMs = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
	Row.GameMs = CyclesToMs(GGameThreadTime);
	Row.RenderMs = CyclesToMs(GRenderThreadTime);
	Row.RhiMs = CyclesToMs(GRHIThreadTime);
	Row.GpuMs = CyclesToMs(RHIGetGPUFrameCycles(0));
	Row.Collections = CollectionsSince;
	Row.CollectMs = static_cast<float>(CollectSecondsSince * 1000.0);
	Row.PsoCreated = static_cast<int32>(Pso.TotalPSOCreations - LastPsoCreated);
	Row.PsoSlow = static_cast<int32>(PsoSlow - LastPsoSlow);
	Row.ShaderJobs = ShaderJobs;
	Row.ShadersDone = FMath::Max(0, LastShaderJobs - ShaderJobs);
	Row.Phase = Phase;
	LastSeconds = Now;
	LastPsoCreated = Pso.TotalPSOCreations;
	LastPsoSlow = PsoSlow;
	LastShaderJobs = ShaderJobs;
	CollectionsSince = 0;
	CollectSecondsSince = 0.0;

	if (const UScctSessionSubsystem* Session = Context.GetSession())
	{
		Row.Ticks = Session->GetTicksThisFrame();
		if (const AScctPawnProxy* Body = Session->FindLocalProxy())
		{
			Row.bBody = true;
			Row.BodyLocation = Body->GetActorLocation();
		}
	}
	if (const APlayerController* Player = Context.GetPlayer())
	{
		if (const APlayerCameraManager* Camera = Player->PlayerCameraManager)
		{
			Row.bCamera = true;
			Row.CameraLocation = Camera->GetCameraLocation();
			Row.CameraRotation = Camera->GetCameraRotation();
			Row.Fov = Camera->GetFOVAngle();
		}
	}
	FindViewModel(Context, Row);
}

void FScctFrameRecorder::FindViewModel(const FScctTestContext& Context, FScctFrameSample& Row)
{
	UWorld* World = Context.GetWorld();
	const AScctFirstPersonView* View = nullptr;
	if (World != nullptr && Row.bCamera)
	{
		for (TActorIterator<AScctFirstPersonView> It(World); It; ++It)
		{
			if (It->IsShown())
			{
				View = *It;
				break;
			}
		}
	}
	if (View == nullptr)
	{
		LastViewModelBones.Reset();
		return;
	}

	// The bones in the camera's frame: what the player sees of the view model's motion, whatever the camera does.
	const FTransform Camera(Row.CameraRotation, Row.CameraLocation);
	TArray<FVector> Bones;
	TInlineComponentArray<UPoseableMeshComponent*> Meshes(View);
	for (const UPoseableMeshComponent* Mesh : Meshes)
	{
		const int32 Count = Mesh != nullptr && Mesh->IsVisible() ? Mesh->GetNumBones() : 0;
		if (Count <= 0)
		{
			continue;
		}
		const int32 Stride = FMath::Max(1, Count / ViewModelBones);
		for (int32 Bone = 0; Bone < Count; Bone += Stride)
		{
			Bones.Add(Camera.InverseTransformPosition(Mesh->GetBoneTransform(Bone).GetLocation()));
		}
	}
	if (Bones.Num() == 0)
	{
		LastViewModelBones.Reset();
		return;
	}

	Row.bViewModel = true;
	if (LastViewModelBones.Num() == Bones.Num())
	{
		float Step = 0.0f;
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			Step += FVector::Dist(Bones[Index], LastViewModelBones[Index]);
		}
		Row.ViewModelStep = Step;
	}
	else
	{
		// A model shown afresh (or another model): no step to compare yet.
		Row.ViewModelStep = -1.0f;
	}
	LastViewModelBones = MoveTemp(Bones);
}

bool FScctFrameRecorder::FindPhase(FName InPhase, int32& OutFirst, int32& OutEnd) const
{
	OutFirst = Samples.IndexOfByPredicate([InPhase](const FScctFrameSample& Row) { return Row.Phase == InPhase; });
	if (OutFirst == INDEX_NONE)
	{
		OutEnd = INDEX_NONE;
		return false;
	}
	OutEnd = OutFirst;
	while (OutEnd < Samples.Num() && Samples[OutEnd].Phase == InPhase)
	{
		++OutEnd;
	}
	return true;
}

double FScctFrameRecorder::Median(TArray<double> Values)
{
	return Quantile(MoveTemp(Values), 0.5);
}

double FScctFrameRecorder::Quantile(TArray<double> Values, double Q)
{
	if (Values.Num() == 0)
	{
		return 0.0;
	}
	Values.Sort();
	const int32 Index = FMath::Clamp(static_cast<int32>(Q * Values.Num()), 0, Values.Num() - 1);
	return Values[Index];
}

FScctFrameTimeStats FScctFrameRecorder::FrameTimes(int32 First, int32 End) const
{
	FScctFrameTimeStats Stats;
	First = FMath::Max(0, First);
	End = FMath::Min(End, Samples.Num());
	if (End <= First)
	{
		return Stats;
	}

	TArray<double> Times;
	double Sum = 0.0;
	for (int32 Index = First; Index < End; ++Index)
	{
		Times.Add(Samples[Index].FrameMs);
		Sum += Samples[Index].FrameMs;
	}
	Stats.Frames = Times.Num();
	Stats.MeanMs = Sum / Times.Num();
	Stats.MedianMs = Median(Times);
	Stats.P99Ms = Quantile(Times, 0.99);
	Stats.MaxMs = FMath::Max(Times);

	TArray<double> Jitter;
	for (const double Time : Times)
	{
		Jitter.Add(FMath::Abs(Time - Stats.MeanMs) / Stats.MeanMs);
	}
	Stats.P99Jitter = Quantile(Jitter, 0.99);
	return Stats;
}

FScctMotionStats FScctFrameRecorder::Motion(const TArray<double>& Steps, const TArray<double>& DeltaSeconds, double StillEpsilon, double MinSpeed, bool bSkipStops)
{
	FScctMotionStats Stats;
	const int32 Count = FMath::Min(Steps.Num(), DeltaSeconds.Num());
	Stats.Frames = Count;
	if (Count < 2 * MotionHalfWindow + 1)
	{
		return Stats;
	}

	TArray<double> Speeds;
	Speeds.SetNumZeroed(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Speeds[Index] = DeltaSeconds[Index] > 0.0 ? Steps[Index] / DeltaSeconds[Index] : 0.0;
	}

	// A start or a stop (a walk into a wall) is not jitter: a window that has another frame under a third of the
	// stretch's typical speed is left out. A single frame that held still in steady motion still counts, since its
	// neighbours are all moving.
	TArray<double> Moving;
	for (const double Speed : Speeds)
	{
		if (Speed >= MinSpeed)
		{
			Moving.Add(Speed);
		}
	}
	const double SlowSpeed = bSkipStops ? Median(Moving) / 3.0 : 0.0;

	// Each frame against the frames around it (itself left out), only where all of them could be judged: the ends of
	// the stretch have no full window.
	TArray<double> Jitter;
	double StepSum = 0.0;
	for (int32 Index = MotionHalfWindow; Index < Count - MotionHalfWindow; ++Index)
	{
		double Sum = 0.0;
		bool bSteady = true;
		for (int32 Other = Index - MotionHalfWindow; Other <= Index + MotionHalfWindow; ++Other)
		{
			if (Other != Index)
			{
				Sum += Speeds[Other];
				bSteady &= Speeds[Other] >= SlowSpeed;
			}
		}
		const double Average = Sum / (2 * MotionHalfWindow);
		if (Average < MinSpeed || !bSteady)
		{
			continue;
		}
		++Stats.Moving;
		StepSum += Steps[Index];
		if (Steps[Index] <= StillEpsilon)
		{
			++Stats.Held;
		}
		Jitter.Add(FMath::Abs(Speeds[Index] - Average) / Average);
	}

	if (Stats.Moving > 0)
	{
		Stats.MeanStep = StepSum / Stats.Moving;
		double Sum = 0.0;
		for (const double Value : Jitter)
		{
			Sum += Value;
		}
		Stats.MeanJitter = Sum / Jitter.Num();
		Stats.P99Jitter = Quantile(Jitter, 0.99);
	}
	return Stats;
}

FScctMotionStats FScctFrameRecorder::CameraTurn(int32 First, int32 End) const
{
	TArray<double> Steps;
	TArray<double> Deltas;
	for (int32 Index = FMath::Max(1, First); Index < FMath::Min(End, Samples.Num()); ++Index)
	{
		const FScctFrameSample& Row = Samples[Index];
		const FScctFrameSample& Before = Samples[Index - 1];
		if (!Row.bCamera || !Before.bCamera)
		{
			continue;
		}
		Steps.Add(FMath::Abs(FRotator::NormalizeAxis(Row.CameraRotation.Yaw - Before.CameraRotation.Yaw)));
		// A test's mouse counts are made at the end of a frame from that frame's delta time and drawn in the next
		// frame, as a mouse's counts of the last frame's time are: the turn is measured against the delta they were
		// made for, so what is left is what the camera adds (smoothing, a frame of delay, steps at the tick rate).
		Deltas.Add(Before.DeltaMs / 1000.0);
	}
	// A turn of a thousandth of a degree is a frame that held still; under 5 degrees a second is not turning.
	return Motion(Steps, Deltas, 0.001, 5.0);
}

FScctMotionStats FScctFrameRecorder::CameraMove(int32 First, int32 End) const
{
	TArray<double> Steps;
	TArray<double> Deltas;
	for (int32 Index = FMath::Max(1, First); Index < FMath::Min(End, Samples.Num()); ++Index)
	{
		const FScctFrameSample& Row = Samples[Index];
		const FScctFrameSample& Before = Samples[Index - 1];
		if (!Row.bCamera || !Before.bCamera)
		{
			continue;
		}
		Steps.Add(FVector::Dist(Row.CameraLocation, Before.CameraLocation));
		Deltas.Add(Row.DeltaMs / 1000.0);
	}
	// A hundredth of a uu is still; under 20 uu/s (a fifth of a slow walk) is not moving.
	return Motion(Steps, Deltas, 0.01, 20.0);
}

FScctMotionStats FScctFrameRecorder::BodyMove(int32 First, int32 End) const
{
	TArray<double> Steps;
	TArray<double> Deltas;
	for (int32 Index = FMath::Max(1, First); Index < FMath::Min(End, Samples.Num()); ++Index)
	{
		const FScctFrameSample& Row = Samples[Index];
		const FScctFrameSample& Before = Samples[Index - 1];
		if (!Row.bBody || !Before.bBody)
		{
			continue;
		}
		Steps.Add(FVector::Dist(Row.BodyLocation, Before.BodyLocation));
		Deltas.Add(Row.DeltaMs / 1000.0);
	}
	return Motion(Steps, Deltas, 0.01, 20.0);
}

FScctMotionStats FScctFrameRecorder::ViewModel(int32 First, int32 End) const
{
	TArray<double> Steps;
	TArray<double> Deltas;
	for (int32 Index = FMath::Max(0, First); Index < FMath::Min(End, Samples.Num()); ++Index)
	{
		const FScctFrameSample& Row = Samples[Index];
		if (!Row.bViewModel || Row.ViewModelStep < 0.0f)
		{
			continue;
		}
		Steps.Add(Row.ViewModelStep);
		Deltas.Add(Row.DeltaMs / 1000.0);
	}
	// The bones' summed motion: a thousandth of a uu is still; under 1 uu/s summed over the bones is a model at rest.
	// A clip's own pace rises and falls (a walk's bob), so slow stretches are not left out as stops.
	return Motion(Steps, Deltas, 0.001, 1.0, false);
}

FString FScctFrameRecorder::Describe(const FScctFrameSample& Row)
{
	return FString::Printf(TEXT("t=%.2f frame=%.1fms delta=%.1fms game=%.1f render=%.1f rhi=%.1f gpu=%.1f ticks=%d gc=%d (%.1fms) pso=%d slowPso=%d shaderJobs=%d shadersDone=%d phase=%s"),
		Row.Time, Row.FrameMs, Row.DeltaMs, Row.GameMs, Row.RenderMs, Row.RhiMs, Row.GpuMs, Row.Ticks, Row.Collections, Row.CollectMs,
		Row.PsoCreated, Row.PsoSlow, Row.ShaderJobs, Row.ShadersDone, *Row.Phase.ToString());
}

FString FScctFrameRecorder::WriteCsv(const FString& Name) const
{
	FString Text = TEXT("frame,time_s,phase,frame_ms,delta_ms,game_ms,render_ms,rhi_ms,gpu_ms,gc,gc_ms,pso_created,pso_slow,shader_jobs,shaders_done,ticks,")
		TEXT("cam_x,cam_y,cam_z,cam_pitch,cam_yaw,cam_roll,fov,body,body_x,body_y,body_z,cam_step,cam_turn,body_step,view_model,view_model_step\n");
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FScctFrameSample& Row = Samples[Index];
		const FScctFrameSample* Before = Index > 0 ? &Samples[Index - 1] : nullptr;
		const double CamStep = Before != nullptr && Before->bCamera && Row.bCamera ? FVector::Dist(Row.CameraLocation, Before->CameraLocation) : 0.0;
		const double CamTurn = Before != nullptr && Before->bCamera && Row.bCamera ? FRotator::NormalizeAxis(Row.CameraRotation.Yaw - Before->CameraRotation.Yaw) : 0.0;
		const double BodyStep = Before != nullptr && Before->bBody && Row.bBody ? FVector::Dist(Row.BodyLocation, Before->BodyLocation) : 0.0;
		Text += FString::Printf(TEXT("%d,%.4f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%.3f,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%d,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%d,%.4f\n"),
			Index, Row.Time, *Row.Phase.ToString(), Row.FrameMs, Row.DeltaMs, Row.GameMs, Row.RenderMs, Row.RhiMs, Row.GpuMs, Row.Collections, Row.CollectMs,
			Row.PsoCreated, Row.PsoSlow, Row.ShaderJobs, Row.ShadersDone, Row.Ticks,
			Row.CameraLocation.X, Row.CameraLocation.Y, Row.CameraLocation.Z, Row.CameraRotation.Pitch, Row.CameraRotation.Yaw, Row.CameraRotation.Roll, Row.Fov,
			Row.bBody ? 1 : 0, Row.BodyLocation.X, Row.BodyLocation.Y, Row.BodyLocation.Z, CamStep, CamTurn, BodyStep,
			Row.bViewModel ? 1 : 0, Row.ViewModelStep);
	}

	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Test") / FString::Printf(TEXT("frames-%s.csv"), *Name));
	return FFileHelper::SaveStringToFile(Text, *Path) ? Path : FString();
}
