#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ScctMapPacks.generated.h"

/** Told once when a map's level can be opened (bOk, with the level's package path) or why it cannot (the reason). */
DECLARE_DELEGATE_TwoParams(FScctMapLevelReady, bool /*bOk*/, const FString& /*LevelOrReason*/);

/**
 * The cooked Unreal map packs of OpenSCCT-Unreal-Content (its docs/FORMAT.md is the contract): the client side.
 *
 * A release bundles the maps imported on the release machine (/Game/Imported/Maps); other maps' levels come as IoStore
 * containers cooked by Tools/Cook/Cook-MapPaks.ps1 and published by OpenSCCT-Unreal-Content: one shared pack of the
 * imported content every map uses and one pack per map. This subsystem reads that repository's index, downloads a
 * map's packs (the shared ones it requires first) into
 *
 *   %LOCALAPPDATA%\OpenSCCT\unreal-packs\<cook id>\shared\<id>\    the shared packs' containers
 *   %LOCALAPPDATA%\OpenSCCT\unreal-packs\<cook id>\maps\<code>\    a map's container
 *   %LOCALAPPDATA%\OpenSCCT\unreal-packs\<cook id>\installed.json  what is installed (version, hashes, level)
 *   %LOCALAPPDATA%\OpenSCCT\unreal-packs\<cook id>\downloads\      archives being downloaded and unpacked
 *
 * checks each archive's size and sha256 and its files' tree hash against the index, and mounts the containers, after
 * which the map's level (/Game/Imported/Maps/<Code>/<Code>) exists for FPackageName::DoesPackageExist and opens like
 * any level. Every pack installed for this game's cook is mounted when the game starts, so the menus' map lists see
 * the levels a player already has.
 *
 * Only packs of this game's own cook are used: the cook id is <engine version>-<ProjectVersion>-<CookRevision>
 * ([OpenSCCT.MapPacks] CookRevision in DefaultGame.ini, which Cook-MapPaks.ps1 also reads), because cooked content
 * only loads in a game built from the engine and project content it was cooked against. A map is used only when its
 * contentHash is the one asked for (the svmap's, ContentHashOfSvmap), so a level is never paired with another version
 * of the simulation's map.
 *
 * The editor cannot load cooked containers: there, EnsureMapLevel answers from the project's own imported levels.
 * A level that is part of the game itself always wins over a downloaded one.
 *
 * Config ([OpenSCCT.MapPacks] in DefaultGame.ini): IndexUrl (http(s):// or file://), CookRevision.
 * Switches:
 *   -ScctMapPackIndex=<url>     another index (file:///C:/... or http://127.0.0.1:8000/index.json for a local test)
 *   -ScctMapPackRoot=<folder>   another install root than %LOCALAPPDATA%\OpenSCCT\unreal-packs
 *   -ScctCookId=<id>            claim another cook id (to test the cook check)
 *   -ScctMapPackFetch=<Code>    when the game is up, fetch that map's level (with the contentHash of the svmap the
 *                               content folder holds, or -ScctMapPackHash=<16 hex digits>, or none with "-") and open
 *                               it with -ScctMapPackOptions=<"?Side=merc?Bots=3"> (unattended test of the whole path)
 */
UCLASS()
class OPENSCCT_API UScctMapPacks : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UScctMapPacks* Get(const UObject* WorldContext);

	/** This game's cook id ("5.8.4-0.1.0-1"). */
	static FString CookId();

	/** The OpenSCCT contentHash of an svmap folder (the first 16 hex digits of its manifest's hashes.sim), empty if none. */
	static FString ContentHashOfSvmap(const FString& SvmapFolder);

	/** ContentHashOfSvmap of the map's svmap in the content folder (FOpenSCCTModule::FindContentRoot), empty if none. */
	static FString ContentHashOfInstalledMap(const FString& Code);

	/**
	 * Makes the map's level openable: at once when the game has it or its pack is installed and mounted, else after
	 * downloading, checking and mounting its packs. ContentHash is the svmap's contentHash the level must have been
	 * imported from (empty: any). Done is called once, on the game thread, possibly before this returns.
	 */
	void EnsureMapLevel(const FString& Code, const FString& ContentHash, FScctMapLevelReady Done);

	/** True while the map's packs are being fetched, with how far (0..1) and a line for the player. */
	bool GetProgress(const FString& Code, float& OutFraction, FString& OutLine) const;

	/** Whether this game can mount cooked packs at all (a packaged game; not the editor). */
	static bool CanMountPacks();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FPack
	{
		bool bShared = false;
		FString Id;             // shared id, or the map's code
		FString MapId;          // "stock/AquaE"
		FString Title;
		FString Cook;
		FString Version;
		FString ContentHash;
		FString Level;
		TArray<FString> Requires;
		FString TreeSha256;
		FString Url;
		int64 Size = 0;
		FString Sha256;
		FString Container;      // "pakchunk101-Windows", once installed
	};

	struct FRequest
	{
		FString Code;
		FString ContentHash;
		TArray<FScctMapLevelReady> Waiting;
		TArray<FPack> Steps;    // what is left to install, in order
		int32 StepCount = 0;
		float Fraction = 0.0f;
		FString Line;
	};

	FString Root() const;
	FString CookFolder() const;
	FString PackFolder(const FPack& Pack) const;

	void LoadInstalled();
	void SaveInstalled() const;
	const FPack* FindInstalled(bool bShared, const FString& Id) const;

	/** Mounts the installed pack's container (once). False with the reason when it cannot. */
	bool MountInstalled(const FPack& Pack, FString& OutError);
	void UnmountFolder(const FString& Folder);
	void MountAllInstalled();

	void FetchIndex(TFunction<void(bool bOk, const FString& Error)> Then);
	bool ParseIndex(const FString& Json, FString& OutError);

	void StartNext();
	void Plan(FRequest& Request);
	void RunStep();
	void Finish(bool bOk, const FString& LevelOrReason);
	void Download(const FString& Url, const FString& File, TFunction<void(bool bOk, const FString& Error)> Then);
	void OnStepDownloaded(const FPack& Pack, const FString& Archive);
	void OnStepUnpacked(const FPack& Pack, const FString& Unpacked, const FString& Container);
	bool MountRequest(const FRequest& Request, FString& OutLevel, FString& OutError);

	bool TickAutoFetch(float DeltaSeconds);

	// The index: this game's cook's shared packs by id, and every cook's maps by code (the cook is checked when chosen).
	TMap<FString, FPack> IndexShared;
	TMap<FString, TArray<FPack>> IndexMaps;
	double IndexFetchedAt = -1.0;
	FString IndexError;
	bool bIndexFetching = false;
	TArray<TFunction<void(bool, const FString&)>> IndexWaiting;

	// What is installed for this cook, and which folders are mounted.
	TMap<FString, FPack> InstalledShared;
	TMap<FString, FPack> InstalledMaps;
	TMap<FString, FString> MountedPaks;          // folder -> .pak path

	TArray<TSharedPtr<FRequest>> Queue;
	TSharedPtr<FRequest> Active;

	FTSTicker::FDelegateHandle AutoFetchTicker;
	FString AutoFetchCode;
	bool bAutoFetchStarted = false;
};
