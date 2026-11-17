#include "Menu/SScctMatchMenu.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctHudModel.h"
#include "Hud/ScctHudSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/SScctSettingsPanel.h"
#include "Menu/ScctMenuDriver.h"
#include "Menu/ScctMenuKit.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/PackageName.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctOnlineApi.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	bool MatchMenuIsBackKey(const FKey& Key)
	{
		return Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right;
	}

	int32 MatchMenuTabDirection(const FKey& Key)
	{
		if (Key == EKeys::Q || Key == EKeys::PageUp || Key == EKeys::Gamepad_LeftShoulder) { return -1; }
		if (Key == EKeys::E || Key == EKeys::PageDown || Key == EKeys::Gamepad_RightShoulder) { return 1; }
		return 0;
	}

	int32 MatchMenuModeIndex(int32 ModeId)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (UScctMenuSubsystem::ModeId(Index) == ModeId) { return Index; }
		}
		return INDEX_NONE;
	}

	const UScctHudSubsystem* MatchMenuHud(const TWeakObjectPtr<UWorld>& World)
	{
		return World.IsValid() ? World->GetSubsystem<UScctHudSubsystem>() : nullptr;
	}

	FLinearColor MatchMenuTeamColour(int32 Team)
	{
		return Team == 0 || Team == 1 ? ScctHudStyle::Accent(Team) : ScctHudStyle::Text();
	}

	FString MatchMenuClock(int32 Seconds)
	{
		return Seconds < 0 ? FString(TEXT("--:--")) : FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	}
}

void SScctMatchMenu::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;
	World = InArgs._World;
	bOnlineLevel = InArgs._bOnlineLevel;
	OnResume = InArgs._OnResume;
	OnLeave = InArgs._OnLeave;
	OnWantsOpen = InArgs._OnWantsOpen;

	SAssignNew(Dialogs, SScctDialogLayer);
	Dialogs->FocusAfter = SharedThis(this);
	Host.Online = Online;
	Host.Dialogs = Dialogs;
	Host.bInMatch = true;
	Host.Open = [this](const FString& Name) { ShowNamed(Name); };
	Host.Back = [this] { GoBack(); };

	ChildSlot
	[
		ScctUi::ScaleForSmallScreens(
			SNew(SOverlay)
			// The match behind, darkened and blurred while the menu is open.
			+ SOverlay::Slot()
			[
				SNew(SBox)
				.Visibility_Lambda([this] { return bOpen ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SBackgroundBlur).BlurStrength(TAttribute<float>::CreateLambda([this] { return 9.0f * Shown; })) ]
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor_Lambda([this] { return FSlateColor(FLinearColor(0.01f, 0.015f, 0.025f, 0.62f * Shown)); })
					]
				]
			]
			+ SOverlay::Slot().Padding(FMargin(72.0f, 56.0f, 72.0f, 48.0f))
			[
				SAssignNew(Body, SBox)
				.Visibility_Lambda([this] { return bOpen ? EVisibility::Visible : IsShowingResults() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			]
			+ SOverlay::Slot()[ Dialogs.ToSharedRef() ])
	];
	Show(EView::Pause);
}

void SScctMatchMenu::SetOpen(bool bInOpen)
{
	if (bOpen == bInOpen)
	{
		return;
	}
	bOpen = bInOpen;
	if (bOpen)
	{
		Shown = 0.0f;
		ScctUi::Play(EScctUiSound::Open);
		// Offline the results are the menu's page while the round is over; otherwise the menu opens on its first page.
		Show(bResultsReady && !bOnlineLevel ? EView::Results : EView::Pause);
	}
	else
	{
		if (Settings.IsValid())
		{
			Settings->OnClosing();
			Settings.Reset();
		}
		if (View == EView::Settings)
		{
			Show(EView::Pause);
		}
	}
}

