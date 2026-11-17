#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
struct FScctBindingsApi;

/** One action of a side as the key bindings editor lists it. */
struct FScctBindingRow
{
	FString Id;
	FString Category;
	FString Label;
	FString Description;
	bool bDoubleTap = false;
	/** Binding names ("W", "Ctrl+F", "Double+H"), primary first. */
	TArray<FString> Keys;
};

/**
 * The key bindings editor's model on the Unreal side: the bridge's editor table (Runtime/ScctBindingsApi.h) behind
 * calls the panel and the unattended test both use, so a key bound by -ScctBindTest goes through exactly what a key
 * captured on screen goes through.
 */
namespace ScctBindings
{
	/** The editor table, after the player's files have been read (null without the bridge). */
	const FScctBindingsApi* Api();

	/** The rows of a side, in the Godot client's order. */
	TArray<FScctBindingRow> Rows(bool bMerc);

	/** How a binding name is shown: "Left Mouse", "Ctrl + F", "Double-tap H", "Wheel Up". */
	FString DisplayName(const FString& Key);

	/** Why Key cannot take the action ("" when it can). */
	FString Check(bool bMerc, const FString& ActionId, const FString& Key);

	/** What Key does apart from the action ("" when free or already the action's), and whether a swap is possible. */
	FString Conflict(bool bMerc, const FString& ActionId, const FString& OldKey, const FString& Key, bool& bOutAlreadyBound, bool& bOutCanSwap);

	/**
	 * The end of a capture: binds Key to the action in place of OldKey ("" adds a key) in the draft, with Mode
	 * saying what happens to what the key did (0 share, 1 replace, 2 swap: EScctBindingCommit). Logs
	 * "ScctBind: spy F -> Interact / action menu (conflict with Switch fire mode: replace)". False with the reason.
	 */
	bool Commit(bool bMerc, const FScctBindingRow& Row, const FString& OldKey, const FString& Key, int32 Mode, const FString& ConflictWith, FString& OutError);

	void Clear(bool bMerc, const FScctBindingRow& Row, const FString& Key);
	void ResetKeys(bool bMerc);
	bool IsSplit();
	void SetSplit(bool bSplit);

	/** Puts the draft in play (the match plays it from its next frame) and tells UScctControlSettings::OnRebound. */
	void Apply();
	/** Writes bindings.json when what is in play is not what the file has. */
	void Save();
	void Discard();
	bool HasEdits();
	bool HasUnsaved();

	/**
	 * -ScctBindTest=<side>,<key>,<action id>[,replace|swap|share][;...] and -ScctBindTest=<side>,reset: binds the
	 * key to the action in place of its primary key, through Commit as a capture would (a conflict is answered with
	 * the mode given, replace by default), or resets the side's keys; then applies and saves, as SAVE would. Each
	 * step is logged. Runs once per process.
	 */
	void RunUnattendedTest();
}

/**
 * The key bindings of one side, drawn as the section of the Spy or Mercenary options page (SScctOptionsPage::
 * RegisterSection): every action the side can bind in the Godot client's order (movement, combat, gadgets, vision,
 * communication, interface) under its category, the action's description as its tooltip, and a button per key it
 * has, primary and secondary, plus an empty one to add a key.
 *
 * Clicking a key opens the capture over the Options screen (Godot OptionsMenu.BindingCapture.cs): the next key or
 * mouse button, with Ctrl, Shift or Alt held for a combination, a modifier pressed and let go alone for the modifier
 * itself, the wheel, and a double tap when "Double tap" is ticked; Escape cancels and Delete or Backspace clears the
 * key (so those two can only be bound with a modifier). A key that already does something on that side opens the
 * conflict: what it does now, and REPLACE (it does only this action), SWAP (it and the key being changed trade),
 * SHARE (it does both) or CANCEL.
 *
 * "Separate spy and mercenary layouts" off makes both sides play the spy's keys (the merc's page then edits them
 * too); RESET KEYS asks, then puts the side's keys back to the defaults. Every change is an edit of the screen like
 * any other: APPLY puts it in play, so in a running match the new key works as soon as the menu is closed, SAVE also
 * writes Saved/Controls/bindings.json, and leaving without saving puts the file's keys back.
 *
 * Unattended switches (for screenshots and checks; with -ScctOptions=Spy or Mercenary):
 *   -ScctBindCapture=<action id>[,<key>]  open the capture of the action's primary key; with a key, press it (a key
 *                                         already in use then shows the conflict question)
 *   -ScctBindTest=...                     see ScctBindings::RunUnattendedTest (runs in any play world)
 */
class SScctBindingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctBindingsPanel) : _bMerc(false) {}
		SLATE_ARGUMENT(bool, bMerc)
	SLATE_END_ARGS()

	/** Registers the panel as the section of the Spy and Mercenary pages (once). */
	static void RegisterSections();

	void Construct(const FArguments& InArgs);

	/** Draws the rows again from the draft. */
	void Refresh();

private:
	void BeginCapture(const FScctBindingRow& Row, const FString& OldKey);
	/** A captured key: binds it, or asks about the conflict first. */
	void Captured(const FScctBindingRow& Row, const FString& OldKey, const FString& Key);
	void AskReset();
	TSharedRef<SWidget> MakeSlot(const FScctBindingRow& Row, const FString& Key);

	bool bMerc = false;
	TSharedPtr<SVerticalBox> List;
	FString Status;
};
