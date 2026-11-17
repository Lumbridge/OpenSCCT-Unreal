#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SScctDialogLayer;
class SScctTabBar;
class UScctOnlineSubsystem;

/**
 * The settings, the same in the front end and in a match: tabs over a list of rows, with what the focused row
 * does beside it.
 *
 *   VIDEO     window mode and resolution (applied together, kept only when confirmed), vertical sync, frame limit,
 *             the quality preset and its groups (global illumination, reflections, shadows, post-processing,
 *             textures, effects, anti-aliasing, view distance), render scale, motion blur, brightness, field of view
 *   AUDIO     master, effects, ambience, music, menu music, menu sounds
 *   CONTROLS  mouse speed and inversions, each side's look speeds, the controller's switches and dead zones
 *   BINDINGS  every action of a side on the keyboard and mouse or on the controller: capture, conflict warning, reset
 *   GAME      units, hold or toggle for the keys that can be either, the wheel, auto-hack, the content's language
 *   PROFILE   the player's name and the account
 *
 * Every change is in force at once (so its effect is seen and heard behind the panel) and written when the panel
 * closes, except the window mode and the resolution, which wait for APPLY.
 * Where the values live: Menu/ScctUserSettings.h and Play/ScctControlSettings.h.
 */
class SScctSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctSettingsPanel) : _InitialTab(0) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		SLATE_ARGUMENT(TSharedPtr<SScctDialogLayer>, Dialogs)
		SLATE_ARGUMENT(int32, InitialTab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctSettingsPanel() override;

	/** The next or the previous tab (Q and E, the controller's shoulders). */
	void StepTab(int32 Direction);

	/** "video", "audio", "controls", "bindings", "game", "profile"; with "bindings/merc" or "bindings/pad" the side or the device. */
	void ShowTabNamed(const FString& Name);

	/** What takes the focus when the panel opens or its tab changes. */
	TSharedPtr<SWidget> GetDefaultFocus() const { return DefaultFocus.Pin(); }

	/** Writes everything to disk. */
	void Commit();

	static int32 FindTab(const FString& Name);

private:
	void Rebuild();
	TSharedRef<SWidget> BuildVideo();
	TSharedRef<SWidget> BuildAudio();
	TSharedRef<SWidget> BuildControls();
	TSharedRef<SWidget> BuildBindings();
	TSharedRef<SWidget> BuildGame();
	TSharedRef<SWidget> BuildProfile();

	void ApplyDisplay();
	void HandleCaptured(const FString& ActionId, const FString& Label, const FKey& Key);
	void Bind(const FString& ActionId, const FString& KeyName);
	void Toast(const FString& Message);

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TSharedPtr<SScctDialogLayer> Dialogs;
	TSharedPtr<SScctTabBar> Tabs;
	TSharedPtr<SBox> Body;
	TWeakPtr<SWidget> DefaultFocus;

	// The window mode and the resolution wait here for APPLY.
	int32 PendingMode = 0;
	FIntPoint PendingSize = FIntPoint::ZeroValue;
	TArray<FIntPoint> Sizes;

	bool bBindMerc = false;
	bool bBindPad = false;
	bool bDirty = false;
};
