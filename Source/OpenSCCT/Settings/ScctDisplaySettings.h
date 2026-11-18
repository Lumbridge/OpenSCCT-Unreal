#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GenericPlatform/GenericWindow.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScctDisplaySettings.generated.h"
class UGameUserSettings;

/**
 * The display, graphics and performance options the settings page (Menu/SScctSettingsPanel.h) has no row of its own
 * for: registers them in the settings registry (Settings/ScctSettings.h), puts them into effect when the game starts,
 * and again as soon as one changes. The settings page draws them at the end of its Video tab.
 *
 * What is NOT here, because it has one home elsewhere: the window mode, the resolution, vertical sync, the frame
 * limit and the quality groups (UGameUserSettings, set by the settings page); the render scale, motion blur and the
 * brightness (UScctUserSettings); the widescreen field of view and the mercenary's cap (UScctControlSettings).
 *
 *   Display.Monitor         which monitor the game is on (UGameUserSettings::SetDisplayProperties); the mode and the
 *                           size are kept, a borderless window takes the monitor's size
 *   Display.WindowPosition  where a window goes on its monitor: centred, top left, or filling the work area
 *   Graphics.*              Settings/ScctGraphicsSettings.h: automatic render scale, anti-aliasing method, texture
 *                           filtering, ambient occlusion
 *   Performance.*           Settings/ScctPerformanceSettings.h: input lag, the renderer for the next start
 *
 * The monitor and the place are applied at start only once the player has set one of them. In the editor (play in
 * editor) the options are registered but nothing is applied: they would change the editor.
 *
 * Unattended switches:
 *   -ScctSettingSet=Display.Monitor=1                      force options for the run (Settings/ScctSettingsForced.h)
 *   -ScctDisplayChange=<Key>=<Value>[;...]                 five seconds after start, set options as the player would
 *   -ScctPerfCheck                                         measure the frame rate (Settings/ScctPerformanceSettings.h)
 */
UCLASS()
class OPENSCCT_API UScctDisplaySettings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Desktop pixels on the selected display (or the display containing the game window). */
	static FIntPoint DesktopSize(const UGameUserSettings& Settings);
	static TArray<FIntPoint> ResolutionChoices(const UGameUserSettings& Settings, EWindowMode::Type Mode, FIntPoint Current);
	/** Applies the pending engine settings, resolving borderless to the desktop before requesting a resize. */
	static void ApplyResolution(UGameUserSettings& Settings);

private:
	void RegisterDisplay();

	void HandleChanged(FName Key);
	bool Tick(float DeltaSeconds);

	/** Moves the game to the chosen monitor, keeping the window mode and size. */
	void ApplyMonitor(bool bAtStart);
	/** Moves a window to its place on its monitor, once the resolution change has gone through. */
	void PlaceWindow();

	FDelegateHandle ChangedHandle;
	FDelegateHandle UserSettingsHandle;
	FTSTicker::FDelegateHandle TickHandle;

	/** Everything is applied on the first tick. */
	bool bStartPending = false;
	/** A setting changed: applied on the next tick. */
	bool bMonitorDirty = false;
	bool bGraphicsDirty = false;
	bool bPerformanceDirty = false;
	/** When to place the window (seconds, FPlatformTime; 0: not asked). */
	double PlaceAt = 0.0;
	bool bApplies = false;

	/** -ScctDisplayChange: when to set its values (0: not asked, or done). */
	double ChangeAt = 0.0;
	FString ChangeSets;
};
