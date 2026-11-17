#include "OpenSCCT.h"

#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogOpenSCCT);

IMPLEMENT_PRIMARY_GAME_MODULE(FOpenSCCTModule, OpenSCCT, "OpenSCCT");

namespace
{
	const TCHAR* const CoreExports = TEXT("OpenScct.UnrealBridge.Exports");

	// How long the bridge may take to stop its match and leave its server when the module goes.
	constexpr int32 ShutdownTimeoutMs = 3000;

	// The feature tables are handed over without a size (GetApi(table*)), so a bridge built with a longer table
	// than the header here would write past it. They are fetched into a buffer with this much room after the
	// table, the version is checked there, and only then is the table copied out.
	constexpr int32 FeatureTableSlack = 4096;
}

FOpenSCCTModule& FOpenSCCTModule::Get()
{
	return FModuleManager::GetModuleChecked<FOpenSCCTModule>(TEXT("OpenSCCT"));
}

void FOpenSCCTModule::ShutdownModule()
{
	if (bTableReady && Api.Shutdown != nullptr)
	{
		// Never throws and gives up after the timeout, so a stuck server thread cannot hang the exit.
		const int32 Stopped = Api.Shutdown(ShutdownTimeoutMs);
		UE_LOG(LogOpenSCCT, Log, TEXT("OpenSCCT bridge shut down (%s)"), Stopped != 0 ? TEXT("everything stopped") : TEXT("gave up waiting"));
	}
	bTableReady = false;
	bConfigured = false;
	Features.Reset();
	Api = {};
}

FString FOpenSCCTModule::FindContentRoot()
{
	FString Root;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctContent="), Root) && !Root.IsEmpty())
	{
		return Root;
	}

	Root = FPlatformMisc::GetEnvironmentVariable(TEXT("SVM_CONTENT"));
	if (!Root.IsEmpty())
	{
		return Root;
	}

	GConfig->GetString(TEXT("OpenSCCT"), TEXT("ContentRoot"), Root, GGameIni);
	return Root;
}

FString FOpenSCCTModule::FindBridgeDirectory()
{
	// OpenSCCT.Build.cs stages the same folder into a packaged game, so one rule serves both.
	return FPaths::ProjectDir() / TEXT("Binaries") / TEXT("Bridge");
}

bool FOpenSCCTModule::LoadTable()
{
	if (bTableReady)
	{
		return true;
	}
	if (!StartError.IsEmpty())
	{
		// One attempt per run: the runtime cannot be started a second time, nor the bridge assembly replaced.
		return false;
	}

	if (!Host.Start(FindBridgeDirectory(), StartError))
	{
		return false;
	}

	// The version first, through an entry that writes nothing: only a bridge of this version is asked for its table.
	using FGetApiVersion = int32(__stdcall*)();
	const auto GetApiVersion = static_cast<FGetApiVersion>(Host.GetFunction(CoreExports, TEXT("GetApiVersion"), StartError));
	if (GetApiVersion == nullptr)
	{
		StartError += TEXT(" The bridge is older than the game. Rebuild Bridge/OpenScct.UnrealBridge.");
		return false;
	}
	const int32 Version = GetApiVersion();
	if (Version != ScctBridgeApiVersion)
	{
		StartError = FString::Printf(TEXT("The bridge is version %d and the game expects %d. Rebuild Bridge/OpenScct.UnrealBridge."), Version, ScctBridgeApiVersion);
		return false;
	}

	// The bridge copies at most the size it is told, so its table can never run over this one.
	using FGetApiSized = int32(__stdcall*)(FScctBridgeApi*, int32);
	const auto GetApiSized = static_cast<FGetApiSized>(Host.GetFunction(CoreExports, TEXT("GetApiSized"), StartError));
	if (GetApiSized == nullptr)
	{
		return false;
	}
	Api = {};
	const int32 Filled = GetApiSized(&Api, sizeof(FScctBridgeApi));
	if (Filled != ScctBridgeApiVersion || Api.Version != ScctBridgeApiVersion || Api.Shutdown == nullptr)
	{
		StartError = FString::Printf(TEXT("The bridge did not hand over its version %d table (it answered %d). Rebuild Bridge/OpenScct.UnrealBridge."), ScctBridgeApiVersion, Filled);
		Api = {};
		return false;
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("OpenSCCT bridge version %d loaded (table of %d bytes)"), Version, static_cast<int32>(sizeof(FScctBridgeApi)));
	bTableReady = true;
	return true;
}

