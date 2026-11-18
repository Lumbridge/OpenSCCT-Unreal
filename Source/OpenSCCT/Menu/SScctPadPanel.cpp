#include "Menu/SScctPadPanel.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Play/ScctRumble.h"
#include "Runtime/ScctFeatures.h"
#include "Runtime/ScctControlsApi.h"
#include "Runtime/ScctMenuApi.h"
#include "Runtime/ScctPadApi.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Xinput.h>
#include "Windows/HideWindowsPlatformTypes.h"
// The Windows headers rename LoadImage to LoadImageW, which breaks FImageUtils::LoadImage in a file that shares this
// one's unity build (the strategic map's pictures).
#ifdef LoadImage
#undef LoadImage
#undef DialogBox
#endif
#endif

namespace
{
	/** Reads one of the bridge's texts ("copy what fits, return the full length"). */
	template <typename TRead>
	FString PadPanelReadText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(256);
		const int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return FString(Buffer.GetData());
	}

	struct FPadPanelKeyName
	{
		FKey Key;
		const TCHAR* Name;
	};

	/** The controller's buttons and triggers as the bindings name them (ControlBindings.PadControls, Xbox names). */
	const TArray<FPadPanelKeyName>& PadPanelKeyNames()
	{
		static const TArray<FPadPanelKeyName> Names = {
			{ EKeys::Gamepad_FaceButton_Bottom, TEXT("A") }, { EKeys::Gamepad_FaceButton_Right, TEXT("B") },
			{ EKeys::Gamepad_FaceButton_Left, TEXT("X") }, { EKeys::Gamepad_FaceButton_Top, TEXT("Y") },
			{ EKeys::Gamepad_LeftShoulder, TEXT("LB") }, { EKeys::Gamepad_RightShoulder, TEXT("RB") },
			{ EKeys::Gamepad_LeftTrigger, TEXT("LT") }, { EKeys::Gamepad_RightTrigger, TEXT("RT") },
			{ EKeys::Gamepad_LeftThumbstick, TEXT("LS") }, { EKeys::Gamepad_RightThumbstick, TEXT("RS") },
			{ EKeys::Gamepad_DPad_Up, TEXT("DpadUp") }, { EKeys::Gamepad_DPad_Down, TEXT("DpadDown") },
			{ EKeys::Gamepad_DPad_Left, TEXT("DpadLeft") }, { EKeys::Gamepad_DPad_Right, TEXT("DpadRight") },
			{ EKeys::Gamepad_Special_Right, TEXT("Start") }, { EKeys::Gamepad_Special_Left, TEXT("Back") },
			{ ScctPaddleKey(0), TEXT("P1") }, { ScctPaddleKey(1), TEXT("P2") },
			{ ScctPaddleKey(2), TEXT("P3") }, { ScctPaddleKey(3), TEXT("P4") },
		};
		return Names;
	}

	FString PadPanelPercent(float Value) { return FString::Printf(TEXT("%d %%"), FMath::RoundToInt32(Value)); }
	FString PadPanelTimes(float Value) { return FString::Printf(TEXT("%.2f x"), Value); }
}

// ---- SScctPadBindRow ------------------------------------------------------------------------------------------------

FString SScctPadBindRow::ControlOf(const FKey& Key)
{
	for (const FPadPanelKeyName& Name : PadPanelKeyNames())
	{
		if (Name.Key == Key)
		{
			return Name.Name;
		}
	}
	return FString();
}

FKey SScctPadBindRow::KeyOf(const FString& Control)
{
	for (const FPadPanelKeyName& Name : PadPanelKeyNames())
	{
		if (Control.Equals(Name.Name, ESearchCase::IgnoreCase))
		{
			return Name.Key;
		}
	}
	return FKey();
}

FString SScctPadBindRow::ShowControl(const FString& Control)
{
	FString Base = Control;
	if (Base.EndsWith(TEXT(":Hold")))
	{
		return TEXT("HOLD ") + ShowControl(Base.LeftChop(5));
	}
	if (Base.Contains(TEXT("+")))
	{
		return Base.Replace(TEXT("+"), TEXT(" + "));
	}
	if (Base.StartsWith(TEXT("Dpad")))
	{
		return TEXT("D-PAD ") + Base.Mid(4).ToUpper();
	}
	if (Base.Len() == 2 && Base[0] == TEXT('P') && FChar::IsDigit(Base[1]))
	{
		return TEXT("PADDLE ") + Base.Mid(1);
	}
	return Base.ToUpper();
}