void SScctMatchMenu::Show(EView NewView, const FString& Sub)
{
	if (View == EView::Settings && NewView != EView::Settings && Settings.IsValid())
	{
		Settings->OnClosing();
		Settings.Reset();
	}
	View = NewView;
	switch (View)
	{
	case EView::Pause:
		Body->SetContent(BuildPause());
		break;
	case EView::Settings:
	{
		const TSharedRef<SScctSettingsPanel> Panel = SNew(SScctSettingsPanel, Host).InitialTab(FMath::Max(SScctSettingsPanel::FindTab(Sub), 0));
		Settings = Panel;
		Body->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Label(Panel->GetKicker().ToUpper(), ScctUi::Accent(), 12.0f) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 18.0f)
			[
				SNew(STextBlock).Font(ScctUi::H1()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(Panel->GetTitle().ToUpper()))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)[ Panel ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 22.0f, 0.0f)[ ScctUi::KeyHint(TEXT("ESC"), TEXT("B"), TEXT("Back")) ]
				+ SHorizontalBox::Slot().AutoWidth()[ ScctUi::KeyHint(TEXT("Q / E"), TEXT("LB / RB"), TEXT("Change tab")) ]
			]);
		if (!Sub.IsEmpty())
		{
			Panel->ShowSub(Sub);
		}
		DefaultFocus = Panel->GetDefaultFocus();
		break;
	}
	case EView::Results:
		Body->SetContent(BuildResults());
		break;
	}
	FocusDefault();
}

void SScctMatchMenu::ShowNamed(const FString& Name)
{
	FString Page = Name, Sub;
	Name.Split(TEXT("/"), &Page, &Sub);
	if (Page.Equals(TEXT("settings"), ESearchCase::IgnoreCase))
	{
		Show(EView::Settings, Sub);
	}
	else if (Page.Equals(TEXT("results"), ESearchCase::IgnoreCase))
	{
		Show(EView::Results);
	}
	else
	{
		Show(EView::Pause);
	}
}

void SScctMatchMenu::GoBack()
{
	ScctUi::Play(EScctUiSound::Back);
	if (View == EView::Pause)
	{
		OnResume.ExecuteIfBound();
	}
	else
	{
		Show(EView::Pause);
	}
}

void SScctMatchMenu::FocusDefault()
{
	if (!bOpen)
	{
		return;
	}
	TSharedPtr<SWidget> Focus = View == EView::Settings && Settings.IsValid() ? Settings->GetDefaultFocus() : DefaultFocus.Pin();
	FSlateApplication::Get().SetKeyboardFocus(Focus.IsValid() ? Focus : TSharedPtr<SWidget>(SharedThis(this)), EFocusCause::SetDirectly);
}

FReply SScctMatchMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!bOpen)
	{
		return FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();
	ScctUi::NoteInput(Key);
	if (Dialogs->IsOpen())
	{
		return Dialogs->OnKeyDown(MyGeometry, InKeyEvent);
	}
	if (MatchMenuIsBackKey(Key))
	{
		if (!(View == EView::Settings && Settings.IsValid() && Settings->HandleBack()))
		{
			GoBack();
		}
		return FReply::Handled();
	}
	if (View == EView::Settings && Settings.IsValid() && !ScctUi::IsTyping())
	{
		const int32 Direction = MatchMenuTabDirection(Key);
		if (Direction != 0)
		{
			Settings->StepTab(Direction);
			return FReply::Handled();
		}
	}
	const EUINavigation Navigation = FSlateApplication::Get().GetNavigationDirectionFromKey(InKeyEvent);
	if (Navigation != EUINavigation::Invalid)
	{
		// The focus fell off the page: an arrow or a pad push brings it back.
		if (HasKeyboardFocus())
		{
			FocusDefault();
			return FReply::Handled();
		}
		// The menu sits in the game's viewport, which takes every key that reaches it for the game (its input mode
		// is game and UI, so the match goes on underneath): the menu moves the focus itself.
		return FReply::Handled().SetNavigation(Navigation, ENavigationGenesis::Keyboard, ENavigationSource::FocusedWidget);
	}
	// Other keys go on: the game mode closes the menu on the player's own menu key, and the controls hold
	// everything else back while the menu is open.
	return FReply::Unhandled();
}

