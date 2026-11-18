#include "Menu/SScctFrontEnd.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "Hub/ScctHubSubsystem.h"
#include "Menu/ScctDashboardCards.h"
#include "Menu/ScctMenuDriver.h"
#include "Menu/ScctMenuKit.h"
#include "Menu/ScctMenuNav.h"
#include "Menu/ScctMenuPage.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Online/ScctContentDownloads.h"
#include "Engine/GameInstance.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Social/ScctSocialSubsystem.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/**
	 * The shade over the backdrop: the left, where the type is, darkens to near black and the right keeps the scene;
	 * the top and the bottom close in a little, as a lens does.
	 */
	class SScctBackdropShade : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScctBackdropShade) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs) {}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const FVector2D Size = Geometry.GetLocalSize();
			const auto Gradient = [&](const FVector2D& Position, const FVector2D& Extent, TArray<FSlateGradientStop> Stops, EOrientation Orientation)
			{
				FSlateDrawElement::MakeGradient(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Extent), FSlateLayoutTransform(FVector2f(Position))), MoveTemp(Stops), Orientation, ESlateDrawEffect::None);
			};
			const FLinearColor Ink(0.002f, 0.004f, 0.008f, 1.0f);
			const auto At = [&Ink](float Alpha) { return FLinearColor(Ink.R, Ink.G, Ink.B, Alpha); };

			// Left to right: the column of type sits on dark, the scene breathes on the right.
			Gradient(FVector2D::ZeroVector, Size, {
				FSlateGradientStop(FVector2D(0.0, 0.0), At(0.9f)),
				FSlateGradientStop(FVector2D(Size.X * 0.34, 0.0), At(0.7f)),
				FSlateGradientStop(FVector2D(Size.X * 0.7, 0.0), At(0.28f)),
				FSlateGradientStop(FVector2D(Size.X, 0.0), At(0.18f)),
			}, Orient_Vertical);
			// Top and bottom.
			Gradient(FVector2D::ZeroVector, FVector2D(Size.X, Size.Y * 0.22), {
				FSlateGradientStop(FVector2D(0.0, 0.0), At(0.55f)),
				FSlateGradientStop(FVector2D(0.0, Size.Y * 0.22), At(0.0f)),
			}, Orient_Horizontal);
			Gradient(FVector2D(0.0, Size.Y * 0.62), FVector2D(Size.X, Size.Y * 0.38), {
				FSlateGradientStop(FVector2D(0.0, 0.0), At(0.0f)),
				FSlateGradientStop(FVector2D(0.0, Size.Y * 0.38), At(0.8f)),
			}, Orient_Horizontal);
			return Layer + 1;
		}
	};

	/** A title that types itself out, with a blinking cursor while it does, as the original's pages do. */
	class SScctTypedTitle : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SScctTypedTitle) {}
			SLATE_ATTRIBUTE(FString, Text)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs) { Text = InArgs._Text; }

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			const FVector2D Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text.Get() + TEXT("_"), ScctUi::H1());
			return FVector2D(Measured.X, Measured.Y);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const FString Full = Text.Get();
			if (Full != Last)
			{
				Last = Full;
				Started = FPlatformTime::Seconds();
			}
			// About 40 letters a second: quick enough not to wait for, slow enough to read as typing.
			const double Age = FPlatformTime::Seconds() - Started;
			const int32 Count = FMath::Min(Full.Len(), static_cast<int32>(Age * 40.0));
			FString Shown = Full.Left(Count);
			if (Count < Full.Len() || FMath::Fmod(Age, 0.6) < 0.3 && Age < 1.2)
			{
				Shown += TEXT("_");
			}
			FSlateDrawElement::MakeText(Out, Layer, Geometry.ToPaintGeometry(), Shown, ScctUi::H1(), ESlateDrawEffect::None, Style.GetColorAndOpacityTint() * ScctHudStyle::Text());
			return Layer + 1;
		}

	private:
		TAttribute<FString> Text;
		mutable FString Last;
		mutable double Started = 0.0;
	};

	/** Escape and the controller's B, Back and Start (Menu/ScctMenuNav.h). */
	bool IsBackKey(const FKey& Key)
	{
		return ScctMenuNav::IsBackKey(Key);
	}

	int32 TabDirection(const FKey& Key)
	{
		return ScctMenuNav::TabDirection(Key);
	}

	const TCHAR* const OnlineSection = TEXT("OpenSCCT.Online");

	FString MapTitle(const FString& Id)
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		return Menu != nullptr ? Menu->GetMapTitle(Id) : Id.ToUpper();
	}

	/** A view of the visit (connecting, a failure): a page with nothing to go back to. */
	class SScctVisitPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctVisitPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<SWidget>& Content, const TSharedPtr<SWidget>& Focus)
		{
			DefaultFocus = Focus;
			ChildSlot.VAlign(VAlign_Center).HAlign(HAlign_Left).Padding(0.0f, 0.0f, 0.0f, 80.0f)
			[
				SNew(SBox).WidthOverride(720.0f)[ ScctUi::Panel(Content, FMargin(40.0f, 36.0f), 0.78f) ]
			];
		}

		virtual FString GetTitle() const override { return FString(); }
	};
}