void SScctPadBindRow::Construct(const FArguments& InArgs)
{
	GetControls = InArgs._GetControls;
	OnCaptured = InArgs._OnCaptured;
	bMerc = InArgs._bMerc;
	BuildRow(InArgs._Label, InArgs._Description,
		SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(ScctUi::Caps(12.0f))
			.Text(this, &SScctPadBindRow::GetShownText)
			.ColorAndOpacity(this, &SScctPadBindRow::GetShownColour)
		]);
}

SScctPadBindRow::~SScctPadBindRow()
{
}

FText SScctPadBindRow::GetShownText() const
{
	if (bCapturing)
	{
		if (!Refusal.IsEmpty())
		{
			return FText::FromString(TEXT("NOT A BINDING · LET GO"));
		}
		return FText::FromString(Preview.IsEmpty() ? FString(TEXT("PRESS A BUTTON · ESC CANCELS")) : ShowControl(Preview) + TEXT(" …"));
	}
	TArray<FString> Controls;
	(GetControls ? GetControls() : FString()).ParseIntoArray(Controls, TEXT("\n"));
	if (Controls.IsEmpty())
	{
		return FText::FromString(TEXT("NOT BOUND"));
	}
	TArray<FString> Shown;
	for (const FString& Control : Controls)
	{
		Shown.Add(ShowControl(Control));
	}
	return FText::FromString(FString::Join(Shown, TEXT("   /   ")));
}

FSlateColor SScctPadBindRow::GetShownColour() const
{
	if (bCapturing)
	{
		return FSlateColor(Refusal.IsEmpty() ? ScctUi::Accent() : ScctHudStyle::Danger());
	}
	return FSlateColor((GetControls && !GetControls().IsEmpty()) ? ScctHudStyle::Text() : ScctHudStyle::TextFaint());
}

void SScctPadBindRow::Press()
{
	const FScctPadApi* Pad = ScctFeature<FScctPadApi>();
	if (Pad == nullptr || bCapturing)
	{
		return;
	}
	Pad->CaptureBegin();
	bCapturing = true;
	Held.Reset();
	Preview.Reset();
	Refusal.Reset();
	CaptureSeconds = 0.0f;
	LastCaptureSample = FPlatformTime::Seconds();
	ScctUi::Describe(Caption, FText::FromString(TEXT("Press the button, hold B, or press a combination (left stick or left bumper with a face button). It is taken when you let go. Escape gives up.")));
	CaptureTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctPadBindRow::TickCapture));
}

void SScctPadBindRow::Finish(const FString& Gesture, bool bCommit)
{
	bCapturing = false;
	Held.Reset();
	Preview.Reset();
	Refusal.Reset();
	if (CaptureTimer.IsValid())
	{
		UnRegisterActiveTimer(CaptureTimer.ToSharedRef());
		CaptureTimer.Reset();
	}
	ScctUi::Describe(Caption, DescriptionText);
	ScctUi::Play(bCommit ? EScctUiSound::Accept : EScctUiSound::Back);
	if (bCommit && OnCaptured)
	{
		OnCaptured(Gesture);
	}
}

