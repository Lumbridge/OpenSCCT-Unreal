#include "Online/ScctMapPacks.h"

#include "OpenSCCT.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScctMapPacks)

namespace
{
	const TCHAR* const ConfigSection = TEXT("OpenSCCT.MapPacks");
	const TCHAR* const DefaultIndexUrl = TEXT("https://raw.githubusercontent.com/OpenSCCT/OpenSCCT-Unreal-Content/main/index.json");
	const TCHAR* const IndexFormat = TEXT("openscct.unreal-content");
	const int32 IndexFormatVersion = 1;
	const TCHAR* const InstalledFormat = TEXT("openscct.unreal-packs-installed");
	// The index is read again after this long (a lobby that asks twice does not fetch it twice).
	const double IndexLifetimeSeconds = 600.0;
	// Downloaded containers are mounted below the game's own (Content/Paks is mounted at 4 and up).
	const int32 PackMountOrder = 1;
	const int64 CopyChunk = 1 << 20;

	FString LevelOfCode(const FString& Code)
	{
		return FString::Printf(TEXT("/Game/Imported/Maps/%s/%s"), *Code, *Code);
	}

	/** SHA-256 of a stream of bytes, as lower-case hex (Windows CNG; the client is Windows only for now). */
	class FSha256
	{
	public:
		FSha256()
		{
#if PLATFORM_WINDOWS
			if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
			{
				bOk = BCryptCreateHash(Algorithm, &Hash, nullptr, 0, nullptr, 0, 0) == 0;
			}
#endif
		}
		~FSha256()
		{
#if PLATFORM_WINDOWS
			if (Hash != nullptr) { BCryptDestroyHash(Hash); }
			if (Algorithm != nullptr) { BCryptCloseAlgorithmProvider(Algorithm, 0); }
#endif
		}
		bool IsOk() const { return bOk; }
		void Update(const uint8* Data, int64 Size)
		{
#if PLATFORM_WINDOWS
			while (bOk && Size > 0)
			{
				const ULONG Part = static_cast<ULONG>(FMath::Min<int64>(Size, MAX_int32));
				bOk = BCryptHashData(Hash, const_cast<PUCHAR>(Data), Part, 0) == 0;
				Data += Part;
				Size -= Part;
			}
#endif
		}
		FString Finish()
		{
			uint8 Digest[32] = {};
#if PLATFORM_WINDOWS
			bOk = bOk && BCryptFinishHash(Hash, Digest, sizeof(Digest), 0) == 0;
#endif
			return bOk ? BytesToHex(Digest, sizeof(Digest)).ToLower() : FString();
		}
		static FString OfString(const FString& Text)
		{
			FSha256 Sha;
			const FTCHARToUTF8 Utf8(*Text);
			Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			return Sha.Finish();
		}

	private:
		bool bOk = false;
#if PLATFORM_WINDOWS
		BCRYPT_ALG_HANDLE Algorithm = nullptr;
		BCRYPT_HASH_HANDLE Hash = nullptr;
#endif
	};

	FString HashFile(const FString& Path, int64& OutSize)
	{
		OutSize = -1;
		TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
		if (!File.IsValid())
		{
			return FString();
		}
		OutSize = File->Size();
		FSha256 Sha;
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(CopyChunk);
		for (int64 Left = OutSize; Left > 0;)
		{
			const int64 Part = FMath::Min<int64>(Left, CopyChunk);
			if (!File->Read(Buffer.GetData(), Part))
			{
				return FString();
			}
			Sha.Update(Buffer.GetData(), Part);
			Left -= Part;
		}
		return Sha.Finish();
	}

	uint16 Read16(const uint8* P) { return static_cast<uint16>(P[0] | (P[1] << 8)); }
	uint32 Read32(const uint8* P) { return static_cast<uint32>(P[0]) | (static_cast<uint32>(P[1]) << 8) | (static_cast<uint32>(P[2]) << 16) | (static_cast<uint32>(P[3]) << 24); }

