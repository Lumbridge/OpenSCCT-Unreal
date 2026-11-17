#include "Play/ScctSoundData.h"

#include "Decoders/VorbisAudioInfo.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IAudioFormat.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"

namespace
{
	uint32 ReadU32(const uint8* At)
	{
		return At[0] | (At[1] << 8) | (At[2] << 16) | (static_cast<uint32>(At[3]) << 24);
	}

	uint16 ReadU16(const uint8* At)
	{
		return static_cast<uint16>(At[0] | (At[1] << 8));
	}

	bool IsTag(const uint8* At, const char* Tag)
	{
		return At[0] == Tag[0] && At[1] == Tag[1] && At[2] == Tag[2] && At[3] == Tag[3];
	}
}

bool ScctSoundFiles::DecodeWav(const TArray64<uint8>& File, FScctPcm& OutPcm, FString& OutError)
{
	const int64 Size = File.Num();
	const uint8* Data = File.GetData();
	if (Size < 12 || !IsTag(Data, "RIFF") || !IsTag(Data + 8, "WAVE"))
	{
		OutError = TEXT("not a RIFF WAVE file");
		return false;
	}

	// The converted waves carry other chunks too (LIST, scot, acid, an empty smpl): only fmt and data are read.
	// Loop points come with the bridge's sound record, not from the file.
	int32 Format = 0, Channels = 0, Rate = 0, Bits = 0;
	const uint8* Samples = nullptr;
	int64 SampleBytes = 0;
	for (int64 At = 12; At + 8 <= Size;)
	{
		const uint8* Chunk = Data + At;
		const int64 ChunkSize = ReadU32(Chunk + 4);
		const int64 Body = At + 8;
		if (IsTag(Chunk, "fmt ") && ChunkSize >= 16 && Body + 16 <= Size)
		{
			Format = ReadU16(Data + Body);
			Channels = ReadU16(Data + Body + 2);
			Rate = static_cast<int32>(ReadU32(Data + Body + 4));
			Bits = ReadU16(Data + Body + 14);
		}
		else if (IsTag(Chunk, "data"))
		{
			Samples = Data + Body;
			// A data chunk that claims more than the file holds is cut to what is there.
			SampleBytes = FMath::Min(ChunkSize, Size - Body);
		}
		At = Body + ChunkSize + (ChunkSize & 1);
	}

	if (Format != 1 || Channels < 1 || Channels > 2 || Rate < 4000 || (Bits != 16 && Bits != 8) || Samples == nullptr)
	{
		OutError = FString::Printf(TEXT("unsupported wave (format %d, %d channels, %d Hz, %d bits, data %s)"),
			Format, Channels, Rate, Bits, Samples != nullptr ? TEXT("found") : TEXT("missing"));
		return false;
	}

	OutPcm.Channels = Channels;
	OutPcm.SampleRate = Rate;
	if (Bits == 16)
	{
		const int64 Count = (SampleBytes / 2 / Channels) * Channels;
		OutPcm.Samples.SetNumUninitialized(static_cast<int32>(Count));
		FMemory::Memcpy(OutPcm.Samples.GetData(), Samples, Count * 2);
	}
	else
	{
		const int64 Count = (SampleBytes / Channels) * Channels;
		OutPcm.Samples.SetNumUninitialized(static_cast<int32>(Count));
		for (int64 Index = 0; Index < Count; ++Index)
		{
			OutPcm.Samples[Index] = static_cast<int16>((static_cast<int32>(Samples[Index]) - 128) << 8);
		}
	}
	return true;
}