EActiveTimerReturnType SScctPadBindRow::TickCapture(double InCurrentTime, float InDeltaTime)
{
	const FScctPadApi* Pad = ScctFeature<FScctPadApi>();
	if (!bCapturing || Pad == nullptr)
	{
		CaptureTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	CaptureSeconds += InDeltaTime;
	if (CaptureSeconds > 15.0f)
	{
		// Nobody is pressing anything: give up rather than hold the focus for ever.
		CaptureTimer.Reset();
		Finish(FString(), false);
		return EActiveTimerReturnType::Stop;
	}

	const FString Gesture = ReadGesture(*Pad);
	if (!Gesture.IsEmpty())
	{
		CaptureTimer.Reset();
		Finish(Gesture, true);
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

FString SScctPadBindRow::ReadGesture(const FScctPadApi& Pad)
{
	const double Now = FPlatformTime::Seconds();
	const float Delta = static_cast<float>(Now - LastCaptureSample);
	LastCaptureSample = Now;
	const FString Down = FString::Join(Held.Array(), TEXT("\n"));
	int32 State = EScctPadCapture::Releasing;
	const FString Text = PadPanelReadText([&](TCHAR* Buffer, int32 Capacity) { return Pad.CaptureUpdate(*Down, Delta, Buffer, Capacity, &State); });
	switch (State)
	{
	case EScctPadCapture::Captured:
		return Text;
	case EScctPadCapture::Refused:
		Refusal = Text;
		Preview.Reset();
		ScctUi::Describe(Caption, FText::FromString(Text));
		break;
	case EScctPadCapture::Listening:
		Refusal.Reset();
		Preview = Text;
		break;
	default:
		break;
	}
	return {};
}

void SScctPadBindRow::CaptureEdge()
{
	// A press and release can both reach Slate between two active-timer callbacks.
	if (const FScctPadApi* Pad = ScctFeature<FScctPadApi>())
	{
		const FString Gesture = ReadGesture(*Pad);
		if (!Gesture.IsEmpty()) Finish(Gesture, true);
	}
}

FReply SScctPadBindRow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (!bCapturing)
	{
		if (Key == EKeys::Delete || Key == EKeys::BackSpace)
		{
			Finish(FString(), true);
			return FReply::Handled();
		}
		const FReply Reply = SScctOptionRow::OnKeyDown(MyGeometry, InKeyEvent);
		if (bCapturing && Key == EKeys::Gamepad_FaceButton_Bottom) Held.Add(TEXT("A"));
		return Reply;
	}

	ScctUi::NoteInput(Key);
	if (Key == EKeys::Escape)
	{
		Finish(FString(), false);
		return FReply::Handled();
	}
	FString Control = ControlOf(Key);
	if (!Control.IsEmpty() && !InKeyEvent.IsRepeat())
	{
		if (Held.IsEmpty()) CaptureEdge();
		// The bindings name logical controls: with a side's stick click and bumper swapped, the physical one stands
		// for the other (LookSettings.PhysicalControl).
		const UScctControlSettings* Controls = UScctControlSettings::Get();
		const FScctLookOptions& Look = bMerc ? Controls->MercPad : Controls->SpyPad;
		if (Look.bSwapLsLb && (Control == TEXT("LS") || Control == TEXT("LB")))
		{
			Control = Control == TEXT("LS") ? TEXT("LB") : TEXT("LS");
		}
		if (Look.bSwapRsRb && (Control == TEXT("RS") || Control == TEXT("RB")))
		{
			Control = Control == TEXT("RS") ? TEXT("RB") : TEXT("RS");
		}
		Held.Add(Control);
		CaptureEdge();
	}
	// Everything else, including the controller's B and the shoulders the page would take, belongs to the capture.
	return FReply::Handled();
}

FReply SScctPadBindRow::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bCapturing)
	{
		return SScctOptionRow::OnKeyUp(MyGeometry, InKeyEvent);
	}
	FString Control = ControlOf(InKeyEvent.GetKey());
	if (!Control.IsEmpty())
	{
		// Released under either name: a swap may have renamed it on the way down.
		Held.Remove(Control);
		if (Control == TEXT("LS") || Control == TEXT("LB")) { Held.Remove(Control == TEXT("LS") ? TEXT("LB") : TEXT("LS")); }
		if (Control == TEXT("RS") || Control == TEXT("RB")) { Held.Remove(Control == TEXT("RS") ? TEXT("RB") : TEXT("RS")); }
		CaptureEdge();
	}
	return FReply::Handled();
}

FReply SScctPadBindRow::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	// A stick moved while capturing must not move the focus away (sticks cannot be bound).
	return bCapturing ? FReply::Handled() : SScctOptionRow::OnAnalogValueChanged(MyGeometry, InAnalogInputEvent);
}

FNavigationReply SScctPadBindRow::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	return bCapturing ? FNavigationReply::Stop() : SScctOptionRow::OnNavigation(MyGeometry, InNavigationEvent);
}

void SScctPadBindRow::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	if (bCapturing)
	{
		Finish(FString(), false);
	}
	SScctOptionRow::OnFocusLost(InFocusEvent);
}

// ---- SScctPadPanel --------------------------------------------------------------------------------------------------

