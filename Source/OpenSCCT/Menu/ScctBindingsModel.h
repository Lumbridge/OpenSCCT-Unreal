#pragma once

#include "CoreMinimal.h"

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
 * calls the Help page (the keys in play) and the unattended -ScctBindTest use; the test goes through the same
 * Check, Conflict and Commit a capture would. Keys are edited on screen on the settings page's BINDINGS tab
 * (Menu/SScctSettingsPanel.h).
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