	/**
	 * Unpacks a pack archive as FORMAT.md describes it: stored (not compressed) entries with plain file names, which
	 * are exactly one IoStore container (<name>.pak, .utoc, .ucas). Fills the files' sha256 by name and the container's
	 * name. False with the reason for anything else.
	 */
	bool UnpackPack(const FString& Zip, const FString& OutDir, TMap<FString, FString>& OutHashes, FString& OutContainer, FString& OutError)
	{
		IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
		TUniquePtr<IFileHandle> File(Platform.OpenRead(*Zip));
		if (!File.IsValid())
		{
			OutError = TEXT("the archive could not be opened");
			return false;
		}
		const int64 FileSize = File->Size();

		// The end of central directory record: 22 bytes, plus a comment of up to 64 KiB.
		const int64 TailSize = FMath::Min<int64>(FileSize, 22 + 65535);
		TArray<uint8> Tail;
		Tail.SetNumUninitialized(TailSize);
		if (TailSize < 22 || !File->Seek(FileSize - TailSize) || !File->Read(Tail.GetData(), TailSize))
		{
			OutError = TEXT("the archive is not a ZIP");
			return false;
		}
		int64 End = INDEX_NONE;
		for (int64 At = TailSize - 22; At >= 0; --At)
		{
			if (Read32(&Tail[At]) == 0x06054b50u)
			{
				End = At;
				break;
			}
		}
		if (End == INDEX_NONE)
		{
			OutError = TEXT("the archive is not a ZIP");
			return false;
		}
		const uint8* Eocd = &Tail[End];
		const uint32 Count = Read16(Eocd + 10);
		const uint32 DirSize = Read32(Eocd + 12);
		const uint32 DirOffset = Read32(Eocd + 16);
		if (Read16(Eocd + 4) != 0 || Read16(Eocd + 6) != 0 || Count != Read16(Eocd + 8) || DirOffset == 0xFFFFFFFFu
			|| static_cast<int64>(DirOffset) + DirSize > FileSize)
		{
			OutError = TEXT("the archive is split or ZIP64, which a pack never is");
			return false;
		}
		TArray<uint8> Dir;
		Dir.SetNumUninitialized(DirSize);
		if (!File->Seek(DirOffset) || !File->Read(Dir.GetData(), DirSize))
		{
			OutError = TEXT("the archive's directory could not be read");
			return false;
		}

		struct FEntry { FString Name; int64 Offset; int64 Size; };
		TArray<FEntry> Entries;
		int64 At = 0;
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			if (At + 46 > DirSize || Read32(&Dir[At]) != 0x02014b50u)
			{
				OutError = TEXT("the archive's directory is broken");
				return false;
			}
			const uint8* Header = &Dir[At];
			const uint16 Flags = Read16(Header + 8);
			const uint16 Method = Read16(Header + 10);
			const uint32 Compressed = Read32(Header + 20);
			const uint32 Size = Read32(Header + 24);
			const uint16 NameLength = Read16(Header + 28);
			const int64 Next = At + 46 + NameLength + Read16(Header + 30) + Read16(Header + 32);
			if (Next > DirSize)
			{
				OutError = TEXT("the archive's directory is broken");
				return false;
			}
			const FString Name = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Header + 46), NameLength));
			if ((Flags & 1) != 0 || Method != 0 || Compressed != Size || Size == 0xFFFFFFFFu)
			{
				OutError = FString::Printf(TEXT("%s is compressed or encrypted (a pack's entries are stored)"), *Name);
				return false;
			}
			if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT("..")) || Name.Contains(TEXT(":")))
			{
				OutError = FString::Printf(TEXT("the archive holds '%s', which is not a plain file name"), *Name);
				return false;
			}
			Entries.Add({ Name, Read32(Header + 42), Size });
			At = Next;
		}

		// Exactly one container: <name>.pak, <name>.utoc and <name>.ucas.
		const FString Container = Entries.IsEmpty() ? FString() : FPaths::GetBaseFilename(Entries[0].Name);
		TSet<FString> Extensions;
		for (const FEntry& Entry : Entries)
		{
			if (FPaths::GetBaseFilename(Entry.Name) != Container)
			{
				OutError = FString::Printf(TEXT("the archive holds %s besides the container %s"), *Entry.Name, *Container);
				return false;
			}
			Extensions.Add(FPaths::GetExtension(Entry.Name).ToLower());
		}
		if (Entries.Num() != 3 || !Extensions.Contains(TEXT("pak")) || !Extensions.Contains(TEXT("utoc")) || !Extensions.Contains(TEXT("ucas")))
		{
			OutError = TEXT("the archive is not one IoStore container (.pak, .utoc, .ucas)");
			return false;
		}

		Platform.CreateDirectoryTree(*OutDir);
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(CopyChunk);
		for (const FEntry& Entry : Entries)
		{
			uint8 Local[30];
			if (!File->Seek(Entry.Offset) || !File->Read(Local, sizeof(Local)) || Read32(Local) != 0x04034b50u)
			{
				OutError = FString::Printf(TEXT("%s's header is broken"), *Entry.Name);
				return false;
			}
			const int64 Data = Entry.Offset + 30 + Read16(Local + 26) + Read16(Local + 28);
			if (Data + Entry.Size > FileSize || !File->Seek(Data))
			{
				OutError = FString::Printf(TEXT("%s runs past the end of the archive"), *Entry.Name);
				return false;
			}
			TUniquePtr<IFileHandle> Out(Platform.OpenWrite(*(OutDir / Entry.Name)));
			if (!Out.IsValid())
			{
				OutError = FString::Printf(TEXT("%s could not be written"), *(OutDir / Entry.Name));
				return false;
			}
			FSha256 Sha;
			for (int64 Left = Entry.Size; Left > 0;)
			{
				const int64 Part = FMath::Min<int64>(Left, CopyChunk);
				if (!File->Read(Buffer.GetData(), Part) || !Out->Write(Buffer.GetData(), Part))
				{
					OutError = FString::Printf(TEXT("%s could not be unpacked (is the disk full?)"), *Entry.Name);
					return false;
				}
				Sha.Update(Buffer.GetData(), Part);
				Left -= Part;
			}
			OutHashes.Add(Entry.Name, Sha.Finish());
		}
		OutContainer = Container;
		return true;
	}

	/** The tree hash of FORMAT.md: "<path>\t<sha256>\n" lines sorted by path, hashed. */
	FString TreeHash(TMap<FString, FString> Hashes)
	{
		Hashes.KeySort([](const FString& A, const FString& B) { return FCString::Strcmp(*A, *B) < 0; });
		FString List;
		for (const TPair<FString, FString>& Pair : Hashes)
		{
			List += Pair.Key + TEXT("\t") + Pair.Value + TEXT("\n");
		}
		return FSha256::OfString(List);
	}

	/** A file:// URL's local path ("file:///C:/x%20y/index.json" -> "C:/x y/index.json"), empty for any other URL. */
	FString LocalPathOf(const FString& Url)
	{
		if (!Url.StartsWith(TEXT("file://"), ESearchCase::IgnoreCase))
		{
			return FString();
		}
		FString Path = Url.Mid(7);
		if (Path.Len() >= 3 && Path[0] == TEXT('/') && Path[2] == TEXT(':'))
		{
			Path.RightChopInline(1);
		}
		return FGenericPlatformHttp::UrlDecode(Path);
	}

	FString JsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : FString();
	}

	FString FormatMB(int64 Bytes)
	{
		return FString::Printf(TEXT("%.0f MB"), FMath::Max(1.0, Bytes / (1024.0 * 1024.0)));
	}
}

