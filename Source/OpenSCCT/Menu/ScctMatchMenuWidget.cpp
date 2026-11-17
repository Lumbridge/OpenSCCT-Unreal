#include "Menu/ScctMatchMenuWidget.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Menu/SScctConfirm.h"
#include "Menu/SScctGearPage.h"
#include "Menu/SScctHelpPage.h"
#include "Menu/SScctOptionsPage.h"
#include "Menu/SScctStrategicMapPage.h"
#include "Menu/ScctMenuStyle.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctMatchMenuApi.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	/** Quit Game, an entry of OpenSCCT's menu beside the original's buttons (EScctMatchMenuButton). */
	constexpr int32 QuitGameEntry = 100;

	/** GUI.SSMenu_LANMainPage s_Init: Sleep(0.5), then keys count (InGameMenuK.KeyIgnoreSeconds). */
	constexpr float MenuKeyHoldSeconds = 0.5f;

	/** -ScctMatchMenuAct acted on in this run. */
	bool bMatchMenuActDone = false;

	/** The context before it was first read: no team yet. */
	FScctMatchMenuContext MakeUnknownContext()
	{
		FScctMatchMenuContext Unknown = {};
		Unknown.LocalTeam = -1;
		return Unknown;
	}
}

/**
 * The layers of the match overlay. Nothing here takes the mouse while the menu is closed: the banner and the
 * results only show.
 */
class SScctMatchOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctMatchOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		/** The level presents an online round (it was opened with the online option). */
		SLATE_ARGUMENT(bool, bOnlineLevel)
		SLATE_EVENT(FSimpleDelegate, OnResume)
		SLATE_EVENT(FSimpleDelegate, OnLeave)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Online = InArgs._Online;
		bOnlineLevel = InArgs._bOnlineLevel;
		OnResume = InArgs._OnResume;
		OnLeave = InArgs._OnLeave;

		ChildSlot
		[
			SAssignNew(Root, SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)

			// What the visit is waiting for, while the round is not being played.
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 150.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility(TAttribute<EVisibility>::CreateSP(this, &SScctMatchOverlay::GetBannerVisibility))
				[
					Ui::Glass(SNew(STextBlock)
						.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 18.0f, 220))
						.ColorAndOpacity(ScctHudStyle::Text())
						.Text(TAttribute<FText>::CreateSP(this, &SScctMatchOverlay::GetBannerText)), FMargin(30.0f, 14.0f))
				]
			]

			// The round's results.
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(620.0f)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bResultsShown && !bMenuOpen ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
				[
					Ui::Glass(SAssignNew(ResultRows, SVerticalBox))
				]
			]

			// The in-match menu: its entries depend on who the player is (RebuildEntries).
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(320.0f)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bMenuOpen && !Options.IsValid() && !bGearOnly && !bMapOnly ? EVisibility::Visible : EVisibility::Collapsed; }))
				[
					Ui::Glass(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 18.0f)[ Ui::Title(Ui::ToText(TEXT("MENU")), 22.0f) ]
						+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(EntryList, SVerticalBox) ])
				]
			]

			// The Options screen, in place of the menu while it is open; the match goes on underneath.
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SAssignNew(OptionsBox, SBox)
				.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bMenuOpen && Options.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; }))
			]
		];
	}

	void ShowOptions(FName Page)
	{
		// The viewport keeps the keyboard (the menu's keys are read from it): Escape reaches the screen through Back.
		SAssignNew(Options, SScctOptionsPage)
			.Online(Online)
			.InitialPage(Page)
			.bTakeKeyboard(false)
			.OnClose(FSimpleDelegate::CreateLambda([this] { CloseOptions(); }));
		OptionsBox->SetContent(Options.ToSharedRef());
	}

	void CloseOptions()
	{
		Options.Reset();
		OptionsBox->SetContent(SNullWidget::NullWidget);
	}

	/**
	 * The Equipment page over the menu. Opened by the ChangeGear binding (bOnly) its closing goes back to the game;
	 * from the menu's CHANGE GEAR it goes back to the menu. APPLY goes back to the game either way: the server's
	 * answer is the HUD's message line.
	 */
	TSharedPtr<SScctGearPage> ShowGear(int32 Side, bool bOnly)
	{
		bGearOnly = bOnly;
		return SScctGearPage::Open(Root.ToSharedRef(), Side, EScctGearMenuContext::Match, false,
			FSimpleDelegate::CreateLambda([this] { if (bMenuOpen) { OnResume.ExecuteIfBound(); } }),
			FSimpleDelegate::CreateLambda([this] { if (bGearOnly && bMenuOpen) { OnResume.ExecuteIfBound(); } bGearOnly = false; }));
	}

	TSharedPtr<SScctGearPage> GetGear() const { return SScctGearPage::FindOpen(Root.ToSharedRef()); }

	void CloseGear()
	{
		if (const TSharedPtr<SScctGearPage> Gear = GetGear())
		{
			Gear->Close();
		}
		bGearOnly = false;
	}

	/**
	 * The strategic map over the menu. Opened by the DisplayStrategicMap binding (bOnly) its closing goes back to the
	 * game; from the menu's STRATEGIC MAP it goes back to the menu.
	 */
	TSharedPtr<SScctStrategicMapPage> ShowStrategicMap(bool bOnly)
	{
		bMapOnly = bOnly;
		const TSharedPtr<SScctStrategicMapPage> Page = SScctStrategicMapPage::Open(Root.ToSharedRef(), EScctMapContext::Match, FString(), false,
			FSimpleDelegate::CreateLambda([this] { if (bMapOnly && bMenuOpen) { OnResume.ExecuteIfBound(); } bMapOnly = false; }));
		if (!Page.IsValid())
		{
			bMapOnly = false;
		}
		return Page;
	}

	TSharedPtr<SScctStrategicMapPage> GetStrategicMap() const { return SScctStrategicMapPage::FindOpen(Root.ToSharedRef()); }

	void CloseStrategicMap()
	{
		if (const TSharedPtr<SScctStrategicMapPage> Map = GetStrategicMap())
		{
			Map->Close();
		}
		bMapOnly = false;
	}

	/**
	 * Quit Session asks first, in the original's words (SSMenu_ConfirmQuitLAN): leaving a match cannot be taken back,
	 * and the host of an online round ends it for everyone.
	 */
	void AskLeave()
	{
		const TCHAR* Question = !bOnlineLevel ? TEXT("Leave the match and go back to the main menu?")
			: Context.IAmServer != 0 ? TEXT("Leaving the game now will end this session.\nAre you sure?")
			: TEXT("Are you sure you wish to quit?\nYou will leave the server.");
		Ask(EScctMatchMenuButton::QuitSession, Question, TEXT("LEAVE"), TEXT("STAY"), [this] { OnLeave.ExecuteIfBound(); });
	}

	/** Quit Game (GUIYesNoQuitPage.QuitButtonClick): leaves the session (telling the server), then the program quits. */
	void AskQuitGame()
	{
		Ask(QuitGameEntry, TEXT("Quit OpenSCCT?\nYou will leave the match and the game closes."), TEXT("QUIT"), TEXT("STAY"), [this]
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Quit Game: leaving the session, then quitting"));
			if (bOnlineLevel)
			{
				if (UScctOnlineSubsystem* Visit = Online.Get())
				{
					Visit->Leave();
				}
			}
			FPlatformMisc::RequestExit(false);
		});
	}

	/** Opens a question whose subject is an entry, so it can be taken back when the entry goes (a resign once out of lives). */
	void Ask(int32 Entry, const TCHAR* Question, const TCHAR* Yes, const TCHAR* No, TFunction<void()> OnYes)
	{
		Asking = Entry;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: asks \"%s\" (%s / %s)"), *FString(Question).Replace(TEXT("\n"), TEXT(" ")), Yes, No);
		SScctConfirm::Open(Root.ToSharedRef(), Question, Yes, No,
			FSimpleDelegate::CreateLambda([this, OnYes] { Asking = -1; UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: answered yes")); OnYes(); }),
			FSimpleDelegate::CreateLambda([this] { Asking = -1; UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: answered no")); }));
	}

	/** The Help page over the menu (Menu/SScctHelpPage.h); its BACK returns to the menu. */
	void ShowHelp()
	{
		SScctHelpPage::Open(Root.ToSharedRef());
	}

	void CloseHelp()
	{
		if (const TSharedPtr<SScctHelpPage> Help = SScctHelpPage::FindOpen(Root.ToSharedRef()))
		{
			Help->Close();
		}
	}

	/** The menu has just opened: the context is read again, the entries laid out, and keys are held for half a second. */
	void Opened()
	{
		OpenedSeconds = 0.0f;
		ContextTimer = 0.0f;
		RefreshContext(true);
	}

	/**
	 * InGameMenuModel.UpdateContext: the match goes on behind the menu, so who the player is is read again; the
	 * entries are laid out anew when they change, and a resign question is taken back once resigning is no longer
	 * allowed. Logs the entries when they change (or when bLog).
	 */
	void RefreshContext(bool bLog)
	{
		FScctMatchMenuContext Fresh = {};
		const FScctMatchMenuApi* Api = ScctFeature<FScctMatchMenuApi>();
		const bool bKnown = Api != nullptr && Api->GetContext(&Fresh) != 0;
		TArray<int32> Entries;
		if (bKnown)
		{
			Entries.Append(Fresh.Entries, FMath::Clamp(Fresh.EntryCount, 0, FScctMatchMenuContext::MaxEntries));
		}
		else
		{
			// Without the bridge's table (or before the round) the entries every player has.
			Entries = { EScctMatchMenuButton::Back, EScctMatchMenuButton::ChangeGear, EScctMatchMenuButton::StrategicMap, EScctMatchMenuButton::Options,
				EScctMatchMenuButton::QuitSession };
		}
		// OpenSCCT keeps Help beside Options (the Godot client's utility button) and Quit Game after Quit Session (the
		// original's quit page had it as a third answer).
		const int32 OptionsAt = Entries.IndexOfByKey(EScctMatchMenuButton::Options);
		Entries.Insert(EScctMatchMenuButton::Help, OptionsAt == INDEX_NONE ? Entries.Num() : OptionsAt + 1);
		Entries.Add(QuitGameEntry);

		if (bKnown && Fresh.LocalTeam != Context.LocalTeam && Context.LocalTeam >= 0 && Fresh.LocalTeam >= 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: the player's team is now %s"), Fresh.LocalTeam == 0 ? TEXT("the spies") : TEXT("the mercenaries"));
		}
		if (bKnown)
		{
			Context = Fresh;
		}
		if (Asking == EScctMatchMenuButton::Resign && Context.CanResign == 0)
		{
			if (const TSharedPtr<SScctConfirm> Open = SScctConfirm::FindOpen(Root.ToSharedRef()))
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: resigning is no longer allowed; the question is taken back"));
				Open->AnswerNo();
			}
		}
		if (!bMenuOpen || (Entries == ShownEntries && !bLog))
		{
			return;
		}
		ShownEntries = Entries;
		EntryList->ClearChildren();
		TArray<FString> Captions;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const int32 Entry = Entries[Index];
			Captions.Add(Caption(Entry));
			EntryList->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Index + 1 < Entries.Num() ? 8.0f : 0.0f)
			[
				Ui::Button(Ui::ToText(Caption(Entry)), FOnClicked::CreateLambda([this, Entry] { Act(Entry); return FReply::Handled(); }),
					Entry == EScctMatchMenuButton::Back)
			];
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: entries [%s] (%s, %s%s%s%s, team %d, lives %d, mode %d)"), *FString::Join(Captions, TEXT(", ")),
			!bKnown ? TEXT("no context") : Context.Online != 0 ? TEXT("online") : TEXT("offline"), Context.IAmServer != 0 ? TEXT("host") : TEXT("client"),
			Context.Tutorial != 0 ? TEXT(", tutorial") : TEXT(""), Context.Practice != 0 ? TEXT(", practice") : TEXT(""),
			Context.MatchBegun != 0 ? TEXT(", match begun") : TEXT(", match not begun"), Context.LocalTeam, Context.Lives, Context.GameMode);
	}

	FString Caption(int32 Entry) const
	{
		switch (Entry)
		{
		case EScctMatchMenuButton::Back: return TEXT("RESUME");
		case EScctMatchMenuButton::ChangeGear: return TEXT("CHANGE GEAR");
		case EScctMatchMenuButton::SwitchTeam: return Context.LocalTeam == 1 ? TEXT("SWITCH TO SPY") : TEXT("SWITCH TO MERC");
		case EScctMatchMenuButton::StrategicMap: return TEXT("STRATEGIC MAP");
		case EScctMatchMenuButton::Options: return TEXT("OPTIONS");
		case EScctMatchMenuButton::Help: return TEXT("HELP");
		case EScctMatchMenuButton::Resign: return TEXT("RESIGN");
		case EScctMatchMenuButton::ReturnToLobby: return TEXT("RETURN TO LOBBY");
		case EScctMatchMenuButton::QuitSession: return bOnlineLevel ? (Context.IAmServer != 0 ? TEXT("END SESSION") : TEXT("LEAVE THE SERVER")) : TEXT("MAIN MENU");
		case QuitGameEntry: return TEXT("QUIT GAME");
		default: return FString();
		}
	}

	/** A button of the menu (PlayMenu.Act through InGameMenuModel.Click), or the same asked unattended. */
	void Act(int32 Entry)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: %s"), *Caption(Entry));
		const FScctMatchMenuApi* Api = ScctFeature<FScctMatchMenuApi>();
		switch (Entry)
		{
		case EScctMatchMenuButton::Back:
			OnResume.ExecuteIfBound();
			break;
		case EScctMatchMenuButton::ChangeGear:
			ShowGear(-1, false);
			break;
		case EScctMatchMenuButton::StrategicMap:
			ShowStrategicMap(false);
			break;
		case EScctMatchMenuButton::Options:
			ShowOptions(NAME_None);
			break;
		case EScctMatchMenuButton::Help:
			ShowHelp();
			break;
		case EScctMatchMenuButton::SwitchTeam:
			// InGameMenuModel.Click(SwitchTeam): the menu closes and the side changes behind it.
			if (Api != nullptr && Api->SwitchTeam() != 0)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Switch Team from team %d: %s"), Context.LocalTeam, *MatchMenuError(Api));
			}
			else
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctMatchMenu: Switch Team refused: %s"), Api != nullptr ? *MatchMenuError(Api) : TEXT("the bridge has no menu table"));
			}
			OnResume.ExecuteIfBound();
			break;
		case EScctMatchMenuButton::Resign:
			Ask(Entry, TEXT("Give up your remaining lives for this round?"), TEXT("RESIGN"), TEXT("CANCEL"), [this]
			{
				const FScctMatchMenuApi* Table = ScctFeature<FScctMatchMenuApi>();
				const bool bDone = Table != nullptr && Table->Resign() != 0;
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Resign %s"), bDone ? TEXT("sent (!resign)") : *(TEXT("refused: ") + MatchMenuError(Table)));
				OnResume.ExecuteIfBound();
			});
			break;
		case EScctMatchMenuButton::ReturnToLobby:
			Ask(Entry, TEXT("Are you sure you wish to return to lobby?\nEveryone goes back with you."), TEXT("RETURN TO LOBBY"), TEXT("CANCEL"), [this]
			{
				OnResume.ExecuteIfBound();
				const FScctMatchMenuApi* Table = ScctFeature<FScctMatchMenuApi>();
				const bool bDone = Table != nullptr && Table->ReturnToLobby() != 0;
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Return to Lobby %s"), bDone ? TEXT("asked of the server") : *(TEXT("refused: ") + MatchMenuError(Table)));
			});
			break;
		case EScctMatchMenuButton::QuitSession:
			AskLeave();
			break;
		case QuitGameEntry:
			AskQuitGame();
			break;
		default:
			break;
		}
	}

	static FString MatchMenuError(const FScctMatchMenuApi* Api)
	{
		if (Api == nullptr)
		{
			return TEXT("the bridge has no menu table");
		}
		TCHAR Buffer[512];
		Buffer[0] = 0;
		Api->LastError(Buffer, UE_ARRAY_COUNT(Buffer));
		return Buffer;
	}

	/**
	 * -ScctMatchMenuAct=<entry>[,yes]: once a run, a second after the menu first opens, acts on an entry as its button
	 * does (SwitchTeam, Resign, Lobby, Leave, Quit, Help, Gear, Map, Options, Resume); with ",yes" the question it asks
	 * is answered yes a second later.
	 */
	void TickUnattended(float DeltaSeconds)
	{
		static bool bParsed = false;
		static FString Script;
		if (!bParsed)
		{
			bParsed = true;
			FParse::Value(FCommandLine::Get(), TEXT("ScctMatchMenuAct="), Script, false);
		}
		if (Script.IsEmpty())
		{
			return;
		}
		UnattendedSeconds += DeltaSeconds;
		if (!bUnattendedActed && !bMatchMenuActDone && UnattendedSeconds >= 1.0f)
		{
			bUnattendedActed = true;
			bMatchMenuActDone = true;
			FString Name = Script;
			FString Answer;
			Script.Split(TEXT(","), &Name, &Answer);
			bUnattendedYes = Answer.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
			static const TMap<FString, int32> Names = {
				{ TEXT("Resume"), EScctMatchMenuButton::Back }, { TEXT("Back"), EScctMatchMenuButton::Back }, { TEXT("Gear"), EScctMatchMenuButton::ChangeGear },
				{ TEXT("ChangeGear"), EScctMatchMenuButton::ChangeGear }, { TEXT("SwitchTeam"), EScctMatchMenuButton::SwitchTeam },
				{ TEXT("Map"), EScctMatchMenuButton::StrategicMap }, { TEXT("StrategicMap"), EScctMatchMenuButton::StrategicMap },
				{ TEXT("Options"), EScctMatchMenuButton::Options }, { TEXT("Help"), EScctMatchMenuButton::Help }, { TEXT("Resign"), EScctMatchMenuButton::Resign },
				{ TEXT("Lobby"), EScctMatchMenuButton::ReturnToLobby }, { TEXT("ReturnToLobby"), EScctMatchMenuButton::ReturnToLobby },
				{ TEXT("Leave"), EScctMatchMenuButton::QuitSession }, { TEXT("QuitSession"), EScctMatchMenuButton::QuitSession }, { TEXT("Quit"), QuitGameEntry },
			};
			const int32* Entry = Names.Find(Name);
			if (Entry == nullptr)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctMatchMenu: -ScctMatchMenuAct: no entry called %s"), *Name);
				return;
			}
			if (!ShownEntries.Contains(*Entry))
			{
				// Acted on all the same, so a check can see the bridge refuse what the menu would not show.
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctMatchMenu: -ScctMatchMenuAct: %s is not on the menu for this player; acting anyway"), *Caption(*Entry));
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: -ScctMatchMenuAct presses %s"), *Caption(*Entry));
			Act(*Entry);
		}
		else if (bUnattendedActed && bUnattendedYes && UnattendedSeconds >= 2.5f)
		{
			bUnattendedYes = false;
			if (const TSharedPtr<SScctConfirm> Open = SScctConfirm::FindOpen(Root.ToSharedRef()))
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: -ScctMatchMenuAct answers yes"));
				Open->AnswerYes();
			}
		}
	}

	/** Escape or the menu key while the menu is open: answers an open question, or goes back from the page on top. False when only the menu is open. */
	bool Back()
	{
		if (const TSharedPtr<SScctConfirm> Open =SScctConfirm::FindOpen(Root.ToSharedRef()))
		{
			Open->AnswerNo();
			return true;
		}
		if (const TSharedPtr<SScctHelpPage> Help = SScctHelpPage::FindOpen(Root.ToSharedRef()))
		{
			Help->Close();
			return true;
		}
		if (const TSharedPtr<SScctGearPage> Gear = GetGear())
		{
			Gear->RequestBack();
			return true;
		}
		if (const TSharedPtr<SScctStrategicMapPage> Map = GetStrategicMap())
		{
			Map->Close();
			return true;
		}
		if (Options.IsValid())
		{
			// Keep the screen alive through its own callback, which may drop it.
			const TSharedPtr<SScctOptionsPage> Screen = Options;
			Screen->RequestBack();
			return true;
		}
		if (bMenuOpen && OpenedSeconds < MenuKeyHoldSeconds)
		{
			// SSMenu_LANMainPage s_Init: the menu takes no key in its first half second, so a key pressed twice by
			// mistake does not close it again at once.
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: a close key %.2f s after opening is ignored (the first %.1f s are held)"), OpenedSeconds, MenuKeyHoldSeconds);
			return true;
		}
		return false;
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		// The context is followed while the menu is closed too, so a team change asked from it is logged when it lands.
		ContextTimer += InDeltaTime;
		if (ContextTimer >= 0.25f)
		{
			ContextTimer = 0.0f;
			RefreshContext(false);
		}
		if (bMenuOpen)
		{
			OpenedSeconds += InDeltaTime;
			TickUnattended(InDeltaTime);
		}
		const UScctOnlineSubsystem* Visit = Online.Get();
		const bool bShow = bOnlineLevel && Visit != nullptr && Visit->GetState() == EScctOnlineState::Results;
		if (bShow && (!bResultsShown || Visit->GetRevision() != SeenRevision))
		{
			SeenRevision = Visit->GetRevision();
			FillResults(*Visit);
		}
		bResultsShown = bShow && ResultRows->NumSlots() > 0;
	}

	TSharedRef<SOverlay> GetRoot() const { return Root.ToSharedRef(); }

	bool bMenuOpen = false;

	/** The Equipment page was opened on its own (the ChangeGear binding), not from the menu. */
	bool bGearOnly = false;

	/** The strategic map was opened on its own (the DisplayStrategicMap binding), not from the menu. */
	bool bMapOnly = false;