void SScctFrontEnd::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;
	SAssignNew(Dialogs, SScctDialogLayer);
	Dialogs->FocusAfter = SharedThis(this);
	// The controller's notices and the question to turn it on are this menu's while it is up.
	ScctMenuNav::RegisterLayer(Dialogs.ToSharedRef(), nullptr);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SScctBackdropShade) ]
		+ SOverlay::Slot()
		[
			ScctUi::ScaleForSmallScreens(
				SNew(SVerticalBox)
				// The page: its title (when it has one) and its content.
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(72.0f, 52.0f, 72.0f, 0.0f)
				[
					SAssignNew(PageBox, SBox)
				]
				// The keys that work here, and the build.
				+ SVerticalBox::Slot().AutoHeight().Padding(72.0f, 18.0f, 72.0f, 26.0f)
				[
					BuildFooter()
				])
		]
		+ SOverlay::Slot()[ Dialogs.ToSharedRef() ]
	];

	Open(TEXT("home"));

	// A hub visit that failed brought the player back here: the hub page is opened, and says why (GetNotice).
	if (const UGameInstance* Instance = Online.IsValid() ? Online->GetGameInstance() : nullptr)
	{
		const UScctHubSubsystem* HubVisit = Instance->GetSubsystem<UScctHubSubsystem>();
		if (HubVisit != nullptr && HubVisit->GetState() == EScctHubState::Failed && !HubVisit->GetNotice().IsEmpty())
		{
			Open(TEXT("hub"));
		}
	}

	if (UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent())
	{
		Menu->SetMusicPlaying(true);
	}
}

TSharedRef<SWidget> SScctFrontEnd::BuildFooter()
{
	const TSharedRef<SHorizontalBox> Hints = SNew(SHorizontalBox);
	Hints->AddSlot().AutoWidth()[ ScctUi::KeyHint(TEXT("ENTER"), TEXT("A"), TEXT("Select")) ];
	Hints->AddSlot().AutoWidth()
	[
		SNew(SBox).Visibility(TAttribute<EVisibility>::CreateLambda([this] { return Stack.Num() > 1 || VisitView != EVisitView::None ? EVisibility::Visible : EVisibility::Collapsed; }))
		[
			ScctUi::KeyHint(TEXT("ESC"), TEXT("B"), TEXT("Back"))
		]
	];
	Hints->AddSlot().AutoWidth()
	[
		SNew(SBox).Visibility(TAttribute<EVisibility>::CreateLambda([this]
		{
			const TSharedPtr<SScctMenuPage> Page = GetTopPage();
			return Page.IsValid() && Page->HasTabs() ? EVisibility::Visible : EVisibility::Collapsed;
		}))
		[
			ScctUi::KeyHint(TEXT("Q / E"), TEXT("LB / RB"), TEXT("Change tab"))
		]
	];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ ScctUi::Rule(0.08f) ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Hints ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Font(ScctUi::Caps(11.0f))
					.ColorAndOpacity(ScctHudStyle::TextFaint())
					.Text(TAttribute<FText>::CreateLambda([] { return FText::FromString(ScctUi::BuildLabel()); }))
				]
				// The master's own version under the game's (the Godot client's BuildLabel and MasterVersionCheck).
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.0f, 3.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(ScctUi::Caps(10.0f))
					.ColorAndOpacity(ScctHudStyle::TextFaint())
					.Text(TAttribute<FText>::CreateLambda([] { return FText::FromString(ScctDashboard::MasterLabel()); }))
				]
			]
		];
}

