#include "Menu/SScctSettingsPanel.h"

#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Menu/SScctBrightnessPage.h"
#include "Menu/SScctPadPanel.h"
#include "Menu/ScctEnhancedImport.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctUserSettings.h"
#include "Misc/App.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctAudioOutput.h"
#include "Play/ScctControlSettings.h"
#include "Runtime/ScctControlsApi.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctGraphicsSettings.h"
#include "Settings/ScctSettings.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* const SettingsTabNames[] = { TEXT("video"), TEXT("audio"), TEXT("controls"), TEXT("controller"), TEXT("bindings"), TEXT("game"), TEXT("profile") };
	/** The registry's own tabs come after the fixed ones. */
	constexpr int32 FirstRegistryTab = UE_ARRAY_COUNT(SettingsTabNames);
	const TCHAR* const QualityNames[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };
	const int32 FrameLimits[] = { 0, 30, 60, 90, 120, 144, 165, 240 };

	UGameUserSettings* EngineSettings()
	{
		return GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	}

	template <typename TRead>
	FString ReadSettingsText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(512);
		const int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return FString(Buffer.GetData());
	}

	TArray<FString> QualityOptions()
	{
		TArray<FString> Out;
		for (const TCHAR* Name : QualityNames) { Out.Add(Name); }
		return Out;
	}

	/** The bindings' name of an Unreal key ("W", "Shift", "LeftMouse"), empty for a key bindings cannot hold. */
	FString KeyboardName(const FKey& Key)
	{
		const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>();
		if (Controls == nullptr)
		{
			return FString();
		}
		const TArray<TPair<FKey, FKey>>& Keys = FScctControls::Keys();
		for (int32 Index = 0; Index < Keys.Num() && Index <= static_cast<int32>(EScctKey::MouseWheelDown); ++Index)
		{
			if ((Keys[Index].Key.IsValid() && Keys[Index].Key == Key) || (Keys[Index].Value.IsValid() && Keys[Index].Value == Key))
			{
				return ReadSettingsText([&](TCHAR* Buffer, int32 Capacity) { return Controls->GetKeyName(Index, Buffer, Capacity); });
			}
		}
		return FString();
	}

	/** The bindings' name of a controller button ("A", "LB", "DpadUp"), empty for anything else. */
	FString PadName(const FKey& Key)
	{
		static const TPair<FKey, const TCHAR*> Names[] = {
			{ EKeys::Gamepad_FaceButton_Bottom, TEXT("A") }, { EKeys::Gamepad_FaceButton_Right, TEXT("B") },
			{ EKeys::Gamepad_FaceButton_Left, TEXT("X") }, { EKeys::Gamepad_FaceButton_Top, TEXT("Y") },
			{ EKeys::Gamepad_LeftShoulder, TEXT("LB") }, { EKeys::Gamepad_RightShoulder, TEXT("RB") },
			{ EKeys::Gamepad_LeftTrigger, TEXT("LT") }, { EKeys::Gamepad_RightTrigger, TEXT("RT") },
			{ EKeys::Gamepad_LeftTriggerAxis, TEXT("LT") }, { EKeys::Gamepad_RightTriggerAxis, TEXT("RT") },
			{ EKeys::Gamepad_LeftThumbstick, TEXT("LS") }, { EKeys::Gamepad_RightThumbstick, TEXT("RS") },
			{ EKeys::Gamepad_DPad_Up, TEXT("DpadUp") }, { EKeys::Gamepad_DPad_Down, TEXT("DpadDown") },
			{ EKeys::Gamepad_DPad_Left, TEXT("DpadLeft") }, { EKeys::Gamepad_DPad_Right, TEXT("DpadRight") },
			{ EKeys::Gamepad_Special_Right, TEXT("Start") }, { EKeys::Gamepad_Special_Left, TEXT("Back") },
		};
		for (const TPair<FKey, const TCHAR*>& Name : Names)
		{
			if (Name.Key == Key)
			{
				return Name.Value;
			}
		}
		return FString();
	}

	/** A registry page's name as a tab label: "Hud" reads "HUD". */
	FString RegistryTabLabel(FName Page)
	{
		const FString Name = Page.ToString();
		return Name.Len() <= 3 ? Name.ToUpper() : Name;
	}

	/** The settings the CONTROLLER tab shows (what its reset puts back, and what the CONTROLS tab's reset keeps). */
	void CopyControllerSettings(const UScctControlSettings& From, UScctControlSettings& To)
	{
		To.bPadEnabled = From.bPadEnabled;
		To.bPadMove = From.bPadMove;
		To.bPadAim = From.bPadAim;
		To.bPadBinds = From.bPadBinds;
		To.bPadSlowWalk = From.bPadSlowWalk;
		To.bPadSpyMove8 = From.bPadSpyMove8;
		To.bPadMercMove8 = From.bPadMercMove8;
		To.PadDeadzoneLeft = From.PadDeadzoneLeft;
		To.PadDeadzoneRight = From.PadDeadzoneRight;
		To.PadVerticalScale = From.PadVerticalScale;
		To.PadRunThreshold = From.PadRunThreshold;
		To.PadTriggerThreshold = From.PadTriggerThreshold;
		To.PadLookScale = From.PadLookScale;
		To.bPadRumble = From.bPadRumble;
		To.PadRumbleStrength = From.PadRumbleStrength;
		To.SpyPad.FreeLook = From.SpyPad.FreeLook;
		To.SpyPad.Look = From.SpyPad.Look;
		To.MercPad.Look = From.MercPad.Look;
	}

	FString Percent(float Value) { return FString::Printf(TEXT("%d %%"), FMath::RoundToInt32(Value)); }
	FString Times(float Value) { return FString::Printf(TEXT("%.2f x"), Value); }
}

TArray<FName> SScctSettingsPanel::RegistryPagesOfTab(int32 Tab)
{
	switch (Tab)
	{
	case 0: return { TEXT("Display"), TEXT("Graphics"), TEXT("Performance") };
	case 1: return { TEXT("Audio"), TEXT("Voice") };
	case 2: return { TEXT("Spy"), TEXT("Mercenary") };
	case 3: return { TEXT("Controller") };
	default: return {};
	}
}

