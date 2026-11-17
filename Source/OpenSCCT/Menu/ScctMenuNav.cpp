#include "Menu/ScctMenuNav.h"

#include "Containers/Ticker.h"
#include "Framework/Application/NavigationConfig.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Menu/SScctPadPanel.h"
#include "Menu/ScctMenuKit.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "UnrealClient.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const ScctPadNavSection = TEXT("OpenSCCT.Pad");
	const TCHAR* const ScctPadNavAskedKey = TEXT("AskedToEnableController");

	/** Seconds after start in which connection events are the controllers already plugged in, not a new one. */
	constexpr double ScctPadNavStartQuiet = 3.0;

	/**
	 * Slate's navigation with the Godot client's stick feel (PadMenuNavigation: a push past 0.65 moves, held it
	 * repeats after 0.4 s, then every 0.16 s). The D-pad and the arrows keep Slate's own key rules.
	 */
	class FScctMenuNavigationConfig : public FNavigationConfig
	{
	public:
		FScctMenuNavigationConfig()
		{
			AnalogNavigationHorizontalThreshold = 0.65f;
			AnalogNavigationVerticalThreshold = 0.65f;
		}

		virtual FString ToString() const override { return TEXT("OpenSCCT menus: ") + FNavigationConfig::ToString(); }

	protected:
		virtual float GetRepeatRateForPressure(float InPressure, int32 InRepeats) const override
		{
			return InRepeats == 0 ? 0.4f : 0.16f;
		}
	};

	struct FScctPadNavLayer
	{
		TWeakPtr<SScctDialogLayer> Layer;
		TFunction<bool()> CanAsk;
	};

	struct FScctPadNavState
	{
		bool bInstalled = false;
		double StartedAt = 0.0;
		TArray<FScctPadNavLayer> Layers;
		FDelegateHandle ConnectionHandle;
		FTSTicker::FDelegateHandle TickHandle;

		// The enable question.
		bool bPromptWanted = false;
		bool bPromptShown = false;

		// -ScctPadNavTest
		TArray<FString> Steps;
		int32 NextStep = 0;
		float StepClock = 0.0f;
		bool bReportPending = false;
		FString LastStep;
		FInputDeviceId FakeDevice;
		bool bTestRun = false;
		bool bAnalogRelease = false;
	};

	FScctPadNavState& PadNav()
	{
		static FScctPadNavState State;
		return State;
	}

	TSharedPtr<SScctDialogLayer> PadNavLayer(TFunction<bool()>* OutCanAsk = nullptr)
	{
		FScctPadNavState& State = PadNav();
		State.Layers.RemoveAll([](const FScctPadNavLayer& Entry) { return !Entry.Layer.IsValid(); });
		if (State.Layers.IsEmpty())
		{
			return nullptr;
		}
		if (OutCanAsk != nullptr)
		{
			*OutCanAsk = State.Layers.Last().CanAsk;
		}
		return State.Layers.Last().Layer.Pin();
	}

	bool PadNavWasAsked()
	{
		bool bAsked = false;
		GConfig->GetBool(ScctPadNavSection, ScctPadNavAskedKey, bAsked, GGameUserSettingsIni);
		return bAsked;
	}

	void PadNavNotice(const FString& Text)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNav: %s"), *Text);
		if (const TSharedPtr<SScctDialogLayer> Layer = PadNavLayer())
		{
			Layer->Toast(Text);
		}
	}

	void PadNavAnswer(bool bEnable)
	{
		FScctPadNavState& State = PadNav();
		State.bPromptWanted = false;
		UScctControlSettings* Controls = UScctControlSettings::Get();
		if (bEnable && Controls != nullptr && Controls->EnsureLoaded())
		{
			Controls->bPadEnabled = true;
			// A check run plays with the setting but leaves the player's controls.json as it was.
			const bool bSaved = State.bTestRun ? Controls->Apply() : Controls->Save();
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNav: controller support turned on from the question (%s)%s"),
				State.bTestRun ? TEXT("this run only") : TEXT("saved"), bSaved ? TEXT("") : *(TEXT(": ") + Controls->GetLastError()));
			PadNavNotice(TEXT("Controller support is on"));
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNav: controller support left off from the question"));
		}
		if (!State.bTestRun)
		{
			GConfig->SetBool(ScctPadNavSection, ScctPadNavAskedKey, true, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}

	bool PadNavSupportOn()
	{
		UScctControlSettings* Controls = UScctControlSettings::Get();
		return Controls == nullptr || !Controls->EnsureLoaded() || Controls->bPadEnabled;
	}

	void PadNavTryPrompt()
	{
		FScctPadNavState& State = PadNav();
		if (!State.bPromptWanted || State.bPromptShown)
		{
			return;
		}
		// Smoke runs never ask (as the Godot client's isolated runs); the check switch does.
		if ((FApp::IsUnattended() && !State.bTestRun) || (!State.bTestRun && PadNavWasAsked()) || PadNavSupportOn())
		{
			State.bPromptWanted = false;
			return;
		}
		TFunction<bool()> CanAsk;
		const TSharedPtr<SScctDialogLayer> Layer = PadNavLayer(&CanAsk);
		if (!Layer.IsValid() || Layer->IsOpen() || (CanAsk && !CanAsk()))
		{
			return; // asked once a menu can take it
		}
		State.bPromptShown = true;
		FScctDialog Dialog;
		Dialog.Title = TEXT("Controller");
		Dialog.Body = TEXT("Would you like to enable native controller support?");
		FScctDialogButton& Yes = Dialog.Buttons.AddDefaulted_GetRef();
		Yes.Label = TEXT("Yes");
		Yes.Look = EScctButtonLook::Primary;
		Yes.OnPressed = [](const FString&) { PadNavAnswer(true); };
		FScctDialogButton& No = Dialog.Buttons.AddDefaulted_GetRef();
		No.Label = TEXT("No");
		No.OnPressed = [](const FString&) { PadNavAnswer(false); };
		Dialog.FocusButton = 0;
		Dialog.CancelButton = 1;
		Layer->Show(Dialog);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNav: asking whether to turn controller support on"));
	}

	void PadNavConnectionChanged(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device)
	{
		IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
		if (Device == Mapper.GetDefaultInputDevice())
		{
			return; // the keyboard and mouse
		}
		const bool bConnected = NewState == EInputDeviceConnectionState::Connected;
		if (!bConnected && NewState != EInputDeviceConnectionState::Disconnected)
		{
			return;
		}
		FScctPadNavState& State = PadNav();
		const bool bAtStart = FPlatformTime::Seconds() - State.StartedAt < ScctPadNavStartQuiet && Device != State.FakeDevice;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNav: input device %d %s (user %d)%s"), Device.GetId(), bConnected ? TEXT("connected") : TEXT("disconnected"),
			User.GetInternalId(), bAtStart ? TEXT(", plugged in before the game started") : TEXT(""));
		if (!bAtStart)
		{
			PadNavNotice(ScctMenuNav::ConnectionText(bConnected));
		}
		if (bConnected)
		{
			State.bPromptWanted = true;
			State.bPromptShown = false;
		}
	}

	FKey PadNavKeyOf(const FString& Step)
	{
		static const TMap<FString, FKey> Keys = {
			{ TEXT("up"), EKeys::Gamepad_DPad_Up }, { TEXT("down"), EKeys::Gamepad_DPad_Down },
			{ TEXT("left"), EKeys::Gamepad_DPad_Left }, { TEXT("right"), EKeys::Gamepad_DPad_Right },
			{ TEXT("a"), EKeys::Gamepad_FaceButton_Bottom }, { TEXT("b"), EKeys::Gamepad_FaceButton_Right },
			{ TEXT("x"), EKeys::Gamepad_FaceButton_Left }, { TEXT("y"), EKeys::Gamepad_FaceButton_Top },
			{ TEXT("start"), EKeys::Gamepad_Special_Right }, { TEXT("back"), EKeys::Gamepad_Special_Left },
			{ TEXT("lb"), EKeys::Gamepad_LeftShoulder }, { TEXT("rb"), EKeys::Gamepad_RightShoulder },
		};
		const FKey* Found = Keys.Find(Step.ToLower());
		return Found != nullptr ? *Found : FKey();
	}

	void PadNavRunStep(const FString& Step)
	{
		FScctPadNavState& State = PadNav();
		FSlateApplication& Slate = FSlateApplication::Get();
		const FKey Key = PadNavKeyOf(Step);
		if (Key.IsValid())
		{
			const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
			Slate.ProcessKeyDownEvent(Event);
			Slate.ProcessKeyUpEvent(Event);
			return;
		}
		if (Step.Equals(TEXT("StickUp"), ESearchCase::IgnoreCase) || Step.Equals(TEXT("StickDown"), ESearchCase::IgnoreCase))
		{
			// Unreal's left stick Y is up positive. A full push now, let go on the next frame.
			const float Value = Step.Equals(TEXT("StickUp"), ESearchCase::IgnoreCase) ? 1.0f : -1.0f;
			Slate.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, Value));
			State.bAnalogRelease = true;
			return;
		}
		IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
		if (Step.Equals(TEXT("Connect"), ESearchCase::IgnoreCase))
		{
			if (!State.FakeDevice.IsValid())
			{
				State.FakeDevice = Mapper.AllocateNewInputDeviceId();
			}
			Mapper.Internal_MapInputDeviceToUser(State.FakeDevice, Mapper.GetPrimaryPlatformUser(), EInputDeviceConnectionState::Connected);
		}
		else if (Step.Equals(TEXT("Disconnect"), ESearchCase::IgnoreCase))
		{
			if (State.FakeDevice.IsValid())
			{
				Mapper.Internal_MapInputDeviceToUser(State.FakeDevice, Mapper.GetPrimaryPlatformUser(), EInputDeviceConnectionState::Disconnected);
			}
		}
		else if (Step.Equals(TEXT("PadOff"), ESearchCase::IgnoreCase))
		{
			if (UScctControlSettings* Controls = UScctControlSettings::Get(); Controls != nullptr && Controls->EnsureLoaded())
			{
				Controls->bPadEnabled = false;
				Controls->Apply();
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNavTest: controller support off for this run (not saved)"));
			}
		}
		else if (Step.Equals(TEXT("Quit"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNavTest: done"));
			FPlatformMisc::RequestExit(false);
		}
		else if (!Step.Equals(TEXT("Wait"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctPadNavTest: unknown step '%s'"), *Step);
		}
	}

	void PadNavTickTest(float DeltaSeconds)
	{
		FScctPadNavState& State = PadNav();
		if (State.bAnalogRelease)
		{
			State.bAnalogRelease = false;
			FSlateApplication::Get().ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.0f));
		}
		if (State.NextStep >= State.Steps.Num() && !State.bReportPending)
		{
			return;
		}
		// The menu must be up (a layer registered) and settled before the first press.
		if (PadNavLayer() == nullptr)
		{
			return;
		}
		State.StepClock += DeltaSeconds;
		if (State.bReportPending)
		{
			if (State.StepClock < 0.45f)
			{
				return;
			}
			State.bReportPending = false;
			const int32 Index = State.NextStep;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNavTest: after %d '%s' the focus is on %s"), Index, *State.LastStep, *ScctMenuNav::DescribeFocus());
			FString Directory = FPaths::ProjectSavedDir() / TEXT("ScctShots");
			FParse::Value(FCommandLine::Get(), TEXT("ScctShotDir="), Directory);
			FScreenshotRequest::RequestScreenshot(Directory / FString::Printf(TEXT("padnav_%d_%s.png"), Index, *State.LastStep.ToLower()), true, false);
			return;
		}
		const float Gap = State.NextStep == 0 ? 3.0f : 0.9f;
		if (State.StepClock < Gap)
		{
			return;
		}
		State.StepClock = 0.0f;
		State.LastStep = State.Steps[State.NextStep++];
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNavTest: step %d '%s' (focus was on %s)"), State.NextStep, *State.LastStep, *ScctMenuNav::DescribeFocus());
		PadNavRunStep(State.LastStep);
		State.bReportPending = !State.LastStep.Equals(TEXT("Quit"), ESearchCase::IgnoreCase);
	}

	bool PadNavTick(float DeltaSeconds)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return true;
		}
		PadNavTryPrompt();
		PadNavTickTest(FMath::Min(DeltaSeconds, 0.1f));
		return true;
	}
}

