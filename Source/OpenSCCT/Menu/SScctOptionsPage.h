#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SEditableTextBox;
class SOverlay;
class SScrollBox;
class SVerticalBox;
class UScctOnlineSubsystem;
struct FScctSettingDesc;

/**
 * What a feature draws on an options page above that page's registered options, when a plain list of options is
 * not enough (the key bindings and mouse of the Spy and Mercenary pages, the pad of the Controller page). The
 * section keeps its own edits; the page asks it whether it has any and tells it when the player applies, saves,
 * resets or throws its edits away, so its changes go with the rest of the screen's. Every callback but Build may
 * be left unset.
 */
struct FScctOptionsSection
{
	/** The section's widgets, built each time its page is shown. */
	TFunction<TSharedRef<SWidget>()> Build;
	/** Edits not applied yet. */
	TFunction<bool()> HasEdits;
	/** Applied but not written to disk yet. */
	TFunction<bool()> HasUnsaved;
	/** Make the edits take effect. */
	TFunction<void()> Apply;
	/** Write what is applied to disk. */
	TFunction<void()> Save;
	/** Drop the edits, and undo what was applied since the last save. */
	TFunction<void()> Discard;
	/** Edit everything back to the defaults (an edit like any other: it still has to be applied). */
	TFunction<void()> ResetToDefaults;
};

/**
 * The Options screen: every page of the settings registry (Settings/ScctSettings.h) as a tab, each visible option of
 * the page drawn by its type (a box to tick, a slider with its value, a choice with arrows either side, a text
 * field), under its group's heading, its tooltip on the whole row. The player's name is at the top, the one the
 * offline match, the server list and joining by address all use.
 *
 * Edits are held by the screen until they are applied: APPLY makes them take effect (the features hear OnChanged and
 * change at once), SAVE applies and writes the user settings file, SAVE & CLOSE does both and leaves. RESET PAGE puts
 * the shown page's options back to their defaults and RESET ALL every page's (after asking); both are edits, still to
 * be applied. The title carries a mark while anything is edited or applied but not saved, and BACK (or Escape) asks
 * before throwing that away, undoing what was applied. A search box lists the options of every page whose label
 * holds the text typed.
 *
 * The same widget is used by the front end (from the home page and the lobby) and over a match (from the in-match
 * menu; the match goes on underneath).
 *
 * Unattended switches (for screenshots):
 *   -ScctOptions=<Page>                 open the screen on that page (front end, or with -ScctMatchMenu in a match)
 *   -ScctSettingSet=<Key>=<Value>[;...]  edit options as if the player had, when the screen opens (not applied);
 *                                        the key PlayerName edits the name
 *   -ScctOptionsSearch=<Text>            type that into the search box
 *   -ScctOptionsSave                     press SAVE once the screen has opened (once per run)
 *   -ScctOptionsDemo                     register a few test options on the Display, Hud and Audio pages, so the
 *                                        screen can be looked at before the features have registered their own
 */
class SScctOptionsPage : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctOptionsPage)
		: _bTakeKeyboard(true)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		/** The page shown first; the first page when empty or unknown. */
		SLATE_ARGUMENT(FName, InitialPage)
		/**
		 * The screen takes the keyboard (Escape goes BACK). Over a match it must not: the viewport keeps the
		 * keyboard and the match's menu passes Escape on through RequestBack.
		 */
		SLATE_ARGUMENT(bool, bTakeKeyboard)
		/** Raised when the screen is left (BACK, SAVE & CLOSE). */
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	/** Adds a section drawn above Page's options (one per page; a second replaces the first). */
	static void RegisterSection(FName Page, const FScctOptionsSection& Section);

	/**
	 * The player's name as the offline match, the server list and joining by address use it: the online subsystem's,
	 * or, before it has first connected (it reads its identity then), the stored one it will read.
	 */
	static FString PlayerName(const UScctOnlineSubsystem* Online);

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** BACK: leaves, asking first when there are changes that were not saved. With a question open, answers No. */
	void RequestBack();

	void ShowPage(FName Page);

	/** Anything edited but not applied, or applied but not saved. */
	bool HasUnsavedChanges() const;

private:
	/** The pages shown as tabs: the registry's in its order, and pages that only have a section. */
	TArray<FName> FindPages() const;

	void RebuildTabs();
	void RebuildBody();
	void AddOptionRow(const TSharedRef<SVerticalBox>& Box, const FScctSettingDesc& Desc);
	TSharedRef<SWidget> MakeControl(const FScctSettingDesc& Desc);

	/** The value the screen shows: the edit if there is one, else the stored value. */
	FString ValueOf(const FScctSettingDesc& Desc) const;
	/** Edits an option; an edit back to the stored value is no edit. */
	void Edit(const FScctSettingDesc& Desc, const FString& Value);

	bool HasNameEdit() const;
	bool HasEdits() const;
	bool HasApplied() const;
	void Apply();
	void Save();
	void Discard();
	void ResetPage(FName Page);
	void ResetAll();
	void Close();

	/** Reads -ScctSettingSet and -ScctOptionsSave. */
	void RunUnattendedSwitches();

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	FSimpleDelegate OnClose;
	bool bTakeKeyboard = true;
	bool bKeyboardTaken = false;

	TSharedPtr<SOverlay> Root;
	TSharedPtr<SBox> TabsBox;
	TSharedPtr<SScrollBox> Body;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SEditableTextBox> SearchBox;

	FName CurrentPage;
	FString Search;

	/** Edits not applied yet, by key, as the text the registry stores. */
	TMap<FName, FString> Edits;
	/** What each option applied since the last save was before it: the stored text, or unset if it had none. */
	TMap<FName, TOptional<FString>> AppliedFrom;
	/** The name in the box; differs from the stored one while it is an edit (applying it also saves it). */
	FString EditedName;
};
