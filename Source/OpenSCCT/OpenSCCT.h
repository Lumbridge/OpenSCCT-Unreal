#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Runtime/ScctBridgeApi.h"
#include "Runtime/ScctDotNetHost.h"

DECLARE_LOG_CATEGORY_EXTERN(LogOpenSCCT, Log, All);

/**
 * The game module. It owns the .NET runtime and the bridge's entry points for the whole process: the core table
 * (Runtime/ScctBridgeApi.h) and the feature tables (Runtime/Scct*Api.h), each fetched once and kept.
 */
class OPENSCCT_API FOpenSCCTModule : public FDefaultGameModuleImpl
{
public:
	static FOpenSCCTModule& Get();

	/** Keeps the engine's development notices out of the game's picture (see HideDevelopmentNotices). */
	virtual void StartupModule() override;

	/** Ends whatever the bridge still runs (a match, an online visit) before the process lets go of the module. */
	virtual void ShutdownModule() override;

	/**
	 * The bridge's entry points, starting the .NET runtime on first use. Null with the reason in OutError when
	 * the runtime or the bridge could not be loaded (that is final for this run: the runtime starts once per
	 * process) or when the content folder is missing or refused (that is asked again on the next call).
	 * Each reason is written to the log once, however often it is asked for.
	 */
	const FScctBridgeApi* GetBridge(FString& OutError);

	/**
	 * The function table of a bridge feature, fetched once per bridge class and kept for the run. TypeName is the
	 * bridge class with the static GetApi(table*) ("OpenScct.UnrealBridge.EventsExports"); the tables are mirrored
	 * in Runtime/Scct*Api.h. Pass the header's version constant as ExpectedVersion (0 skips the check). Copies the
	 * table into OutTable and returns the feature's version, or returns 0 with the reason in OutError; a missing or
	 * mismatched feature is written to the log once, however often it is asked for.
	 *
	 * Runtime/ScctFeatures.h has ScctFeature<TTable>(), which knows each table's class name and version.
	 */
	template <typename TTable>
	int32 GetFeatureTable(const TCHAR* TypeName, TTable& OutTable, FString& OutError, int32 ExpectedVersion = 0)
	{
		const void* Table = FindFeatureTable(TypeName, sizeof(TTable), ExpectedVersion, OutError);
		if (Table == nullptr)
		{
			return 0;
		}
		FMemory::Memcpy(&OutTable, Table, sizeof(TTable));
		// Every feature table starts with its version.
		return *static_cast<const int32*>(Table);
	}

	/**
	 * The kept copy of a feature table (see GetFeatureTable), valid until the module shuts down, or null with the
	 * reason in OutError. TableSize is the sizeof of the C++ mirror.
	 */
	const void* FindFeatureTable(const TCHAR* TypeName, int32 TableSize, int32 ExpectedVersion, FString& OutError);

	/** The text of the bridge's last failure. */
	FString GetBridgeError() const;

	/** The converted content folder: -ScctContent=, else the SVM_CONTENT variable, else the project setting. */
	static FString FindContentRoot();

	/** Where the built bridge is: Binaries/Bridge under the project, in the editor and in a packaged game. */
	static FString FindBridgeDirectory();

private:
	/** Loads the runtime and the core table. False with StartError set when that cannot work in this run. */
	bool LoadTable();

	/** GetBridge without its log line. */
	const FScctBridgeApi* FindBridge(FString& OutError);

	struct FFeature
	{
		/** The table's bytes as the bridge filled them; empty when the feature could not be fetched. */
		TArray<uint8> Table;
		FString Error;
	};

	FScctDotNetHost Host;
	FScctBridgeApi Api = {};

	/** The runtime is up and Api is filled. */
	bool bTableReady = false;

	/** The content folder was accepted; until then every GetBridge tries it again. */
	bool bConfigured = false;

	/** Why the runtime or the table could not be loaded. Final for the run. */
	FString StartError;

	/** The last GetBridge failure written to the log, so the same reason is not written again every call. */
	FString LoggedBridgeError;

	/** The feature tables fetched so far, by bridge class name; failures are kept too, so each is reported once. */
	TMap<FString, FFeature> Features;
};