const FScctBridgeApi* FOpenSCCTModule::GetBridge(FString& OutError)
{
	if (!LoadTable())
	{
		OutError = StartError;
		return nullptr;
	}
	if (bConfigured)
	{
		return &Api;
	}

	// A missing or refused content folder is not final: the next call looks again (a setting may have changed).
	const FString ContentRoot = FindContentRoot();
	if (ContentRoot.IsEmpty() || !FPaths::DirectoryExists(ContentRoot))
	{
		OutError = FString::Printf(TEXT("The OpenSCCT content folder was not found ('%s'). Pass -ScctContent=<folder> or set SVM_CONTENT."), *ContentRoot);
		return nullptr;
	}
	if (Api.Configure(*ContentRoot) == 0)
	{
		OutError = GetBridgeError();
		return nullptr;
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("OpenSCCT bridge %d ready, content at %s"), Api.Version, *ContentRoot);
	bConfigured = true;
	return &Api;
}

const void* FOpenSCCTModule::FindFeatureTable(const TCHAR* TypeName, int32 TableSize, int32 ExpectedVersion, FString& OutError)
{
	// The features read the content the core was configured with, so the core comes first. Its failures are its
	// own (and may pass), so they are not kept against the feature.
	if (GetBridge(OutError) == nullptr)
	{
		return nullptr;
	}

	if (const FFeature* Known = Features.Find(TypeName))
	{
		if (Known->Table.Num() < TableSize)
		{
			OutError = Known->Error.IsEmpty()
				? FString::Printf(TEXT("%s was fetched as a table of %d bytes and is now asked for as %d."), TypeName, Known->Table.Num(), TableSize)
				: Known->Error;
			return nullptr;
		}
		return Known->Table.GetData();
	}

	FFeature Feature;
	using FGetTable = int32(__stdcall*)(void*);
	if (TableSize < static_cast<int32>(sizeof(int32)))
	{
		Feature.Error = FString::Printf(TEXT("%s: a feature table starts with its version, %d bytes cannot be one."), TypeName, TableSize);
	}
	else if (const auto GetTable = static_cast<FGetTable>(Host.GetFunction(TypeName, TEXT("GetApi"), Feature.Error)))
	{
		TArray<uint8> Buffer;
		Buffer.SetNumZeroed(TableSize + FeatureTableSlack);
		const int32 Version = GetTable(Buffer.GetData());
		if (Version == 0)
		{
			Feature.Error = FString::Printf(TEXT("%s.GetApi handed over no table."), TypeName);
		}
		else if (ExpectedVersion != 0 && Version != ExpectedVersion)
		{
			Feature.Error = FString::Printf(TEXT("%s is version %d and the game expects %d. Rebuild Bridge/OpenScct.UnrealBridge."), TypeName, Version, ExpectedVersion);
		}
		else
		{
			Buffer.SetNum(TableSize);
			Feature.Table = MoveTemp(Buffer);
			UE_LOG(LogOpenSCCT, Log, TEXT("Bridge feature %s version %d ready"), TypeName, Version);
		}
	}

	if (!Feature.Error.IsEmpty())
	{
		// Said once here; later calls get the same text back without another log line.
		UE_LOG(LogOpenSCCT, Error, TEXT("Bridge feature unavailable: %s"), *Feature.Error);
	}

	const FFeature& Kept = Features.Add(TypeName, MoveTemp(Feature));
	OutError = Kept.Error;
	return Kept.Table.Num() > 0 ? Kept.Table.GetData() : nullptr;
}

FString FOpenSCCTModule::GetBridgeError() const
{
	if (Api.LastError == nullptr)
	{
		return StartError;
	}

	const int32 Length = Api.LastError(nullptr, 0);
	TArray<TCHAR> Buffer;
	Buffer.SetNumZeroed(Length + 1);
	Api.LastError(Buffer.GetData(), Buffer.Num());
	return FString(Buffer.GetData());
}