namespace ScctMenuNav
{
	bool IsBackKey(const FKey& Key)
	{
		return Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Left || Key == EKeys::Gamepad_Special_Right;
	}

	int32 TabDirection(const FKey& Key)
	{
		if (Key == EKeys::Q || Key == EKeys::PageUp || Key == EKeys::Gamepad_LeftShoulder) { return -1; }
		if (Key == EKeys::E || Key == EKeys::PageDown || Key == EKeys::Gamepad_RightShoulder) { return 1; }
		return 0;
	}

	const TArray<FKey>& PadCloseKeys()
	{
		static const TArray<FKey> Keys = { EKeys::Gamepad_Special_Right, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_Special_Left };
		return Keys;
	}

	TSharedPtr<SWidget> FindFirstFocusable(const TSharedPtr<SWidget>& Root)
	{
		// A text field is chosen only when nothing else is there: started in one, the keys would type, not move.
		TFunction<TSharedPtr<SWidget>(const TSharedPtr<SWidget>&, bool)> Walk = [&Walk](const TSharedPtr<SWidget>& Widget, bool bTextToo) -> TSharedPtr<SWidget>
		{
			if (!Widget.IsValid() || !Widget->GetVisibility().IsVisible() || !Widget->IsEnabled())
			{
				return nullptr;
			}
			if (Widget->SupportsKeyboardFocus())
			{
				const FName Type = Widget->GetType();
				const bool bText = Type == TEXT("SEditableText") || Type == TEXT("SMultiLineEditableText");
				if (!bText || bTextToo)
				{
					return Widget;
				}
			}
			FChildren* Children = Widget->GetChildren();
			for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
			{
				if (TSharedPtr<SWidget> Found = Walk(Children->GetChildAt(Index), bTextToo))
				{
					return Found;
				}
			}
			return nullptr;
		};
		const TSharedPtr<SWidget> Found = Walk(Root, false);
		return Found.IsValid() ? Found : Walk(Root, true);
	}

