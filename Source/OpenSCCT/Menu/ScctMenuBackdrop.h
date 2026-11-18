#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ScctMenuBackdrop.generated.h"

class AScctFrontEndGameMode;
class ULevelStreamingDynamic;
class UScctSessionSubsystem;
struct FScctBackdropApi;

/**
 * The live picture behind the main menu: a real match played by four bots (two spies against two mercs, Story) on
 * one of the imported maps, watched through a camera that follows a bot and cuts to another every so often, and a
 * change to another map every few minutes or when the match is over.
 *
 * The front end's game mode owns one of these and ticks it. The still backdrop the game mode has always shown stays
 * the first picture and the fall back: the director only takes over once that still (or the level's own scene) is on
 * screen, and when nothing can be played (the option is off, no imported map, no converted content, no prepared bot
 * navigation) the still is all there is.
 *
 * A change of picture never stops the menu: the next map's level streams in hidden and its match is started on a
 * worker thread of the bridge (Runtime/ScctBackdropApi.h) while the current match goes on being shown. When both
 * are ready the scene dips to the menu shade's own dark ink (GetCover, drawn by the shade in Menu/SScctFrontEnd.cpp
 * under its gradients, so the menu itself does not change), the levels and the matches are exchanged under it, and
 * the scene comes back once the new match has a view and players to show. The old level is unloaded afterwards.
 *
 * The match gives way to everything the player starts: an online or hub visit suspends it (the picture holds where
 * it was), and leaving the level for a match, a demo or a tutorial stops it (Shutdown, from the game mode's
 * StartToLeaveMap). Coming back to the menu is a new world, which starts again by itself.
 *
 * Unattended switches (off by default):
 *   -ScctMenuLive=0|1                the option "Live menu background" for this run, whatever the settings say
 *   -ScctMenuLiveSwapSeconds=S       change maps every S seconds instead of every four minutes
 *   -ScctMenuLiveMaps=AquaE,SteeE    the maps in this order (round and round) instead of a shuffle of all of them
 *
 * Log lines to look for: "Menu: live backdrop is ..." (a match is on screen) and "Menu: backdrop swap A -> B ..."
 * with how long the scene was covered and the longest frames around the change.
 */
UCLASS()
class OPENSCCT_API UScctMenuBackdrop : public UObject
{
	GENERATED_BODY()

public:
	/** How much of the scene behind the menu is covered by the shade's ink right now: 0 none, 1 all of it. */
	static float GetCover();

	/**
	 * Once a frame from the game mode. bStillSettled: the still backdrop is on screen, or there is none to wait for.
	 * StillMap and StillLevel: that still's map and its streamed level (empty and null for the level's own scene).
	 */
	void Tick(AScctFrontEndGameMode& GameMode, float DeltaSeconds, bool bStillSettled, const FString& StillMap, ULevelStreamingDynamic* StillLevel);

	/** The level is being left, or the game mode ends: the match and whatever is being made ready are dropped. */
	void Shutdown(UWorld* World);

private:
	enum class EPhase : uint8
	{
		/** Nothing under way: the still, a held picture, or the wait before the next try. */
		Idle,
		/** The next map's level streams in hidden and its match starts on the bridge's worker. */
		Preparing,
		/** The scene dips to the shade's ink. */
		CoverIn,
		/** Covered: the level shown until now is taken out of the world. */
		HideOld,
		/** Covered: the next level is put in the world. */
		ShowNew,
		/** Covered: the new match runs its first ticks until it has a view and players. */
		Warm,
		/** The scene comes back. */
		CoverOut,
		/** A match is on screen. */
		Live,
	};

	void StartNext(UWorld& World, const FScctBackdropApi& Api);
	void TickPreparing(UWorld& World, const FScctBackdropApi& Api);
	void Exchange(UWorld& World, UScctSessionSubsystem& Session);
	void Adopt(UWorld& World, const FScctBackdropApi& Api, UScctSessionSubsystem& Session);
	void GiveUpOnNext(UWorld& World, const FScctBackdropApi& Api, UScctSessionSubsystem& Session, AScctFrontEndGameMode& GameMode, const TCHAR* Why);
	void Suspend(UWorld& World, const FScctBackdropApi* Api, UScctSessionSubsystem* Session, AScctFrontEndGameMode& GameMode, const TCHAR* Why);
	void HoldPicture(AScctFrontEndGameMode& GameMode);
	void DropNextLevel();
	void SetPhase(EPhase NewPhase);
	bool IsWanted(const UWorld& World, const UScctSessionSubsystem* Session, const TCHAR*& OutWhy) const;
	FString PickNext();

	EPhase Phase = EPhase::Idle;
	double PhaseStarted = 0.0;

	/** The map whose level is in the world now (empty: the level's own scene) and that level. */
	FString ShownMap;
	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> ShownLevel;

	/** The map being made ready and its level (the shown level itself when the map does not change). */
	FString NextMap;
	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> NextLevel;

	/** The level shown before the last exchange, unloaded once the new picture is up. */
	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> OldLevel;

	/** The maps in the order they are tried, and the place in it. */
	TArray<FString> Order;
	int32 OrderIndex = 0;
	bool bOrderRead = false;
	bool bOrderGiven = false;

	/** Maps that could not be played this run (no converted content, no prepared bot navigation, a failed start). */
	TSet<FString> Refused;

	/** Wall clock time before which nothing new is tried. */
	double NextTryAt = 0.0;

	/** When the match on screen was shown, and when it was seen to be over (0: it is not). */
	double LiveSince = 0.0;
	double EndedSince = 0.0;
	double SwapSeconds = 240.0;

	/** The last view a backdrop match drew, held as the still picture when the match stops without a new one. */
	bool bHaveLastView = false;
	FVector LastViewLocation = FVector::ZeroVector;
	FRotator LastViewRotation = FRotator::ZeroRotator;

	/** The scene is covered for an exchange that brings a match (false: it only comes back to a held picture). */
	bool bLiveAfterCover = false;
	bool bFirstShown = false;

	/** Under the cover, once the new match is seen: the level shown before has been told to go. */
	bool bOldDropped = false;

	/** The picture was held because the match went away while the next one was being made ready. */
	bool bHeldForLoss = false;
	bool bSuspended = false;
	bool bShutDown = false;
	bool bSwitchesRead = false;
	int32 ForcedOption = -1;
	double CreatedAt = 0.0;

	/** What a change cost, for its log line. */
	double PrepareStarted = 0.0;
	double OldDroppedAt = 0.0;
	double CoverStarted = 0.0;
	double AdoptMs = 0.0;
	double LongestLoadingFrameMs = 0.0;
	double LongestCoveredFrameMs = 0.0;
	double LongestFadeFrameMs = 0.0;

	/** When the still was first seen settled, and the seconds each covered stretch of the last change took. */
	double SettledAt = 0.0;
	double HideSeconds = 0.0;
	double ShowSeconds = 0.0;
	double WarmSeconds = 0.0;
	FString SwapFrom;
};
