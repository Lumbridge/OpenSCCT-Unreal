#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"

/**
 * One decoded sound file: 16-bit samples, channels interleaved. Never changed after it was decoded, so the game
 * thread, the decode tasks and the audio render thread may all read it at once.
 */
struct FScctPcm
{
	TArray<int16> Samples;
	int32 Channels = 1;
	int32 SampleRate = 22050;

	int32 NumFrames() const { return Channels > 0 ? Samples.Num() / Channels : 0; }
	float Seconds() const { return SampleRate > 0 ? static_cast<float>(NumFrames()) / SampleRate : 0.0f; }
	int64 Bytes() const { return static_cast<int64>(Samples.Num()) * sizeof(int16); }
};

using FScctPcmPtr = TSharedPtr<const FScctPcm, ESPMode::ThreadSafe>;

/** Reads the game's loose sound files. Safe to call from any thread. */
namespace ScctSoundFiles
{
	/** A RIFF WAVE file with 16-bit (or 8-bit) PCM. False with the reason in OutError for anything else. */
	bool DecodeWav(const TArray64<uint8>& File, FScctPcm& OutPcm, FString& OutError);

	/** An Ogg Vorbis file, decoded whole with the engine's Vorbis decoder. */
	bool DecodeOgg(const TArray64<uint8>& File, FScctPcm& OutPcm, FString& OutError);

	/** Loads and decodes Path by its extension (.wav or .ogg). */
	bool Load(const FString& Path, FScctPcm& OutPcm, FString& OutError);
}

/**
 * The decoded sounds of the player's content folder, by the bridge's file name id. The game's sounds are loose
 * files that may not be shipped inside Unreal assets, so they are decoded when first asked for: on a worker task,
 * once per file, and kept until memory is wanted for others (the files not used for the longest go first).
 *
 * Game thread only, except for the decode tasks it starts itself.
 */
class FScctSoundCache
{
public:
	enum class EState : uint8
	{
		Loading,
		Ready,
		Failed,
	};

	~FScctSoundCache();

	/**
	 * The state of a file, starting its decode when it was never asked for. Path is the full path of the file and
	 * is only read the first time. OutPcm is set when the state is Ready.
	 */
	EState Request(int32 FileId, const FString& Path, FScctPcmPtr& OutPcm);

	/**
	 * Drops files that no voice plays, oldest use first, until the cache holds at most BudgetBytes; files unused
	 * for MaxIdleSeconds go regardless. IsInUse says whether a voice still needs a file. Calls OnEvicted for each.
	 */
	void Trim(int64 BudgetBytes, double MaxIdleSeconds, TFunctionRef<bool(int32)> IsInUse, TFunctionRef<void(int32)> OnEvicted);

	/** Waits for running decodes and forgets everything. */
	void Reset();

	int64 GetBytes() const { return TotalBytes; }
	int32 GetNumFiles() const { return Entries.Num(); }
	int32 GetNumFailed() const { return NumFailed; }
	int32 GetNumDecoded() const { return NumDecoded; }

private:
	/** What a decode task hands back. The task owns it together with the cache entry. */
	struct FResult
	{
		FScctPcm Pcm;
		FString Error;
		bool bOk = false;
	};

	struct FEntry
	{
		FString Path;
		UE::Tasks::TTask<void> Task;
		TSharedPtr<FResult, ESPMode::ThreadSafe> Result;
		FScctPcmPtr Pcm;
		double LastUsed = 0.0;
		EState State = EState::Loading;
	};

	TMap<int32, FEntry> Entries;
	int64 TotalBytes = 0;
	int32 NumFailed = 0;
	int32 NumDecoded = 0;
};