TArray<FString> SScctPadPanel::ConnectedControllers()
{
	TArray<FString> Out;
#if PLATFORM_WINDOWS
	// Unreal plays XInput controllers on Windows (the XInputDevice plugin); the slots are asked directly, which works
	// before a controller has been touched. The DLL is the one the plugin itself loads.
	using FGetCapabilities = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
	static FGetCapabilities GetCapabilities = []() -> FGetCapabilities
	{
		for (const TCHAR* Dll : { TEXT("xinput1_4.dll"), TEXT("xinput1_3.dll"), TEXT("xinput9_1_0.dll") })
		{
			if (HMODULE Module = ::LoadLibraryW(Dll))
			{
				return reinterpret_cast<FGetCapabilities>(reinterpret_cast<void*>(::GetProcAddress(Module, "XInputGetCapabilities")));
			}
		}
		return nullptr;
	}();
	for (DWORD Slot = 0; GetCapabilities != nullptr && Slot < XUSER_MAX_COUNT; ++Slot)
	{
		XINPUT_CAPABILITIES Caps = {};
		if (GetCapabilities(Slot, 0, &Caps) != ERROR_SUCCESS)
		{
			continue;
		}
		const TCHAR* Kind = TEXT("controller");
		switch (Caps.SubType)
		{
		case XINPUT_DEVSUBTYPE_GAMEPAD: Kind = TEXT("gamepad"); break;
		case XINPUT_DEVSUBTYPE_WHEEL: Kind = TEXT("wheel"); break;
		case XINPUT_DEVSUBTYPE_ARCADE_STICK: Kind = TEXT("arcade stick"); break;
		case XINPUT_DEVSUBTYPE_FLIGHT_STICK: Kind = TEXT("flight stick"); break;
		default: break;
		}
		const bool bRumble = Caps.Vibration.wLeftMotorSpeed != 0 || Caps.Vibration.wRightMotorSpeed != 0;
		Out.Add(FString::Printf(TEXT("Controller %u (%s%s)"), Slot + 1, Kind, bRumble ? TEXT(", vibrates") : TEXT("")));
	}
#endif
	return Out;
}

void SScctPadPanel::Construct(const FArguments& InArgs, const FScctMenuHost& InHost)
{
	Host = InHost;
	OnControlsChanged = InArgs._OnControlsChanged;
	OnBindingsChanged = InArgs._OnBindingsChanged;
	bMerc = InArgs._bMerc;

	// -ScctPadBindTest="spy,slot1,LS+Y;merc,jump,B:Hold"
	FString Script;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctPadBindTest="), Script, false))
	{
		TArray<FString> Items;
		Script.ParseIntoArray(Items, TEXT(";"));
		for (const FString& Item : Items)
		{
			TArray<FString> Parts;
			Item.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 3)
			{
				TestSteps.Add({ Parts[0].TrimStartAndEnd().Equals(TEXT("merc"), ESearchCase::IgnoreCase), Parts[1].TrimStartAndEnd(), Parts[2].TrimStartAndEnd() });
			}
		}
		if (!TestSteps.IsEmpty())
		{
			bMerc = TestSteps[0].bMerc;
			bTestBindDirect = true;
			TestWait = 1.5f;
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctPadPanel::TickPadBindTest));
		}
	}

	TArray<FString> Pads = ConnectedControllers();
	TArray<FInputDeviceId> Devices;
	IPlatformInputDeviceMapper::Get().GetAllConnectedInputDevices(Devices);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadPanel: %d controller(s) connected (%s); Unreal's input device mapper has %d connected device(s)"),
		Pads.Num(), Pads.IsEmpty() ? TEXT("none") : *FString::Join(Pads, TEXT(", ")), Devices.Num());

	ChildSlot[ BuildSettings() ];
	RebuildButtons();
}

void SScctPadPanel::Changed()
{
	if (OnControlsChanged)
	{
		OnControlsChanged();
	}
}

void SScctPadPanel::Toast(const FString& Message, bool bBad)
{
	if (Host.Dialogs.IsValid())
	{
		Host.Dialogs->Toast(Message, bBad ? ScctHudStyle::Danger() : ScctUi::Accent());
	}
}