// ---- identity and places ----------------------------------------------------------------------------------------

UScctMapPacks* UScctMapPacks::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Instance = World != nullptr ? World->GetGameInstance() : nullptr;
	return Instance != nullptr ? Instance->GetSubsystem<UScctMapPacks>() : nullptr;
}

FString UScctMapPacks::CookId()
{
	FString Id;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctCookId="), Id) && !Id.IsEmpty())
	{
		return Id;
	}
	const FEngineVersion& Engine = FEngineVersion::Current();
	FString Version;
	GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
	int32 Revision = 1;
	GConfig->GetInt(ConfigSection, TEXT("CookRevision"), Revision, GGameIni);
	return FString::Printf(TEXT("%d.%d.%d-%s-%d"), Engine.GetMajor(), Engine.GetMinor(), Engine.GetPatch(), *Version, Revision);
}

FString UScctMapPacks::ContentHashOfSvmap(const FString& SvmapFolder)
{
	FString Text;
	TSharedPtr<FJsonObject> Manifest;
	if (!FFileHelper::LoadFileToString(Text, *(SvmapFolder / TEXT("manifest.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest) || !Manifest.IsValid())
	{
		return FString();
	}
	const TSharedPtr<FJsonObject>* Hashes = nullptr;
	FString Sim;
	if (!Manifest->TryGetObjectField(TEXT("hashes"), Hashes) || !(*Hashes)->TryGetStringField(TEXT("sim"), Sim) || Sim.Len() < 16)
	{
		return FString();
	}
	return Sim.Left(16).ToLower();
}

FString UScctMapPacks::ContentHashOfInstalledMap(const FString& Code)
{
	return ContentHashOfSvmap(FOpenSCCTModule::FindContentRoot() / TEXT("svmap") / Code);
}

bool UScctMapPacks::CanMountPacks()
{
#if WITH_EDITOR
	return false;
#else
	return FPlatformProperties::RequiresCookedData() && FCoreDelegates::MountPak.IsBound();
#endif
}

FString UScctMapPacks::Root() const
{
	FString Folder;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctMapPackRoot="), Folder) && !Folder.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(Folder);
	}
	FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
	if (LocalAppData.IsEmpty())
	{
		LocalAppData = FPlatformProcess::UserSettingsDir();
	}
	return FPaths::ConvertRelativePathToFull(LocalAppData / TEXT("OpenSCCT") / TEXT("unreal-packs"));
}

FString UScctMapPacks::CookFolder() const
{
	return Root() / CookId();
}

FString UScctMapPacks::PackFolder(const FPack& Pack) const
{
	return CookFolder() / (Pack.bShared ? TEXT("shared") : TEXT("maps")) / Pack.Id;
}

// ---- life ---------------------------------------------------------------------------------------------------------

void UScctMapPacks::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadInstalled();
	if (CanMountPacks())
	{
		MountAllInstalled();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Map packs: cook %s, %d shared and %d map pack(s) installed in %s%s"), *CookId(), InstalledShared.Num(),
		InstalledMaps.Num(), *CookFolder(), CanMountPacks() ? TEXT("") : TEXT(" (this build cannot mount packs: the editor plays the imported levels)"));

	if (FParse::Value(FCommandLine::Get(), TEXT("ScctMapPackFetch="), AutoFetchCode) && !AutoFetchCode.IsEmpty())
	{
		AutoFetchTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctMapPacks::TickAutoFetch), 0.5f);
	}
}

void UScctMapPacks::Deinitialize()
{
	if (AutoFetchTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AutoFetchTicker);
		AutoFetchTicker.Reset();
	}
	Queue.Reset();
	Active.Reset();
	Super::Deinitialize();
}

bool UScctMapPacks::TickAutoFetch(float)
{
	UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance != nullptr ? Instance->GetWorld() : nullptr;
	if (bAutoFetchStarted || World == nullptr || !World->HasBegunPlay())
	{
		return !bAutoFetchStarted;
	}
	bAutoFetchStarted = true;

	FString Hash;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ScctMapPackHash="), Hash))
	{
		Hash = ContentHashOfInstalledMap(AutoFetchCode);
	}
	if (Hash == TEXT("-"))
	{
		Hash.Reset();
	}
	FString Options;
	FParse::Value(FCommandLine::Get(), TEXT("ScctMapPackOptions="), Options);
	UE_LOG(LogOpenSCCT, Display, TEXT("Map packs: -ScctMapPackFetch: fetching the level of %s (contentHash %s)"), *AutoFetchCode,
		Hash.IsEmpty() ? TEXT("any") : *Hash);
	TWeakObjectPtr<UScctMapPacks> WeakThis(this);
	EnsureMapLevel(AutoFetchCode, Hash, FScctMapLevelReady::CreateLambda([WeakThis, Options](bool bOk, const FString& LevelOrReason)
	{
		UScctMapPacks* Self = WeakThis.Get();
		if (!bOk || Self == nullptr)
		{
			UE_LOG(LogOpenSCCT, Error, TEXT("Map packs: -ScctMapPackFetch failed: %s"), *LevelOrReason);
			return;
		}
		UWorld* Now = Self->GetGameInstance()->GetWorld();
		UE_LOG(LogOpenSCCT, Display, TEXT("Map packs: -ScctMapPackFetch: opening %s%s"), *LevelOrReason, *Options);
		GEngine->SetClientTravel(Now, *(LevelOrReason + Options), TRAVEL_Absolute);
	}));
	return false;
}