FReply SScctMatchMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	if (!bOpen)
	{
		return FReply::Unhandled();
	}
	// The left stick moves the focus as the pad's arrows do (the navigation config spaces out the repeats).
	const EUINavigation Navigation = Dialogs->IsOpen() ? EUINavigation::Invalid : FSlateApplication::Get().GetNavigationDirectionFromAnalog(InAnalogInputEvent);
	if (Navigation != EUINavigation::Invalid)
	{
		return FReply::Handled().SetNavigation(Navigation, ENavigationGenesis::Controller, ENavigationSource::FocusedWidget);
	}
	return FReply::Handled();
}

void SScctMatchMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	Shown = ScctHudStyle::Ease(Shown, bOpen ? 1.0f : 0.0f, FMath::Min(InDeltaTime, 0.05f), 10.0f);

	// Unattended runs only (-ScctMenuDo): a page asked for while the menu is closed opens it.
	FScctMenuDriver::FHost DriverHost;
	DriverHost.ShowPage = [this](const FString& Name)
	{
		if (!bOpen)
		{
			OnWantsOpen.ExecuteIfBound();
		}
		ShowNamed(Name);
	};
	FScctMenuDriver::Tick(InDeltaTime, DriverHost);

	// The results: online when the server sent them, offline a moment after the round ended (the HUD's own end
	// banner has the first seconds).
	bool bReady = false;
	int32 Revision = 0;
	if (bOnlineLevel)
	{
		const UScctOnlineSubsystem* Visit = Online.Get();
		bReady = Visit != nullptr && Visit->GetState() == EScctOnlineState::Results && Visit->HasFlag(EScctOnlineStatusFlags::ResultsReady);
		Revision = Visit != nullptr ? Visit->GetRevision() : 0;
	}
	else if (const UScctHudSubsystem* Hud = MatchMenuHud(World))
	{
		const FScctHudModel& Model = Hud->GetModel();
		bReady = Model.bHasRound && Model.Round.Ended != 0 && Model.EndAge > 4.0f;
	}
	if (bReady != bResultsReady || (bReady && Revision != ResultsRevision))
	{
		bResultsReady = bReady;
		ResultsRevision = Revision;
		if (bReady && (View == EView::Results || !bOpen))
		{
			Show(EView::Results);
		}
		else if (!bReady && View == EView::Results && !bOpen)
		{
			Show(EView::Pause);
		}
		if (bReady && !bOnlineLevel && !bOfflineResultsOpened)
		{
			// Offline nothing comes after the round by itself: the menu opens on the results with the ways on.
			bOfflineResultsOpened = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("Match menu: the round is over, showing the results"));
			OnWantsOpen.ExecuteIfBound();
		}
	}
}

// ---- pages ----------------------------------------------------------------------------------------------------------

FString SScctMatchMenu::GetMapId() const
{
	if (!World.IsValid())
	{
		return FString();
	}
	FString Map = UGameplayStatics::ParseOption(World->URL.ToString(), TEXT("ScctMap"));
	return Map.IsEmpty() ? FPackageName::GetShortName(World->URL.Map) : Map;
}

bool SScctMatchMenu::IsMercLocal() const
{
	if (const UScctHudSubsystem* Hud = MatchMenuHud(World))
	{
		return Hud->GetModel().LocalTeam == 1;
	}
	return false;
}

bool SScctMatchMenu::CanChangeSide() const
{
	if (bOnlineLevel || !World.IsValid())
	{
		return false;
	}
	// Merc Deathmatch has mercenaries only.
	const FString Mode = UGameplayStatics::ParseOption(World->URL.ToString(), TEXT("Mode"));
	return !Mode.Equals(TEXT("MercDeathMatch"), ESearchCase::IgnoreCase);
}