TSharedRef<SWidget> SScctPadPanel::BuildSettings()
{
	UScctControlSettings* Controls = UScctControlSettings::Get();
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](const TSharedRef<SWidget>& Row) { Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ Row ]; };
	const auto Switch = [this, &Add](const FString& Caption, const TCHAR* Description, bool* Value) -> TSharedRef<SWidget>
	{
		const TSharedRef<SWidget> Row = SNew(SScctToggle)
			.Label(FText::FromString(Caption))
			.Description(FText::FromString(Description))
			.Get([Value] { return *Value; })
			.Set([this, Value](bool bValue) { *Value = bValue; Changed(); });
		Add(Row);
		return Row;
	};
	const auto Slider = [this, &Add](const FString& Caption, const TCHAR* Description, float* Value, float Min, float Max, float Step, TFunction<FString(float)> Format)
	{
		Add(SNew(SScctSlider)
			.Label(FText::FromString(Caption))
			.Description(FText::FromString(Description))
			.Min(Min).Max(Max).Step(Step)
			.Get([Value] { return *Value; })
			.Set([this, Value](float NewValue) { *Value = NewValue; Changed(); })
			.Format(Format));
	};

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Device")) ];
	FirstRow = Switch(TEXT("Enable controller"), TEXT("Play with a controller at all. Off, its sticks and buttons do nothing in a match (the menus still answer it). These settings are for both sides."), &Controls->bPadEnabled);
	Rows->AddSlot().AutoHeight().Padding(18.0f, 6.0f, 18.0f, 10.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 16.0f, 0.0f)
		[
			SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(TEXT("Connected controllers")))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
		[
			SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).Justification(ETextJustify::Right)
			.Text(TAttribute<FText>::CreateLambda([]
			{
				// Asked again about once a second, so plugging one in shows while the page is open.
				static double LastAsked = -10.0;
				static FString Shown;
				const double Now = FPlatformTime::Seconds();
				if (Now - LastAsked > 1.0)
				{
					LastAsked = Now;
					const TArray<FString> Pads = ConnectedControllers();
					Shown = Pads.IsEmpty() ? FString(TEXT("No controller connected")) : FString::Join(Pads, TEXT("\n"));
				}
				return FText::FromString(Shown);
			}))
		]
	];

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Input and sticks")) ];
	Switch(TEXT("Enable stick movement"), TEXT("The left stick moves."), &Controls->bPadMove);
	Switch(TEXT("Enable stick aiming"), TEXT("The right stick looks."), &Controls->bPadAim);
	Switch(TEXT("Enable controller buttons"), TEXT("The buttons act (off: the sticks only)."), &Controls->bPadBinds);
#if PLATFORM_WINDOWS
	Switch(TEXT("Filter duplicate controller input"), TEXT("Ignore matching keyboard and mouse input sent by a controller layout. Turn this off if your layout intentionally sends both."), &Controls->bFilterPadMirrors);