bool ScctSoundFiles::DecodeOgg(const TArray64<uint8>& File, FScctPcm& OutPcm, FString& OutError)
{
	if (File.Num() > MAX_int32)
	{
		OutError = TEXT("stream too large");
		return false;
	}

	FVorbisAudioInfo Decoder;
	FSoundQualityInfo Info;
	if (!Decoder.ReadCompressedInfo(File.GetData(), static_cast<uint32>(File.Num()), &Info))
	{
		OutError = TEXT("not an Ogg Vorbis stream the engine's decoder reads");
		return false;
	}
	if (Info.NumChannels < 1 || Info.NumChannels > 2 || Info.SampleRate < 4000 || Info.SampleDataSize == 0)
	{
		OutError = FString::Printf(TEXT("unsupported stream (%u channels, %u Hz, %u bytes)"), Info.NumChannels, Info.SampleRate, Info.SampleDataSize);
		return false;
	}

	OutPcm.Channels = static_cast<int32>(Info.NumChannels);
	OutPcm.SampleRate = static_cast<int32>(Info.SampleRate);
	OutPcm.Samples.SetNumZeroed(static_cast<int32>(Info.SampleDataSize / sizeof(int16)));
	Decoder.ExpandFile(reinterpret_cast<uint8*>(OutPcm.Samples.GetData()), &Info);
	return true;
}

bool ScctSoundFiles::Load(const FString& Path, FScctPcm& OutPcm, FString& OutError)
{
	TArray64<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *Path, FILEREAD_Silent))
	{
		OutError = TEXT("file not found or unreadable");
		return false;
	}
	return FPaths::GetExtension(Path).Equals(TEXT("ogg"), ESearchCase::IgnoreCase)
		? DecodeOgg(File, OutPcm, OutError)
		: DecodeWav(File, OutPcm, OutError);
}

FScctSoundCache::~FScctSoundCache()
{
	Reset();
}

FScctSoundCache::EState FScctSoundCache::Request(int32 FileId, const FString& Path, FScctPcmPtr& OutPcm)
{
	FEntry* Entry = Entries.Find(FileId);
	if (Entry == nullptr)
	{
		Entry = &Entries.Add(FileId);
		Entry->Path = Path;
		Entry->Result = MakeShared<FResult, ESPMode::ThreadSafe>();
		Entry->Task = UE::Tasks::Launch(TEXT("ScctSoundDecode"), [Result = Entry->Result, Path]()
		{
			Result->bOk = ScctSoundFiles::Load(Path, Result->Pcm, Result->Error);
		});
	}

	Entry->LastUsed = FPlatformTime::Seconds();
	if (Entry->State == EState::Loading && Entry->Task.IsCompleted())
	{
		if (Entry->Result->bOk)
		{
			TSharedRef<FScctPcm, ESPMode::ThreadSafe> Pcm = MakeShared<FScctPcm, ESPMode::ThreadSafe>(MoveTemp(Entry->Result->Pcm));
			TotalBytes += Pcm->Bytes();
			Entry->Pcm = Pcm;
			Entry->State = EState::Ready;
			++NumDecoded;
		}
		else
		{
			Entry->State = EState::Failed;
			++NumFailed;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Audio: cannot decode %s: %s"), *Entry->Path, *Entry->Result->Error);
		}
		Entry->Result.Reset();
		Entry->Task = {};
	}

	OutPcm = Entry->Pcm;
	return Entry->State;
}

void FScctSoundCache::Trim(int64 BudgetBytes, double MaxIdleSeconds, TFunctionRef<bool(int32)> IsInUse, TFunctionRef<void(int32)> OnEvicted)
{
	const double Now = FPlatformTime::Seconds();
	for (;;)
	{
		// The file used longest ago that nothing plays. Failed files are kept: they hold no memory, and the
		// failure should not be decoded and reported again.
		int32 Oldest = 0;
		double OldestUse = Now;
		for (const TPair<int32, FEntry>& Pair : Entries)
		{
			if (Pair.Value.State == EState::Ready && Pair.Value.LastUsed < OldestUse && !IsInUse(Pair.Key))
			{
				Oldest = Pair.Key;
				OldestUse = Pair.Value.LastUsed;
			}
		}
		if (Oldest == 0 || (TotalBytes <= BudgetBytes && Now - OldestUse < MaxIdleSeconds))
		{
			return;
		}

		TotalBytes -= Entries[Oldest].Pcm->Bytes();
		Entries.Remove(Oldest);
		OnEvicted(Oldest);
	}
}

void FScctSoundCache::Reset()
{
	for (TPair<int32, FEntry>& Pair : Entries)
	{
		if (Pair.Value.Task.IsValid())
		{
			Pair.Value.Task.Wait();
		}
	}
	Entries.Reset();
	TotalBytes = 0;
}
