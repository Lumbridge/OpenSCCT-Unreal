#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctTutorialApi.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctTutorialPlay.generated.h"

class UScctFrontEndWidget;

/**
 * Offline play of a tutorial map or a Visit a Map, and the saved tutorial progress (gaps 54 and 101; the Godot
 * client's Tutorials/TutorialPlay.cs and TutorialProfile.cs, spec/game/tutorials.md).
 *
 * A tutorial plays its map alone as the side of the tutorial: Story, no bots, the offline launch rules (?Practice: no
 * time limit, unlimited lives, 5 s respawn), the in-game help off. A visit plays any Story map alone as the side
 * picked, with the help paths shown (?Help: the level's profile options hand DisplayHelpInGame to the simulation
 * before the map loads) and unlimited gear. Both go through the same level URL as the offline page's matches, with
 * ?Tutorial or ?Visit added so the level's round watch (UScctTutorialWatch) knows what it plays. Leaving either by the
 * in-match menu comes back to the Tutorials page.
 *
 * The progress is the profile's tutorial part, in <project>/Saved/Profile/tutorials.json (the Godot client's
 * tutorials.json, the same names): the tutorial maps done, the exam and the maps the first-time prompt was confirmed
 * for. The bridge keeps it (Bridge/OpenScct.UnrealBridge/Tutorials); it is read once a run.
 *
 * Unattended, from the front end (once a run):
 *   -ScctTutorial=TSP20      opens the side's Tutorial Maps page and plays that map as its row does
 *   -ScctVisit=AquaE[,merc]  opens Visit a Map and launches that map as its LAUNCH does
 *   -ScctTutorialReset       forgets all progress (and saves) before anything else
 */
UCLASS()
class OPENSCCT_API UScctTutorialSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UScctTutorialSubsystem* Get(const UObject* WorldContext);

	/** The one instance of the running game, for the pages (which have no world of their own). */
	static UScctTutorialSubsystem* GetCurrent() { return Current.Get(); }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** <project>/Saved/Profile/tutorials.json. */
	static FString GetProgressFile();

	/** The saved progress (zeros without the bridge). */
	FScctTutorialProgress GetProgress() const;

	/** The tutorial maps of a side (0 spies, 1 mercs), the spies' exam last. */
	TArray<FString> GetMaps(int32 Team) const;

	/** The side's tutorial Index is done (the spies' exam: the exam flag). */
	bool IsDone(int32 Team, int32 Index) const;

	/** "Spy tutorials 2 of 4, exam passed. Mercenary tutorials 1 of 3." */
	FString GetProgressLine() const;

	/** The map's level is imported and the content has its simulation map. */
	static bool CanPlay(const FString& Map);

	/** Tuto_MapsList.LoadMap: plays Map alone as the side of Team (0 spies, 1 mercs), the help off. */
	bool PlayTutorial(const FString& Map, int32 Team);

	/** Visit_Map_Set's Launch: Map alone as the side, with the help paths shown and unlimited gear. */
	bool Visit(const FString& Map, bool bMerc);

	/** The front end opened after a tutorial play: true once, then the Tutorials page should show. */
	bool TakeReturnToTutorials();

	/** Called when the front end's menu is up: the unattended -ScctTutorial / -ScctVisit switches, once a run. */
	void RunCommandLine(UScctFrontEndWidget* Menu);

private:
	bool Start(const FString& Map, bool bMerc, bool bVisit);
	const FScctTutorialApi* Api() const;

	static TWeakObjectPtr<UScctTutorialSubsystem> Current;

	bool bReturnToTutorials = false;
	bool bCommandLineDone = false;
};

/**
 * The round watch of a tutorial or visit level (Godot's TutorialRoundWatch; SPlayerController GameEnded.PlayerTick's
 * offline tutorial branch). Made for levels opened with ?Tutorial or ?Visit. Once the round has a winner, a won
 * tutorial is marked done and saved, and the log says what follows ("ScctTutorial: TSP20 done, next TSP21"). After
 * the end delay (K.EndMenuDelay, 10 s) the next tutorial of that side plays (none once the spies' exam is passed or
 * every merc tutorial is done), or the Tutorials page opens again; a visit always goes back to the page.
 *
 * Unattended:
 *   -ScctTutorialWin=S    S seconds into the round, ends it with the local player's side winning, as the map's own
 *                         story end does (for checking the progress and what follows without playing the map)
 */
UCLASS()
class OPENSCCT_API UScctTutorialWatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UScctTutorialWatch, STATGROUP_Tickables); }

private:
	void Leave();

	FString Map;
	bool bVisit = false;
	bool bMerc = false;
	bool bSeenRound = false;
	bool bEnded = false;
	bool bLeft = false;
	float RoundSeconds = 0.0f;
	float EndedFor = 0.0f;
	float ForceWinAt = -1.0f;
	FString Next;
};