int32 SScctSettingsPanel::FindTab(const FString& Name)
{
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SettingsTabNames); ++Index)
	{
		if (Name.Equals(SettingsTabNames[Index], ESearchCase::IgnoreCase))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void SScctSettingsPanel::Construct(const FArguments& InArgs, const FScctMenuHost& InHost)
{
	Host = InHost;
	UScctControlSettings::Get()->EnsureLoaded();
	// The bindings open on the side being played (the spy's with no match running).
	if (const FScctControlsApi* Controls = ScctFeature<FScctControlsApi>())
	{
		bBindMerc = Controls->GetSide() == 1;
	}
	if (UGameUserSettings* Engine = EngineSettings())
	{
		PendingMode = static_cast<int32>(Engine->GetFullscreenMode());
		PendingSize = Engine->GetScreenResolution();
	}
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Sizes);
	if (Sizes.IsEmpty())
	{
		Sizes = { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160) };
	}
	Sizes.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y < B.X * B.Y; });
	if (PendingSize.X > 0 && !Sizes.Contains(PendingSize))
	{
		Sizes.Add(PendingSize);
	}

	const FString Labels[] = {
		ScctUi::GameText(TEXT("All_Settings"), TEXT("BTNVideo.Caption"), TEXT("Video")),
		ScctUi::GameText(TEXT("All_Settings"), TEXT("BTNAudio.Caption"), TEXT("Audio")),
		TEXT("Controls"),
		TEXT("Controller"),
		TEXT("Key bindings"),
		ScctUi::GameText(TEXT("All_Settings"), TEXT("BTNGame.Caption"), TEXT("Game")),
		TEXT("Profile"),
	};
	TArray<FString> TabLabels(Labels, UE_ARRAY_COUNT(Labels));
	for (const TCHAR* Name : SettingsTabNames)
	{
		TabNames.Add(Name);
	}
	// The registry's pages no fixed tab holds, each a tab of its own.
	TArray<FName> Held;
	for (int32 Tab = 0; Tab < FirstRegistryTab; ++Tab)
	{
		Held.Append(RegistryPagesOfTab(Tab));
	}
	for (const FName Page : FScctSettings::Pages())
	{
		if (!Held.Contains(Page))
		{
			RegistryTabs.Add(Page);
			TabLabels.Add(RegistryTabLabel(Page));
			TabNames.Add(Page.ToString().ToLower());
		}
	}

	TSharedPtr<SScctButton> ResetButton;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)
		[
			SAssignNew(Tabs, SScctTabBar)
			.Tabs(TabLabels)
			.Initial(InArgs._InitialTab)
			.OnChanged(FSimpleDelegate::CreateSP(this, &SScctSettingsPanel::Rebuild))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(860.0f).Padding(0.0f, 0.0f, 24.0f, 0.0f)
			[
				ScctUi::Panel(SAssignNew(Body, SBox), FMargin(12.0f, 6.0f, 6.0f, 12.0f), 0.72f)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(420.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Card(FString(), ScctUi::DescriptionPane(), FMargin(26.0f, 24.0f)) ]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
					[
						SNew(SScctButton)
						.FillWidth(true)
						.Text(FText::FromString(TEXT("IMPORT FROM ENHANCED SCCT VERSUS")))
						.Description(FText::FromString(TEXT("Take over your settings from an Enhanced SCCT Versus (Reloaded) install: sensitivities, the controller, display, graphics, HUD, voice, volumes, gear and both key layouts. Choose the install's folder, or use the profile kept on this PC.")))
						.OnPressed(FSimpleDelegate::CreateSP(this, &SScctSettingsPanel::AskImportEnhanced))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
						[
							SAssignNew(ResetButton, SScctButton)
							.Text(FText::FromString(ScctUi::GameText(TEXT("Audio_Settings"), TEXT("ButtonY"), TEXT("Reset")).ToUpper() + TEXT(" TAB")))
							.Description(FText::FromString(TEXT("Put every setting of this tab back to its default.")))
							.OnPressed(FSimpleDelegate::CreateSP(this, &SScctSettingsPanel::ResetTab))
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SScctButton)
							.Look(EScctButtonLook::Primary)
							.FillWidth(true)
							.Text(FText::FromString(TEXT("SAVE AND CLOSE")))
							.Description(FText::FromString(TEXT("Write the settings and go back. Every change is already in force; Escape does the same.")))
							.OnPressed(FSimpleDelegate::CreateLambda([this] { if (Host.Back) { Host.Back(); } }))
						]
					]
				]
			]
		]
	];
	Rebuild();
}

FString SScctSettingsPanel::GetTitle() const
{
	return ScctUi::GameText(TEXT("All_Settings"), TEXT("TitlePage"), TEXT("Options"));
}

FString SScctSettingsPanel::GetKicker() const
{
	return Host.bInMatch ? TEXT("The match goes on while you change them") : TEXT("Settings");
}

void SScctSettingsPanel::StepTab(int32 Direction)
{
	Tabs->Step(Direction);
}

void SScctSettingsPanel::ShowSub(const FString& Sub)
{
	FString Tab = Sub, Rest;
	Sub.Split(TEXT("/"), &Tab, &Rest);
	if (!Rest.IsEmpty())
	{
		bBindMerc = Rest.Contains(TEXT("merc"));
		if (Rest.Contains(TEXT("pad")))
		{
			// "bindings/merc/pad": the controller's buttons are on the CONTROLLER tab.
			Tab = TEXT("controller");
		}
	}
	const int32 Index = TabNames.IndexOfByPredicate([&Tab](const FString& Name) { return Name.Equals(Tab, ESearchCase::IgnoreCase); });
	if (Index != INDEX_NONE)
	{
		if (Index == Tabs->GetTab())
		{
			Rebuild();
		}
		Tabs->SetTab(Index);
	}
}

bool SScctSettingsPanel::HandleBack()
{
	// A key row that is waiting for a key took the Escape itself; anything else closes the page.
	return false;
}

void SScctSettingsPanel::Toast(const FString& Message, bool bBad)
{
	if (Host.Dialogs.IsValid())
	{
		Host.Dialogs->Toast(Message, bBad ? ScctHudStyle::Danger() : ScctUi::Accent());
	}
}

void SScctSettingsPanel::Rebuild()
{
	TSharedRef<SWidget> Rows = SNullWidget::NullWidget;
	switch (Tabs->GetTab())
	{
	case 0: Rows = BuildVideo(); break;
	case 1: Rows = BuildAudio(); break;
	case 2: Rows = BuildControls(); break;
	case 3: Rows = BuildController(); break;
	case 4: Rows = BuildBindings(); break;
	case 5: Rows = BuildGame(); break;
	case 6: Rows = BuildProfile(); break;
	default: Rows = RegistryTabs.IsValidIndex(Tabs->GetTab() - FirstRegistryTab) ? BuildRegistryTab(RegistryTabs[Tabs->GetTab() - FirstRegistryTab]) : BuildProfile(); break;
	}
	Body->SetContent(
		SNew(SScrollBox)
		.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
		+ SScrollBox::Slot().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Rows ]);
	if (const TSharedPtr<SWidget> Focus = DefaultFocus.Pin())
	{
		FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
	}
}

void SScctSettingsPanel::ResetTab()
{
	const int32 Tab = Tabs->GetTab();
	ScctUi::Confirm(Host.Dialogs, TEXT("Reset this tab"), TEXT("Put every setting of this tab back to its default?"), TEXT("Reset"), TEXT("Cancel"), [this, Tab]
	{
		UScctUserSettings* Settings = UScctUserSettings::Get();
		UScctControlSettings* Controls = UScctControlSettings::Get();
		switch (Tab)
		{
		case 0: Settings->ResetVideo(); Settings->ApplyAll(); ApplyQuality(); break;
		case 1: Settings->ResetAudio(); Settings->ApplyAll(); if (UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent()) { Menu->RefreshVolumes(); } break;
		case 2:
		{
			// The bridge holds the defaults: an empty settings object, clamped by Apply, is not them, so the file is
			// read again after it is removed. The controller's settings have a tab of their own and are kept.
			UScctControlSettings* Kept = NewObject<UScctControlSettings>();
			CopyControllerSettings(*Controls, *Kept);
			IFileManager::Get().Delete(*UScctControlSettings::GetSettingsFile());
			Controls->Load();
			CopyControllerSettings(*Kept, *Controls);
			Controls->Apply();
			bControlsDirty = true;
			break;
		}
		case 3: ResetController(); break;
		case 4: Controls->ResetBindings(bBindMerc); bBindingsDirty = true; break;
		case 5: Settings->ResetInterface(); Settings->ApplyAll(); Controls->bAutoHackComms = false; Controls->bMouseWheelSpeed = false; ApplyControls(); break;
		default: if (RegistryTabs.IsValidIndex(Tab - FirstRegistryTab)) { FScctSettings::ResetPage(RegistryTabs[Tab - FirstRegistryTab]); } break;
		}
		for (const FName Page : RegistryPagesOfTab(Tab))
		{
			FScctSettings::ResetPage(Page);
		}
		Toast(TEXT("Defaults restored."));
		Rebuild();
	});
}