// ---- installed packs ----------------------------------------------------------------------------------------------

void UScctMapPacks::LoadInstalled()
{
	InstalledShared.Reset();
	InstalledMaps.Reset();
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *(CookFolder() / TEXT("installed.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()
		|| JsonString(Json, TEXT("format")) != InstalledFormat || JsonString(Json, TEXT("cook")) != CookId())
	{
		return;
	}
	for (const bool bShared : { true, false })
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Json->TryGetArrayField(bShared ? TEXT("shared") : TEXT("maps"), List))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			FPack Pack;
			Pack.bShared = bShared;
			Pack.Id = JsonString(Entry, bShared ? TEXT("id") : TEXT("code"));
			Pack.MapId = bShared ? FString() : JsonString(Entry, TEXT("id"));
			Pack.Title = JsonString(Entry, TEXT("title"));
			Pack.Cook = CookId();
			Pack.Version = JsonString(Entry, TEXT("version"));
			Pack.ContentHash = JsonString(Entry, TEXT("contentHash"));
			Pack.Level = JsonString(Entry, TEXT("level"));
			Pack.TreeSha256 = JsonString(Entry, TEXT("treeSha256"));
			Pack.Sha256 = JsonString(Entry, TEXT("sha256"));
			Pack.Container = JsonString(Entry, TEXT("container"));
			if (Entry.IsValid())
			{
				Entry->TryGetStringArrayField(TEXT("requires"), Pack.Requires);
			}
			if (!Pack.Id.IsEmpty() && !Pack.Container.IsEmpty())
			{
				(bShared ? InstalledShared : InstalledMaps).Add(Pack.Id, Pack);
			}
		}
	}
}

void UScctMapPacks::SaveInstalled() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("format"), InstalledFormat);
	Json->SetNumberField(TEXT("formatVersion"), 1);
	Json->SetStringField(TEXT("cook"), CookId());
	for (const bool bShared : { true, false })
	{
		TArray<TSharedPtr<FJsonValue>> List;
		for (const TPair<FString, FPack>& Pair : bShared ? InstalledShared : InstalledMaps)
		{
			const FPack& Pack = Pair.Value;
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			if (bShared)
			{
				Entry->SetStringField(TEXT("id"), Pack.Id);
			}
			else
			{
				Entry->SetStringField(TEXT("code"), Pack.Id);
				Entry->SetStringField(TEXT("id"), Pack.MapId);
				Entry->SetStringField(TEXT("contentHash"), Pack.ContentHash);
				Entry->SetStringField(TEXT("level"), Pack.Level);
				TArray<TSharedPtr<FJsonValue>> Requires;
				for (const FString& Id : Pack.Requires)
				{
					Requires.Add(MakeShared<FJsonValueString>(Id));
				}
				Entry->SetArrayField(TEXT("requires"), Requires);
			}
			Entry->SetStringField(TEXT("title"), Pack.Title);
			Entry->SetStringField(TEXT("version"), Pack.Version);
			Entry->SetStringField(TEXT("treeSha256"), Pack.TreeSha256);
			Entry->SetStringField(TEXT("sha256"), Pack.Sha256);
			Entry->SetStringField(TEXT("container"), Pack.Container);
			List.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Json->SetArrayField(bShared ? TEXT("shared") : TEXT("maps"), List);
	}
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	const FString Path = CookFolder() / TEXT("installed.json");
	const FString Temp = Path + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) || !IFileManager::Get().Move(*Path, *Temp, true, true))
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("Map packs: %s could not be written"), *Path);
	}
}

const UScctMapPacks::FPack* UScctMapPacks::FindInstalled(bool bShared, const FString& Id) const
{
	return (bShared ? InstalledShared : InstalledMaps).Find(Id);
}

bool UScctMapPacks::MountInstalled(const FPack& Pack, FString& OutError)
{
	const FString Folder = PackFolder(Pack);
	if (MountedPaks.Contains(Folder))
	{
		return true;
	}
	if (!CanMountPacks())
	{
		OutError = TEXT("this build cannot mount packs");
		return false;
	}
	const FString Pak = Folder / (Pack.Container + TEXT(".pak"));
	for (const TCHAR* Extension : { TEXT(".pak"), TEXT(".utoc"), TEXT(".ucas") })
	{
		if (!FPaths::FileExists(Folder / (Pack.Container + Extension)))
		{
			OutError = FString::Printf(TEXT("%s%s is missing"), *(Folder / Pack.Container), Extension);
			return false;
		}
	}
	// The pak platform file mounts the IoStore container beside the .pak, and the container's shader library
	// (named after the chunk in the container's name) is opened as it mounts.
	if (FCoreDelegates::MountPak.Execute(Pak, PackMountOrder) == nullptr)
	{
		OutError = FString::Printf(TEXT("%s could not be mounted"), *Pak);
		return false;
	}
	MountedPaks.Add(Folder, Pak);
	UE_LOG(LogOpenSCCT, Log, TEXT("Map packs: mounted %s %s %s (%s)"), Pack.bShared ? TEXT("shared pack") : TEXT("map"), *Pack.Id, *Pack.Version, *Pak);
	return true;
}

void UScctMapPacks::UnmountFolder(const FString& Folder)
{
	FString Pak;
	if (MountedPaks.RemoveAndCopyValue(Folder, Pak) && FCoreDelegates::OnUnmountPak.IsBound())
	{
		FCoreDelegates::OnUnmountPak.Execute(Pak);
		UE_LOG(LogOpenSCCT, Log, TEXT("Map packs: unmounted %s"), *Pak);
	}
}