#endif
	Slider(TEXT("Left stick dead zone"), TEXT("Travel at the centre of the left stick that does nothing. Raise it if the player creeps with the stick untouched."), &Controls->PadDeadzoneLeft, 0.0f, 0.9f, 0.01f, [](float V) { return PadPanelPercent(V * 100.0f); });
	Slider(TEXT("Right stick dead zone"), TEXT("Travel at the centre of the right stick that does nothing. Raise it if the view drifts."), &Controls->PadDeadzoneRight, 0.0f, 0.9f, 0.01f, [](float V) { return PadPanelPercent(V * 100.0f); });
	Slider(TEXT("Trigger press threshold"), TEXT("How far a trigger goes down before it counts as pressed."), &Controls->PadTriggerThreshold, 0.0f, 0.99f, 0.01f, [](float V) { return PadPanelPercent(V * 100.0f); });

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Movement and aim")) ];
	Slider(TEXT("Up/down sensitivity multiplier"), TEXT("The look stick's up and down speed against its sideways speed."), &Controls->PadVerticalScale, 0.1f, 3.0f, 0.05f, PadPanelTimes);
	Switch(TEXT("Movement speed follows stick tilt"), TEXT("A part push of the left stick walks slower (Reloaded's analog walk). Off, only its direction counts."), &Controls->bPadSlowWalk);
	Slider(TEXT("Spy runs from"), TEXT("How far out the left stick goes before the spy runs."), &Controls->PadRunThreshold, 0.1f, 1.0f, 0.01f, [](float V) { return PadPanelPercent(V * 100.0f); });
	Switch(TEXT("Eight-way movement (spy)"), TEXT("The spy's movement snaps to the eight directions of the original's digital pad."), &Controls->bPadSpyMove8);
	Switch(TEXT("Eight-way movement (mercenary)"), TEXT("The mercenary's movement snaps to eight directions."), &Controls->bPadMercMove8);
	Slider(TEXT("Controller turning speed"), TEXT("A multiplier over the look stick's whole speed; 1 is OpenSCCT Enhanced's. It combines with each side's look below."), &Controls->PadLookScale, 0.25f, 5.0f, 0.05f, PadPanelTimes);
	Slider(TEXT("Spy free look"), TEXT("The spy's look with the stick, gun holstered."), &Controls->SpyPad.FreeLook, 0.05f, 10.0f, 0.05f, PadPanelTimes);
	Slider(TEXT("Spy gun look"), TEXT("The spy's look with the stick, gun drawn."), &Controls->SpyPad.Look, 0.05f, 10.0f, 0.05f, PadPanelTimes);
	Slider(TEXT("Mercenary look"), TEXT("The mercenary's look with the stick."), &Controls->MercPad.Look, 0.05f, 10.0f, 0.05f, PadPanelTimes);
	// The views with a look of their own, as the Godot client's controller tuning has them ("Scope & cameras").
	Slider(TEXT("Spy binoculars"), TEXT("The stick's look through the binoculars, times the spy's look."), &Controls->SpyPad.Scope, 0.05f, 10.0f, 0.05f, PadPanelTimes);
	Slider(TEXT("Spy sticky camera"), TEXT("The stick's look of a sticky camera."), &Controls->SpyPad.Cam, 0.05f, 10.0f, 0.05f, PadPanelTimes);
	Slider(TEXT("Mercenary sniper scope"), TEXT("The stick's look through the scope, times the mercenary's look."), &Controls->MercPad.Scope, 0.05f, 10.0f, 0.05f, PadPanelTimes);
	Slider(TEXT("Mercenary scope vertical"), TEXT("The scope's up and down speed: 1 is the original's, 2.45 turns up and down as fast as sideways."), &Controls->MercPad.ScopeVertical, 0.05f, 10.0f, 0.05f, PadPanelTimes);
	Slider(TEXT("Mercenary camera network"), TEXT("The stick's look of the camera network."), &Controls->MercPad.Cam, 0.05f, 10.0f, 0.05f, PadPanelTimes);

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Vibration")) ];
	Switch(TEXT("Vibration"), TEXT("The controller rumbles with the match: shots, gadgets, blasts, grabs, a sticky shock. Off on a first run, as in Reloaded."), &Controls->bPadRumble);
	Add(SNew(SScctSlider)
		.Label(FText::FromString(TEXT("Vibration strength")))
		.Description(FText::FromString(TEXT("Both motors are scaled by this.")))
		.Min(0.0f).Max(100.0f).Step(5.0f)
		.Get([Controls] { return Controls->PadRumbleStrength * 100.0f; })
		.Set([this, Controls](float Value) { Controls->PadRumbleStrength = Value / 100.0f; Changed(); })
		.Format([](float Value) { return Value <= 0.0f ? FString(TEXT("OFF")) : PadPanelPercent(Value); }));
	Rows->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 4.0f, 16.0f, 10.0f)
	[
		SNew(SScctButton)
		.Text(FText::FromString(TEXT("TEST VIBRATION")))
		.Description(FText::FromString(TEXT("A short pulse of both motors (the blast's rumble) at the strength above, on the controller in use.")))
		.OnPressed(FSimpleDelegate::CreateLambda([]
		{
			UWorld* World = nullptr;
			if (GEngine != nullptr && GEngine->GameViewport != nullptr)
			{
				World = GEngine->GameViewport->GetWorld();
			}
			UScctRumbleSubsystem::PlayTest(World);
		}))
	];

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Buttons")) ];
	Add(SNew(SScctStepper)
		.Label(FText::FromString(TEXT("Side")))
		.Description(FText::FromString(TEXT("Each side has its own controller buttons.")))
		.Options({ ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 0, TEXT("Spy")), ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 1, TEXT("Mercenary")) })
		.Get([this] { return bMerc ? 1 : 0; })
		.Set([this](int32 Value) { bMerc = Value == 1; RebuildButtons(); }));
	Rows->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 4.0f, 16.0f, 4.0f)
	[
		SNew(SScctButton)
		.Look(EScctButtonLook::Chip)
		.Text(FText::FromString(TEXT("RESET THIS SIDE'S BUTTONS")))
		.Description(FText::FromString(TEXT("Put this side's controller buttons (and its keys) back to the defaults.")))
		.OnPressed(FSimpleDelegate::CreateLambda([this]
		{
			ScctUi::Confirm(Host.Dialogs, TEXT("Reset the bindings"), FString::Printf(TEXT("Put every key and controller button of the %s back to the defaults?"), bMerc ? TEXT("mercenary") : TEXT("spy")), TEXT("Reset"), TEXT("Cancel"), [this]
			{
				UScctControlSettings::Get()->ResetBindings(bMerc);
				if (OnBindingsChanged) { OnBindingsChanged(); }
				Toast(TEXT("Bindings restored."));
			});
		}))
	];
	Rows->AddSlot().AutoHeight()[ SAssignNew(ButtonRows, SVerticalBox) ];
	return Rows;
}