TSharedPtr<SScctMenuPage> SScctFrontEnd::GetTopPage() const
{
	if (VisitView == EVisitView::Lobby)
	{
		return LobbyPage;
	}
	if (VisitView != EVisitView::None)
	{
		return VisitPage;
	}
	return VisitView == EVisitView::None && Stack.Num() > 0 ? Stack.Last() : nullptr;
}

void SScctFrontEnd::Open(const FString& Name)
{
	FScctMenuHost Host;
	Host.Online = Online;
	Host.Dialogs = Dialogs;
	Host.Open = [this](const FString& Next) { Open(Next); };
	Host.Back = [this] { GoBack(); };

	FString Page = Name, Sub;
	Name.Split(TEXT("/"), &Page, &Sub);
	const TSharedPtr<SScctMenuPage> Made = ScctMenuPages::Make(Page, Host);
	if (!Made.IsValid())
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Menu: no page is called '%s'"), *Page);
		return;
	}
	Push(Made.ToSharedRef());
	StackNames.SetNum(Stack.Num());
	StackNames.Last() = Page.ToLower();
	if (!Sub.IsEmpty())
	{
		Made->ShowSub(Sub);
	}
}

void SScctFrontEnd::Push(const TSharedRef<SScctMenuPage>& Page)
{
	// The control that opened the page gets the focus back when it closes.
	if (Stack.Num() > 0)
	{
		CoveredFocus.SetNum(Stack.Num());
		const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
		CoveredFocus.Last() = Focused.IsValid() && Stack.Last()->HasFocusedDescendants() ? Focused : TSharedPtr<SWidget>();
	}
	ReturnFocus.Reset();
	Stack.Add(Page);
	if (Stack.Num() > 1)
	{
		ScctUi::Play(EScctUiSound::Open);
	}
	Present();
}

void SScctFrontEnd::GoBack()
{
	if (Stack.Num() <= 1)
	{
		AskQuit();
		return;
	}
	Stack.Last()->OnClosing();
	Stack.Pop();
	ReturnFocus = CoveredFocus.IsValidIndex(Stack.Num() - 1) ? CoveredFocus[Stack.Num() - 1] : TWeakPtr<SWidget>();
	CoveredFocus.SetNum(Stack.Num() - 1);
	ScctUi::Play(EScctUiSound::Back);
	Present();
}

void SScctFrontEnd::AskQuit()
{
	ScctUi::Confirm(Dialogs,
		ScctUi::GameText(TEXT("Menu_Multi"), TEXT("MainPage_Quit.Caption"), TEXT("Quit")),
		ScctUi::GameText(TEXT("ConfirmQuitPage"), TEXT("LabelCaption"), TEXT("Are you sure you want to quit?")),
		ScctUi::GameText(TEXT("ConfirmQuitPage"), TEXT("sBT_Yes"), TEXT("Quit Game")),
		ScctUi::GameText(TEXT("ConfirmQuitPage"), TEXT("sBT_Cancel"), TEXT("Cancel")),
		[] { UE_LOG(LogOpenSCCT, Log, TEXT("Menu: quit")); FPlatformMisc::RequestExit(false); }, true);
}