void UScctMapPacks::MountAllInstalled()
{
	for (const bool bShared : { true, false })
	{
		for (const TPair<FString, FPack>& Pair : bShared ? InstalledShared : InstalledMaps)
		{
			FString Error;
			if (!MountInstalled(Pair.Value, Error))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Map packs: installed %s is not usable: %s"), *Pair.Key, *Error);
			}
		}
	}
}

// ---- the index ----------------------------------------------------------------------------------------------------

void UScctMapPacks::FetchIndex(TFunction<void(bool bOk, const FString& Error)> Then)
{
	if (IndexFetchedAt >= 0.0 && FPlatformTime::Seconds() - IndexFetchedAt < IndexLifetimeSeconds && IndexError.IsEmpty())
	{
		Then(true, FString());
		return;
	}
	IndexWaiting.Add(MoveTemp(Then));
	if (bIndexFetching)
	{
		return;
	}
	bIndexFetching = true;

	FString Url;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ScctMapPackIndex="), Url) || Url.IsEmpty())
	{
		if (!GConfig->GetString(ConfigSection, TEXT("IndexUrl"), Url, GGameIni) || Url.IsEmpty())
		{
			Url = DefaultIndexUrl;
		}
	}
	const FString File = CookFolder() / TEXT("downloads") / TEXT("index.json");
	UE_LOG(LogOpenSCCT, Log, TEXT("Map packs: reading the index %s"), *Url);
	TWeakObjectPtr<UScctMapPacks> WeakThis(this);
	Download(Url, File, [WeakThis, File, Url](bool bOk, const FString& Error)
	{
		UScctMapPacks* Self = WeakThis.Get();
		if (Self == nullptr)
		{
			return;
		}
		FString Text;
		FString Problem = Error;
		if (bOk && !FFileHelper::LoadFileToString(Text, *File))
		{
			Problem = TEXT("the downloaded index could not be read");
		}
		if (Problem.IsEmpty())
		{
			Self->ParseIndex(Text, Problem);
		}
		IFileManager::Get().Delete(*File, false, true, true);
		Self->IndexError = Problem.IsEmpty() ? FString() : FString::Printf(TEXT("The map level index (%s) could not be read: %s."), *Url, *Problem);
		Self->IndexFetchedAt = FPlatformTime::Seconds();
		Self->bIndexFetching = false;
		if (!Self->IndexError.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Map packs: %s"), *Self->IndexError);
		}
		TArray<TFunction<void(bool, const FString&)>> Waiting = MoveTemp(Self->IndexWaiting);
		for (TFunction<void(bool, const FString&)>& Callback : Waiting)
		{
			Callback(Self->IndexError.IsEmpty(), Self->IndexError);
		}
	});
}

bool UScctMapPacks::ParseIndex(const FString& Text, FString& OutError)
{
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		OutError = TEXT("it is not JSON");
		return false;
	}
	if (JsonString(Json, TEXT("format")) != IndexFormat)
	{
		OutError = TEXT("it is not an OpenSCCT-Unreal-Content index");
		return false;
	}
	int32 Version = 0;
	Json->TryGetNumberField(TEXT("formatVersion"), Version);
	if (Version > IndexFormatVersion)
	{
		OutError = FString::Printf(TEXT("it is format version %d, which needs a newer game"), Version);
		return false;
	}

	const FString Mine = CookId();
	IndexShared.Reset();
	IndexMaps.Reset();
	for (const bool bShared : { true, false })
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Json->TryGetArrayField(bShared ? TEXT("shared") : TEXT("maps"), List))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			const TSharedPtr<FJsonObject>* Download = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObjectField(TEXT("download"), Download))
			{
				continue;
			}
			FPack Pack;
			Pack.bShared = bShared;
			Pack.Id = JsonString(Entry, bShared ? TEXT("id") : TEXT("code"));
			Pack.MapId = bShared ? FString() : JsonString(Entry, TEXT("id"));
			Pack.Title = JsonString(Entry, TEXT("title"));
			Pack.Cook = JsonString(Entry, TEXT("cook"));
			Pack.Version = JsonString(Entry, TEXT("version"));
			Pack.ContentHash = JsonString(Entry, TEXT("contentHash")).ToLower();
			Pack.Level = JsonString(Entry, TEXT("level"));
			Entry->TryGetStringArrayField(TEXT("requires"), Pack.Requires);
			Pack.TreeSha256 = JsonString(Entry, TEXT("treeSha256")).ToLower();
			Pack.Url = JsonString(*Download, TEXT("url"));
			(*Download)->TryGetNumberField(TEXT("size"), Pack.Size);
			Pack.Sha256 = JsonString(*Download, TEXT("sha256")).ToLower();
			// A map code is a folder name; anything else could not be a pack of ours.
			const bool bSafeId = !Pack.Id.IsEmpty() && !Pack.Id.Contains(TEXT("/")) && !Pack.Id.Contains(TEXT("\\")) && !Pack.Id.Contains(TEXT(".."));
			if (!bSafeId || Pack.Url.IsEmpty() || Pack.Sha256.Len() != 64 || Pack.Size <= 0)
			{
				continue;
			}
			if (bShared)
			{
				if (Pack.Cook == Mine)
				{
					IndexShared.Add(Pack.Id, Pack);
				}
			}
			else
			{
				if (Pack.Level.IsEmpty())
				{
					Pack.Level = LevelOfCode(Pack.Id);
				}
				IndexMaps.FindOrAdd(Pack.Id).Add(Pack);
			}
		}
	}
	int32 Usable = 0;
	for (const TPair<FString, TArray<FPack>>& Pair : IndexMaps)
	{
		Usable += Pair.Value.ContainsByPredicate([&Mine](const FPack& Pack) { return Pack.Cook == Mine; }) ? 1 : 0;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Map packs: the index lists %d map(s), %d for this game's cook %s, and %d shared pack(s) for it"),
		IndexMaps.Num(), Usable, *Mine, IndexShared.Num());
	return true;
}

