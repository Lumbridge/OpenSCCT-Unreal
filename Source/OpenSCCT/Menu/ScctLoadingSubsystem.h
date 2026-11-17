#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctLoadingSubsystem.generated.h"

class SWidget;
class UTexture2D;
struct FWorldContext;

/** What the loading page says about the level that is on its way. */
struct FScctLoadingPage
{
	/** The map as every page names it ("ENHANCED AQUARIUS"); empty for the front end. */
	FString Title;

	/** One line under the title: the mode and the side played, or where the travel goes. */
	FString Detail;

	/** A tip for the side, from the player's content, with his keys in it; may be empty. */
	FString Tip;

	/** What the page waits for, in place of "LOADING" ("JOINING THE ROUND"); empty for a level load. */
	FString Status;

	/** A Splinter Cell fact (Menu/ScctSplashTrivia.h), shown when there is no tip to give; may be empty. */
	FString Trivia;

	/** The side's accent: 0 spies, 1 mercs, -1 none. */
	int32 Side = -1;

	/** The map's loading picture from the player's content, or null. */
	UTexture2D* Picture = nullptr;
};

/**
 * The loading page. A level load blocks the game for seconds: Unreal loads the level's package, and then the
 * bridge loads the OpenSCCT map and starts the match inside the level's BeginPlay. Until now the window stood
 * still on the last picture (or black) all that time.
 *
 * This subsystem hears every level load of the game (FCoreUObjectDelegates::PreLoadMapWithContext) and gives the
 * engine's movie player a page to show for it: the map's name, the mode and side, the map's own loading picture
 * and one of the side's tips, all from the player's content through the bridge (Runtime/ScctPlayApi.h). The movie
 * player draws the page on its own thread while the game thread is busy and takes it away when the level has
 * begun play, which is after the match has started.
 *
 * Unattended switch (off by default):
 *   -ScctLoadingHold=S   once the level is up, keep the same page over the picture for S more seconds, so a
 *                        screenshot of an unattended run can show it (the movie player's own drawing happens
 *                        while the game thread, which takes the screenshots, is blocked)
 */
UCLASS()
class OPENSCCT_API UScctLoadingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The page as a widget: the same one for the movie player and for -ScctLoadingHold. */
	static TSharedRef<SWidget> MakePage(const FScctLoadingPage& Page);

	/**
	 * The page of a round of Map (an OpenSCCT map's folder name) for a side (0 spy, 1 merc, -1 watching): the map's
	 * title, the side's tip and the map's loading picture from the player's content, with Detail and Status as
	 * given. The match overlay covers an online level with it while the round is travelled to or joined.
	 */
	FScctLoadingPage DescribeRound(const FString& Map, int32 Side, const FString& Detail, const FString& Status);

private:
	void HandlePreLoadMap(const FWorldContext& Context, const FString& MapName);
	void HandlePrepareLoadingScreen();
	void HandlePostLoadMap(UWorld* World);

	/** What a load of MapName with these URL options shows. */
	FScctLoadingPage DescribeLoad(const FWorldContext& Context, const FString& MapName);

	/** The page of the load that is about to start; the movie player asks for it once per load. */
	FScctLoadingPage Pending;
	bool bHasPending = false;

	/** The loading pictures read so far, by file: kept so the page's brush stays valid while it is drawn. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Pictures;

	TSharedPtr<SWidget> HeldPage;
	FTSTicker::FDelegateHandle HoldTicker;

	/** Notes the URL of a travel that waits for the next engine tick (FCoreDelegates::OnEndFrame). */
	void NoteTravel();

	/** The URL NoteTravel saw last; emptied once a load used it. */
	FString PendingTravelURL;
	FDelegateHandle PrepareHandle;
};
