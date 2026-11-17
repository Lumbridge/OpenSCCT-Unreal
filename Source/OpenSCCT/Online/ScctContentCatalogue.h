#pragma once

#include "CoreMinimal.h"

class UScctOnlineSubsystem;

/** One row of the Maps & Content page: a map of OpenSCCT-Content (or a downloaded one it no longer lists), or a shared pack. */
struct FScctContentRow
{
	bool bShared = false;
	FString Key;      // the map code, or the shared pack's id
	FString Title;
	FString Category; // stock, original, community, enhanced, recovered, ports
	int64 Size = 0;   // the download's size in bytes (0 when the index no longer lists it)
	FString State;    // available, downloaded, update, ingame, unlisted, notneeded
	FString Version;
	FString Job;      // empty, running, done, stopped, failed
	FString JobLine;  // "Downloading 4.1 / 15.0 MB", the error, "... downloaded"
	float Fraction = 0.0f;
	FString Picture;  // a PNG of the map, empty without one
	TArray<FString> Modes;
	FString Detail;   // the row's longer description, lines separated by '\n'

	bool IsRunning() const { return Job == TEXT("running"); }
	bool HasFailed() const { return Job == TEXT("failed") || Job == TEXT("stopped"); }
	/** Downloaded into the packs folder (so it can be removed). */
	bool IsDownloaded() const { return State == TEXT("downloaded") || State == TEXT("update") || State == TEXT("unlisted"); }
	/** On this PC in some form: downloaded, or in the converted game files. */
	bool IsHere() const { return IsDownloaded() || State == TEXT("ingame"); }
	/** The key its download is known by (ScctContentDownloads::GetDownload). */
	FString JobKey() const { return bShared ? TEXT("shared:") + Key : Key; }
};

/**
 * The Maps & Content page's side of the bridge's content table (Runtime/ScctContentApi.h version 2, the bridge's
 * Online/ContentCatalogue.cs): the catalogue of OpenSCCT-Content with what this PC has, downloads of maps and shared
 * packs, removals and the downloads folder. Downloads land where every map catalog of this client looks, so a map
 * that came in is offered by Create Session (and plays through the runtime map level when it has no imported one).
 *
 * Unattended switches (read when the online subsystem starts the content watcher):
 *   -ScctContentRemove=<map>[,<map>]    removes those downloaded maps, then logs each one's state in the catalogue
 *   -ScctContentDownload=<map>[,<map>]  once the index is read, downloads those maps, logs each job to completion,
 *                                       and logs whether Create Session now offers the map
 */
namespace ScctContentCatalogue
{
	/** Starts reading the index if due (bForce: now). 0 while loading, 1 read, 2 failed, -1 without the table. */
	int32 RefreshIndex(bool bForce = false);

	/** The rows. False when the bridge's table is not there. */
	bool GetRows(TArray<FScctContentRow>& Out);

	/** Where the content comes from and where downloads go. */
	FString GetLibrary();

	/** Loading, the index's failure, or "112 maps on OpenSCCT-Content, 2 downloaded." */
	FString GetSummary();

	/** The folder downloads go into. */
	FString GetPacksFolder();

	/** Download, Update and Retry: 1 started, 0 the index has no such map or pack, -1 the index is not read yet, -2 failure. */
	int32 Download(const FScctContentRow& Row);

	/** Stop. */
	void Stop(const FScctContentRow& Row);

	/** Remove: 1 removed, 0 not downloaded, -1 its download is running, -2 failed (OutError says why). */
	int32 Remove(const FString& Map, FString& OutError);

	/** Open folder: shows the downloads folder in Explorer (made first if missing). False with why in OutError. */
	bool OpenFolder(FString& OutError);

	/** After a download or a removal: the menus read their map list again (titles, pictures, Play's list). */
	void RefreshMenuMaps();

	/** Reads the unattended switches above and starts their watcher. Called once by ScctContentDownloads::Start. */
	void StartUnattended(UScctOnlineSubsystem* Online);
}