void SScctMatchMenu::Restart(bool bSwapSide)
{
	UWorld* Level = World.Get();
	if (Level == nullptr)
	{
		return;
	}
	// The level again with the same options (the menu's own play page wrote them), the side swapped if asked.
	TArray<FString> Options;
	bool bHadSide = false;
	for (const FString& Option : Level->URL.Op)
	{
		if (Option.StartsWith(TEXT("Side="), ESearchCase::IgnoreCase))
		{
			bHadSide = true;
			const bool bMerc = Option.Mid(5).Equals(TEXT("merc"), ESearchCase::IgnoreCase);
			Options.Add(bSwapSide ? FString(bMerc ? TEXT("Side=spy") : TEXT("Side=merc")) : Option);
		}
		else
		{
			Options.Add(Option);
		}
	}
	if (bSwapSide && !bHadSide)
	{
		Options.Add(IsMercLocal() ? TEXT("Side=spy") : TEXT("Side=merc"));
	}
	const FString Joined = FString::Join(Options, TEXT("?"));
	UE_LOG(LogOpenSCCT, Log, TEXT("Match menu: %s %s?%s"), bSwapSide ? TEXT("changing side,") : TEXT("playing again,"), *Level->URL.Map, *Joined);
	UGameplayStatics::OpenLevel(Level, FName(*Level->URL.Map), true, Joined);
}

TSharedRef<SWidget> SScctMatchMenu::BuildMatchCard()
{
	UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
	const FString Map = GetMapId();
	const TWeakObjectPtr<UWorld> WeakWorld = World;
	const bool bMerc = IsMercLocal();
	return ScctUi::Card(bOnlineLevel ? TEXT("Online match") : TEXT("Match against bots"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Picture(Menu != nullptr ? Menu->GetMapPicture(Map, bMerc) : nullptr, 452.0f, 200.0f) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(Menu != nullptr ? Menu->GetMapTitle(Map) : Map.ToUpper()))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(ScctUi::Caps(11.5f)).ColorAndOpacity(ScctHudStyle::Accent(bMerc ? 1 : 0))
			.Text_Lambda([WeakWorld, bMerc]
			{
				const UScctHudSubsystem* Hud = MatchMenuHud(WeakWorld);
				UScctMenuSubsystem* M = UScctMenuSubsystem::GetCurrent();
				const int32 Mode = Hud != nullptr && Hud->GetModel().bHasRound ? MatchMenuModeIndex(Hud->GetModel().Round.GameMode) : INDEX_NONE;
				const FString ModeName = M != nullptr && Mode != INDEX_NONE ? M->GetModeName(Mode) : FString();
				const FString Side = ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), bMerc ? 1 : 0, bMerc ? TEXT("Mercenary") : TEXT("Spy"));
				return FText::FromString((ModeName.IsEmpty() ? Side : ModeName + TEXT(" · ") + Side).ToUpper());
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ ScctUi::Rule(0.1f) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			// The score and the clock of the round.
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Label(ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 0, TEXT("Spy")).ToUpper(), ScctHudStyle::Accent(0), 11.0f) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(ScctUi::Display(34.0f)).ColorAndOpacity(ScctHudStyle::Text())
					.Text_Lambda([WeakWorld] { const UScctHudSubsystem* H = MatchMenuHud(WeakWorld); return FText::AsNumber(H != nullptr ? H->GetModel().Round.TeamScore[0] : 0); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ ScctUi::Label(TEXT("TIME LEFT"), ScctHudStyle::TextDim(), 11.0f) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(ScctUi::Display(34.0f)).ColorAndOpacity(ScctHudStyle::Text())
					.Text_Lambda([WeakWorld] { const UScctHudSubsystem* H = MatchMenuHud(WeakWorld); return FText::FromString(MatchMenuClock(H != nullptr && H->GetModel().bHasRound ? H->GetModel().Round.RemainingSeconds : -1)); })
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[ ScctUi::Label(ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 1, TEXT("Mercenary")).ToUpper(), ScctHudStyle::Accent(1), 11.0f) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock).Font(ScctUi::Display(34.0f)).ColorAndOpacity(ScctHudStyle::Text())
					.Text_Lambda([WeakWorld] { const UScctHudSubsystem* H = MatchMenuHud(WeakWorld); return FText::AsNumber(H != nullptr ? H->GetModel().Round.TeamScore[1] : 0); })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
			.Text(FText::FromString(bOnlineLevel
				? TEXT("The match goes on while this menu is open: your character stands where you left it.")
				: TEXT("The bots play on while this menu is open: your character stands where you left it.")))
		], FMargin(24.0f, 20.0f));
}