// ---- fetching a map's level ---------------------------------------------------------------------------------------

void UScctMapPacks::EnsureMapLevel(const FString& Code, const FString& ContentHash, FScctMapLevelReady Done)
{
	const FString Hash = ContentHash.ToLower();
	const FString Level = LevelOfCode(Code);
	const FPack* Installed = FindInstalled(false, Code);

	if (!CanMountPacks() || Installed == nullptr)
	{
		// The game's own level (an imported one in the editor, or one a release was cooked with) wins.
		if (FPackageName::DoesPackageExist(Level))
		{
			Done.ExecuteIfBound(true, Level);
			return;
		}
		if (!CanMountPacks())
		{
			Done.ExecuteIfBound(false, FString::Printf(TEXT("%s has no level in this game, and only a packaged game can download one (the editor plays the levels imported into Content/Imported)."), *Code));
			return;
		}
	}
	else if (Hash.IsEmpty() || Installed->ContentHash == Hash)
	{
		// Installed: mounted at start, or now. Nothing is downloaded, so this also works offline.
		FRequest Request;
		Request.Code = Code;
		Request.ContentHash = Hash;
		FString Mounted, Error;
		if (MountRequest(Request, Mounted, Error))
		{
			Done.ExecuteIfBound(true, Mounted);
			return;
		}
		UE_LOG(LogOpenSCCT, Warning, TEXT("Map packs: the installed level of %s cannot be used (%s); fetching it again"), *Code, *Error);
	}

	for (const TSharedPtr<FRequest>& Request : Queue)
	{
		if (Request->Code == Code && Request->ContentHash == Hash)
		{
			Request->Waiting.Add(MoveTemp(Done));
			return;
		}
	}
	if (Active.IsValid() && Active->Code == Code && Active->ContentHash == Hash)
	{
		Active->Waiting.Add(MoveTemp(Done));
		return;
	}
	TSharedPtr<FRequest> Request = MakeShared<FRequest>();
	Request->Code = Code;
	Request->ContentHash = Hash;
	Request->Line = FString::Printf(TEXT("Looking for the level of %s..."), *Code);
	Request->Waiting.Add(MoveTemp(Done));
	Queue.Add(Request);
	StartNext();
}

bool UScctMapPacks::GetProgress(const FString& Code, float& OutFraction, FString& OutLine) const
{
	const TSharedPtr<FRequest>* Found = Queue.FindByPredicate([&Code](const TSharedPtr<FRequest>& Request) { return Request->Code == Code; });
	const FRequest* Request = Active.IsValid() && Active->Code == Code ? Active.Get() : Found != nullptr ? Found->Get() : nullptr;
	if (Request == nullptr)
	{
		return false;
	}
	OutFraction = Request->Fraction;
	OutLine = Request == Active.Get() ? Request->Line : FString::Printf(TEXT("Waiting to download the level of %s..."), *Code);
	return true;
}

void UScctMapPacks::StartNext()
{
	if (Active.IsValid() || Queue.IsEmpty())
	{
		return;
	}
	Active = Queue[0];
	Queue.RemoveAt(0);
	TWeakObjectPtr<UScctMapPacks> WeakThis(this);
	FetchIndex([WeakThis](bool bOk, const FString& Error)
	{
		UScctMapPacks* Self = WeakThis.Get();
		if (Self == nullptr || !Self->Active.IsValid())
		{
			return;
		}
		if (!bOk)
		{
			Self->Finish(false, Error);
			return;
		}
		Self->Plan(*Self->Active);
	});
}

void UScctMapPacks::Plan(FRequest& Request)
{
	const FString Mine = CookId();
	const TArray<FPack>* Entries = IndexMaps.Find(Request.Code);
	if (Entries == nullptr || Entries->IsEmpty())
	{
		Finish(false, FString::Printf(TEXT("OpenSCCT-Unreal-Content has no level of %s."), *Request.Code));
		return;
	}
	TArray<const FPack*> OfMine;
	for (const FPack& Pack : *Entries)
	{
		if (Pack.Cook == Mine)
		{
			OfMine.Add(&Pack);
		}
	}
	if (OfMine.IsEmpty())
	{
		Finish(false, FString::Printf(TEXT("OpenSCCT-Unreal-Content's level of %s is made for another version of the game (cook %s; this game is %s)."),
			*Request.Code, *(*Entries)[0].Cook, *Mine));
		return;
	}
	const FPack* const* Chosen = OfMine.FindByPredicate([&Request](const FPack* Pack) { return Request.ContentHash.IsEmpty() || Pack->ContentHash == Request.ContentHash; });
	if (Chosen == nullptr)
	{
		Finish(false, FString::Printf(TEXT("OpenSCCT-Unreal-Content's level of %s was made from another version of the map (%s; this game's map is %s)."),
			*Request.Code, *OfMine[0]->ContentHash, *Request.ContentHash));
		return;
	}
	const FPack& Map = **Chosen;

	Request.Steps.Reset();
	for (const FString& Id : Map.Requires)
	{
		const FPack* Shared = IndexShared.Find(Id);
		if (Shared == nullptr)
		{
			Finish(false, FString::Printf(TEXT("The level of %s needs the shared pack %s, which OpenSCCT-Unreal-Content does not list."), *Request.Code, *Id));
			return;
		}
		const FPack* Have = FindInstalled(true, Id);
		if (Have == nullptr || Have->Sha256 != Shared->Sha256)
		{
			Request.Steps.Add(*Shared);
		}
	}
	const FPack* Have = FindInstalled(false, Map.Id);
	if (Have == nullptr || Have->Sha256 != Map.Sha256)
	{
		Request.Steps.Add(Map);
	}
	Request.StepCount = Request.Steps.Num();
	int64 Total = 0;
	for (const FPack& Step : Request.Steps)
	{
		Total += Step.Size;
	}
	UE_LOG(LogOpenSCCT, Display, TEXT("Map packs: the level of %s is %s %s; %d pack(s) to download (%s)"), *Request.Code, *Map.MapId, *Map.Version,
		Request.StepCount, *FormatMB(Total));
	RunStep();
}