void SScctFrontEnd::Present()
{
	const TSharedPtr<SScctMenuPage> Page = GetTopPage();
	TSharedRef<SWidget> Content = SNullWidget::NullWidget;
	if (VisitView != EVisitView::None && VisitView != EVisitView::Lobby)
	{
		Content = BuildVisit();
	}
	else if (Page.IsValid())
	{
		const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		if (!Page->GetTitle().IsEmpty())
		{
			// The title block keeps its place whatever the page's height, so moving between pages does not jump.
			const TWeakPtr<SScctMenuPage> Weak = Page;
			Box->AddSlot().AutoHeight().Padding(2.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Font(ScctUi::Caps(12.0f))
				.ColorAndOpacity(ScctUi::Accent())
				.Text(TAttribute<FText>::CreateLambda([Weak] { const TSharedPtr<SScctMenuPage> Pinned = Weak.Pin(); return Pinned.IsValid() ? FText::FromString(Pinned->GetKicker().ToUpper()) : FText::GetEmpty(); }))
			];
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 26.0f)
			[
				SNew(SScctTypedTitle).Text(TAttribute<FString>::CreateLambda([Weak] { const TSharedPtr<SScctMenuPage> Pinned = Weak.Pin(); return Pinned.IsValid() ? Pinned->GetTitle().ToUpper() : FString(); }))
			];
		}
		Box->AddSlot().FillHeight(1.0f)[ Page.ToSharedRef() ];
		Content = Box;
	}
	PageBox->SetContent(Content);
	Shown = 0.0f;
	PageBox->SetRenderOpacity(0.0f);
	bFocusPending = true;
}

void SScctFrontEnd::ShowPageNamed(const FString& Name)
{
	// Back to the main menu first, so the page opens over it as if it had been chosen there.
	while (Stack.Num() > 1)
	{
		// A page left this way still keeps what was changed on it (settings write their files).
		Stack.Last()->OnClosing();
		Stack.Pop();
	}
	CoveredFocus.Reset();
	ReturnFocus.Reset();
	if (Stack.IsEmpty())
	{
		Open(TEXT("home"));
	}
	if (Name.Equals(TEXT("home"), ESearchCase::IgnoreCase))
	{
		Present();
	}
	else if (Name.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
	{
		Present();
		AskQuit();
	}
	else if (Name.Equals(TEXT("start"), ESearchCase::IgnoreCase))
	{
		Open(TEXT("play/start"));
	}
	else
	{
		Open(Name);
	}
}

FReply SScctFrontEnd::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	ScctUi::NoteInput(Key);
	if (Dialogs->IsOpen())
	{
		return Dialogs->OnKeyDown(MyGeometry, InKeyEvent);
	}
	const TSharedPtr<SScctMenuPage> Page = GetTopPage();

	if (IsBackKey(Key))
	{
		if (Page.IsValid() && Page->HandleBack())
		{
			return FReply::Handled();
		}
		if (VisitView != EVisitView::None)
		{
			// Leaving the lobby or giving up a connection is asked for on the page itself; Escape there only goes
			// back where that is harmless.
			if (VisitView == EVisitView::Failed && Online.IsValid())
			{
				ScctUi::Play(EScctUiSound::Back);
				Online->Leave();
			}
			return FReply::Handled();
		}
		GoBack();
		return FReply::Handled();
	}

	if (!ScctUi::IsTyping())
	{
		const int32 Direction = TabDirection(Key);
		if (Direction != 0 && Page.IsValid() && Page->HasTabs())
		{
			Page->StepTab(Direction);
			return FReply::Handled();
		}
		if (Page.IsValid() && Page->HandleShortcut(Key))
		{
			return FReply::Handled();
		}
	}

	// The focus fell off the page (a click on nothing): the first arrow or stick push brings it back.
	if (HasKeyboardFocus() && Page.IsValid() && FSlateApplication::Get().GetNavigationDirectionFromKey(InKeyEvent) != EUINavigation::Invalid)
	{
		const TSharedPtr<SWidget> Focus = Page->GetDefaultFocus().IsValid() ? Page->GetDefaultFocus() : ScctMenuNav::FindFirstFocusable(Page);
		if (Focus.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::Navigation);
			return FReply::Handled();
		}
	}
	// The D-pad leaves a text field as it leaves a button (a typed arrow moves in the text; the pad has no text to move in).
	const EUINavigation PadNavigation = Key.IsGamepadKey() ? FSlateApplication::Get().GetNavigationDirectionFromKey(InKeyEvent) : EUINavigation::Invalid;
	if (PadNavigation != EUINavigation::Invalid && ScctUi::IsTyping())
	{
		return FReply::Handled().SetNavigation(PadNavigation, ENavigationGenesis::Controller, ENavigationSource::FocusedWidget);
	}
	return FReply::Unhandled();
}

