#pragma once

#include "CoreMinimal.h"
#include "DSP/Dsp.h"

#include <atomic>

namespace Audio
{
	class FAudioCapture;
}

/**
 * The microphone for voice chat: one Unreal capture stream (Audio::FAudioCapture, the AudioCapture plugin's WASAPI
 * back end on Windows) opened on the player's chosen input device, or the system's when that one is not connected.
 *
 * The capture callback runs on the device's own thread. It mixes the device's channels down to mono and pushes the
 * samples into a single-producer single-consumer ring (Audio::TCircularAudioBuffer, no lock); the game thread takes
 * them out with Drain and hands them to the bridge's SubmitCapture, which resamples from the device rate to 48 kHz.
 * Half a second fits in the ring; what a long hitch of the game thread leaves no room for is dropped, which is what
 * a live voice wants.
 */
class FScctVoiceCapture
{
public:
	FScctVoiceCapture();
	~FScctVoiceCapture();

	/**
	 * Opens and starts the device whose name is Device (empty: the system's input device). Returns false when no
	 * device could be opened; OutProblem says why, and is also set when the chosen one was missing and the system's
	 * was opened instead.
	 */
	bool Open(const FString& Device, FString& OutProblem);
	void Close();
	bool IsOpen() const { return bOpen; }

	/** The device's sample rate as the callback last reported it (0 until the first block arrives). */
	int32 GetSampleRate() const { return SampleRate.load(std::memory_order_relaxed); }

	/** The device that was opened ("system default" when none was named). */
	const FString& GetDeviceName() const { return OpenedName; }

	/** Game thread: moves every queued sample into Out (mono, -1 to 1, at GetSampleRate). */
	int32 Drain(TArray<float>& Out);

	/** Samples the device delivered, and samples the ring had no room for, since the stream opened. */
	int64 GetCapturedSamples() const { return Captured.load(std::memory_order_relaxed); }
	int64 GetDroppedSamples() const { return Dropped.load(std::memory_order_relaxed); }

	/** The input devices Unreal sees, by name, for the log and the options. */
	static TArray<FString> ListDevices();

private:
	void OnAudio(const float* Audio, int32 NumFrames, int32 NumChannels, int32 InSampleRate);

	TUniquePtr<Audio::FAudioCapture> Capture;
	Audio::TCircularAudioBuffer<float> Ring;
	/** The capture thread's mono scratch (only that thread touches it). */
	TArray<float> Mono;
	std::atomic<int32> SampleRate{ 0 };
	std::atomic<int64> Captured{ 0 };
	std::atomic<int64> Dropped{ 0 };
	FString OpenedName;
	bool bOpen = false;
};