TSharedRef<SWidget> SScctMatchMenu::BuildPause()
{
	const TSharedRef<SVerticalBox> Entries = SNew(SVerticalBox);
	const auto Entry = [&Entries](const FString& Caption, const FString& Sub, const FString& Description, TFunction<void()> OnPressed, EScctButtonLook Look = EScctButtonLook::Menu) -> TSharedRef<SScctButton>
	{
		TSharedPtr<SScctButton> Made;
		Entries->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SAssignNew(Made, SScctButton)
			.Look(Look)
			.Text(FText::FromString(Caption.ToUpper()))
			.SubText(FText::FromString(Sub))
			.Description(FText::FromString(Description))
			.OnPressed(FSimpleDelegate::CreateLambda([OnPressed] { OnPressed(); }))
		];
		return Made.ToSharedRef();
	};

	DefaultFocus = Entry(TEXT("Resume"), TEXT("Back to the match"), TEXT("Close the menu and play on. Escape and B do the same."), [this] { ScctUi::Play(EScctUiSound::Accept); OnResume.ExecuteIfBound(); });
	Entry(ScctUi::GameText(TEXT("Start_Menu"), TEXT("Options.Caption"), TEXT("Options")), TEXT("Video, audio, controls and key bindings"),
		TEXT("The settings, the same as in the main menu. Changes are in force at once."), [this] { ScctUi::Play(EScctUiSound::Accept); Show(EView::Settings); });
	if (CanChangeSide())
	{
		const bool bMerc = IsMercLocal();
		const FString Other = ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), bMerc ? 0 : 1, bMerc ? TEXT("Spy") : TEXT("Mercenary"));
		Entry(ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Button2"), TEXT("Switch Team")), FString::Printf(TEXT("Play this map as a %s"), *Other.ToLower()),
			TEXT("Start this match again on the other side, with the same map, mode, bots and rules."), [this, Other]
			{
				ScctUi::Confirm(Dialogs, TEXT("Change side"), FString::Printf(TEXT("Start the match again as a %s? This round's score is lost."), *Other.ToLower()),
					TEXT("Change side"), TEXT("Cancel"), [this] { Restart(true); });
			});
		Entry(TEXT("Restart"), TEXT("The same match from the start"), TEXT("Start this match again with the same side, map, mode, bots and rules."), [this]
		{
			ScctUi::Confirm(Dialogs, TEXT("Restart"), TEXT("Start the match again from the beginning? This round's score is lost."), TEXT("Restart"), TEXT("Cancel"), [this] { Restart(false); });
		});
	}
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (bOnlineLevel && Visit != nullptr && Visit->HasFlag(EScctOnlineStatusFlags::CanControl))
	{
		Entry(ScctUi::GameText(TEXT("Start_Menu"), TEXT("Returntolobby.Caption"), TEXT("Return to lobby")), TEXT("End the match for everyone"),
			TEXT("You control this server: stop the match and bring every player back to the lobby."), [this]
			{
				ScctUi::Confirm(Dialogs, ScctUi::GameText(TEXT("Start_Menu"), TEXT("Returntolobby.Caption"), TEXT("Return to lobby")),
					ScctUi::GameText(TEXT("BackToLobbyValidation"), TEXT("LabelCaption"), TEXT("Stop the match and bring everyone back to the lobby?")),
					TEXT("Return to lobby"), TEXT("Cancel"), [this] { if (UScctOnlineSubsystem* V = Online.Get()) { V->BackToLobby(); } }, true);
			});
	}
	const FString LeaveCaption = bOnlineLevel ? FString(TEXT("Leave the server")) : ScctUi::GameText(TEXT("Start_Menu"), TEXT("Quit.Caption"), TEXT("Quit Session"));
	Entry(LeaveCaption, bOnlineLevel ? TEXT("Back to the menus; your place is given up") : TEXT("Back to the main menu"),
		bOnlineLevel ? TEXT("Leave the server. The match goes on without you.") : TEXT("End this match and go back to the main menu."), [this, LeaveCaption]
		{
			ScctUi::Confirm(Dialogs, LeaveCaption,
				bOnlineLevel ? TEXT("Leave the server and go back to the menus?") : TEXT("End the match and go back to the main menu?"),
				bOnlineLevel ? TEXT("Leave") : TEXT("Quit match"), TEXT("Cancel"), [this] { OnLeave.ExecuteIfBound(); }, true);
		});

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(700.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 0.0f, 0.0f, 4.0f)
				[
					ScctUi::Label(bOnlineLevel ? TEXT("ONLINE · THE MATCH GOES ON") : TEXT("PAUSED · THE BOTS PLAY ON"), ScctUi::Accent(), 12.0f)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 30.0f)
				[
					SNew(STextBlock).Font(ScctUi::Display(52.0f)).ColorAndOpacity(ScctHudStyle::Text())
					.Text(FText::FromString(ScctUi::GameText(TEXT("Start_Menu"), TEXT("TitlePage"), TEXT("In-Game Menu")).ToUpper()))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(-24.0f, 0.0f, 0.0f, 0.0f)[ SNew(SBox).WidthOverride(480.0f)[ Entries ] ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 30.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 22.0f, 0.0f)[ ScctUi::KeyHint(TEXT("ENTER"), TEXT("A"), TEXT("Select")) ]
					+ SHorizontalBox::Slot().AutoWidth()[ ScctUi::KeyHint(TEXT("ESC"), TEXT("B"), TEXT("Resume")) ]
				]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(500.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ BuildMatchCard() ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
				[
					ScctUi::Card(FString(), ScctUi::DescriptionPane(), FMargin(24.0f, 18.0f))
				]
			]
		];
}

