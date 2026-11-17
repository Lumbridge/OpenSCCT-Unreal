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
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Runtime/ScctFeatures.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctOnlineApi.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
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

	FParse::Value(FCommandLine::Get(), TEXT("ScctResultsAction="), ResultsAction);
	FParse::Value(FCommandLine::Get(), TEXT("ScctResultsActionAfter="), ResultsActionAfter);
	if (!bOnlineLevel)
	{
		FParse::Value(FCommandLine::Get(), TEXT("ScctEndMatch="), EndMatchAt);
	}

	ChildSlot
	[
		ScctUi::ScaleForSmallScreens(
			SNew(SOverlay)
			// The match behind, darkened and blurred while the menu or the results are up (the results without the
			// menu take no clicks: the round's end plays on behind them).
			+ SOverlay::Slot()
			[
				SNew(SBox)
				.Visibility_Lambda([this] { return bOpen ? EVisibility::Visible : IsShowingResults() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
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
		// The results are the menu's page while the round is over (their buttons are the ways on); otherwise the menu
		// opens on its first page.
		Show(bResultsReady ? EView::Results : EView::Pause);
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
		// A closed menu keeps no focus: the hidden buttons would otherwise still take Enter and the arrows.
		if (HasFocusedDescendants() || HasKeyboardFocus())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);
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
	const bool bFromSettings = View == EView::Settings && NewView == EView::Pause;
	View = NewView;
	switch (View)
	{
	case EView::Pause:
		Body->SetContent(BuildPause());
		// Back from the settings: the focus is where it was, on the entry that opened them.
		if (bFromSettings && OptionsEntry.IsValid())
		{
			DefaultFocus = OptionsEntry;
		}
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
	Shown = ScctHudStyle::Ease(Shown, bOpen || IsShowingResults() ? 1.0f : 0.0f, FMath::Min(InDeltaTime, 0.05f), 10.0f);

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
		else if (!bReady && View == EView::Results)
		{
			if (bOpen && bResultsOpenedMenu)
			{
				// The visit moved on (the next round or the lobby): the menu opened for the results closes with them.
				UE_LOG(LogOpenSCCT, Log, TEXT("Match menu: the results are gone (the visit moved on)"));
				OnResume.ExecuteIfBound();
			}
			Show(EView::Pause);
		}
		if (!bReady)
		{
			bResultsOpenedMenu = false;
		}
		else if (!bResultsOpenedMenu)
		{
			// The results take the mouse and the pad: offline nothing comes after the round by itself, and online the
			// results' Next, Back to lobby and Leave are the ways on.
			bResultsOpenedMenu = true;
			ResultsShownFor = 0.0f;
			bResultsActionDone = false;
			UE_LOG(LogOpenSCCT, Log, TEXT("Match menu: the round is over, showing the results (%s)"), bOnlineLevel ? TEXT("online") : TEXT("offline"));
			LogResults();
			OnWantsOpen.ExecuteIfBound();
		}
	}
	if (bResultsReady)
	{
		ResultsShownFor += InDeltaTime;
	}
	LevelTime += InDeltaTime;
	RunResultsUnattended();
}

void SScctMatchMenu::LogResults() const
{
	int32 Winner = 255;
	FString Reason;
	TArray<TArray<FString>> Rows;
	TArray<int32> Teams;
	TArray<bool> Local;
	if (!ReadResults(Winner, Reason, Rows, Teams, Local))
	{
		return;
	}
	const int32 LocalTeam = GetResultsLocalTeam();
	UE_LOG(LogOpenSCCT, Log, TEXT("Results: winner %d, the local player played %d (%s), %s, %d line(s)"), Winner, LocalTeam,
		Winner > 1 || (LocalTeam != 0 && LocalTeam != 1) ? TEXT("no verdict") : Winner == LocalTeam ? TEXT("won") : TEXT("lost"), *Reason, Rows.Num());
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Results:   %s side %d score %s kills %s deaths %s%s"), *Rows[Index][0], Teams[Index], *Rows[Index][1], *Rows[Index][2], *Rows[Index][3],
			Local[Index] ? TEXT(" (local)") : TEXT(""));
	}
}

int32 SScctMatchMenu::GetResultsLocalTeam() const
{
	// Online the side the local player played this round is his own results line (a Revenge game swaps the sides).
	const UScctOnlineSubsystem* Visit = Online.Get();
	return bOnlineLevel && Visit != nullptr ? Visit->GetResultsLocalTeam() : IsMercLocal() ? 1 : 0;
}

void SScctMatchMenu::ResultsNext()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return;
	}
	// The server starts the next game (or goes back to the lobby) once every player on a side is ready, or when its
	// results countdown runs out.
	FScctOnlineResults Results;
	TArray<FScctOnlineResultRow> Rows;
	const bool bMore = Visit->GetResults(Results, Rows) && Results.GamesLeft > 0;
	UE_LOG(LogOpenSCCT, Log, TEXT("Results: Next, ready for the %s"), bMore ? TEXT("next game of the series") : TEXT("lobby"));
	Visit->SetReady(true);
}