void SScctSettingsPanel::AskImportEnhanced()
{
	const FString Usual = ScctEnhancedImport::UsualProfileFolder();
	FScctDialog Dialog;
	Dialog.Title = TEXT("Import from Enhanced SCCT Versus");
	Dialog.Body = FString::Printf(TEXT("Type or browse to the folder of your Enhanced SCCT Versus install (the one with its System folder; any folder inside it will do), or take the profile kept on this PC%s.\n\nYour sensitivities, controller, display, graphics, HUD, voice, volumes, gear and both key layouts replace the ones here; your name is kept."),
		Usual.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s: profile, volumes, gear and keys, without the install's own settings)"), *Usual));
	Dialog.bTextField = true;
	Dialog.TextHint = TEXT("Install folder, as C:\\Games\\SCCT Versus Reloaded");
	Dialog.Buttons.Add({ TEXT("Cancel"), EScctButtonLook::Secondary, nullptr });
	Dialog.Buttons.Add({ TEXT("This PC's profile"), EScctButtonLook::Secondary, [this](const FString&) { RunImportEnhanced(FString(), true); } });
	Dialog.Buttons.Add({ TEXT("Browse..."), EScctButtonLook::Secondary, [this](const FString&)
	{
		FString Folder;
		if (ScctEnhancedImport::PickFolder(TEXT("The Enhanced SCCT Versus install folder"), Folder))
		{
			RunImportEnhanced(Folder, false);
		}
	} });
	Dialog.Buttons.Add({ TEXT("Import"), EScctButtonLook::Primary, [this](const FString& Typed)
	{
		const FString Folder = Typed.TrimStartAndEnd().TrimQuotes();
		if (Folder.IsEmpty())
		{
			Toast(TEXT("Type the install's folder, or choose Browse or This PC's profile."), true);
			return;
		}
		RunImportEnhanced(Folder, false);
	} });
	Dialog.FocusButton = 3;
	Dialog.CancelButton = 0;
	if (Host.Dialogs.IsValid())
	{
		Host.Dialogs->Show(Dialog);
	}
}

void SScctSettingsPanel::RunImportEnhanced(const FString& Folder, bool bWithoutInstall)
{
	const FScctEnhancedImportResult Result = ScctEnhancedImport::Import(Folder, bWithoutInstall);
	// The controls and bindings files were written by the import itself: nothing is left for Commit to write.
	bControlsDirty = false;
	bBindingsDirty = false;
	Rebuild();
	FScctDialog Dialog;
	Dialog.Title = Result.bOk ? TEXT("Imported from Enhanced SCCT Versus") : TEXT("Nothing was imported");
	Dialog.Body = ScctEnhancedImport::Summary(Result);
	Dialog.Buttons.Add({ TEXT("OK"), EScctButtonLook::Primary, nullptr });
	if (Host.Dialogs.IsValid())
	{
		Host.Dialogs->Show(Dialog);
	}
}

void SScctSettingsPanel::Commit()
{
	UScctUserSettings::Get()->Save();
	FScctSettings::Save();
	UScctControlSettings* Controls = UScctControlSettings::Get();
	if (bControlsDirty)
	{
		Controls->Save();
		bControlsDirty = false;
	}
	if (bBindingsDirty)
	{
		Controls->SaveBindings();
		bBindingsDirty = false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Settings written (%s, %s)"), *UScctControlSettings::GetSettingsFile(), *UScctControlSettings::GetBindingsFile());
}

void SScctSettingsPanel::ApplyControls()
{
	UScctControlSettings::Get()->Apply();
	bControlsDirty = true;
}

void SScctSettingsPanel::ApplyQuality()
{
	if (UGameUserSettings* Engine = EngineSettings())
	{
		Engine->ApplyNonResolutionSettings();
		Engine->SaveSettings();
	}
	// The scalability groups set some of the variables the registry's graphics options set: those go again on top.
	ScctGraphicsSettings::Apply();
}

// ---- video ----------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctSettingsPanel::BuildVideo()
{
	UScctUserSettings* Settings = UScctUserSettings::Get();
	UScctControlSettings* Controls = UScctControlSettings::Get();
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](const TSharedRef<SWidget>& Row) { Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ Row ]; };

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Display")) ];
	TSharedPtr<SWidget> First;
	Add(SAssignNew(First, SScctStepper)
		.Label(FText::FromString(TEXT("Window mode")))
		.Description(FText::FromString(TEXT("Fullscreen takes the monitor over; borderless keeps the desktop's resolution and switches away instantly; windowed is a window. Takes effect with Apply display.")))
		.Options({ TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed") })
		.Get([this] { return PendingMode; })
		.Set([this](int32 Value) { PendingMode = Value; }));
	DefaultFocus = First;
	Add(SNew(SScctStepper)
		.Label(FText::FromString(ScctUi::GameLabel(TEXT("Video_Settings"), TEXT("NUMResolution.Caption"), TEXT("Screen Resolution"))))
		.Description(FText::FromString(TEXT("The window's size in pixels (in borderless the desktop's is used). Takes effect with Apply display.")))
		.Wrap(false)
		.GetOptions([this] { TArray<FString> Out; for (const FIntPoint& Size : Sizes) { Out.Add(FString::Printf(TEXT("%d x %d"), Size.X, Size.Y)); } return Out; })
		.Get([this] { return FMath::Max(Sizes.IndexOfByKey(PendingSize), 0); })
		.Set([this](int32 Value) { if (Sizes.IsValidIndex(Value)) { PendingSize = Sizes[Value]; } }));
	Rows->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 4.0f, 16.0f, 10.0f)
	[
		SNew(SScctButton)
		.Text(FText::FromString(TEXT("APPLY DISPLAY")))
		.Description(FText::FromString(TEXT("Switch to the window mode and the resolution above. They are kept only when you confirm them within 15 seconds.")))
		.OnPressed(FSimpleDelegate::CreateSP(this, &SScctSettingsPanel::ApplyDisplay))
	];
	Add(SNew(SScctToggle)
		.Label(FText::FromString(TEXT("Vertical sync")))
		.Description(FText::FromString(TEXT("Wait for the monitor between frames: no tearing, a little more input delay.")))
		.Get([] { const UGameUserSettings* E = EngineSettings(); return E != nullptr && E->IsVSyncEnabled(); })
		.Set([this](bool bValue) { if (UGameUserSettings* E = EngineSettings()) { E->SetVSyncEnabled(bValue); ApplyQuality(); } }));
	Add(SNew(SScctStepper)
		.Label(FText::FromString(TEXT("Frame limit")))
		.Description(FText::FromString(TEXT("The most frames a second the game draws. A limit just under the monitor's refresh rate keeps the delay low and the fans quiet.")))
		.Wrap(false)
		.GetOptions([] { TArray<FString> Out; for (const int32 Limit : FrameLimits) { Out.Add(Limit == 0 ? FString(TEXT("Unlimited")) : FString::Printf(TEXT("%d fps"), Limit)); } return Out; })
		.Get([] { const UGameUserSettings* E = EngineSettings(); const int32 Now = E != nullptr ? FMath::RoundToInt32(E->GetFrameRateLimit()) : 0; for (int32 I = 0; I < UE_ARRAY_COUNT(FrameLimits); ++I) { if (FrameLimits[I] == Now) { return I; } } return 0; })
		.Set([this](int32 Value) { if (UGameUserSettings* E = EngineSettings()) { E->SetFrameRateLimit(static_cast<float>(FrameLimits[Value])); ApplyQuality(); } }));

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Quality")) ];
	Add(SNew(SScctStepper)
		.Label(FText::FromString(TEXT("Quality preset")))
		.Description(FText::FromString(TEXT("Sets every group below at once. Epic is the look this game is made for (ray-traced global illumination and reflections, virtual shadow maps); lower it on a slower graphics card.")))
		.Wrap(false)
		.GetOptions([] { TArray<FString> Out = QualityOptions(); Out.Add(TEXT("Custom")); return Out; })
		.Get([] { const UGameUserSettings* E = EngineSettings(); const int32 Level = E != nullptr ? E->GetOverallScalabilityLevel() : 3; return Level < 0 ? 5 : Level; })
		.Set([this](int32 Value) { if (UGameUserSettings* E = EngineSettings()) { if (Value <= 4) { E->SetOverallScalabilityLevel(Value); ApplyQuality(); } } }));

	struct FGroup
	{
		const TCHAR* Name;
		const TCHAR* Description;
		int32 (UGameUserSettings::*Get)() const;
		void (UGameUserSettings::*Set)(int32);
	};
	static const FGroup Groups[] = {
		{ TEXT("Global illumination"), TEXT("How light bounces around the rooms (Lumen). The largest single cost and the largest single part of the look."), &UGameUserSettings::GetGlobalIlluminationQuality, &UGameUserSettings::SetGlobalIlluminationQuality },
		{ TEXT("Reflections"), TEXT("Reflections in glass, water, wet floors and polished metal."), &UGameUserSettings::GetReflectionQuality, &UGameUserSettings::SetReflectionQuality },
		{ TEXT("Shadows"), TEXT("The sharpness and reach of shadows. The spies live in them, so keep them as high as you can."), &UGameUserSettings::GetShadowQuality, &UGameUserSettings::SetShadowQuality },
		{ TEXT("Post-processing"), TEXT("Depth of field, bloom, lens effects and the vision modes' finish."), &UGameUserSettings::GetPostProcessingQuality, &UGameUserSettings::SetPostProcessingQuality },
		{ TEXT("Textures"), TEXT("Texture detail. Lower it when the graphics card runs out of memory."), &UGameUserSettings::GetTextureQuality, &UGameUserSettings::SetTextureQuality },
		{ TEXT("Effects"), TEXT("Smoke, sparks, particles and the gadgets' effects."), &UGameUserSettings::GetVisualEffectQuality, &UGameUserSettings::SetVisualEffectQuality },
		{ TEXT("Anti-aliasing"), TEXT("Smooths jagged edges (temporal super resolution)."), &UGameUserSettings::GetAntiAliasingQuality, &UGameUserSettings::SetAntiAliasingQuality },
		{ TEXT("View distance"), TEXT("How far detail is kept."), &UGameUserSettings::GetViewDistanceQuality, &UGameUserSettings::SetViewDistanceQuality },
		{ TEXT("Foliage"), TEXT("Plants and grass density."), &UGameUserSettings::GetFoliageQuality, &UGameUserSettings::SetFoliageQuality },
		{ TEXT("Shading"), TEXT("The detail of materials and of skin and cloth shading."), &UGameUserSettings::GetShadingQuality, &UGameUserSettings::SetShadingQuality },
	};
	for (const FGroup& Group : Groups)
	{
		const FGroup* G = &Group;
		Add(SNew(SScctStepper)
			.Label(FText::FromString(Group.Name))
			.Description(FText::FromString(Group.Description))
			.Wrap(false)
			.Options(QualityOptions())
			.Get([G] { const UGameUserSettings* E = EngineSettings(); return E != nullptr ? FMath::Clamp((E->*(G->Get))(), 0, 4) : 3; })
			.Set([this, G](int32 Value) { if (UGameUserSettings* E = EngineSettings()) { (E->*(G->Set))(Value); ApplyQuality(); } }));
	}
	Add(SNew(SScctSlider)
		.Label(FText::FromString(TEXT("Render scale")))
		.Description(FText::FromString(TEXT("The share of the screen's pixels the 3D picture is drawn at before it is scaled up. Below 100 % is faster and softer.")))
		.Min(50.0f).Max(100.0f).Step(5.0f)
		.Get([Settings] { return static_cast<float>(Settings->RenderScale); })
		.Set([Settings](float Value) { Settings->RenderScale = FMath::RoundToInt32(Value); Settings->ApplyAll(); })
		.Format([](float Value) { return Percent(Value); }));
	Add(SNew(SScctToggle)
		.Label(FText::FromString(TEXT("Motion blur")))
		.Description(FText::FromString(TEXT("Blur of fast movement, as a camera sees it.")))
		.Get([Settings] { return Settings->bMotionBlur; })
		.Set([Settings](bool bValue) { Settings->bMotionBlur = bValue; Settings->ApplyAll(); }));
	Add(SNew(SScctSlider)
		.Label(FText::FromString(TEXT("Brightness")))
		.Description(FText::FromString(TEXT("The display gamma. Raise it until the darkest corners of a room are just visible, no further: the spies need their shadows.")))
		.Min(1.6f).Max(3.0f).Step(0.1f)
		.Get([Settings] { return Settings->Gamma; })
		.Set([Settings](float Value) { Settings->Gamma = Value; Settings->ApplyAll(); })
		.Format([](float Value) { return Percent((Value / 2.2f) * 100.0f); }));
	Rows->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 4.0f, 16.0f, 10.0f)
	[
		SNew(SScctButton)
		.Text(FText::FromString(TEXT("CALIBRATE BRIGHTNESS")))
		.Description(FText::FromString(TEXT("Set the brightness against a test pattern of near-black squares, the original game's calibration picture.")))
		.OnPressed(FSimpleDelegate::CreateLambda([] { SScctBrightnessPage::Open(); }))
	];

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Field of view")) ];
	Add(SNew(SScctToggle)
		.Label(FText::FromString(TEXT("Wider view on widescreen")))
		.Description(FText::FromString(TEXT("Keep the original 4:3 picture's height and add width on a wide screen (on), or stretch the 4:3 picture (off).")))
		.Get([Controls] { return Controls->bWidescreen; })
		.Set([this, Controls](bool bValue) { Controls->bWidescreen = bValue; ApplyControls(); }));
	Add(SNew(SScctSlider)
		.Label(FText::FromString(TEXT("Mercenary field of view cap")))
		.Description(FText::FromString(TEXT("The widest horizontal field of view the mercenary's first person view may have, in degrees.")))
		.Min(95.0f).Max(112.0f).Step(1.0f)
		.Get([Controls] { return Controls->MercFovCap; })
		.Set([this, Controls](float Value) { Controls->MercFovCap = Value; ApplyControls(); })
		.Format([](float Value) { return FString::Printf(TEXT("%d°"), FMath::RoundToInt32(Value)); }));
	for (const FName Page : RegistryPagesOfTab(0))
	{
		AddRegistryRows(Rows, Page);
	}
	return Rows;
}