bool SScctMatchMenu::ReadResults(int32& OutWinner, FString& OutReason, TArray<TArray<FString>>& OutRows, TArray<int32>& OutTeams, TArray<bool>& OutLocal) const
{
	OutWinner = 255;
	if (bOnlineLevel)
	{
		const UScctOnlineSubsystem* Visit = Online.Get();
		FScctOnlineResults Results;
		TArray<FScctOnlineResultRow> Rows;
		if (Visit == nullptr || !Visit->GetResults(Results, Rows))
		{
			return false;
		}
		OutWinner = Results.WinningTeam;
		switch (Results.WinningReason)
		{
		case 0: OutReason = Results.ObjectivesNeeded > 0 ? FString::Printf(TEXT("%d of %d objectives taken"), Results.ObjectivesTaken, Results.ObjectivesNeeded) : TEXT("The score limit was reached"); break;
		case 1: OutReason = TEXT("Time ran out"); break;
		case 2: OutReason = TEXT("A side was eliminated"); break;
		default: OutReason = TEXT("The round was stopped"); break;
		}
		Rows.StableSort([](const FScctOnlineResultRow& A, const FScctOnlineResultRow& B) { return A.Team != B.Team ? A.Team < B.Team : A.Score > B.Score; });
		for (const FScctOnlineResultRow& Row : Rows)
		{
			OutRows.Add({ Row.Name, FString::FromInt(Row.Score), FString::FromInt(Row.Kills), FString::FromInt(Row.Deaths) });
			OutTeams.Add(Row.Team);
			OutLocal.Add(Row.Id == Visit->GetStatus().LocalId);
		}
		return true;
	}

	const UScctHudSubsystem* Hud = MatchMenuHud(World);
	if (Hud == nullptr || !Hud->GetModel().bHasRound)
	{
		return false;
	}
	const FScctHudModel& Model = Hud->GetModel();
	OutWinner = Model.Round.Ended != 0 ? Model.Round.WinningTeam : 255;
	OutReason = Model.Round.Ended == 0 ? FString(TEXT("The round is still being played"))
		: Model.Round.RemainingSeconds == 0 ? FString(TEXT("Time ran out"))
		: Model.Round.NumObjectives > 0 ? FString::Printf(TEXT("%d of %d objectives taken"), Model.Round.NumObjectives - Model.Round.ObjectivesLeft, Model.Round.NumObjectives)
		: FString(TEXT("The round is over"));
	TArray<FScctHudScoreRow> Rows = Model.Scores;
	Rows.StableSort([](const FScctHudScoreRow& A, const FScctHudScoreRow& B) { return A.Team != B.Team ? A.Team < B.Team : A.Score > B.Score; });
	for (const FScctHudScoreRow& Row : Rows)
	{
		OutRows.Add({ Model.Text(Row.NameId), FString::FromInt(Row.Score), FString::FromInt(Row.Kills), FString::FromInt(Row.Deaths) });
		OutTeams.Add(Row.Team);
		OutLocal.Add((Row.Flags & EScctHudScoreFlags::Local) != 0);
	}
	return true;
}