void UScctMapPacks::RunStep()
{
	if (!Active.IsValid())
	{
		return;
	}
	FRequest& Request = *Active;
	if (Request.Steps.IsEmpty())
	{
		FString Level, Error;
		if (MountRequest(Request, Level, Error))
		{
			Finish(true, Level);
		}
		else
		{
			Finish(false, FString::Printf(TEXT("The level of %s was downloaded but cannot be opened: %s."), *Request.Code, *Error));
		}
		return;
	}

	const FPack Pack = Request.Steps[0];
	const int32 Number = Request.StepCount - Request.Steps.Num() + 1;
	const FString What = Pack.bShared ? FString::Printf(TEXT("%s"), Pack.Title.IsEmpty() ? *Pack.Id : *Pack.Title) : FString::Printf(TEXT("the level of %s"), Pack.Title.IsEmpty() ? *Pack.Id : *Pack.Title);
	Request.Line = FString::Printf(TEXT("Downloading %s (%s, %d of %d)..."), *What, *FormatMB(Pack.Size), Number, Request.StepCount);
	Request.Fraction = 0.0f;
	UE_LOG(LogOpenSCCT, Display, TEXT("Map packs: %s from %s"), *Request.Line, *Pack.Url);

	const FString Downloads = CookFolder() / TEXT("downloads");
	const FString Archive = Downloads / FString::Printf(TEXT("%s-%s-%s.zip"), Pack.bShared ? TEXT("shared") : TEXT("map"), *Pack.Id, *Pack.Version);
	TWeakObjectPtr<UScctMapPacks> WeakThis(this);
	Download(Pack.Url, Archive, [WeakThis, Pack, Archive](bool bOk, const FString& Error)
	{
		UScctMapPacks* Self = WeakThis.Get();
		if (Self == nullptr || !Self->Active.IsValid())
		{
			IFileManager::Get().Delete(*Archive, false, true, true);
			return;
		}
		if (!bOk)
		{
			IFileManager::Get().Delete(*Archive, false, true, true);
			Self->Finish(false, FString::Printf(TEXT("%s could not be downloaded: %s."), *Pack.Url, *Error));
			return;
		}
		Self->OnStepDownloaded(Pack, Archive);
	});
}

void UScctMapPacks::Download(const FString& Url, const FString& File, TFunction<void(bool bOk, const FString& Error)> Then)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	IFileManager::Get().Delete(*File, false, true, true);

	const FString Local = LocalPathOf(Url);
	if (!Local.IsEmpty() || !Url.Contains(TEXT("://")))
	{
		// A file:// URL (or a plain path): copied off the game thread.
		const FString Source = Local.IsEmpty() ? Url : Local;
		Async(EAsyncExecution::ThreadPool, [Source, File, Then = MoveTemp(Then)]() mutable
		{
			const bool bOk = IFileManager::Get().Copy(*File, *Source, true, true) == COPY_OK;
			AsyncTask(ENamedThreads::GameThread, [bOk, Source, Then = MoveTemp(Then)]()
			{
				Then(bOk, bOk ? FString() : FString::Printf(TEXT("%s could not be read"), *Source));
			});
		});
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetURL(Url);
	Http->SetVerb(TEXT("GET"));
	Http->SetHeader(TEXT("User-Agent"), TEXT("OpenSCCT-Unreal"));
	Http->SetActivityTimeout(60.0f);
	// The body goes straight to the file: a shared pack is well over a hundred megabytes.
	FArchive* Writer = IFileManager::Get().CreateFileWriter(*File);
	if (Writer == nullptr)
	{
		Then(false, FString::Printf(TEXT("%s could not be written"), *File));
		return;
	}
	TSharedRef<FArchive> Stream(Writer);
	const bool bStreamed = Http->SetResponseBodyReceiveStream(Stream);
	if (!bStreamed)
	{
		Stream->Close();
	}
	TWeakObjectPtr<UScctMapPacks> WeakThis(this);
	Http->OnRequestProgress64().BindLambda([WeakThis](FHttpRequestPtr Request, uint64, uint64 Received)
	{
		UScctMapPacks* Self = WeakThis.Get();
		if (Self != nullptr && Self->Active.IsValid() && !Self->Active->Steps.IsEmpty() && Self->Active->Steps[0].Size > 0)
		{
			Self->Active->Fraction = FMath::Clamp(static_cast<float>(static_cast<double>(Received) / Self->Active->Steps[0].Size), 0.0f, 1.0f);
		}
	});
	Http->OnProcessRequestComplete().BindLambda([Stream, bStreamed, File, Then = MoveTemp(Then)](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected) mutable
	{
		Stream->Close();
		const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
		if (!bConnected || !Response.IsValid())
		{
			Then(false, TEXT("no answer (offline?)"));
			return;
		}
		if (Code != 200)
		{
			Then(false, FString::Printf(TEXT("HTTP %d"), Code));
			return;
		}
		if (!bStreamed && !FFileHelper::SaveArrayToFile(Response->GetContent(), *File))
		{
			Then(false, FString::Printf(TEXT("%s could not be written"), *File));
			return;
		}
		Then(true, FString());
	});
	if (!Http->ProcessRequest())
	{
		Stream->Close();
		Then(false, TEXT("the request could not be started"));
	}
}