void SScctFrontEnd::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	FScctMenuDriver::FHost DriverHost;
	DriverHost.ShowPage = [this](const FString& Name) { ShowPageNamed(Name); };
	FScctMenuDriver::Tick(InDeltaTime, DriverHost);

	// The page slides up into place.
	Shown = ScctHudStyle::Ease(Shown, 1.0f, FMath::Min(InDeltaTime, 0.05f), 12.0f);
	PageBox->SetRenderOpacity(Shown);
	PageBox->SetRenderTransform(FSlateRenderTransform(FVector2D(0.0, 16.0 * (1.0 - Shown))));

	if (bFocusPending && !Dialogs->IsOpen())
	{
		const TSharedPtr<SScctMenuPage> Page = GetTopPage();
		TSharedPtr<SWidget> Focus = ReturnFocus.Pin();
		ReturnFocus.Reset();
		if (!Focus.IsValid() || !Focus->GetVisibility().IsVisible() || !Focus->IsEnabled())
		{
			Focus = Page.IsValid() ? Page->GetDefaultFocus() : nullptr;
		}
		if (!Focus.IsValid() && Page.IsValid())
		{
			// A page that names no control to start on: its first one, so a controller has somewhere to be.
			Focus = ScctMenuNav::FindFirstFocusable(Page);
		}
		else if (!Focus.IsValid() && VisitView != EVisitView::None)
		{
			Focus = ScctMenuNav::FindFirstFocusable(PageBox);
		}
		if (Focus.IsValid())
		{
			FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
		}
		else
		{
			FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
		}
		bFocusPending = false;
	}
	else if (!Dialogs->IsOpen() && !HasKeyboardFocus() && !HasFocusedDescendants())
	{
		// Something outside the menu took the focus (the viewport after a click on the 3D view): it comes back here,
		// so the keyboard and the controller keep working.
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}

	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return;
	}
	TickSocial();

	// A visit takes the screen over, whichever page it was started from, and gives that page back at its end.
	const EVisitView Wanted = Visit->IsVisiting() ? FindVisitView() : EVisitView::None;
	if (Wanted != VisitView)
	{
		VisitView = Wanted;
		if (VisitView == EVisitView::Lobby && !LobbyPage.IsValid())
		{
			FScctMenuHost Host;
			Host.Online = Online;
			Host.Dialogs = Dialogs;
			Host.Open = [this](const FString& Next) { Open(Next); };
			// The lobby's Leave: asked first, as leaving gives up the place on the server.
			Host.Back = [this]
			{
				ScctUi::Confirm(Dialogs, ScctUi::GameText(TEXT("Lobby_Create"), TEXT("ButtonB"), TEXT("Leave")), TEXT("Leave the server and go back to the menus?"),
					ScctUi::GameText(TEXT("Lobby_Create"), TEXT("ButtonB"), TEXT("Leave")), TEXT("Stay"),
					[this] { if (UScctOnlineSubsystem* Visit = Online.Get()) { ScctUi::Play(EScctUiSound::Back); Visit->Leave(); } }, true);
			};
			LobbyPage = ScctMenuPages::MakeLobby(Host);
		}
		else if (VisitView != EVisitView::Lobby)
		{
			LobbyPage.Reset();
		}
		Present();
	}
}

void SScctFrontEnd::TickSocial()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	UScctSocialSubsystem* Social = Visit != nullptr && Visit->GetGameInstance() != nullptr ? Visit->GetGameInstance()->GetSubsystem<UScctSocialSubsystem>() : nullptr;
	if (Social == nullptr)
	{
		return;
	}

	// A friend's Join or a website join asks for a server's password, or for the page where the player signs in.
	FScctSocialPasswordPrompt Prompt;
	if (Social->TakePasswordPrompt(Prompt))
	{
		ShowPageNamed(TEXT("home"));
		FScctMenuHost Host;
		Host.Online = Online;
		Host.Dialogs = Dialogs;
		Host.Open = [this](const FString& Next) { Open(Next); };
		Host.Back = [this] { GoBack(); };
		Push(ScctMenuPages::MakeJoinPassword(Host, Prompt));
		StackNames.SetNum(Stack.Num());
		StackNames.Last() = TEXT("password");
	}
	if (const FString PageToOpen = Social->TakePageToOpen(); !PageToOpen.IsEmpty())
	{
		ShowPageNamed(PageToOpen);
	}
	ReportSocialPage();
}