TSharedRef<SWidget> SScctMatchMenu::BuildResults()
{
	int32 Winner = 255;
	FString Reason;
	TArray<TArray<FString>> Rows;
	TArray<int32> Teams;
	TArray<bool> Local;
	const bool bHave = ReadResults(Winner, Reason, Rows, Teams, Local);

	const FString WinText = ScctUi::GameText(TEXT("Ending_Menu"), TEXT("TextTeamWin"), TEXT("wins"));
	const bool bDecided = Winner == 0 || Winner == 1;
	const bool bEnded = bOnlineLevel || bResultsReady;
	const FString Headline = bDecided
		? FString::Printf(TEXT("%s %s"), *ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), Winner, Winner == 0 ? TEXT("Spy") : TEXT("Mercenary")), *WinText)
		: bEnded ? FString(TEXT("Round over")) : FString(TEXT("Scoreboard"));
	const bool bLocalWon = bDecided && Winner == (IsMercLocal() ? 1 : 0);
	const FString Kicker = !bEnded ? FString(TEXT("ROUND IN PROGRESS"))
		: bLocalWon ? ScctUi::GameText(TEXT("Ending_Menu"), TEXT("txt_Victory"), TEXT("Victory")).ToUpper()
		: bDecided ? FString(TEXT("DEFEAT")) : FString(TEXT("NO WINNER"));

	// Two tables, one per side.
	const auto SideTable = [&](int32 Team) -> TSharedRef<SWidget>
	{
		const TSharedRef<SVerticalBox> Table = SNew(SVerticalBox);
		const auto Line = [&Table](const TArray<FString>& Cells, const FLinearColor& Colour, const FSlateFontInfo& Font, bool bHighlight)
		{
			const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
			Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)[ SNew(STextBlock).Font(Font).ColorAndOpacity(Colour).Text(FText::FromString(Cells[0])) ];
			for (int32 Index = 1; Index < Cells.Num(); ++Index)
			{
				Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(76.0f).HAlign(HAlign_Right)[ SNew(STextBlock).Font(Font).ColorAndOpacity(Colour).Text(FText::FromString(Cells[Index])) ] ];
			}
			Table->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(bHighlight ? ScctUi::Accent().CopyWithNewOpacity(0.16f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.03f))
				.Padding(FMargin(14.0f, 8.0f))[ Row ]
			];
		};
		Line({ TEXT("PLAYER"), TEXT("SCORE"), TEXT("KILLS"), TEXT("DEATHS") }, ScctHudStyle::TextDim(), ScctUi::Caps(11.0f), false);
		int32 Count = 0;
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			if (Teams[Index] == Team)
			{
				++Count;
				Line(Rows[Index], Local[Index] ? ScctHudStyle::Accent(Team) : ScctHudStyle::Text(), ScctUi::Body(Local[Index]), Local[Index]);
			}
		}
		if (Count == 0)
		{
			Table->AddSlot().AutoHeight().Padding(14.0f, 8.0f)[ ScctUi::Label(TEXT("Nobody"), ScctHudStyle::TextDim(), 12.0f) ];
		}
		return ScctUi::Card(ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), Team, Team == 0 ? TEXT("Spy") : TEXT("Mercenary")), Table, FMargin(18.0f, 16.0f));
	};

	const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	if (!bOnlineLevel)
	{
		TSharedPtr<SScctButton> Again;
		Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SAssignNew(Again, SScctButton)
			.Look(EScctButtonLook::Primary)
			.MinWidth(220.0f)
			.Text(FText::FromString(TEXT("PLAY AGAIN")))
			.Description(FText::FromString(TEXT("The same match again: map, mode, side, bots and rules.")))
			.OnPressed(FSimpleDelegate::CreateLambda([this] { Restart(false); }))
		];
		DefaultFocus = Again;
		if (CanChangeSide())
		{
			Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
			[
				SNew(SScctButton)
				.Text(FText::FromString(ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Button2"), TEXT("Switch Team")).ToUpper()))
				.Description(FText::FromString(TEXT("The same match on the other side.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { Restart(true); }))
			];
		}
		Buttons->AddSlot().AutoWidth()
		[
			SNew(SScctButton)
			.Text(FText::FromString(TEXT("MAIN MENU")))
			.Description(FText::FromString(TEXT("Back to the main menu.")))
			.OnPressed(FSimpleDelegate::CreateLambda([this] { OnLeave.ExecuteIfBound(); }))
		];
	}
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 30.0f, 0.0f, 0.0f)
		[
			ScctUi::Label(Kicker, bDecided && !bLocalWon ? ScctHudStyle::Danger() : ScctUi::Accent(), 14.0f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 6.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock).Font(ScctUi::Display(64.0f)).ColorAndOpacity(MatchMenuTeamColour(Winner)).Text(FText::FromString(Headline.ToUpper()))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 30.0f)
		[
			SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim())
			.Text_Lambda([Weak, Reason, bOnline = bOnlineLevel]
			{
				FString Line = Reason;
				if (bOnline && Weak.IsValid() && Weak->GetStatus().Countdown > 0.0f)
				{
					Line += FString::Printf(TEXT("   ·   next in %d s"), FMath::CeilToInt32(Weak->GetStatus().Countdown));
				}
				return FText::FromString(Line);
			})
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(1180.0f)
			[
				!bHave
				? StaticCastSharedRef<SWidget>(ScctUi::Card(TEXT("Results"), ScctUi::Label(TEXT("No results yet."), ScctHudStyle::TextDim(), 13.0f)))
				: StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 12.0f, 0.0f)[ SideTable(0) ]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(12.0f, 0.0f, 0.0f, 0.0f)[ SideTable(1) ])
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 28.0f, 0.0f, 0.0f)
		[
			SNew(SBox).Visibility_Lambda([this] { return bOpen ? EVisibility::Visible : EVisibility::Collapsed; })[ Buttons ]
		];
}