void SScctSettingsPanel::ApplyDisplay()
{
	UGameUserSettings* Engine = EngineSettings();
	if (Engine == nullptr)
	{
		return;
	}
	const EWindowMode::Type OldMode = Engine->GetFullscreenMode();
	const FIntPoint OldSize = Engine->GetScreenResolution();
	Engine->SetFullscreenMode(static_cast<EWindowMode::Type>(FMath::Clamp(PendingMode, 0, 2)));
	Engine->SetScreenResolution(PendingSize);
	if (FApp::IsUnattended())
	{
		// Nobody can see a window that moves: the choice is kept but the window stays as it is.
		UE_LOG(LogOpenSCCT, Log, TEXT("Settings: display %dx%d mode %d kept for the next run (unattended: the window is not changed)"), PendingSize.X, PendingSize.Y, PendingMode);
	}
	else
	{
		Engine->ApplyResolutionSettings(false);
	}

	FScctDialog Dialog;
	Dialog.Title = TEXT("Keep this display?");
	Dialog.Body = ScctUi::GameText(TEXT("Validate_VideoSettings"), TEXT("KeepText"), TEXT("Do you want to keep these settings? Original settings will be reset in:"));
	Dialog.Timeout = 15.0f;
	Dialog.Buttons.Add({ TEXT("Keep"), EScctButtonLook::Primary, [this](const FString&)
	{
		if (UGameUserSettings* E = EngineSettings()) { E->ConfirmVideoMode(); E->SaveSettings(); }
		UScctUserSettings::Get()->LogValues(TEXT("display kept"));
	} });
	Dialog.Buttons.Add({ TEXT("Revert"), EScctButtonLook::Secondary, [this, OldMode, OldSize](const FString&)
	{
		if (UGameUserSettings* E = EngineSettings())
		{
			E->SetFullscreenMode(OldMode);
			E->SetScreenResolution(OldSize);
			if (!FApp::IsUnattended()) { E->ApplyResolutionSettings(false); }
			E->SaveSettings();
		}
		PendingMode = static_cast<int32>(OldMode);
		PendingSize = OldSize;
	} });
	Dialog.FocusButton = 1;
	Dialog.CancelButton = 1;
	if (Host.Dialogs.IsValid())
	{
		Host.Dialogs->Show(Dialog);
	}
}

