#include "OpenSCCT.h"
#include "Runtime/ScctControlsApi.h"

#include "Engine/Engine.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"
#include "Misc/MessageDialog.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Play/ScctExposure.h"
#include "Play/ScctLightAudit.h"
#include "Play/ScctLightPlan.h"
#include "Social/ScctLaunchOptions.h"

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

namespace
{
	/**
	 * A development build of the engine writes its own notices over the game's picture: "Preparing Shaders (120)",
	 * "Preparing Textures", the texture streaming pool being over budget, the renderer's warnings. They are for
	 * the people who build a level, and the log has every one of them; the game's picture is the player's. The
	 * engine draws them all in one block (UEngine::DrawMapWarnings), which this switches off. The editor keeps
	 * them. Messages the game puts on the screen itself (a match that could not start) are not part of that block.
	 */
	void HideDevelopmentNotices()
	{
		if (GEngine != nullptr && !GIsEditor)
		{
			GEngine->bSuppressMapWarnings = true;
		}
	}
}

void FOpenSCCTModule::StartupModule()
{
	FDefaultGameModuleImpl::StartupModule();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FKey Paddle = ScctPaddleKey(Index);
		if (!EKeys::GetKeyDetails(Paddle))
		{
			EKeys::AddKey(FKeyDetails(Paddle, FText::Format(NSLOCTEXT("ScctInput", "Paddle", "Paddle {0}"), Index + 1), FKeyDetails::GamepadKey));
		}
	}
	ScctLaunchOptions::Apply();
	FScctLightPlan::Start();
	FScctLightAudit::Start();

	// The game module is loaded before the engine object exists.
	if (GEngine != nullptr)
	{
		HideDevelopmentNotices();
	}
	else
	{
		FCoreDelegates::GetOnPostEngineInit().AddStatic(&HideDevelopmentNotices);
	}
}

FOpenSCCTModule& FOpenSCCTModule::Get()
{
	return FModuleManager::GetModuleChecked<FOpenSCCTModule>(TEXT("OpenSCCT"));
}

void FOpenSCCTModule::ShutdownModule()
{
	FScctExposure::Stop();
	FScctLightAudit::Stop();

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

static FString RequestedContentRoot()
{
	FString Root;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctContent="), Root) && !Root.IsEmpty())
	{
		return Root;
	}

	// The launcher's spelling of the same option.
	if (!ScctLaunchOptions::ContentRoot().IsEmpty())
	{
		return ScctLaunchOptions::ContentRoot();
	}

#if WITH_EDITOR
	Root = FPlatformMisc::GetEnvironmentVariable(TEXT("SVM_CONTENT"));
	if (!Root.IsEmpty())
	{
		return Root;
	}
#endif

	GConfig->GetString(TEXT("OpenSCCT"), TEXT("ContentRoot"), Root, GGameIni);
	return Root;
}

FString FOpenSCCTModule::FindContentRoot()
{
	FOpenSCCTModule& Module = Get();
	FString Error;
	Module.FindBridge(Error);
	return Module.ResolvedContentRoot;
}

void FOpenSCCTModule::OpenSteamSetup()
{
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString Setup = FPaths::Combine(Project, TEXT("../contenttools/setup/OpenScct.UnrealSetup.dll"));
	FPaths::CollapseRelativeDirectories(Setup);
	const FString DotNet = FPaths::Combine(Project, TEXT("Binaries/DotNet/dotnet.exe"));
	if (FPaths::FileExists(DotNet) && FPaths::FileExists(Setup))
	{
		const FString Arguments = FString::Printf(TEXT("\"%s\""), *Setup);
		FProcHandle Process = FPlatformProcess::CreateProc(*DotNet, *Arguments, false, false, false, nullptr, 0, nullptr, nullptr);
		if (Process.IsValid())
		{
			FPlatformProcess::CloseProc(Process);
			return;
		}
	}
	FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok,
		INVTEXT("Unreal setup could not start. Extract or repair the complete OpenSCCT Unreal release, including its contenttools folder."));
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
	const FScctBridgeApi* Bridge = FindBridge(OutError);
	if (Bridge == nullptr && OutError != LoggedBridgeError)
	{
		// Callers may ask every frame (a feature waiting for the bridge): each reason is written once.
		LoggedBridgeError = OutError;
		UE_LOG(LogOpenSCCT, Error, TEXT("OpenSCCT bridge unavailable: %s"), *OutError);
	}
	return Bridge;
}

const FScctBridgeApi* FOpenSCCTModule::FindBridge(FString& OutError)
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

	// The standalone setup commits a generation after conversion completes.
	// Several presentation subsystems ask per frame. Retry a missing or interrupted build periodically.
	const double Now = FPlatformTime::Seconds();
	if (Now < NextContentCheck)
	{
		OutError = LoggedBridgeError;
		return nullptr;
	}
	NextContentCheck = Now + 2.0;
	if (Api.Configure(*RequestedContentRoot()) == 0)
	{
		OutError = GetBridgeError();
		return nullptr;
	}
	using FGetRoot = int32(__cdecl*)(TCHAR*, int32);
	auto GetRoot = static_cast<FGetRoot>(Host.GetFunction(CoreExports, TEXT("GetConfiguredContentRoot"), OutError));
	if (GetRoot == nullptr) return nullptr;
	TArray<TCHAR> Root;
	Root.SetNumZeroed(GetRoot(nullptr, 0) + 1);
	GetRoot(Root.GetData(), Root.Num());
	ResolvedContentRoot = FString(Root.GetData());
	UE_LOG(LogOpenSCCT, Log, TEXT("OpenSCCT bridge %d ready, content at %s"), Api.Version, *ResolvedContentRoot);
	bConfigured = true;
	LoggedBridgeError.Reset();
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