void SScctPadPanel::RebuildButtons()
{
	ButtonRows->ClearChildren();
	RowsByAction.Reset();
	UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
	const FScctMenuApi* Api = ScctFeature<FScctMenuApi>();
	if (Menu == nullptr || Api == nullptr || ScctFeature<FScctPadApi>() == nullptr || Menu->GetActions().IsEmpty())
	{
		ButtonRows->AddSlot().AutoHeight().Padding(18.0f, 20.0f)
		[
			SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Danger()).AutoWrapText(true)
			.Text(FText::FromString(TEXT("The controller's buttons cannot be read: the bridge did not start (see the log).")))
		];
		return;
	}

	const int32 Side = bMerc ? EScctMenuSides::Merc : EScctMenuSides::Spy;
	FString Category;
	for (const FScctMenuAction& Action : Menu->GetActions())
	{
		if ((Action.Sides & Side) == 0)
		{
			continue;
		}
		if (Category != Action.Category)
		{
			Category = Action.Category;
			ButtonRows->AddSlot().AutoHeight()[ ScctUi::Section(Category) ];
		}
		const FString Id = Action.Id;
		const FString Label = Action.Label;
		const bool bRowMerc = bMerc;
		TSharedPtr<SScctPadBindRow> Row;
		ButtonRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
		[
			SAssignNew(Row, SScctPadBindRow)
			.Label(FText::FromString(Label))
			.Description(FText::FromString(TEXT("Enter or A, then press the button, hold B, or press a combination such as LS + Y; it is taken when you let go. Escape gives up; Delete clears it. Controllers that report separate back paddles can bind P1 through P4.")))
			.bMerc(bRowMerc)
			.GetControls([Api, Id, bRowMerc]
			{
				return PadPanelReadText([&](TCHAR* Buffer, int32 Capacity) { return Api->GetActionKeys(bRowMerc ? 1 : 0, 1, *Id, Buffer, Capacity); });
			})
			.OnCaptured([this, Id, Label](const FString& Gesture) { HandlePadCaptured(Id, Label, Gesture); })
		];
		RowsByAction.Add(Id, Row);
	}
}

void SScctPadPanel::HandlePadCaptured(const FString& ActionId, const FString& Label, const FString& Gesture)
{
	const FScctMenuApi* Api = ScctFeature<FScctMenuApi>();
	if (Api == nullptr)
	{
		return;
	}
	if (Gesture.IsEmpty())
	{
		BindPad(ActionId, FString());
		Toast(FString::Printf(TEXT("%s is not bound on the controller now."), *Label));
		return;
	}

	// What the gesture does now, besides this action.
	const FString Taken = PadPanelReadText([&](TCHAR* Buffer, int32 Capacity) { return Api->GetKeyActions(bMerc ? 1 : 0, 1, *Gesture, Buffer, Capacity); });
	TArray<FString> Others;
	Taken.ParseIntoArray(Others, TEXT("\n"));
	Others.Remove(Label);
	if (Others.IsEmpty() || bTestBindDirect)
	{
		if (!Others.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadBind: %s was bound to %s; replaced (unattended check)"), *Gesture, *FString::Join(Others, TEXT(", ")));
		}
		BindPad(ActionId, Gesture);
		return;
	}
	const FString Shown = SScctPadBindRow::ShowControl(Gesture);
	ScctUi::Confirm(Host.Dialogs, TEXT("Button already in use"),
		FString::Printf(TEXT("%s is bound to %s. Bind it to %s instead? It will no longer do %s."), *Shown, *FString::Join(Others, TEXT(", ")), *Label, Others.Num() == 1 ? *Others[0] : TEXT("those")),
		TEXT("Replace"), TEXT("Cancel"), [this, ActionId, Gesture] { BindPad(ActionId, Gesture); });
}

