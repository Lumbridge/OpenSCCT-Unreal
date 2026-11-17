#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Runtime/ScctBridgeApi.h"
#include "Runtime/ScctDotNetHost.h"

DECLARE_LOG_CATEGORY_EXTERN(LogOpenSCCT, Log, All);

/** The game module. It owns the .NET runtime and the bridge's entry points for the whole process. */
class FOpenSCCTModule : public FDefaultGameModuleImpl
{
public:
	static FOpenSCCTModule& Get();

	/**
	 * The bridge's entry points, starting the .NET runtime on first use. Null when the runtime or the bridge
	 * could not be loaded, with the reason in OutError.
	 */
	const FScctBridgeApi* GetBridge(FString& OutError);

	/**
	 * Fetches the function table of a bridge feature: calls the static GetApi(table*) of the bridge class
	 * TypeName (for example "OpenScct.UnrealBridge.Events.EventsExports"). The tables are mirrored in
	 * Runtime/Scct*Api.h. Returns the feature's version, or 0 with the reason in OutError.
	 */
	template <typename TTable>
	int32 GetFeatureTable(const TCHAR* TypeName, TTable& OutTable, FString& OutError)
	{
		if (GetBridge(OutError) == nullptr)
		{
			return 0;
		}

		using FGetTable = int32(__stdcall*)(TTable*);
		const auto GetTable = static_cast<FGetTable>(Host.GetFunction(TypeName, TEXT("GetApi"), OutError));
		return GetTable != nullptr ? GetTable(&OutTable) : 0;
	}

	/** The text of the bridge's last failure. */
	FString GetBridgeError() const;

	/** The converted content folder: -ScctContent=, else the SVM_CONTENT variable, else the project setting. */
	static FString FindContentRoot();

private:
	FScctDotNetHost Host;
	FScctBridgeApi Api = {};
	bool bApiReady = false;
	FString StartError;
};
