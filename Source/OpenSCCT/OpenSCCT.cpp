#include "OpenSCCT.h"

#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogOpenSCCT);

IMPLEMENT_PRIMARY_GAME_MODULE(FOpenSCCTModule, OpenSCCT, "OpenSCCT");

FOpenSCCTModule& FOpenSCCTModule::Get()
{
	return FModuleManager::GetModuleChecked<FOpenSCCTModule>(TEXT("OpenSCCT"));
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

const FScctBridgeApi* FOpenSCCTModule::GetBridge(FString& OutError)
{
	if (bApiReady)
	{
		return &Api;
	}
	if (!StartError.IsEmpty())
	{
		// One attempt per run: the runtime cannot be started a second time after a failed start.
		OutError = StartError;
		return nullptr;
	}

	const FString BridgeDirectory = FPaths::ProjectDir() / TEXT("Binaries") / TEXT("Bridge");
	if (!Host.Start(BridgeDirectory, StartError))
	{
		OutError = StartError;
		return nullptr;
	}

	using FGetApi = int32(__stdcall*)(FScctBridgeApi*);
	const auto GetApi = static_cast<FGetApi>(Host.GetFunction(TEXT("OpenScct.UnrealBridge.Exports"), TEXT("GetApi"), StartError));
	if (GetApi == nullptr)
	{
		OutError = StartError;
		return nullptr;
	}

	const int32 Version = GetApi(&Api);
	if (Version != ScctBridgeApiVersion)
	{
		StartError = FString::Printf(TEXT("The bridge is version %d and the game expects %d. Rebuild Bridge/OpenScct.UnrealBridge."), Version, ScctBridgeApiVersion);
		OutError = StartError;
		return nullptr;
	}

	const FString ContentRoot = FindContentRoot();
	if (ContentRoot.IsEmpty() || !FPaths::DirectoryExists(ContentRoot))
	{
		StartError = FString::Printf(TEXT("The OpenSCCT content folder was not found ('%s'). Pass -ScctContent=<folder> or set SVM_CONTENT."), *ContentRoot);
		OutError = StartError;
		return nullptr;
	}
	if (Api.Configure(*ContentRoot) == 0)
	{
		StartError = GetBridgeError();
		OutError = StartError;
		return nullptr;
	}

	UE_LOG(LogOpenSCCT, Log, TEXT("OpenSCCT bridge %d ready, content at %s"), Version, *ContentRoot);
	bApiReady = true;
	return &Api;
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