void SScctPadPanel::BindPad(const FString& ActionId, const FString& Gesture)
{
	const FScctMenuApi* Api = ScctFeature<FScctMenuApi>();
	if (Api != nullptr && Api->BindAction(bMerc ? 1 : 0, 1, *ActionId, *Gesture, 0) != 0)
	{
		UScctControlSettings::Get()->NotifyRebound();
		if (OnBindingsChanged)
		{
			OnBindingsChanged();
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadBind: %s %s bound to '%s' on the controller"), bMerc ? TEXT("merc") : TEXT("spy"), *ActionId, *Gesture);
		if (!Gesture.IsEmpty())
		{
			Toast(FString::Printf(TEXT("Bound to %s."), *SScctPadBindRow::ShowControl(Gesture)));
		}
	}
	else
	{
		Toast(FString::Printf(TEXT("%s cannot be bound."), *SScctPadBindRow::ShowControl(Gesture)), true);
	}
}

EActiveTimerReturnType SScctPadPanel::TickPadBindTest(double InCurrentTime, float InDeltaTime)
{
	if (TestWait > 0.0f)
	{
		TestWait -= InDeltaTime;
		return EActiveTimerReturnType::Continue;
	}
	if (!TestSteps.IsValidIndex(TestStep))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadBind: check done (%d gesture(s))"), TestSteps.Num());
		return EActiveTimerReturnType::Stop;
	}

	FPadBindStep& Step = TestSteps[TestStep];
	FSlateApplication& Slate = FSlateApplication::Get();
	TArray<FString> Parts;
	FString Base = Step.Gesture;
	const bool bHold = Base.EndsWith(TEXT(":Hold"));
	if (bHold)
	{
		Base.LeftChopInline(5);
	}
	Base.ParseIntoArray(Parts, TEXT("+"));
	const auto Send = [&Slate](const FString& Control, bool bDown)
	{
		const FKey Key = SScctPadBindRow::KeyOf(Control);
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		bDown ? Slate.ProcessKeyDownEvent(Event) : Slate.ProcessKeyUpEvent(Event);
	};

	switch (TestPhase)
	{
	case 0:
	{
		if (Step.bMerc != bMerc)
		{
			bMerc = Step.bMerc;
			RebuildButtons();
			TestWait = 0.3f;
			return EActiveTimerReturnType::Continue;
		}
		const TSharedPtr<SScctPadBindRow> Row = RowsByAction.FindRef(Step.Action).Pin();
		if (!Row.IsValid())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctPadBind: the %s has no action '%s'"), bMerc ? TEXT("mercenary") : TEXT("spy"), *Step.Action);
			++TestStep;
			return EActiveTimerReturnType::Continue;
		}
		Slate.SetKeyboardFocus(Row, EFocusCause::Navigation);
		Row->DrivenPress();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadBind: capturing %s '%s' (was '%s'); sending %s"), bMerc ? TEXT("merc") : TEXT("spy"), *Step.Action,
			*UScctControlSettings::Get()->GetBinding(bMerc, true, Step.Gesture), *Step.Gesture);
		TestPhase = 1;
		TestWait = 0.2f;
		break;
	}
	case 1:
		// Down, in order: the modifier first for a combination.
		for (const FString& Part : Parts)
		{
			Send(Part, true);
		}
		TestPhase = 2;
		TestWait = bHold ? 0.45f : 0.15f;
		break;
	case 2:
		for (int32 Index = Parts.Num() - 1; Index >= 0; --Index)
		{
			Send(Parts[Index], false);
		}
		TestPhase = 3;
		TestWait = 0.3f;
		break;
	default:
	{
		const TSharedPtr<SScctPadBindRow> Row = RowsByAction.FindRef(Step.Action).Pin();
		const bool bStillCapturing = Row.IsValid() && Row->IsCapturing();
		UScctControlSettings* Controls = UScctControlSettings::Get();
		const bool bSaved = Controls->SaveBindings();
		const FScctMenuApi* Api = ScctFeature<FScctMenuApi>();
		const FString Keys = Api != nullptr
			? PadPanelReadText([&](TCHAR* Buffer, int32 Capacity) { return Api->GetActionKeys(bMerc ? 1 : 0, 1, *Step.Action, Buffer, Capacity); }).Replace(TEXT("\n"), TEXT(", "))
			: FString();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctPadBind: %s '%s' stored binding %s = '%s'; the action is now on [%s]; bindings %s to %s%s"),
			bMerc ? TEXT("merc") : TEXT("spy"), *Step.Action, *Step.Gesture, *Controls->GetBinding(bMerc, true, Step.Gesture), *Keys,
			bSaved ? TEXT("written") : TEXT("NOT written"), *UScctControlSettings::GetBindingsFile(), bStillCapturing ? TEXT(" (the row was still capturing: nothing was captured)") : TEXT(""));
		++TestStep;
		TestPhase = 0;
		TestWait = 0.3f;
		break;
	}
	}
	return EActiveTimerReturnType::Continue;
}