	void Install()
	{
		FScctPadNavState& State = PadNav();
		if (State.bInstalled || !FSlateApplication::IsInitialized())
		{
			return;
		}
		State.bInstalled = true;
		State.StartedAt = FPlatformTime::Seconds();
		FSlateApplication::Get().SetNavigationConfig(MakeShared<FScctMenuNavigationConfig>());
		State.ConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddStatic(&PadNavConnectionChanged);
		State.TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&PadNavTick), 0.0f);

		FString Script;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctPadNavTest="), Script, false))
		{
			Script.ParseIntoArray(State.Steps, TEXT(","));
			for (FString& Step : State.Steps)
			{
				Step.TrimStartAndEndInline();
			}
			State.bTestRun = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNavTest: %d step(s): %s"), State.Steps.Num(), *Script);
		}

		// A controller already plugged in at start gets the question too (no connection event comes for it).
		const TArray<FString> Pads = SScctPadPanel::ConnectedControllers();
		State.bPromptWanted = Pads.Num() > 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadNav: controller menu navigation on (D-pad and left stick move, A presses, B / Back / Start go back); %d controller(s) at start"), Pads.Num());
	}

	void RegisterLayer(const TSharedRef<SScctDialogLayer>& Layer, TFunction<bool()> CanAsk)
	{
		Install();
		FScctPadNavState& State = PadNav();
		State.Layers.RemoveAll([&Layer](const FScctPadNavLayer& Entry) { return !Entry.Layer.IsValid() || Entry.Layer == Layer; });
		State.Layers.Add({ Layer, MoveTemp(CanAsk) });
	}

	FString ConnectionText(bool bConnected)
	{
		return bConnected ? TEXT("Gamepad connected") : TEXT("Gamepad disconnected");
	}

	FString DescribeFocus()
	{
		if (!FSlateApplication::IsInitialized())
		{
			return TEXT("nothing");
		}
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!Focused.IsValid())
		{
			return TEXT("nothing");
		}
		const IScctDriven* Driven = IScctDriven::FindByWidget(Focused.Get());
		if (Driven != nullptr)
		{
			return FString::Printf(TEXT("%s '%s'"), *Focused->GetTypeAsString(), *Driven->GetDrivenLabel());
		}
		// A plain button: its first line of text.
		TFunction<FString(const TSharedRef<SWidget>&)> FirstText = [&FirstText](const TSharedRef<SWidget>& Widget) -> FString
		{
			if (Widget->GetType() == TEXT("STextBlock"))
			{
				return StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
			}
			FChildren* Children = Widget->GetChildren();
			for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
			{
				const FString Found = FirstText(Children->GetChildAt(Index));
				if (!Found.IsEmpty())
				{
					return Found;
				}
			}
			return FString();
		};
		const FString Text = Focused->GetTypeAsString().Contains(TEXT("Button")) ? FirstText(Focused.ToSharedRef()) : FString();
		return Text.IsEmpty() ? Focused->GetTypeAsString() : FString::Printf(TEXT("%s '%s'"), *Focused->GetTypeAsString(), *Text.Replace(TEXT("\n"), TEXT(" ")));
	}
}
