#pragma once

#include "CoreMinimal.h"
#include "Play/ScctSoundData.h"
#include "Sound/SoundWaveProcedural.h"
#include "ScctSoundWave.generated.h"

/**
 * One of the game's loose sound files as something Unreal can play. It holds the decoded samples (shared with
 * the sound cache) and hands every voice that plays it its own reader, so one object serves any number of voices
 * at once; the engine resamples from the file's rate and applies pitch, volume, 3D and submix sends as for any wave.
 *
 * A looping wave plays in from the start and then repeats LoopStart..LoopEnd (frames) for ever; a wave that does
 * not loop ends by itself after its last sample.
 */
UCLASS()
class OPENSCCT_API UScctSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	/** LoopStart/LoopEnd of -1: the whole file. */
	void Init(const FScctPcmPtr& InPcm, bool bInLoop, int32 InLoopStart, int32 InLoopEnd);

	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	FScctPcmPtr Pcm;
	bool bLoop = false;
	int32 LoopStart = 0;
	int32 LoopEnd = 0;
};