private:
	EVisibility GetBannerVisibility() const
	{
		return bMenuOpen || bResultsShown || GetBannerText().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
	}

	FText GetBannerText() const
	{
		const UScctOnlineSubsystem* Visit = Online.Get();
		if (!bOnlineLevel || Visit == nullptr)
		{
			return FText::GetEmpty();
		}
		switch (Visit->GetState())
		{
		case EScctOnlineState::Travelling: return Ui::ToText(TEXT("TRAVELLING"));
		case EScctOnlineState::Loading:
			return Ui::ToText(Visit->HasFlag(EScctOnlineStatusFlags::ReportedWaiting) ? TEXT("WAITING FOR THE OTHER PLAYERS") : TEXT("JOINING THE ROUND"));
		case EScctOnlineState::Failed: return Ui::ToText(TEXT("CONNECTION LOST"));
		default: return FText::GetEmpty();
		}
	}

	void FillResults(const UScctOnlineSubsystem& Visit)
	{
		ResultRows->ClearChildren();
		FScctOnlineResults Results;
		TArray<FScctOnlineResultRow> Rows;
		if (!Visit.GetResults(Results, Rows))
		{
			return;
		}

		const FString Winner = Results.WinningTeam == 0 ? TEXT("THE SPIES WIN") : Results.WinningTeam == 1 ? TEXT("THE MERCENARIES WIN") : TEXT("ROUND OVER");
		ResultRows->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(Ui::ToText(Winner))
			.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 26.0f, 220))
			.ColorAndOpacity(ScctHudStyle::Accent(Results.WinningTeam))
		];
		const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
		ResultRows->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 14.0f)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([Weak, GamesLeft = Results.GamesLeft]
			{
				const int32 Left = Weak.IsValid() ? FMath::CeilToInt32(Weak->GetStatus().Countdown) : 0;
				return FText::FromString(FString::Printf(TEXT("%s in %d s"), GamesLeft > 0 ? TEXT("Next game") : TEXT("Back to the lobby"), Left));
			}), 11.5f, FSlateColor(ScctHudStyle::TextDim()))
		];
		ResultRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Ui::Rule() ];

		Rows.StableSort([](const FScctOnlineResultRow& A, const FScctOnlineResultRow& B) { return A.Team != B.Team ? A.Team < B.Team : A.Score > B.Score; });
		const auto Cell = [](const FString& String, const FLinearColor& Colour, float Width) -> TSharedRef<SWidget>
		{
			return SNew(SBox).WidthOverride(Width).HAlign(HAlign_Right)[ Ui::Text(Ui::ToText(String), 12.5f, FSlateColor(Colour)) ];
		};
		const auto Line = [&](const FString& Name, const FString& Score, const FString& Kills, const FString& Deaths, const FLinearColor& Colour)
		{
			ResultRows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[ Ui::Text(Ui::ToText(Name), 12.5f, FSlateColor(Colour)) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Cell(Score, Colour, 80.0f) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Cell(Kills, Colour, 80.0f) ]
				+ SHorizontalBox::Slot().AutoWidth()[ Cell(Deaths, Colour, 80.0f) ]
			];
		};
		Line(TEXT("PLAYER"), TEXT("SCORE"), TEXT("KILLS"), TEXT("DEATHS"), ScctHudStyle::TextFaint());
		for (const FScctOnlineResultRow& Row : Rows)
		{
			Line(Row.Name, FString::FromInt(Row.Score), FString::FromInt(Row.Kills), FString::FromInt(Row.Deaths),
				Row.Id == Visit.GetStatus().LocalId ? ScctHudStyle::Accent(Row.Team) : ScctHudStyle::Text());
		}
	}

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	TSharedPtr<SOverlay> Root;
	TSharedPtr<SBox> OptionsBox;
	TSharedPtr<SScctOptionsPage> Options;
	FSimpleDelegate OnResume;
	FSimpleDelegate OnLeave;
	TSharedPtr<SVerticalBox> ResultRows;
	/** The menu's buttons, laid out from the context. */
	TSharedPtr<SVerticalBox> EntryList;
	TArray<int32> ShownEntries;
	FScctMatchMenuContext Context = MakeUnknownContext();
	/** The entry whose question is open, -1 none. */
	int32 Asking = -1;
	float OpenedSeconds = 0.0f;
	float ContextTimer = 0.0f;
	float UnattendedSeconds = 0.0f;
	bool bUnattendedActed = false;
	bool bUnattendedYes = false;
	bool bOnlineLevel = false;
	bool bResultsShown = false;
	int32 SeenRevision = -1;
};