// ---- audio ----------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctSettingsPanel::BuildAudio()
{
	UScctUserSettings* Settings = UScctUserSettings::Get();
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Volume = [&Rows, Settings](const FString& Caption, const TCHAR* Description, int32* Value) -> TSharedRef<SWidget>
	{
		const TSharedRef<SWidget> Row = SNew(SScctSlider)
			.Label(FText::FromString(Caption))
			.Description(FText::FromString(Description))
			.Min(0.0f).Max(100.0f).Step(5.0f)
			.Get([Value] { return static_cast<float>(*Value); })
			.Set([Value, Settings](float NewValue)
			{
				*Value = FMath::RoundToInt32(NewValue);
				Settings->ApplyAll();
				if (UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent()) { Menu->RefreshVolumes(); }
			})
			.Format([](float NewValue) { return NewValue <= 0.0f ? FString(TEXT("OFF")) : Percent(NewValue); });
		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ Row ];
		return Row;
	};

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Volume")) ];
	DefaultFocus = Volume(TEXT("Master volume"), TEXT("Everything the game plays."), &Settings->MasterVolume);
	Volume(ScctUi::GameLabel(TEXT("Audio_Settings"), TEXT("Effect.Caption"), TEXT("Effects Volume")), TEXT("Footsteps, weapons, gadgets, voices of the match."), &Settings->EffectsVolume);
	Volume(TEXT("Ambience volume"), TEXT("The maps' ambient sound: machines, rain, crowds, the hum of a room."), &Settings->AmbientVolume);
	Volume(ScctUi::GameLabel(TEXT("Audio_Settings"), TEXT("Music.Caption"), TEXT("Music Volume")), TEXT("The match's music."), &Settings->MusicVolume);
	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Menus")) ];
	Volume(TEXT("Menu music volume"), TEXT("The music of the menus and the lobby."), &Settings->MenuMusicVolume);
	Volume(TEXT("Menu sounds volume"), TEXT("The menus' clicks and beeps."), &Settings->MenuSoundsVolume);
	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Match")) ];
	Volume(TEXT("Objective sounds volume"), TEXT("The jingles that mark an objective. At OFF they are not played at all."), &Settings->ObjectiveVolume);
	Volume(TEXT("Voice chat volume"), TEXT("Other players' voices."), &Settings->VoiceVolume);
	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Hub")) ];
	Volume(TEXT("Jukebox music"), TEXT("How loud the hub's jukebox plays when you stand near it."), &Settings->HubJukeboxVolume);
	Volume(TEXT("Activity chimes"), TEXT("How loud the chimes of the hub's courses, aim range and ball game play."), &Settings->HubChimesVolume);

	// The output device and mode (Play/ScctAudioOutput.h): kept in the settings registry, drawn here because the device
	// list changes while the page is open (a device plugged in or out).
	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Output")) ];
	Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
	[
		SNew(SScctStepper)
			.Label(FText::FromString(TEXT("Output device")))
			.Description(FText::FromString(TEXT("Where the game's sound plays. Default follows the system's default device; a device that is unplugged falls back to it until it is back.")))
			.Wrap(false)
			.GetOptions([]
			{
				const UScctAudioOutputSubsystem* Output = UScctAudioOutputSubsystem::GetCurrent();
				return Output != nullptr ? Output->DeviceChoices() : TArray<FString>{ TEXT("Default") };
			})
			.Get([] { const UScctAudioOutputSubsystem* Output = UScctAudioOutputSubsystem::GetCurrent(); return Output != nullptr ? Output->ChosenDeviceIndex() : 0; })
			.Set([](int32 Index) { if (UScctAudioOutputSubsystem* Output = UScctAudioOutputSubsystem::GetCurrent()) { Output->ChooseDevice(Index); } })
	];
	Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
	[
		SNew(SScctStepper)
			.Label(FText::FromString(TEXT("Output mode")))
			.Description(FText::FromString(TEXT("Headphones renders the sounds around you binaurally (HRTF), so above, below and behind can be told apart. Speakers pans them between the speakers.")))
			.Wrap(false)
			.Options({ TEXT("Speakers"), TEXT("Headphones") })
			.Get([] { return UScctAudioOutputSubsystem::HeadphonesChosen() ? 1 : 0; })
			.Set([](int32 Index) { FScctSettings::SetInt(TEXT("Audio.OutputMode"), Index); FScctSettings::Save(); })
	];

	for (const FName Page : RegistryPagesOfTab(1))
	{
		AddRegistryRows(Rows, Page);
	}
	return Rows;
}