void SScctFrontEnd::ReportSocialPage()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	UScctSocialSubsystem* Social = Visit != nullptr && Visit->GetGameInstance() != nullptr ? Visit->GetGameInstance()->GetSubsystem<UScctSocialSubsystem>() : nullptr;
	if (Social == nullptr)
	{
		return;
	}
	// The Discord status names the page on top (Godot's front end pages, by the names the menus open them by).
	StackNames.SetNum(Stack.Num());
	const FString Top = VisitView == EVisitView::None && StackNames.Num() > 0 ? StackNames.Last() : FString();
	EScctSocialPage Page = EScctSocialPage::Other;
	if (Top == TEXT("online") || Top == TEXT("servers")) { Page = EScctSocialPage::Servers; }
	else if (Top == TEXT("address") || Top == TEXT("join")) { Page = EScctSocialPage::JoinByAddress; }
	else if (Top == TEXT("play") || Top == TEXT("offline")) { Page = EScctSocialPage::OfflineSetup; }
	else if (Top == TEXT("hub")) { Page = EScctSocialPage::Hub; }
	else if (Top == TEXT("settings") || Top == TEXT("options")) { Page = EScctSocialPage::Options; }
	else if (Top == TEXT("host") || Top == TEXT("create")) { Page = EScctSocialPage::HostSetup; }
	Social->SetFrontEndPage(Page);
}

SScctFrontEnd::EVisitView SScctFrontEnd::FindVisitView() const
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return EVisitView::None;
	}
	switch (Visit->GetState())
	{
	case EScctOnlineState::Connecting: return EVisitView::Connecting;
	case EScctOnlineState::Lobby: return EVisitView::Lobby;
	case EScctOnlineState::Travelling: return EVisitView::Travelling;
	case EScctOnlineState::Loading:
	case EScctOnlineState::Match:
	case EScctOnlineState::Results: return EVisitView::Round;
	default: return EVisitView::Failed;
	}
}

// ---- the visit's own views ----------------------------------------------------------------------------------------
//
// The loading screen of a level change belongs in the Travelling and Round cases below (and in the match
// overlay's banner, Menu/ScctMatchMenuWidget.cpp).