TSharedRef<SWidget> UScctMatchMenuWidget::RebuildWidget()
{
	const UWorld* World = GetWorld();
	SAssignNew(Overlay, SScctMatchOverlay)
		.Online(UScctOnlineSubsystem::Get(this))
		.bOnlineLevel(World != nullptr && World->URL.HasOption(UScctOnlineSubsystem::OnlineOption))
		.OnResume(FSimpleDelegate::CreateWeakLambda(this, [this]
		{
			SetMenuOpen(false);
			OnResumed.ExecuteIfBound();
		}))
		.OnLeave(FSimpleDelegate::CreateUObject(this, &UScctMatchMenuWidget::LeaveMatch));
	Overlay->bMenuOpen = bMenuOpen;
	if (bMenuOpen)
	{
		Overlay->Opened();
	}
	return Overlay.ToSharedRef();
}

void UScctMatchMenuWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Overlay.Reset();
}

void UScctMatchMenuWidget::LeaveMatch()
{
	UScctOnlineSubsystem* Visit = UScctOnlineSubsystem::Get(this);
	const UWorld* World = GetWorld();
	if (Visit == nullptr || World == nullptr)
	{
		return;
	}
	if (World->URL.HasOption(UScctOnlineSubsystem::OnlineOption))
	{
		// The front end opens by itself once the visit is over.
		Visit->Leave();
	}
	else
	{
		Visit->OpenFrontEnd();
	}
}

