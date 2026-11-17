#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuKit.h"
#include "Menu/ScctMenuPage.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UScctControlSettings;

/**
 * The settings page's CONTROLLER tab (Menu/SScctSettingsPanel.h), as the Godot client's Controller page and its
 * sides' controller bindings (OptionsMenu PadPage, OptionsMenu.ControllerTuning, Options/PadBindingCapture):
 *
 *   DEVICE      the controller on or off, and the controllers Windows has connected (XInput slots 1 to 4)
 *   STICKS      stick movement, stick aim and the buttons on or off, the dead zones, the trigger threshold
 *   MOVEMENT    the up and down multiplier, speed follows the stick's tilt, the spy's run point, eight-way movement
 *               per side, the look speed per side and the overall turning speed
 *   VIBRATION   on or off, the strength, and TEST (the blast's rumble on the controller in use)
 *   BUTTONS     what every action of a side is bound to on the controller, and capture of a new binding: a button,
 *               Hold B, or a combination (LS or LB with a face button and the others the bindings know), with the
 *               conflict warning of the key bindings; reset a side
 *
 * Every value is UScctControlSettings' (Play/ScctControlSettings.h), the one copy; the panel only edits it and says
 * so through OnControlsChanged and OnBindingsChanged, and the settings page writes the files when it closes.
 *
 * -ScctPadBindTest=<spy|merc>,<action id>,<gesture>[;...] (gesture: "A", "LS+Y", "B:Hold") captures each through
 * the real capture row, by controller key events sent to Slate as a controller sends them, binds it, writes the
 * bindings and logs "ScctPadBind: ..." with what the bindings file now holds.
 */
class SScctPadPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctPadPanel) : _bMerc(false) {}
		SLATE_ARGUMENT(bool, bMerc)
		/** A setting of UScctControlSettings changed (the page applies and later saves it). */
		SLATE_ARGUMENT(TFunction<void()>, OnControlsChanged)
		/** A binding changed (the page saves the bindings when it closes). */
		SLATE_ARGUMENT(TFunction<void()>, OnBindingsChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FScctMenuHost& InHost);

	/** The row that takes the focus when the tab opens. */
	TSharedPtr<SWidget> GetFirstRow() const { return FirstRow; }

	/** The controllers Windows has connected now, as the DEVICE section lists them ("Controller 1 (gamepad)"). */
	static TArray<FString> ConnectedControllers();

private:
	TSharedRef<SWidget> BuildSettings();
	void RebuildButtons();
	void HandlePadCaptured(const FString& ActionId, const FString& Label, const FString& Gesture);
	void BindPad(const FString& ActionId, const FString& Gesture);
	void Toast(const FString& Message, bool bBad = false);
	void Changed();

	EActiveTimerReturnType TickPadBindTest(double InCurrentTime, float InDeltaTime);

	FScctMenuHost Host;
	TFunction<void()> OnControlsChanged;
	TFunction<void()> OnBindingsChanged;
	bool bMerc = false;
	TSharedPtr<SWidget> FirstRow;
	TSharedPtr<SVerticalBox> ButtonRows;

	/** The capture rows by action id, for the unattended check. */
	TMap<FString, TWeakPtr<class SScctPadBindRow>> RowsByAction;

	// -ScctPadBindTest
	struct FPadBindStep
	{
		bool bMerc = false;
		FString Action;
		FString Gesture;
	};
	TArray<FPadBindStep> TestSteps;
	int32 TestStep = 0;
	int32 TestPhase = 0;
	float TestWait = 0.0f;
	bool bTestBindDirect = false;
};

/**
 * One action's controller binding: its buttons as key caps, and on a press the capture of a new gesture. Controller
 * buttons are read from Slate's key events while it captures, as the controller sends them; the bridge's capture
 * (Bridge/.../Controls/PadCapture.cs) decides what was meant once a control is let go. Escape gives up (B and Start
 * are bindable); Delete or Backspace clears.
 */
class SScctPadBindRow : public SScctOptionRow
{
public:
	SLATE_BEGIN_ARGS(SScctPadBindRow) : _bMerc(false) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FText, Description)
		SLATE_ARGUMENT(bool, bMerc)
		/** The controls bound now, LF between them ("LS+Y\nB:Hold"); empty when unbound. */
		SLATE_ARGUMENT(TFunction<FString()>, GetControls)
		/** A gesture was captured ("A", "LS+Y", "B:Hold"); empty means "clear". */
		SLATE_ARGUMENT(TFunction<void(const FString&)>, OnCaptured)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SScctPadBindRow() override;

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

	bool IsCapturing() const { return bCapturing; }

	/** The binding name of a controller key ("A", "LB", "DpadUp", "LT"), empty for any other key. */
	static FString ControlOf(const FKey& Key);
	/** The controller key of a binding name, invalid for none. */
	static FKey KeyOf(const FString& Control);
	/** "LS+Y" as the rows show it: "LS + Y"; "B:Hold": "HOLD B". */
	static FString ShowControl(const FString& Control);

protected:
	virtual void Press() override;

private:
	void Finish(const FString& Gesture, bool bCommit);
	EActiveTimerReturnType TickCapture(double InCurrentTime, float InDeltaTime);
	FText GetShownText() const;
	FSlateColor GetShownColour() const;

	TFunction<FString()> GetControls;
	TFunction<void(const FString&)> OnCaptured;
	bool bMerc = false;
	bool bCapturing = false;
	TSet<FString> Held;
	FString Preview;
	FString Refusal;
	float CaptureSeconds = 0.0f;
	TSharedPtr<FActiveTimerHandle> CaptureTimer;
};
