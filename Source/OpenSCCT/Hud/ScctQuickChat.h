#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctQuickChat.generated.h"

class AScctPlayerController;
class FScctQuickChatInput;
class SScctQuickChatPanel;
class SWidget;

/** One category of the quick chat: its name and its four phrases. */
struct FScctQuickChatCategory
{
	const TCHAR* Name;
	const TCHAR* Messages[4];
};

/**
 * The quick chat's choices as Godot's QuickChatState has them: a category, then a phrase; only a completed choice gives
 * a line. The menu closes by itself TimeoutSeconds after it opened or after the category was picked, and a line sent
 * keeps the menu shut for CooldownSeconds.
 */
struct FScctQuickChatState
{
	static constexpr double TimeoutSeconds = 8.0;
	static constexpr double CooldownSeconds = 1.5;
	static constexpr int32 NumChoices = 4;

	/** Attack, Defend, Information, Responses, with Godot's phrases. */
	static const FScctQuickChatCategory& Category(int32 Index);

	bool IsOpen() const { return bOpen; }
	/** The category picked, or -1 while the categories are listed. */
	int32 GetCategory() const { return CategoryIndex; }
	/** Seconds left before the menu closes by itself. */
	double TimeLeft(double Now) const { return bOpen ? FMath::Max(Expires - Now, 0.0) : 0.0; }

	/** False when it is open already or the last line was sent less than CooldownSeconds ago. */
	bool Open(double Now);

	/** Picks choice Index (0..3): the category first, then a phrase, which is returned (and the menu closes). */
	TOptional<FString> Choose(int32 Index, double Now);

	/** From the phrases back to the categories; from the categories, closes. */
	void Back();

	/** Closes the menu once its time is up; true when it did now. */
	bool Expire(double Now);

	void Close();

private:
	bool bOpen = false;
	int32 CategoryIndex = -1;
	double Expires = 0.0;
	double NextSend = 0.0;
};

/**
 * The team quick chat of a match (Godot's QuickChatMenu): the QuickChat binding (bound on the controls page or with
 * ScctBind; it has no default key, as in the Godot client) opens a menu at the right of the screen listing the four
 * categories; 1 to 4 (the number row or the keypad) or the controller's D-pad (up, right, down, left) pick one, and
 * then one of its four phrases, which goes to the team as a TEAM line through the chat (UScctChatSubsystem::Send:
 * the session's own connection, or the visit's). Esc, Backspace or the controller's B (or Back, Start) go from the
 * phrases back to the categories and from the categories close the menu; QuickChat again closes it, and it closes by
 * itself after 8 seconds without a choice.
 *
 * THE KEYS. Unlike the composer, the menu does not take the keys from the game: the player keeps moving and looking.
 * Only the keys it reads (1-4, Num 1-4, Esc, Backspace, the D-pad, B, Back, Start) are held back while it is open, by
 * a Slate input preprocessor that eats their presses (and the releases of those presses) before the viewport, so a
 * digit does not also pick a gadget and Esc does not also open the match menu. It does not open while a menu or the
 * composer has the keys, and closes when one of them takes them.
 *
 * Unattended switches (off by default; for runs with nobody at the keyboard):
 *   -ScctQuickChat=C,P       open the menu -ScctQuickChatAt seconds (default 8) after the match began, then press
 *                            the key of category C (1-4) -ScctQuickChatHold seconds later (default 2) and the key of
 *                            phrase P (1-4) as long again after that; the presses go through Slate as a keyboard's do
 *   -ScctQuickChat=C         open and pick category C only, leaving the phrases on screen until the menu times out
 * Every open, choice, close and line sent writes a "ScctQuickChat:" line to the log (the line itself as "ScctChat:
 * sent team ...").
 */
UCLASS()
class OPENSCCT_API UScctQuickChatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	bool IsOpen() const { return State.IsOpen(); }

	/** Opens the menu on the categories; false when it cannot open now (a menu or the composer has the keys, no match). */
	bool Open();
	void Close(const TCHAR* Why);

	/** Picks choice Index (0..3) as its key does. */
	void Choose(int32 Index);
	void Back();

	/**
	 * A key the preprocessor saw going down: true when the menu reads it, and the game must not have it. Also called for
	 * the keys of the unattended switch.
	 */
	bool HandleKeyDown(const FKey& Key, bool bRepeat);

	/** A key coming up: true when its press was the menu's, so its release is too. */
	bool HandleKeyUp(const FKey& Key);

	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);

	/** In a level that plays a match, with a local player: the panel is put in the viewport. */
	bool EnsurePanel();
	void RemovePanel();

	/** Something else has the keys (a menu, the composer): the menu cannot be open. */
	bool IsBlocked() const;

	void RunUnattended(float DeltaTime);

	FScctQuickChatState State;
	double Clock = 0.0;

	TSharedPtr<SScctQuickChatPanel> Panel;
	TSharedPtr<SWidget> ViewportContent;
	TSharedPtr<FScctQuickChatInput> InputProcessor;

	TWeakObjectPtr<AScctPlayerController> CommandSource;
	FDelegateHandle CommandHandle;

	/** The keys whose presses the menu ate: their releases are eaten too, even after it closed. */
	TSet<FKey> EatenKeys;

	// The unattended switch: its steps, timed from the match's start.
	int32 DebugCategory = 0;
	int32 DebugPhrase = 0;
	float DebugAt = 8.0f;
	float DebugHold = 2.0f;
	int32 DebugStep = 0;
	float DebugClock = 0.0f;
	bool bDebugStarted = false;
};