void UScctMatchMenuWidget::SetMenuOpen(bool bOpen)
{
	bMenuOpen = bOpen;
	if (!Overlay.IsValid())
	{
		return;
	}
	const bool bWasOpen = Overlay->bMenuOpen;
	Overlay->bMenuOpen = bOpen;
	if (bOpen && !bWasOpen)
	{
		Overlay->Opened();
	}
	if (!bOpen)
	{
		// Closed from outside (the game mode): what the Options screen had not applied is dropped with it.
		Overlay->CloseOptions();
		Overlay->CloseGear();
		Overlay->CloseStrategicMap();
		Overlay->CloseHelp();
		if (const TSharedPtr<SScctConfirm> Open = SScctConfirm::FindOpen(Overlay->GetRoot()))
		{
			Open->AnswerNo();
		}
		return;
	}

	// -ScctOptions / -ScctConfirmTest: the first time the menu opens (with -ScctMatchMenu), for screenshots.
	static bool bUnattendedDone = false;
	if (!bUnattendedDone)
	{
		bUnattendedDone = true;
		FString OptionsPage;
		if (FParse::Value(FCommandLine::Get(), TEXT("ScctOptions="), OptionsPage))
		{
			ShowOptions(FName(*OptionsPage));
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("ScctConfirmTest")))
		{
			Overlay->AskLeave();
		}
	}
}

