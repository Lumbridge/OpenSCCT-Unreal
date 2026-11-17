#include "Play/ScctSoundWave.h"

#include "Sound/SoundGenerator.h"

namespace
{
	/** Reads one voice's samples out of the shared decoded file. Runs on the audio render thread. */
	class FScctPcmGenerator : public ISoundGenerator
	{
	public:
		FScctPcmGenerator(const FScctPcmPtr& InPcm, bool bInLoop, int32 InLoopStart, int32 InLoopEnd)
			: Pcm(InPcm)
			, LoopStart(InLoopStart)
			, LoopEnd(InLoopEnd)
			, bLoop(bInLoop)
		{
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Channels = Pcm->Channels;
			const int16* Source = Pcm->Samples.GetData();
			const int32 EndFrame = bLoop ? LoopEnd : Pcm->NumFrames();
			const float Scale = 1.0f / 32768.0f;

			int32 Written = 0;
			for (int32 Left = NumSamples / Channels; Left > 0;)
			{
				if (Frame >= EndFrame)
				{
					if (!bLoop)
					{
						break;
					}
					Frame = LoopStart;
				}

				const int32 Run = FMath::Min(Left, EndFrame - Frame);
				const int16* From = Source + static_cast<int64>(Frame) * Channels;
				for (int32 Index = 0; Index < Run * Channels; ++Index)
				{
					OutAudio[Written + Index] = From[Index] * Scale;
				}
				Written += Run * Channels;
				Frame += Run;
				Left -= Run;
			}

			if (Written < NumSamples)
			{
				// The end of a wave that does not loop: the rest of the block is silence and the voice is over.
				FMemory::Memzero(OutAudio + Written, (NumSamples - Written) * sizeof(float));
				bFinished = true;
			}
			return NumSamples;
		}

		virtual bool IsFinished() const override { return bFinished; }
		virtual int32 GetNumChannels() const override { return Pcm->Channels; }

	private:
		FScctPcmPtr Pcm;
		int32 Frame = 0;
		int32 LoopStart;
		int32 LoopEnd;
		bool bLoop;
		bool bFinished = false;
	};
}

void UScctSoundWave::Init(const FScctPcmPtr& InPcm, bool bInLoop, int32 InLoopStart, int32 InLoopEnd)
{
	check(InPcm.IsValid());
	Pcm = InPcm;
	bLoop = bInLoop;

	const int32 Frames = Pcm->NumFrames();
	LoopEnd = InLoopEnd > 0 && InLoopEnd <= Frames ? InLoopEnd : Frames;
	LoopStart = InLoopStart >= 0 && InLoopStart < LoopEnd ? InLoopStart : 0;

	NumChannels = Pcm->Channels;
	SetSampleRate(static_cast<uint32>(Pcm->SampleRate));
	Duration = bLoop ? INDEFINITELY_LOOPING_DURATION : Pcm->Seconds();
	bLooping = false;
	bCanProcessAsync = true;
	SoundGroup = SOUNDGROUP_Effects;

	// A loop must still be running when the listener comes back into its reach; a one-shot that cannot be heard
	// may simply be dropped.
	VirtualizationMode = bLoop ? EVirtualizationMode::PlayWhenSilent : EVirtualizationMode::Disabled;
}

ISoundGeneratorPtr UScctSoundWave::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return Pcm.IsValid() && Pcm->NumFrames() > 0
		? ISoundGeneratorPtr(MakeShared<FScctPcmGenerator, ESPMode::ThreadSafe>(Pcm, bLoop, LoopStart, LoopEnd))
		: ISoundGeneratorPtr();
}
