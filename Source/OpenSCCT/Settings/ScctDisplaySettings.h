#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctDisplaySettings.generated.h"

/**
 * The player's display, graphics and performance settings: registers the Display, Graphics and Performance pages'
 * options in the settings registry (Settings/ScctSettings.h), puts them into effect when the game starts, and again
 * as soon as one changes. The Graphics and Performance pages are in Settings/ScctGraphicsSettings.h and
 * Settings/ScctPerformanceSettings.h; the Display page is here:
 *
 *   Display.Mode            borderless window, fullscreen or windowed (UGameUserSettings::SetFullscreenMode)
 *   Display.Monitor         which monitor the game is on (UGameUserSettings::SetDisplayProperties)
 *   Display.Resolution      the window's size, or the fullscreen mode's (the nearest the monitor supports);
 *                           a borderless window is always the monitor's size
 *   Display.WindowPosition  where a window goes on its monitor: centred, top left, or filling the work area
 *   Display.Widescreen      the wider widescreen field of view, and
 *   Display.MercFovCap      the cap on the merc's horizontal field of view: both kept in the controls' own file
 *                           (UScctControlSettings, controls.json), so there is one value; the option writes
 *                           through to it and follows it at start
 *   Display.Brightness      the 3D picture's display gamma (2.2 times the value), with its test pattern
 *                           (Menu/SScctBrightnessPage) one button away on the page
 *
 * The window settings are applied at start only once the player has set one of them (until then the window is as
 * Unreal opened it, -ResX and -ForceRes included). A change of mode, resolution or monitor while playing asks to be
 * kept (Menu/SScctKeepDisplay) and goes back to the last kept display after 15 seconds without an answer. When kept,
 * UGameUserSettings is saved too, so the next start opens the window that way at once.
 *
 * In the editor (play in editor) the options are registered but nothing is applied: they would change the editor.
 *
 * Unattended switches:
 *   -ScctSettingSet=Display.Mode=2;Display.Resolution=1   force options for the run (Settings/ScctSettingsForced.h)
 *   -ScctDisplayChange=<Key>=<Value>[;...]                 five seconds after start, set options as the player would,
 *                                                          to see the keep-or-go-back question and its countdown
 *   -ScctPerfCheck                                         measure the frame rate (Settings/ScctPerformanceSettings.h)
 */
UCLASS()
class OPENSCCT_API UScctDisplaySettings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void RegisterDisplay();
	void RegisterSections();

	void HandleChanged(FName Key);
	bool Tick(float DeltaSeconds);

	/** The window: mode, monitor, size and place. */
	void ApplyWindow(bool bAtStart);
	/** Moves a window to its place on its monitor, once the resolution change has gone through. */
	void PlaceWindow();
	void ApplyBrightness();

	/** Display.Widescreen and Display.MercFovCap from and to the controls' file. */
	void SyncControlsFromFile();
	void WriteControls();

	/** The window settings as they are now, to go back to. */
	TMap<FName, FString> WindowSnapshot() const;
	void AskToKeep();
	void Keep();
	void GoBack();

	FDelegateHandle ChangedHandle;
	FTSTicker::FDelegateHandle TickHandle;

	/** Everything is applied on the first tick. */
	bool bStartPending = false;
	/** A setting changed: applied on the next tick, so the changes of one APPLY go in together. */
	bool bWindowDirty = false;
	bool bGraphicsDirty = false;
	bool bPerformanceDirty = false;
	/** The window was applied and its place is to be set at PlaceAt (seconds, FPlatformTime). */
	double PlaceAt = 0.0;
	/** Going back: the change that follows is not asked about. */
	bool bGoingBack = false;
	bool bSyncingControls = false;
	bool bApplies = false;

	/**
	 * The window settings last kept: their values, and how each was held (the text stored in the user settings file
	 * and the value forced for the run, either unset), so going back leaves both exactly as they were.
	 */
	TMap<FName, FString> Kept;
	TMap<FName, TOptional<FString>> KeptStored;
	TMap<FName, TOptional<FString>> KeptForced;
	void RememberKept();
	/** The deadline of the question about a change (0: none open). */
	double KeepDeadline = 0.0;

	/** -ScctDisplayChange: when to set its values (0: not asked, or done). */
	double ChangeAt = 0.0;
	FString ChangeSets;
};