void UScctMatchMenuWidget::ShowOptions(FName Page)
{
	if (Overlay.IsValid())
	{
		Overlay->ShowOptions(Page);
	}
}

bool UScctMatchMenuWidget::ShowGear(int32 Side)
{
	if (!Overlay.IsValid())
	{
		return false;
	}
	// The game mode opens the menu (the keys and the mouse go to it); the page then opens over it on its own.
	const bool bWasOpen = bMenuOpen;
	if (!bWasOpen)
	{
		OnOpenRequested.ExecuteIfBound();
	}
	if (!bMenuOpen)
	{
		return false;
	}
	return Overlay->ShowGear(Side, !bWasOpen).IsValid();
}

bool UScctMatchMenuWidget::ShowStrategicMap()
{
	if (!Overlay.IsValid())
	{
		return false;
	}
	const bool bWasOpen = bMenuOpen;
	if (!bWasOpen)
	{
		OnOpenRequested.ExecuteIfBound();
	}
	if (!bMenuOpen)
	{
		return false;
	}
	const bool bOpened = Overlay->ShowStrategicMap(!bWasOpen).IsValid();
	if (!bOpened && !bWasOpen)
	{
		// Nothing to show: the menu that was opened for it closes again.
		Overlay->bMapOnly = false;
		OnResumed.ExecuteIfBound();
	}
	return bOpened;
}

TSharedPtr<SScctStrategicMapPage> UScctMatchMenuWidget::GetStrategicMapPage() const
{
	return Overlay.IsValid() ? Overlay->GetStrategicMap() : nullptr;
}

TSharedPtr<SScctGearPage> UScctMatchMenuWidget::GetGearPage() const
{
	return Overlay.IsValid() ? Overlay->GetGear() : nullptr;
}

bool UScctMatchMenuWidget::Back()
{
	return Overlay.IsValid() && Overlay->Back();
}
