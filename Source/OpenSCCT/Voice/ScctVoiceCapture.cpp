#include "Voice/ScctVoiceCapture.h"

#include "AudioCaptureCore.h"

namespace
{
	/** Half a second at the highest rate a device may run at. */
	constexpr uint32 RingSamples = 96000;

	/** Frames per device callback asked for (about 10 ms at 48 kHz; the device may choose otherwise). */
	constexpr uint32 FramesPerCallback = 480;
}

FScctVoiceCapture::FScctVoiceCapture()
{
	Ring.SetCapacity(RingSamples);
}

FScctVoiceCapture::~FScctVoiceCapture()
{
	Close();
}

TArray<FString> FScctVoiceCapture::ListDevices()
{
	TArray<FString> Names;
	Audio::FAudioCapture Probe;
	TArray<Audio::FCaptureDeviceInfo> Devices;
	Probe.GetCaptureDevicesAvailable(Devices);
	for (const Audio::FCaptureDeviceInfo& Info : Devices)
	{
		Names.Add(Info.DeviceName);
	}
	return Names;
}

bool FScctVoiceCapture::Open(const FString& Device, FString& OutProblem)
{
	Close();
	OutProblem.Reset();
	Capture = MakeUnique<Audio::FAudioCapture>();

	TArray<Audio::FCaptureDeviceInfo> Devices;
	Capture->GetCaptureDevicesAvailable(Devices);
	if (Devices.Num() == 0)
	{
		OutProblem = TEXT("the system reports no microphones");
		Capture.Reset();
		return false;
	}

	Audio::FAudioCaptureDeviceParams Params;
	OpenedName = TEXT("system default");
	if (!Device.IsEmpty())
	{
		const int32 Index = Devices.IndexOfByPredicate([&Device](const Audio::FCaptureDeviceInfo& Info) { return Info.DeviceName == Device; });
		if (Index != INDEX_NONE)
		{
			Params.DeviceIndex = Index;
			OpenedName = Device;
		}
		else
		{
			// As the Godot client does: the system's choice rather than no voice at all.
			OutProblem = FString::Printf(TEXT("'%s' is not connected - using the system's input device"), *Device);
		}
	}

	Ring.SetCapacity(RingSamples);
	SampleRate.store(0, std::memory_order_relaxed);
	Captured.store(0, std::memory_order_relaxed);
	Dropped.store(0, std::memory_order_relaxed);

	// OpenAudioCaptureStream, its replacement, is not exported from AudioCaptureCore in this engine version; this one
	// hands the same buffer on as floats, which is what the default 32 bit float encoding delivers.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const Audio::FOnCaptureFunction OnCapture = [this](const float* Audio, int32 NumFrames, int32 NumChannels, int32 InSampleRate, double, bool)
	{
		OnAudio(Audio, NumFrames, NumChannels, InSampleRate);
	};
	const bool bOpened = Capture->OpenCaptureStream(Params, OnCapture, FramesPerCallback);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!bOpened || !Capture->StartStream())
	{
		OutProblem = FString::Printf(TEXT("the input device '%s' could not be opened"), *OpenedName);
		Capture->CloseStream();
		Capture.Reset();
		return false;
	}
	bOpen = true;
	return true;
}

void FScctVoiceCapture::Close()
{
	if (Capture.IsValid())
	{
		// Stops the device thread before the ring and this object can go away.
		Capture->AbortStream();
		Capture.Reset();
	}
	bOpen = false;
}

void FScctVoiceCapture::OnAudio(const float* Audio, int32 NumFrames, int32 NumChannels, int32 InSampleRate)
{
	if (Audio == nullptr || NumFrames <= 0 || NumChannels <= 0)
	{
		return;
	}
	SampleRate.store(InSampleRate, std::memory_order_relaxed);
	if (Mono.Num() < NumFrames)
	{
		Mono.SetNumUninitialized(NumFrames);
	}
	const float Scale = 1.0f / NumChannels;
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		float Sum = 0.0f;
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			Sum += Audio[Frame * NumChannels + Channel];
		}
		Mono[Frame] = Sum * Scale;
	}
	const int32 Pushed = Ring.Push(Mono.GetData(), NumFrames);
	Captured.fetch_add(NumFrames, std::memory_order_relaxed);
	if (Pushed < NumFrames)
	{
		Dropped.fetch_add(NumFrames - Pushed, std::memory_order_relaxed);
	}
}

int32 FScctVoiceCapture::Drain(TArray<float>& Out)
{
	Out.Reset();
	const int32 Available = static_cast<int32>(Ring.Num());
	if (Available <= 0)
	{
		return 0;
	}
	Out.SetNumUninitialized(Available);
	const int32 Popped = Ring.Pop(Out.GetData(), Available);
	Out.SetNum(Popped, EAllowShrinking::No);
	return Popped;
}