void SScctMatchMenu::ResultsToLobby()
{
	if (UScctOnlineSubsystem* Visit = Online.Get())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Results: Back to lobby, everyone"));
		Visit->BackToLobby();
	}
}

void SScctMatchMenu::ResultsAskLeave()
{
	// Asked again, with a warning while the match demo is still being saved (ResultsScreen.ToMainMenu).
	const UScctOnlineSubsystem* Visit = Online.Get();
	const bool bPending = Visit != nullptr && (Visit->GetDemoFlags() & EScctRoundDemoFlags::PendingSave) != 0;
	ScctUi::Confirm(Dialogs, TEXT("Leave"),
		bPending ? TEXT("Your match demo is still being saved. Leaving now cancels the download. Leave the session?") : TEXT("Leave the session?"),
		TEXT("Leave"), TEXT("Stay"), [this] { UE_LOG(LogOpenSCCT, Log, TEXT("Results: leaving the session")); OnLeave.ExecuteIfBound(); }, true);
}

void SScctMatchMenu::RunResultsUnattended()
{
	// Once a run, so the match a rematch opens plays on.
	static bool bEndedThisRun = false;
	if (EndMatchAt >= 0.0f && LevelTime >= EndMatchAt && !bEndedThisRun)
	{
		EndMatchAt = -1.0f;
		bEndedThisRun = true;
		const FScctRoundApi* Round = ScctFeature<FScctRoundApi>();
		const int32 Queued = Round != nullptr ? Round->EndLocalRound() : 0;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctEndMatch: running the local match's clock out on its server: %s"), Queued != 0 ? TEXT("queued") : TEXT("refused (no local match, or no round table)"));
	}

	// Once a run: a rematch opens the level again, which would end and rematch again for ever.
	static bool bPressedThisRun = false;
	if (ResultsAction.IsEmpty() || bResultsActionDone || bPressedThisRun || !bResultsReady || ResultsShownFor < ResultsActionAfter)
	{
		return;
	}
	bResultsActionDone = true;
	bPressedThisRun = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("-ScctResultsAction: pressing %s"), *ResultsAction);
	if (ResultsAction.Equals(TEXT("next"), ESearchCase::IgnoreCase) && bOnlineLevel) { ResultsNext(); }
	else if (ResultsAction.Equals(TEXT("lobby"), ESearchCase::IgnoreCase) && bOnlineLevel) { ResultsToLobby(); }
	else if (ResultsAction.Equals(TEXT("leave"), ESearchCase::IgnoreCase) && bOnlineLevel) { OnLeave.ExecuteIfBound(); }
	else if (ResultsAction.Equals(TEXT("rematch"), ESearchCase::IgnoreCase) && !bOnlineLevel) { Restart(false); }
	else if (ResultsAction.Equals(TEXT("menu"), ESearchCase::IgnoreCase) && !bOnlineLevel) { UE_LOG(LogOpenSCCT, Log, TEXT("Results: main menu")); OnLeave.ExecuteIfBound(); }
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctResultsAction: '%s' is not a button of the %s results"), *ResultsAction, bOnlineLevel ? TEXT("online") : TEXT("offline"));
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
	OptionsEntry = Entry(ScctUi::GameText(TEXT("Start_Menu"), TEXT("Options.Caption"), TEXT("Options")), TEXT("Video, audio, controls and key bindings"),
		TEXT("The settings, the same as in the main menu. Changes are in force at once."), [this] { ScctUi::Play(EScctUiSound::Accept); Show(EView::Settings); });
	if (CanChangeSide())
	{
		const bool bMerc = IsMercLocal();
		const FString Other = ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), bMerc ? 0 : 1, bMerc ? TEXT("Spy") : TEXT("Mercenary"));
		const FString SwitchCaption = ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Button2"), TEXT("Switch Team"));
		Entry(SwitchCaption, FString::Printf(TEXT("Play this map as a %s"), *Other.ToLower()),
			TEXT("Start this match again on the other side, with the same map, mode, bots and rules."), [this, Other, SwitchCaption]
			{
				// The dialog is named as the entry that opened it.
				ScctUi::Confirm(Dialogs, SwitchCaption, FString::Printf(TEXT("Start the match again as a %s? This round's score is lost."), *Other.ToLower()),
					SwitchCaption, TEXT("Cancel"), [this] { Restart(true); });
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
		if (Results.ObjectivesNeeded > 0)
		{
			OutReason += FString::Printf(TEXT("   ·   objectives %d / %d"), Results.ObjectivesTaken, Results.ObjectivesNeeded);
		}
		// The round table's extras, in the order of the results' lines: objectives and who left early.
		const TArray<FScctRoundResultExtra> Extras = Visit->GetResultExtras();
		TMap<int32, FScctRoundResultExtra> ExtraOf;
		for (int32 Index = 0; Index < Extras.Num() && Index < Rows.Num(); ++Index)
		{
			if (Extras[Index].Id == Rows[Index].Id)
			{
				ExtraOf.Add(Rows[Index].Id, Extras[Index]);
			}
		}
		Rows.StableSort([](const FScctOnlineResultRow& A, const FScctOnlineResultRow& B) { return A.Team != B.Team ? A.Team < B.Team : A.Score > B.Score; });
		for (const FScctOnlineResultRow& Row : Rows)
		{
			FString Name = Row.Name;
			if (const FScctRoundResultExtra* Extra = ExtraOf.Find(Row.Id))
			{
				if (Results.ObjectivesNeeded > 0 && Extra->Objectives > 0)
				{
					Name += FString::Printf(TEXT("  · %d obj"), Extra->Objectives);
				}
				if (Extra->LeftEarly != 0)
				{
					Name += TEXT("  (left)");
				}
			}
			OutRows.Add({ Name, FString::FromInt(Row.Score), FString::FromInt(Row.Kills), FString::FromInt(Row.Deaths) });
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

	const bool bDecided = Winner == 0 || Winner == 1;
	const bool bEnded = bOnlineLevel || bResultsReady;
	// The original's line per winning side, a list: ("Shadownet Spies team wins","ARGUS PMC Mercenaries team wins").
	const FString Headline = bDecided
		? ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TextTeamWin"), Winner, Winner == 0 ? TEXT("Spies win") : TEXT("Mercenaries win"))
		: bEnded ? FString(TEXT("Round over")) : FString(TEXT("Scoreboard"));
	const int32 LocalTeam = GetResultsLocalTeam();
	const bool bSpectator = LocalTeam != 0 && LocalTeam != 1;
	const bool bLocalWon = bDecided && !bSpectator && Winner == LocalTeam;
	const FString Kicker = !bEnded ? FString(TEXT("ROUND IN PROGRESS"))
		: bLocalWon ? FString(TEXT("YOU WIN")) : bDecided && !bSpectator ? FString(TEXT("YOU LOSE")) : bDecided ? FString(TEXT("ROUND OVER")) : FString(TEXT("NO WINNER"));

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
	int32 GamesLeft = 0;
	if (bOnlineLevel && Weak.IsValid())
	{
		FScctOnlineResults Results;
		TArray<FScctOnlineResultRow> ResultRows;
		GamesLeft = Weak->GetResults(Results, ResultRows) ? Results.GamesLeft : 0;

		// NEXT: ready for the next game of the series, or for the lobby once the series is over (ResultsScreen.Rematch).
		TSharedPtr<SScctButton> Next;
		Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SAssignNew(Next, SScctButton)
			.Look(EScctButtonLook::Primary)
			.MinWidth(220.0f)
			.IsEnabled_Lambda([Weak] { return Weak.IsValid() && !Weak->HasFlag(EScctOnlineStatusFlags::LocalReady); })
			.Text_Lambda([Weak, GamesLeft]
			{
				const bool bReady = Weak.IsValid() && Weak->HasFlag(EScctOnlineStatusFlags::LocalReady);
				const bool bControl = Weak.IsValid() && Weak->HasFlag(EScctOnlineStatusFlags::CanControl);
				return FText::FromString(bReady ? TEXT("READY") : GamesLeft > 0 ? TEXT("NEXT ROUND") : bControl ? TEXT("RETURN TO LOBBY") : TEXT("CONTINUE TO LOBBY"));
			})
			.Description(FText::FromString(TEXT("Say you are ready: the next game of the series (or the lobby) starts once every player on a side is.")))
			.OnPressed(FSimpleDelegate::CreateLambda([this] { ResultsNext(); }))
		];
		DefaultFocus = Next;
		// BACK TO LOBBY: the player who controls the session sends everyone there at once, without waiting for the
		// others to be ready (the rest of the series is dropped).
		Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([Weak] { return Weak.IsValid() && Weak->HasFlag(EScctOnlineStatusFlags::CanControl) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SScctButton)
				.Text(FText::FromString(GamesLeft > 0 ? TEXT("END SERIES, TO LOBBY") : TEXT("EVERYONE TO LOBBY NOW")))
				.Description(FText::FromString(TEXT("You control this session: everyone back to the lobby now, without waiting.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { ResultsToLobby(); }))
			]
		];
		Buttons->AddSlot().AutoWidth()
		[
			SNew(SScctButton)
			.Text(FText::FromString(TEXT("LEAVE")))
			.Description(FText::FromString(TEXT("Leave the session and go back to the main menu.")))
			.OnPressed(FSimpleDelegate::CreateLambda([this] { ResultsAskLeave(); }))
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 30.0f, 0.0f, 0.0f)
		[
			ScctUi::Label(Kicker, bDecided && !bLocalWon ? ScctHudStyle::Danger() : ScctUi::Accent(), 14.0f)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 6.0f, 0.0f, 6.0f)
		[
			// One line, made smaller where the screen is too narrow for it (a wrapped headline left "WINS" alone).
			SNew(SScaleBox).Stretch(EStretch::ScaleToFitX).StretchDirection(EStretchDirection::DownOnly)
			[
				SNew(STextBlock).Font(ScctUi::Display(46.0f)).ColorAndOpacity(MatchMenuTeamColour(Winner))
				.Text(FText::FromString(Headline.ToUpper()))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, bOnlineLevel ? 8.0f : 30.0f)
		[
			SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::TextDim())
			.Text(FText::FromString(Reason))
		]
		// Online: the series and the server's countdown, who the next game waits for, and the personal demo.
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 24.0f)
		[
			SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).Justification(ETextJustify::Center).AutoWrapText(true)
			.Visibility(bOnlineLevel ? EVisibility::Visible : EVisibility::Collapsed)
			.Text_Lambda([Weak, GamesLeft]
			{
				if (!Weak.IsValid())
				{
					return FText::GetEmpty();
				}
				const int32 Left = FMath::CeilToInt32(Weak->GetStatus().Countdown);
				const FString Series = GamesLeft > 0 ? FString::Printf(TEXT("%d game%s left in the series"), GamesLeft, GamesLeft == 1 ? TEXT("") : TEXT("s")) : FString(TEXT("The series is over"));
				// Only a dedicated server counts down (OnlineServer.ResultsSeconds); a listen host waits for everyone.
				const TCHAR* Then = GamesLeft > 0 ? TEXT("next game") : TEXT("back to the lobby");
				TArray<FString> Waiting;
				for (const FScctOnlinePlayer& Player : Weak->GetPlayers())
				{
					if ((Player.Flags & (EScctOnlinePlayerFlags::Ready | EScctOnlinePlayerFlags::Spectator | EScctOnlinePlayerFlags::Bot)) == 0 && Player.Team != 255)
					{
						Waiting.Add(Player.Name);
					}
				}
				FString Line = Left > 0 ? FString::Printf(TEXT("%s  ·  %s in %d s"), *Series, Then, Left) : FString::Printf(TEXT("%s  ·  %s once everyone is ready"), *Series, Then);
				Line += Waiting.Num() == 0 ? FString(TEXT("\nEveryone is ready.")) : FString::Printf(TEXT("\nWaiting for %s."), *FString::Join(Waiting, TEXT(", ")));
				// The personal demo (OnlineClient.DemoRecordingStatus): shown while the player records or a demo is still saved.
				const int32 Demo = Weak->GetDemoFlags();
				if ((Demo & (EScctRoundDemoFlags::Recording | EScctRoundDemoFlags::PendingSave)) != 0)
				{
					Line += TEXT("\nMatch demo: ") + Weak->GetRoundText(EScctRoundText::DemoStatus);
					if ((Demo & EScctRoundDemoFlags::PendingSave) != 0)
					{
						Line += TEXT(" Stay connected until saving finishes.");
					}
					else if ((Demo & EScctRoundDemoFlags::Saved) != 0)
					{
						Line += TEXT(" Saved as ") + FPaths::GetCleanFilename(Weak->GetRoundText(EScctRoundText::LastDemoPath));
					}
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