// ---- controls -------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctSettingsPanel::BuildControls()
{
	UScctControlSettings* Controls = UScctControlSettings::Get();
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](const TSharedRef<SWidget>& Row) { Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ Row ]; };
	const auto Speed = [this, &Add](const FString& Caption, const TCHAR* Description, float* Value, float Min, float Max, float Step) -> TSharedRef<SWidget>
	{
		const TSharedRef<SWidget> Row = SNew(SScctSlider)
			.Label(FText::FromString(Caption))
			.Description(FText::FromString(Description))
			.Min(Min).Max(Max).Step(Step)
			.Get([Value] { return *Value; })
			.Set([this, Value](float NewValue) { *Value = NewValue; ApplyControls(); })
			.Format([](float NewValue) { return Times(NewValue); });
		Add(Row);
		return Row;
	};
	const auto Switch = [this, &Add](const FString& Caption, const TCHAR* Description, TFunction<bool()> Get, TFunction<void(bool)> Set)
	{
		Add(SNew(SScctToggle)
			.Label(FText::FromString(Caption))
			.Description(FText::FromString(Description))
			.Get(Get)
			.Set([this, Set](bool bValue) { Set(bValue); ApplyControls(); }));
	};
	const auto HoldToggle = [this, &Add](const FString& Caption, const TCHAR* Description, bool FScctLookOptions::* Field, bool bMerc)
	{
		UScctControlSettings* C = UScctControlSettings::Get();
		Add(SNew(SScctStepper)
			.Label(FText::FromString(Caption))
			.Description(FText::FromString(Description))
			.Options({ TEXT("Toggle"), TEXT("Hold") })
			.Get([C, Field, bMerc] { return (bMerc ? C->MercMouse : C->SpyMouse).*Field ? 1 : 0; })
			.Set([this, C, Field, bMerc](int32 Value)
			{
				// The keyboard's and the controller's choice are the same choice.
				(bMerc ? C->MercMouse : C->SpyMouse).*Field = Value == 1;
				(bMerc ? C->MercPad : C->SpyPad).*Field = Value == 1;
				ApplyControls();
			}));
	};

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Mouse")) ];
	DefaultFocus = Speed(TEXT("Mouse speed"), TEXT("A multiplier over every mouse count, for both sides. 1 is the original game's turn per count."), &Controls->MouseScale, 0.1f, 4.0f, 0.05f);
	Switch(ScctUi::GameLabel(TEXT("Controller_Settings"), TEXT("InvertYCaption"), TEXT("Invert Y Axis")), TEXT("Pushing the mouse forward looks down."), [Controls] { return Controls->bInvertY; }, [Controls](bool bValue) { Controls->bInvertY = bValue; });
	Switch(ScctUi::GameLabel(TEXT("Controller_Settings"), TEXT("InvertXCaption"), TEXT("Invert X Axis (Spy)")), TEXT("The spy's free look turns the other way."), [Controls] { return Controls->bInvertXSpy; }, [Controls](bool bValue) { Controls->bInvertXSpy = bValue; });

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Spy")) ];
	Speed(TEXT("Free look"), TEXT("The spy's look with the gun holstered."), &Controls->SpyMouse.FreeLook, 0.05f, 10.0f, 0.05f);
	Speed(TEXT("Gun look"), TEXT("The spy's look with the gun drawn."), &Controls->SpyMouse.Look, 0.05f, 10.0f, 0.05f);
	Speed(TEXT("Binoculars"), TEXT("The look through the binoculars, times the look."), &Controls->SpyMouse.Scope, 0.05f, 10.0f, 0.05f);
	Speed(TEXT("Sticky camera"), TEXT("The look of a sticky camera."), &Controls->SpyMouse.Cam, 0.05f, 10.0f, 0.05f);
	HoldToggle(TEXT("Walk key"), TEXT("Walk while the key is held, or switch walking on and off."), &FScctLookOptions::bWalkHold, false);
	HoldToggle(TEXT("First person key"), TEXT("Look in first person while the key is held, or switch."), &FScctLookOptions::bPerspectiveHold, false);
	HoldToggle(TEXT("Cloak key"), TEXT("Cloak while the key is held, or switch."), &FScctLookOptions::bCloakHold, false);
	Switch(TEXT("Free look while rolling"), TEXT("The spy's camera may turn during a roll."), [Controls] { return Controls->bSpyRollFreeLook; }, [Controls](bool bValue) { Controls->bSpyRollFreeLook = bValue; });

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Mercenary")) ];
	Speed(TEXT("Look"), TEXT("The mercenary's look."), &Controls->MercMouse.Look, 0.05f, 10.0f, 0.05f);
	Speed(TEXT("Sniper scope"), TEXT("The look through the scope, times the look."), &Controls->MercMouse.Scope, 0.05f, 10.0f, 0.05f);
	Speed(TEXT("Scope vertical"), TEXT("The scope's up and down speed: 1 is the original's, 2.45 turns up and down as fast as sideways."), &Controls->MercMouse.ScopeVertical, 0.05f, 10.0f, 0.05f);
	Speed(TEXT("Camera network"), TEXT("The look of the camera network."), &Controls->MercMouse.Cam, 0.05f, 10.0f, 0.05f);
	HoldToggle(TEXT("Walk key "), TEXT("Walk while the key is held, or switch walking on and off."), &FScctLookOptions::bWalkHold, true);
	HoldToggle(TEXT("Scope key"), TEXT("Look through the scope while the key is held, or switch."), &FScctLookOptions::bScopeHold, true);

	// The controller's switches, dead zones and speeds are on the CONTROLLER tab (Menu/SScctPadPanel.h).
	for (const FName Page : RegistryPagesOfTab(2))
	{
		AddRegistryRows(Rows, Page);
	}
	return Rows;
}

// ---- controller -----------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctSettingsPanel::BuildController()
{
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const TSharedRef<SScctPadPanel> Pad = SNew(SScctPadPanel, Host)
		.bMerc(bBindMerc)
		.OnControlsChanged([this] { ApplyControls(); })
		.OnBindingsChanged([this] { bBindingsDirty = true; });
	Rows->AddSlot().AutoHeight()[ Pad ];
	DefaultFocus = Pad->GetFirstRow();
	for (const FName Page : RegistryPagesOfTab(3))
	{
		AddRegistryRows(Rows, Page);
	}
	return Rows;
}

void SScctSettingsPanel::ResetController()
{
	// Only the controller's settings: the defaults are the bridge's, read into a scratch object.
	UScctControlSettings* Controls = UScctControlSettings::Get();
	UScctControlSettings* Defaults = NewObject<UScctControlSettings>();
	if (!Controls->GetDefaults(*Defaults))
	{
		Toast(Controls->GetLastError(), true);
		return;
	}
	CopyControllerSettings(*Defaults, *Controls);
	ApplyControls();
}