TSharedRef<SWidget> SScctFrontEnd::BuildVisit()
{
	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	FString Kicker = TEXT("ONLINE");
	FString Heading;
	FString PrimaryLabel;
	FString SecondaryLabel = TEXT("Cancel");
	TFunction<void()> OnPrimary;
	TAttribute<EVisibility> PrimaryVisibility = EVisibility::Visible;
	TAttribute<FText> Body = FText::GetEmpty();
	bool bBusy = true;
	bool bFailed = false;
	switch (VisitView)
	{
	case EVisitView::Connecting:
		Heading = FString::Printf(TEXT("Connecting to %s"), Visit->GetServerName().IsEmpty() ? TEXT("the server") : *Visit->GetServerName());
		// What the join has tried so far, one attempt per line.
		Body = TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? FText::FromString(Weak->GetConnectLog()) : FText::GetEmpty(); });
		break;
	case EVisitView::Travelling:
		Heading = Visit->GetMapToLoad().IsEmpty() ? TEXT("Returning to the lobby") : FString::Printf(TEXT("Travelling to %s"), *MapTitle(Visit->GetMapToLoad()));
		Body = FText::FromString(Visit->GetServerName());
		SecondaryLabel = TEXT("Leave");
		break;
	case EVisitView::Round:
		SecondaryLabel = TEXT("Leave");
		if (Visit->HasFlag(EScctOnlineStatusFlags::RoundRefused))
		{
			Heading = TEXT("Round in progress");
			// The map from OpenSCCT-Content, as the lobby offers it: ready for the next round once it is in.
			Body = TAttribute<FText>::CreateLambda([]
			{
				FScctLobbyMapFetch Fetch;
				ScctContentDownloads::GetLobbyFetch(Fetch);
				const FString Base = TEXT("The server is playing a map that is not installed here. You are back in the lobby when the round is over.");
				return FText::FromString(Fetch.IsShown() ? Base + TEXT("\n\n") + Fetch.Line : Base);
			});
			PrimaryLabel = TEXT("Download map");
			OnPrimary = [] { ScctContentDownloads::StartLobbyFetch(); };
			PrimaryVisibility = TAttribute<EVisibility>::CreateLambda([]
			{
				FScctLobbyMapFetch Fetch;
				ScctContentDownloads::GetLobbyFetch(Fetch);
				return Fetch.CanDownload() ? EVisibility::Visible : EVisibility::Collapsed;
			});
			bBusy = false;
		}
		else
		{
			Heading = FString::Printf(TEXT("Joining the round on %s"), *MapTitle(Visit->GetMapToLoad()));
			Body = FText::FromString(Visit->GetServerName());
		}
		break;
	default:
	{
		bBusy = false;
		bFailed = Visit->GetState() != EScctOnlineState::Left;
		Kicker = bFailed ? TEXT("CONNECTION") : TEXT("ONLINE");
		Heading = bFailed ? TEXT("Could not join") : TEXT("You left the server");
		SecondaryLabel = TEXT("Back");
		Body = TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? FText::FromString(Weak->GetNotice()) : FText::GetEmpty(); });
		FString LastAddress;
		GConfig->GetString(OnlineSection, TEXT("LastAddress"), LastAddress, GGameUserSettingsIni);
		if (bFailed && !LastAddress.IsEmpty() && !Visit->HasFlag(EScctOnlineStatusFlags::Kicked))
		{
			PrimaryLabel = TEXT("Try again");
			OnPrimary = [Weak, LastAddress]
			{
				if (UScctOnlineSubsystem* Pinned = Weak.Get())
				{
					Pinned->Leave();
					Pinned->Join(LastAddress);
				}
			};
		}
		break;
	}
	}

	const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	TSharedPtr<SWidget> Focus;
	if (OnPrimary)
	{
		TSharedPtr<SScctButton> Made;
		Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SAssignNew(Made, SScctButton).Look(EScctButtonLook::Primary).MinWidth(150.0f).Text(FText::FromString(PrimaryLabel.ToUpper())).Visibility(PrimaryVisibility)
			.OnPressed(FSimpleDelegate::CreateLambda([OnPrimary] { OnPrimary(); }))
		];
		if (PrimaryVisibility.Get() == EVisibility::Visible)
		{
			Focus = Made;
		}
	}
	{
		TSharedPtr<SScctButton> Made;
		Buttons->AddSlot().AutoWidth()
		[
			SAssignNew(Made, SScctButton).MinWidth(130.0f).Text(FText::FromString(SecondaryLabel.ToUpper()))
			.OnPressed(FSimpleDelegate::CreateLambda([Weak, this]
			{
				if (VisitView == EVisitView::Failed || VisitView == EVisitView::Connecting)
				{
					if (Weak.IsValid()) { Weak->Leave(); }
					return;
				}
				ScctUi::Confirm(Dialogs, TEXT("Leave the server"), TEXT("Leave this server and go back to the main menu?"), TEXT("Leave"), TEXT("Stay"),
					[Weak] { if (Weak.IsValid()) { Weak->Leave(); } }, true);
			}))
		];
		if (!Focus.IsValid())
		{
			Focus = Made;
		}
	}

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ ScctUi::Label(Kicker, bFailed ? ScctHudStyle::Danger() : ScctUi::Accent(), 12.0f) ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)
		[
			SNew(SBox).Visibility(bBusy ? EVisibility::Visible : EVisibility::Collapsed)[ SNew(SScctSpinner).Size(30.0f) ]
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(ScctUi::H1()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true).Text(FText::FromString(Heading.ToUpper()))
		]
	];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ ScctUi::Rule(0.12f) ];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 28.0f)
	[
		SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(bFailed ? ScctHudStyle::Text() : ScctHudStyle::TextDim()).AutoWrapText(true).LineHeightPercentage(1.2f).Text(Body)
	];
	Box->AddSlot().AutoHeight().HAlign(HAlign_Left)[ Buttons ];

	const TSharedRef<SScctVisitPage> Page = SNew(SScctVisitPage, Box, Focus);
	VisitPage = Page;
	return Page;
}