void UScctMapPacks::OnStepDownloaded(const FPack& Pack, const FString& Archive)
{
	Active->Line = FString::Printf(TEXT("Checking %s..."), Pack.bShared ? *Pack.Id : *FString::Printf(TEXT("the level of %s"), *Pack.Id));
	Active->Fraction = 1.0f;
	const FString Unpacked = PackFolder(Pack) + TEXT(".new");
	TWeakObjectPtr<UScctMapPacks> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Pack, Archive, Unpacked]()
	{
		FString Error, Container;
		int64 Size = 0;
		const FString Sha = HashFile(Archive, Size);
		IFileManager::Get().DeleteDirectory(*Unpacked, false, true);
		if (Size != Pack.Size)
		{
			Error = FString::Printf(TEXT("the archive is %lld bytes, the index says %lld"), Size, Pack.Size);
		}
		else if (Sha.IsEmpty())
		{
			Error = TEXT("the archive could not be hashed");
		}
		else if (Sha != Pack.Sha256)
		{
			Error = FString::Printf(TEXT("the archive's sha256 is %s, the index says %s"), *Sha, *Pack.Sha256);
		}
		else
		{
			TMap<FString, FString> Hashes;
			if (UnpackPack(Archive, Unpacked, Hashes, Container, Error) && !Pack.TreeSha256.IsEmpty())
			{
				const FString Tree = TreeHash(Hashes);
				if (Tree != Pack.TreeSha256)
				{
					Error = FString::Printf(TEXT("the unpacked files' tree hash is %s, the index says %s"), *Tree, *Pack.TreeSha256);
				}
			}
		}
		IFileManager::Get().Delete(*Archive, false, true, true);
		if (!Error.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*Unpacked, false, true);
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Pack, Unpacked, Container, Error]()
		{
			UScctMapPacks* Self = WeakThis.Get();
			if (Self == nullptr || !Self->Active.IsValid())
			{
				return;
			}
			if (!Error.IsEmpty())
			{
				Self->Finish(false, FString::Printf(TEXT("The download of %s is not the one OpenSCCT-Unreal-Content lists (%s), so it was thrown away."), *Pack.Url, *Error));
				return;
			}
			Self->OnStepUnpacked(Pack, Unpacked, Container);
		});
	});
}

void UScctMapPacks::OnStepUnpacked(const FPack& Pack, const FString& Unpacked, const FString& Container)
{
	// A pack being replaced is unmounted first; its folder then gives way to the new one.
	const FString Folder = PackFolder(Pack);
	UnmountFolder(Folder);
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	if (!IFileManager::Get().Move(*Folder, *Unpacked, true, true))
	{
		IFileManager::Get().DeleteDirectory(*Unpacked, false, true);
		Finish(false, FString::Printf(TEXT("%s could not be installed in %s."), *Pack.Id, *Folder));
		return;
	}
	FPack Installed = Pack;
	Installed.Container = Container;
	(Pack.bShared ? InstalledShared : InstalledMaps).Add(Pack.Id, Installed);
	SaveInstalled();
	UE_LOG(LogOpenSCCT, Display, TEXT("Map packs: installed %s %s %s in %s"), Pack.bShared ? TEXT("shared pack") : TEXT("map"), *Pack.Id, *Pack.Version, *Folder);

	Active->Steps.RemoveAt(0);
	RunStep();
}

bool UScctMapPacks::MountRequest(const FRequest& Request, FString& OutLevel, FString& OutError)
{
	const FPack* Map = FindInstalled(false, Request.Code);
	if (Map == nullptr)
	{
		OutError = TEXT("it is not installed");
		return false;
	}
	for (const FString& Id : Map->Requires)
	{
		const FPack* Shared = FindInstalled(true, Id);
		if (Shared == nullptr)
		{
			OutError = FString::Printf(TEXT("the shared pack %s is not installed"), *Id);
			return false;
		}
		if (!MountInstalled(*Shared, OutError))
		{
			return false;
		}
	}
	if (!MountInstalled(*Map, OutError))
	{
		return false;
	}
	OutLevel = Map->Level.IsEmpty() ? LevelOfCode(Request.Code) : Map->Level;
	if (!FPackageName::DoesPackageExist(OutLevel))
	{
		OutError = FString::Printf(TEXT("the mounted pack has no %s"), *OutLevel);
		return false;
	}
	return true;
}

void UScctMapPacks::Finish(bool bOk, const FString& LevelOrReason)
{
	if (!Active.IsValid())
	{
		return;
	}
	const TSharedPtr<FRequest> Done = Active;
	Active.Reset();
	if (bOk)
	{
		UE_LOG(LogOpenSCCT, Display, TEXT("Map packs: the level of %s is ready: %s"), *Done->Code, *LevelOrReason);
	}
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Map packs: no level for %s: %s"), *Done->Code, *LevelOrReason);
	}
	for (FScctMapLevelReady& Callback : Done->Waiting)
	{
		Callback.ExecuteIfBound(bOk, LevelOrReason);
	}
	StartNext();
}