// ---- bindings -------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctSettingsPanel::BuildBindings()
{
	UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
	const FScctMenuApi* Api = ScctFeature<FScctMenuApi>();
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](const TSharedRef<SWidget>& Row) { Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ Row ]; };

	TSharedPtr<SWidget> First;
	Add(SAssignNew(First, SScctStepper)
		.Label(FText::FromString(TEXT("Side")))
		.Description(FText::FromString(TEXT("Each side has its own keys.")))
		.Options({ ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 0, TEXT("Spy")), ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 1, TEXT("Mercenary")) })
		.Get([this] { return bBindMerc ? 1 : 0; })
		.Set([this](int32 Value) { bBindMerc = Value == 1; Rebuild(); }));
	DefaultFocus = First;
	// The keyboard and mouse here; the controller's buttons, with Hold B and the combinations, on the CONTROLLER tab.
	bBindPad = false;
	Rows->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 4.0f, 16.0f, 4.0f)
	[
		SNew(SScctButton)
		.Look(EScctButtonLook::Chip)
		.Text(FText::FromString(TEXT("RESET THIS SIDE")))
		.Description(FText::FromString(TEXT("Put this side's keys and controller buttons back to the defaults.")))
		.OnPressed(FSimpleDelegate::CreateLambda([this]
		{
			ScctUi::Confirm(Host.Dialogs, TEXT("Reset the bindings"), FString::Printf(TEXT("Put every key and button of the %s back to the defaults?"), bBindMerc ? TEXT("mercenary") : TEXT("spy")), TEXT("Reset"), TEXT("Cancel"), [this]
			{
				UScctControlSettings::Get()->ResetBindings(bBindMerc);
				bBindingsDirty = true;
				Toast(TEXT("Bindings restored."));
				Rebuild();
			});
		}))
	];

	if (Menu == nullptr || Api == nullptr || Menu->GetActions().IsEmpty())
	{
		Rows->AddSlot().AutoHeight().Padding(18.0f, 20.0f)
		[
			SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Danger()).AutoWrapText(true)
			.Text(FText::FromString(TEXT("The bindings cannot be read: the bridge did not start (see the log).")))
		];
		return Rows;
	}

	const int32 Side = bBindMerc ? EScctMenuSides::Merc : EScctMenuSides::Spy;
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
			Rows->AddSlot().AutoHeight()[ ScctUi::Section(Category) ];
		}
		const FString Id = Action.Id;
		const FString Label = Action.Label;
		const bool bMerc = bBindMerc;
		const bool bPad = bBindPad;
		Add(SNew(SScctKeyRow)
			.Label(FText::FromString(Label))
			.Description(FText::FromString(FString::Printf(TEXT("Enter or A, then press the new %s. Escape gives up; Delete clears it."), bPad ? TEXT("controller button") : TEXT("key or mouse button"))))
			.GetKeys([Api, Id, bMerc, bPad]
			{
				return ReadSettingsText([&](TCHAR* Buffer, int32 Capacity) { return Api->GetActionKeys(bMerc ? 1 : 0, bPad ? 1 : 0, *Id, Buffer, Capacity); });
			})
			.OnCaptured([this, Id, Label](const FKey& Key) { HandleCaptured(Id, Label, Key); }));
	}
	return Rows;
}

void SScctSettingsPanel::HandleCaptured(const FString& ActionId, const FString& Label, const FKey& Key)
{
	const FScctMenuApi* Api = ScctFeature<FScctMenuApi>();
	if (Api == nullptr)
	{
		return;
	}
	if (!Key.IsValid())
	{
		Bind(ActionId, FString());
		Toast(FString::Printf(TEXT("%s is not bound now."), *Label));
		return;
	}
	const FString Name = bBindPad ? PadName(Key) : KeyboardName(Key);
	if (Name.IsEmpty())
	{
		Toast(FString::Printf(TEXT("%s cannot be bound here."), *Key.GetDisplayName().ToString()), true);
		return;
	}

	// What the key does now, besides this action.
	const FString Taken = ReadSettingsText([&](TCHAR* Buffer, int32 Capacity) { return Api->GetKeyActions(bBindMerc ? 1 : 0, bBindPad ? 1 : 0, *Name, Buffer, Capacity); });
	TArray<FString> Others;
	Taken.ParseIntoArray(Others, TEXT("\n"));
	Others.Remove(Label);
	if (Others.IsEmpty())
	{
		Bind(ActionId, Name);
		return;
	}
	ScctUi::Confirm(Host.Dialogs, TEXT("Key already in use"),
		FString::Printf(TEXT("%s is bound to %s. Bind it to %s instead? It will no longer do %s."), *Name, *FString::Join(Others, TEXT(", ")), *Label, Others.Num() == 1 ? *Others[0] : TEXT("those")),
		TEXT("Replace"), TEXT("Cancel"), [this, ActionId, Name] { Bind(ActionId, Name); });
}

void SScctSettingsPanel::Bind(const FString& ActionId, const FString& KeyName)
{
	const FScctMenuApi* Api = ScctFeature<FScctMenuApi>();
	if (Api != nullptr && Api->BindAction(bBindMerc ? 1 : 0, bBindPad ? 1 : 0, *ActionId, *KeyName, 0) != 0)
	{
		bBindingsDirty = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("Settings: %s %s bound to '%s'"), bBindMerc ? TEXT("merc") : TEXT("spy"), *ActionId, *KeyName);
	}
}

// ---- game and profile -----------------------------------------------------------------------------------------------

TSharedRef<SWidget> SScctSettingsPanel::BuildGame()
{
	UScctUserSettings* Settings = UScctUserSettings::Get();
	UScctControlSettings* Controls = UScctControlSettings::Get();
	UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	const auto Add = [&Rows](const TSharedRef<SWidget>& Row) { Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ Row ]; };

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Interface")) ];
	TSharedPtr<SWidget> First;
	Add(SAssignNew(First, SScctStepper)
		.Label(FText::FromString(ScctUi::GameLabel(TEXT("Controller_Settings"), TEXT("UnitSystem.Caption"), TEXT("Unit System"))))
		.Description(FText::FromString(TEXT("The unit of the distances the HUD shows.")))
		.Options({ ScctUi::GameTextItem(TEXT("Controller_Settings"), TEXT("txt_Unit"), 1, TEXT("Meters")), ScctUi::GameTextItem(TEXT("Controller_Settings"), TEXT("txt_Unit"), 0, TEXT("Feet")) })
		.Get([Settings] { return Settings->bDistanceInFeet ? 1 : 0; })
		.Set([Settings](int32 Value) { Settings->bDistanceInFeet = Value == 1; Settings->ApplyAll(); }));
	DefaultFocus = First;
	FString Language = Menu != nullptr && ScctFeature<FScctMenuApi>() != nullptr
		? ReadSettingsText([](TCHAR* Buffer, int32 Capacity) { return ScctFeature<FScctMenuApi>()->GetText(static_cast<int32>(EScctMenuText::Language), Buffer, Capacity); })
		: FString();
	Add(SNew(SScctStepper)
		.Label(FText::FromString(TEXT("Language")))
		.Description(FText::FromString(TEXT("The language of the game's own texts is the language of the copy of the game the content was converted from. Convert another copy to play in its language.")))
		.Options({ Language.IsEmpty() ? FString(TEXT("From the content")) : Language.ToUpper() })
		.Get([] { return 0; })
		.Set([](int32) {}));

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Keys")) ];
	Add(SNew(SScctToggle)
		.Label(FText::FromString(TEXT("Auto-hack enemy communications")))
		.Description(FText::FromString(TEXT("Hack the enemy's communications whenever a spy bullet or a sticky camera can, without holding the key.")))
		.Get([Controls] { return Controls->bAutoHackComms; })
		.Set([this, Controls](bool bValue) { Controls->bAutoHackComms = bValue; ApplyControls(); }));
	Add(SNew(SScctStepper)
		.Label(FText::FromString(TEXT("Mouse wheel")))
		.Description(FText::FromString(TEXT("What turning the mouse wheel does.")))
		.Options({ TEXT("Changes gadget"), TEXT("Sets walk speed") })
		.Get([Controls] { return Controls->bMouseWheelSpeed ? 1 : 0; })
		.Set([this, Controls](int32 Value) { Controls->bMouseWheelSpeed = Value == 1; ApplyControls(); }));
	return Rows;
}

