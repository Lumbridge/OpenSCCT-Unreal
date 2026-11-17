#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"

class SBox;
class SScctTabBar;
class SVerticalBox;

/**
 * The settings, the same page in the front end and over a match: tabs over a list of rows, with what the focused
 * row does beside it.
 *
 *   VIDEO     window mode and resolution (applied together, kept only when confirmed), vertical sync, frame limit,
 *             the quality preset and its groups (global illumination, reflections, shadows, post-processing,
 *             textures, effects, anti-aliasing, view distance, foliage, shading), render scale, motion blur,
 *             brightness, the field of view
 *   AUDIO     master, effects, ambience, music, menu music, menu sounds
 *   CONTROLS  mouse speed and inversions, each side's look speeds, hold or toggle, the controller's switches and
 *             dead zones
 *   BINDINGS  every action of a side on the keyboard and mouse or on the controller: capture, conflict warning, reset
 *   GAME      units, auto-hack, the wheel, the content's language
 *   PROFILE   the player's name and the account
 *   ...       one tab per page of the settings registry (Settings/ScctSettings.h) that no tab above holds (HUD,
 *             Online...), each registered option drawn by its type; the registry's Display, Graphics and Performance
 *             options are drawn at the end of VIDEO, Audio at the end of AUDIO, Spy, Mercenary and Controller at the end
 *             of CONTROLS
 *
 * Every change is in force at once (so its effect is seen and heard behind the page) and written when the page
 * closes, except the window mode and the resolution, which wait for APPLY and a confirmation.
 * Where the values live: Menu/ScctUserSettings.h, Play/ScctControlSettings.h, UGameUserSettings and the registry.
 */
class SScctSettingsPanel : public SScctMenuPage
{
public:
	SLATE_BEGIN_ARGS(SScctSettingsPanel) : _InitialTab(0) {}
		SLATE_ARGUMENT(int32, InitialTab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FScctMenuHost& InHost);

	virtual FString GetTitle() const override;
	virtual FString GetKicker() const override;
	virtual bool HasTabs() const override { return true; }
	virtual void StepTab(int32 Direction) override;
	virtual void ShowSub(const FString& Sub) override;
	virtual void OnClosing() override { Commit(); }
	virtual bool HandleBack() override;

	/** Writes everything to disk. */
	void Commit();

	/** "video", "audio", "controls", "bindings", "game", "profile"; -1 for none (the registry's tabs are this page's own). */
	static int32 FindTab(const FString& Name);

	/** The registry's pages drawn at the end of a fixed tab (0 video, 1 audio, 2 controls), or none. */
	static TArray<FName> RegistryPagesOfTab(int32 Tab);

private:
	void Rebuild();
	TSharedRef<SWidget> BuildVideo();
	TSharedRef<SWidget> BuildAudio();
	TSharedRef<SWidget> BuildControls();
	TSharedRef<SWidget> BuildBindings();
	TSharedRef<SWidget> BuildGame();
	TSharedRef<SWidget> BuildProfile();

	/** The registry's options of Page under their groups' headings, added to Rows; the first row made, if any. */
	TSharedPtr<SWidget> AddRegistryRows(const TSharedRef<SVerticalBox>& Rows, FName Page);
	TSharedRef<SWidget> BuildRegistryTab(FName Page);

	void ApplyDisplay();
	void ApplyQuality();
	void ApplyControls();
	void HandleCaptured(const FString& ActionId, const FString& Label, const FKey& Key);
	void Bind(const FString& ActionId, const FString& KeyName);
	void ResetTab();
	void Toast(const FString& Message, bool bBad = false);

	TSharedPtr<SScctTabBar> Tabs;

	/** The tabs' names ("video" ... "profile", then the registry's pages in lower case) and the registry's own pages. */
	TArray<FString> TabNames;
	TArray<FName> RegistryTabs;
	TSharedPtr<SBox> Body;

	// The window mode and the resolution wait here for APPLY.
	int32 PendingMode = 0;
	FIntPoint PendingSize = FIntPoint::ZeroValue;
	TArray<FIntPoint> Sizes;

	bool bBindMerc = false;
	bool bBindPad = false;
	bool bControlsDirty = false;
	bool bBindingsDirty = false;
};
