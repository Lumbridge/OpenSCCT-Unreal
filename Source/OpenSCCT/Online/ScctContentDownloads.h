#pragma once

#include "CoreMinimal.h"

class UScctOnlineSubsystem;

/** The lobby's missing-map line as the bridge describes it (ContentApi::GetLobbyFetch). */
struct FScctLobbyMapFetch
{
	FString Line;
	int32 Flags = 0;     // EScctContentLobbyFlags
	int32 State = 0;     // EScctContentDownloadState of the lobby's download
	float Fraction = 0.0f;

	bool IsShown() const;
	bool CanDownload() const;
	bool IsRunning() const;
	bool IsProblem() const;
};

/**
 * Downloads from OpenSCCT-Content (Runtime/ScctContentApi.h, the bridge's Online/ContentPacks.cs): the lobby's
 * missing-map download and the prebuilt bot navigation. A downloaded map lands in the packs folder every map catalog
 * of this client searches, so it plays through the runtime map level (Play/ScctRuntimeMapActor) when there is no
 * imported level for it.
 *
 * Cooked Unreal map paks are another track: a map with an imported level of its own would need its pak fetched too.
 * The hook for that is ScctContentDownloads::OnMapInstalled below, which is told every map that came in.
 *
 * Unattended switches:
 *   -ScctAutoDownload        in a lobby whose map this player lacks, press Download as soon as it is offered, and log
 *                            the progress, the install and the local map state that follows
 */
namespace ScctContentDownloads
{
	/** The lobby's line. False when the bridge's table is not there. */
	bool GetLobbyFetch(FScctLobbyMapFetch& Out);

	/** The lobby's Download (and Retry). True when a download started. */
	bool StartLobbyFetch();

	/** The lobby's Stop. */
	void CancelLobbyFetch();

	/** Moves on each time a map finished installing (-1 without the table). */
	int32 Generation();

	/** EScctBotNavigationState for a map, with the question (or why nothing is offered) in OutText. */
	int32 CheckBotNavigation(const FString& Map, FString& OutText);

	/** Starts the map's bot navigation download. True when it started. */
	bool StartBotNavigation(const FString& Map);

	/** A key's download (a map, or "nav:" and a map): its EScctContentDownloadState, with its line in OutText. */
	int32 GetDownload(const FString& Key, FString& OutText, float& OutFraction);

	/** Stops a key's download. */
	void CancelDownload(const FString& Key);

	/**
	 * Starts watching the downloads for this game: the -ScctAutoDownload switch, a log line for each map that comes
	 * in (and the hook below), and the local player's map state after it. Called once by the online subsystem.
	 */
	void Start(UScctOnlineSubsystem* Online);

	/** Told each map that finished installing, with its folder (the hook for a cooked map pak fetch). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMapInstalled, const FString& /*Map*/, const FString& /*Folder*/);
	FOnMapInstalled& OnMapInstalled();
}