TSharedRef<SWidget> SScctSettingsPanel::BuildProfile()
{
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
	UScctOnlineSubsystem* Visit = Weak.Get();
	const bool bSignedIn = Visit != nullptr && Visit->GetStatus().AccountState == static_cast<int32>(EScctOnlineAccountState::SignedIn);
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Player")) ];
	TSharedPtr<SWidget> First;
	Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
	[
		SAssignNew(First, SScctTextRow)
		.IsEnabled(!bSignedIn)
		.Label(FText::FromString(TEXT("Name")))
		.Description(FText::FromString(bSignedIn ? TEXT("Signed in: you play under your account's name.") : TEXT("The name the other players see, offline and on servers that take guests.")))
		.Hint(TEXT("Player"))
		.Initial(Visit != nullptr ? (bSignedIn ? Visit->GetAccountName() : Visit->GetPlayerName()) : FString())
		.MaxLength(24)
		.OnChanged([Weak](const FString& Name) { if (Weak.IsValid() && !Name.IsEmpty()) { Weak->SetPlayerName(Name); } })
	];
	DefaultFocus = First;

	Rows->AddSlot().AutoHeight()[ ScctUi::Section(TEXT("Account")) ];
	Rows->AddSlot().AutoHeight().Padding(18.0f, 4.0f, 18.0f, 12.0f)
	[
		SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
		.Text(TAttribute<FText>::CreateLambda([Weak]
		{
			const UScctOnlineSubsystem* V = Weak.Get();
			if (V == nullptr || !V->HasMaster()) { return FText::FromString(TEXT("No master server is set, so there is no account to sign in to.")); }
			switch (static_cast<EScctOnlineAccountState>(V->GetStatus().AccountState))
			{
			case EScctOnlineAccountState::SignedIn: return FText::FromString(FString::Printf(TEXT("Signed in as %s. Your rank and name follow you to every server."), *V->GetAccountName()));
			case EScctOnlineAccountState::SigningIn: return FText::FromString(FString::Printf(TEXT("Finish signing in in your browser. Code: %s"), *V->GetSignInCode()));
			case EScctOnlineAccountState::Failed: return FText::FromString(V->GetAccountError());
			default: return FText::FromString(TEXT("Not signed in. Sign in to keep your name and rank, and to join the official servers."));
			}
		}))
	];
	Rows->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(16.0f, 0.0f)
	[
		SNew(SScctButton)
		.IsEnabled(TAttribute<bool>::CreateLambda([Weak] { return Weak.IsValid() && Weak->HasMaster(); }))
		.Text(TAttribute<FText>::CreateLambda([Weak] { return FText::FromString(Weak.IsValid() && Weak->GetStatus().AccountState == static_cast<int32>(EScctOnlineAccountState::SignedIn) ? TEXT("SIGN OUT") : TEXT("SIGN IN")); }))
		.Description(FText::FromString(TEXT("Sign in with your OpenSCCT account in your browser, or sign out.")))
		.OnPressed(FSimpleDelegate::CreateLambda([Weak]
		{
			if (UScctOnlineSubsystem* V = Weak.Get())
			{
				V->GetStatus().AccountState == static_cast<int32>(EScctOnlineAccountState::SignedIn) ? V->SignOut() : V->BeginSignIn();
			}
		}))
	];
	return Rows;
}

TSharedPtr<SWidget> SScctSettingsPanel::AddRegistryRows(const TSharedRef<SVerticalBox>& Rows, FName Page)
{
	TSharedPtr<SWidget> First;
	FString Group;
	bool bFirstGroup = true;
	for (const FScctSettingDesc* Desc : FScctSettings::OptionsOf(Page))
	{
		const FString ThisGroup = Desc->Group.IsEmpty() ? Page.ToString() : Desc->Group.ToString();
		if (bFirstGroup || ThisGroup != Group)
		{
			Rows->AddSlot().AutoHeight()[ ScctUi::Section(ThisGroup) ];
			Group = ThisGroup;
			bFirstGroup = false;
		}
		const FName Key = Desc->Key;
		const FText Label = Desc->Label;
		const FText Tooltip = Desc->Tooltip;
		TSharedPtr<SWidget> Row;
		switch (Desc->Type)
		{
		case EScctSettingType::Bool:
			Row = SNew(SScctToggle)
				.Label(Label)
				.Description(Tooltip)
				.Get([Key] { return FScctSettings::GetBool(Key); })
				.Set([Key](bool bValue) { FScctSettings::SetBool(Key, bValue); });
			break;
		case EScctSettingType::Float:
		case EScctSettingType::Int:
		{
			const bool bInt = Desc->Type == EScctSettingType::Int;
			const float Step = Desc->Step > 0.0f ? Desc->Step : (bInt ? 1.0f : (Desc->Max - Desc->Min) / 100.0f);
			const TFunction<FString(float)> Format = Desc->FormatValue;
			Row = SNew(SScctSlider)
				.Label(Label)
				.Description(Tooltip)
				.Min(Desc->Min).Max(Desc->Max).Step(Step)
				.Get([Key, bInt] { return bInt ? static_cast<float>(FScctSettings::GetInt(Key)) : FScctSettings::GetFloat(Key); })
				.Set([Key, bInt](float Value) { if (bInt) { FScctSettings::SetInt(Key, FMath::RoundToInt32(Value)); } else { FScctSettings::SetFloat(Key, Value); } })
				.Format([Format, bInt](float Value) { return Format ? Format(Value) : (bInt ? FString::FromInt(FMath::RoundToInt32(Value)) : FString::SanitizeFloat(FMath::RoundToFloat(Value * 100.0f) / 100.0f)); });
			break;
		}
		case EScctSettingType::Choice:
		{
			TArray<FString> Options;
			for (const FText& Choice : Desc->Choices)
			{
				Options.Add(Choice.ToString());
			}
			Row = SNew(SScctStepper)
				.Label(Label)
				.Description(Tooltip)
				.Wrap(false)
				.Options(Options)
				.Get([Key, Count = Options.Num()] { return FMath::Clamp(FScctSettings::GetInt(Key), 0, FMath::Max(Count - 1, 0)); })
				.Set([Key](int32 Value) { FScctSettings::SetInt(Key, Value); });
			break;
		}
		default:
			Row = SNew(SScctTextRow)
				.Label(Label)
				.Description(Tooltip)
				.Initial(FScctSettings::GetString(Key))
				.OnChanged([Key](const FString& Value) { FScctSettings::SetString(Key, Value); });
			break;
		}
		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)[ Row.ToSharedRef() ];
		if (!First.IsValid())
		{
			First = Row;
		}
	}
	return First;
}

TSharedRef<SWidget> SScctSettingsPanel::BuildRegistryTab(FName Page)
{
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	DefaultFocus = AddRegistryRows(Rows, Page);
	return Rows;
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeSettings(const FScctMenuHost& Host)
{
	return SNew(SScctSettingsPanel, Host);
}
