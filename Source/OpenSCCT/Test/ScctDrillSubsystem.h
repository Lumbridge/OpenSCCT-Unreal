#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Test/SScctDrillCoach.h"
#include "ScctDrillSubsystem.generated.h"

class SWidget;
struct FScctLessonApi;
struct FScctLessonState;

/** A TestRange drill as the menus offer it. */
struct FScctDrillInfo
{
	/** vision-cancelling or spy-grab: the URL option's value and the console command's argument. */
	const TCHAR* Kind;

	/** The menu's name for it. */
	const TCHAR* Title;

	/** One line on what it trains. */
	const TCHAR* Description;
};

/**
 * The interactive TestRange drills of the Godot client's Tutorials menu (game/Scripts/Tutorials/InteractiveLessonPlay.cs
 * and InteractiveLessonCoach.cs), on the bridge's copy of the same lesson code (BrowserLesson, the wiki's browser
 * lessons too):
 *   vision-cancelling   walk off the tower; switch vision at touchdown while moving to cut the landing recovery short.
 *                       The coach measures a baseline drop, then each drop's recovery, switch timing and time saved,
 *                       and a green portal at the foot of the tower puts the spy back on the ledge.
 *   spy-grab            a held mercenary bot in the lane, who cannot attack or kill; walk up behind him and grab.
 *                       The coach says what the grab test says from where the spy stands and counts the grabs; the
 *                       target can be set walking, and the practice grab-area overlay shows from the start.
 *
 * A drill is the level TestRange with ?Drill=<kind> (LaunchURL). This subsystem reads the option as the level begins
 * play, before the game mode starts its match, and arms the bridge, which makes that start the drill's (spy side,
 * Play Level rules, the drill's bot, the drill's start: Test/LessonLaunch.cs); once the match plays it starts the
 * lesson and shows the coach panel (SScctDrillCoach) at the top left.
 *
 * Ways in:
 *   scct.Drill <kind>            the console command, from the front end or a match: opens the drill's level
 *   UScctDrillSubsystem::Launch  the same for a menu button (GetDrills lists what to offer)
 *   ?Drill=<kind>                the URL option itself, on TestRange
 *
 * The coach's buttons (clickable while the in-match menu has freed the mouse) and the console command
 * scct.DrillAction <retry|moving|grabarea|...> do the same things. Every measured attempt writes a log line
 * "Drill vision-cancelling attempt: recovery X s (...)", every grab "Drill spy-grab: grab N (...)".
 *
 * Unattended switch (for runs with nobody at the keyboard):
 *   -ScctDrillActions="<seconds> <action>;..."   calls scct.DrillAction's actions that many seconds after the lesson
 *                                               placed the players ("8 retry;9 moving;12 grabarea")
 */
UCLASS()
class OPENSCCT_API UScctDrillSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UScctDrillSubsystem();

	/** The drills there are, for a menu to offer. */
	static TConstArrayView<FScctDrillInfo> GetDrills();

	static bool IsKind(const FString& Kind);

	/** The level URL of a drill ("/Game/Imported/Maps/TestRange/TestRange?Drill=spy-grab?Side=spy?Bots=1"). */
	static FString LaunchURL(const FString& Kind, const FString& PlayerName = FString());

	/** Opens a drill's level from any world. False (and a log line) for an unknown kind. */
	static bool Launch(const UObject* WorldContext, const FString& Kind, const FString& PlayerName = FString());

	/**
	 * One of the coach's actions: retry (vision-cancelling: back to the ledge; spy-grab: the drill again from the
	 * start), moving (the target walks or stops), grabarea (the overlay on or off), or any of the lesson's own
	 * ("reset", "forward", "night", "grab"...). Returns what happened, for the console.
	 */
	FString Action(const FString& Name);

	/** A drill runs in this world. */
	bool IsActive() const { return bLessonStarted; }

	const FString& GetKind() const { return Kind; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleMatchStarted();
	void HandleMatchStopped();
	void HandleFrame(float DeltaSeconds);

	/** Fills the coach's model from the bridge's state. */
	void UpdateModel(const FScctLessonState& State);

	/** Writes the log lines of attempts and grabs not logged yet. */
	void LogProgress(const FScctLessonState& State);

	/** Projects vision-cancelling's return portal for this frame's camera. */
	void ProjectPortal(const FScctLessonState& State);

	/** -ScctDrillActions: runs the actions that are due. */
	void RunScriptedActions(float DeltaSeconds);

	bool IsGrabAreaShown() const;

	void ShowCoach();
	void HideCoach();

	FString Kind;
	const FScctLessonApi* Api = nullptr;
	bool bLessonStarted = false;

	TSharedRef<FScctDrillCoachModel> Model;
	TSharedPtr<SScctDrillCoach> Coach;
	TSharedPtr<SWidget> ViewportContent;

	/** The grab area was asked for once the spy-grab lesson had placed the players. */
	bool bGrabAreaAsked = false;

	/** The last attempt number and grab count logged, and the feedback line last logged. */
	int32 LoggedAttempt = 0;
	int32 LoggedGrabs = 0;
	FString LoggedFeedback;

	/** -ScctDrillActions, by seconds after the lesson placed the players, and the time since. */
	TArray<TPair<float, FString>> ScriptedActions;
	float ReadySeconds = -1.0f;
};
