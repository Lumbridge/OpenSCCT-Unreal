// The in-match menu's entries for who the player is (the Godot client's InGameMenuModel, through the bridge's menu
// table), the pages it opens over itself (Equipment, strategic map, help) and the unattended -ScctMatchMenuAct.
// The rest of the menu is Menu/SScctMatchMenu.cpp.

#include "Menu/SScctMatchMenu.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctHudEdit.h"
#include "Menu/SScctGearPage.h"
#include "Menu/SScctHelpPage.h"
#include "Menu/SScctStrategicMapPage.h"
#include "Menu/ScctMenuKit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctGearMenuApi.h"
#include "Runtime/ScctMapApi.h"
#include "Widgets/SOverlay.h"

namespace
{
	/** Quit Game, an entry of OpenSCCT's menu beside the original's buttons (EScctMatchMenuButton). */
	constexpr int32 MatchMenuQuitGameEntry = 100;

	/** Change side and Restart of a local match (the level again), OpenSCCT's own entries. */
	constexpr int32 MatchMenuChangeSideEntry = 101;
	constexpr int32 MatchMenuRestartEntry = 102;

	/** Edit HUD layout: the HUD's edit mode over the match (Hud/ScctHudEdit.h), OpenSCCT's own entry beside Help. */
	constexpr int32 MatchMenuHudEditEntry = 103;
	/** Help & feedback (Menu/SScctHelpFeedbackPage.h), OpenSCCT's own entry after Help. */
	constexpr int32 MatchMenuFeedbackEntry = 104;

	/** GUI.SSMenu_LANMainPage s_Init: Sleep(0.5), then keys count (InGameMenuK.KeyIgnoreSeconds). */
	constexpr float MatchMenuKeyHoldSeconds = 0.5f;

	/** -ScctMatchMenuAct acted on in this run. */
	bool bMatchMenuActDoneThisRun = false;

	FString MatchMenuTableError(const FScctMatchMenuApi* Api)
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
}

// ---- the pages over the menu ----------------------------------------------------------------------------------------

TSharedPtr<SScctGearPage> SScctMatchMenu::ShowGear(int32 Side, bool bOnly)
{
	if (!PageLayer.IsValid())
	{
		return nullptr;
	}
	bPageOnly = bOnly;
	const TSharedPtr<SScctGearPage> Page = SScctGearPage::Open(PageLayer.ToSharedRef(), Side, EScctGearMenuContext::Match, true,
		// APPLY goes back to the game either way: the server's answer is the HUD's message line.
		FSimpleDelegate::CreateLambda([this] { if (bOpen) { OnResume.ExecuteIfBound(); } }),
		FSimpleDelegate::CreateLambda([this] { if (bPageOnly && bOpen) { OnResume.ExecuteIfBound(); } bPageOnly = false; FocusDefault(); }));
	if (Page.IsValid())
	{
		Page->SetDialogs(Dialogs);
	}
	else
	{
		bPageOnly = false;
	}
	return Page;
}

TSharedPtr<SScctGearPage> SScctMatchMenu::GetGear() const
{
	return PageLayer.IsValid() ? SScctGearPage::FindOpen(PageLayer.ToSharedRef()) : nullptr;
}

TSharedPtr<SScctStrategicMapPage> SScctMatchMenu::ShowStrategicMap(bool bOnly)
{
	if (!PageLayer.IsValid())
	{
		return nullptr;
	}
	bPageOnly = bOnly;
	const TSharedPtr<SScctStrategicMapPage> Page = SScctStrategicMapPage::Open(PageLayer.ToSharedRef(), EScctMapContext::Match, FString(), true,
		FSimpleDelegate::CreateLambda([this] { if (bPageOnly && bOpen) { OnResume.ExecuteIfBound(); } bPageOnly = false; FocusDefault(); }));
	if (!Page.IsValid())
	{
		bPageOnly = false;
	}
	return Page;
}

TSharedPtr<SScctStrategicMapPage> SScctMatchMenu::GetStrategicMap() const
{
	return PageLayer.IsValid() ? SScctStrategicMapPage::FindOpen(PageLayer.ToSharedRef()) : nullptr;
}

void SScctMatchMenu::ShowHelp()
{
	if (PageLayer.IsValid())
	{
		bPageOnly = false;
		SScctHelpPage::Open(PageLayer.ToSharedRef(), FSimpleDelegate::CreateLambda([this] { FocusDefault(); }));
		// The page has no control of its own to focus: the menu holds the keys, so Enter presses nothing behind it
		// and Escape comes to the menu, which closes the page.
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

bool SScctMatchMenu::BackFromOverlayPage()
{
	if (!PageLayer.IsValid())
	{
		return false;
	}
	if (const TSharedPtr<SScctHelpPage> Help = SScctHelpPage::FindOpen(PageLayer.ToSharedRef()))
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
	return false;
}

void SScctMatchMenu::CloseOverlayPages()
{
	if (!PageLayer.IsValid())
	{
		return;
	}
	// Closed from outside (the game mode): what the Equipment page had not applied is dropped with it.
	bPageOnly = false;
	if (const TSharedPtr<SScctHelpPage> Help = SScctHelpPage::FindOpen(PageLayer.ToSharedRef()))
	{
		Help->Close();
	}
	if (const TSharedPtr<SScctGearPage> Gear = GetGear())
	{
		Gear->Close();
	}
	if (const TSharedPtr<SScctStrategicMapPage> Map = GetStrategicMap())
	{
		Map->Close();
	}
	bPageOnly = false;
}

// ---- the entries ----------------------------------------------------------------------------------------------------

TArray<int32> SScctMatchMenu::GetEntries() const
{
	TArray<int32> Entries;
	if (bContextKnown)
	{
		Entries.Append(Context.Entries, FMath::Clamp(Context.EntryCount, 0, FScctMatchMenuContext::MaxEntries));
	}
	else
	{
		// Without the bridge's table (or before the round) the entries every player has.
		Entries = { EScctMatchMenuButton::Back, EScctMatchMenuButton::ChangeGear, EScctMatchMenuButton::StrategicMap, EScctMatchMenuButton::Options,
			EScctMatchMenuButton::QuitSession };
	}
	// OpenSCCT keeps Help beside Options (the Godot client's utility button).
	if (!Entries.Contains(EScctMatchMenuButton::Help))
	{
		const int32 OptionsAt = Entries.IndexOfByKey(EScctMatchMenuButton::Options);
		Entries.Insert(EScctMatchMenuButton::Help, OptionsAt == INDEX_NONE ? Entries.Num() : OptionsAt + 1);
	}
	// The HUD layout's edit mode beside Help (the Godot client's HUD editor is in its options).
	Entries.Insert(MatchMenuHudEditEntry, Entries.IndexOfByKey(EScctMatchMenuButton::Help) + 1);
	// Help & feedback right after Help (the Godot client opens it over any screen).
	Entries.Insert(MatchMenuFeedbackEntry, Entries.IndexOfByKey(EScctMatchMenuButton::Help) + 1);
	// A local match outside practice starts again on the other side or as it was (practice changes side in the round).
	if (CanChangeSide() && !Entries.Contains(EScctMatchMenuButton::SwitchTeam) && Context.Tutorial == 0)
	{
		const int32 HudEditAt = Entries.IndexOfByKey(MatchMenuHudEditEntry);
		Entries.Insert({ MatchMenuChangeSideEntry, MatchMenuRestartEntry }, HudEditAt + 1);
	}
	// The player who controls the server may bring everyone back, as the server list's own host flag says.
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (bOnlineLevel && Visit != nullptr && Visit->HasFlag(EScctOnlineStatusFlags::CanControl) && !Entries.Contains(EScctMatchMenuButton::ReturnToLobby))
	{
		const int32 QuitAt = Entries.IndexOfByKey(EScctMatchMenuButton::QuitSession);
		Entries.Insert(EScctMatchMenuButton::ReturnToLobby, QuitAt == INDEX_NONE ? Entries.Num() : QuitAt);
	}
	// Quit Game after Quit Session (the original's quit page had it as a third answer).
	Entries.Add(MatchMenuQuitGameEntry);
	return Entries;
}

FString SScctMatchMenu::EntryCaption(int32 Entry) const
{
	const bool bMerc = bContextKnown ? Context.LocalTeam == 1 : IsMercLocal();
	switch (Entry)
	{
	case EScctMatchMenuButton::Back: return TEXT("Resume");
	case EScctMatchMenuButton::ChangeGear: return TEXT("Change gear");
	case EScctMatchMenuButton::SwitchTeam: return bMerc ? TEXT("Switch to spy") : TEXT("Switch to merc");
	case EScctMatchMenuButton::StrategicMap: return TEXT("Strategic map");
	case EScctMatchMenuButton::Options: return ScctUi::GameText(TEXT("Start_Menu"), TEXT("Options.Caption"), TEXT("Options"));
	case EScctMatchMenuButton::Help: return TEXT("Help");
	case EScctMatchMenuButton::Resign: return TEXT("Resign");
	case EScctMatchMenuButton::ReturnToLobby: return ScctUi::GameText(TEXT("Start_Menu"), TEXT("Returntolobby.Caption"), TEXT("Return to lobby"));
	case EScctMatchMenuButton::QuitSession:
		return bOnlineLevel ? (Context.IAmServer != 0 && bContextKnown ? TEXT("End session") : TEXT("Leave the server"))
			: ScctUi::GameText(TEXT("Start_Menu"), TEXT("Quit.Caption"), TEXT("Main menu"));
	case MatchMenuQuitGameEntry: return TEXT("Quit game");
	case MatchMenuChangeSideEntry: return ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Button2"), TEXT("Switch Team"));
	case MatchMenuRestartEntry: return TEXT("Restart");
	case MatchMenuHudEditEntry: return TEXT("Edit HUD layout");
	case MatchMenuFeedbackEntry: return TEXT("Help & feedback");
	default: return FString();
	}
}

void SScctMatchMenu::RefreshContext(bool bLog)
{
	FScctMatchMenuContext Fresh = {};
	const FScctMatchMenuApi* Api = ScctFeature<FScctMatchMenuApi>();
	const bool bKnown = Api != nullptr && Api->GetContext(&Fresh) != 0;
	if (bKnown && bContextKnown && Fresh.LocalTeam != Context.LocalTeam && Context.LocalTeam >= 0 && Fresh.LocalTeam >= 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: the player's team is now %s"), Fresh.LocalTeam == 0 ? TEXT("the spies") : TEXT("the mercenaries"));
	}
	if (bKnown)
	{
		Context = Fresh;
		bContextKnown = true;
	}
	// InGameMenuModel.UpdateContext: an open resign question is taken back once resigning is no longer allowed.
	if (Asking == EScctMatchMenuButton::Resign && bContextKnown && Context.CanResign == 0 && Dialogs.IsValid() && Dialogs->IsOpen())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: resigning is no longer allowed; the question is taken back"));
		Asking = -1;
		Dialogs->Answer(0);
	}

	const TArray<int32> Entries = GetEntries();
	// A page over the menu keeps the focus: the pause page is laid out again once it has closed.
	const bool bPageOver = (PageLayer.IsValid() && PageLayer->GetChildren()->Num() > 0) || (Dialogs.IsValid() && Dialogs->IsOpen());
	if (!bOpen || bPageOver || (Entries == ShownEntries && !bLog))
	{
		return;
	}
	const bool bChanged = Entries != ShownEntries;
	ShownEntries = Entries;
	TArray<FString> Captions;
	for (const int32 Entry : Entries)
	{
		Captions.Add(EntryCaption(Entry).ToUpper());
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: entries [%s] (%s, %s%s%s%s, team %d, lives %d, mode %d)"), *FString::Join(Captions, TEXT(", ")),
		!bContextKnown ? TEXT("no context") : Context.Online != 0 ? TEXT("online") : TEXT("offline"), Context.IAmServer != 0 ? TEXT("host") : TEXT("client"),
		Context.Tutorial != 0 ? TEXT(", tutorial") : TEXT(""), Context.Practice != 0 ? TEXT(", practice") : TEXT(""),
		Context.MatchBegun != 0 ? TEXT(", match begun") : TEXT(", match not begun"), Context.LocalTeam, Context.Lives, Context.GameMode);
	// The entries came or went while the pause page is on show: it is laid out again (the focus goes to Resume).
	if (bChanged && View == EView::Pause)
	{
		Show(EView::Pause);
	}
}

void SScctMatchMenu::Ask(int32 Entry, const FString& Title, const FString& Question, const FString& Yes, TFunction<void()> OnYes)
{
	Asking = Entry;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: asks \"%s\" (%s / Cancel)"), *Question.Replace(TEXT("\n"), TEXT(" ")), *Yes);
	FScctDialog Dialog;
	Dialog.Title = Title;
	Dialog.Body = Question;
	Dialog.Buttons.Add({ TEXT("Cancel"), EScctButtonLook::Secondary, [this](const FString&) { Asking = -1; UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: answered no")); } });
	Dialog.Buttons.Add({ Yes, EScctButtonLook::Danger, [this, OnYes](const FString&) { Asking = -1; UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: answered yes")); OnYes(); } });
	Dialog.FocusButton = 0;
	Dialog.CancelButton = 0;
	Dialogs->Show(Dialog);
}

void SScctMatchMenu::Act(int32 Entry)
{
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: %s"), *EntryCaption(Entry).ToUpper());
	ScctUi::Play(EScctUiSound::Accept);
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
		bFeedbackView = false;
		Show(EView::Settings);
		break;
	case MatchMenuFeedbackEntry:
		bFeedbackView = true;
		Show(EView::Settings);
		break;
	case EScctMatchMenuButton::Help:
		ShowHelp();
		break;
	case MatchMenuHudEditEntry:
	{
		// The menu closes first (the game has the keys again), then the edit mode takes them over the live HUD.
		OnResume.ExecuteIfBound();
		UScctHudEditSubsystem* Edit = World.IsValid() ? World->GetSubsystem<UScctHudEditSubsystem>() : nullptr;
		if (Edit == nullptr || !Edit->Start())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctMatchMenu: the HUD layout's edit mode could not open"));
		}
		break;
	}
	case EScctMatchMenuButton::SwitchTeam:
		// InGameMenuModel.Click(SwitchTeam): the menu closes and the side changes behind it.
		if (Api != nullptr && Api->SwitchTeam() != 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Switch Team from team %d: %s"), Context.LocalTeam, *MatchMenuTableError(Api));
		}
		else
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctMatchMenu: Switch Team refused: %s"), *MatchMenuTableError(Api));
		}
		OnResume.ExecuteIfBound();
		break;
	case MatchMenuChangeSideEntry:
	{
		const bool bMerc = IsMercLocal();
		const FString Other = ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), bMerc ? 0 : 1, bMerc ? TEXT("Spy") : TEXT("Mercenary"));
		Ask(Entry, EntryCaption(Entry), FString::Printf(TEXT("Start the match again as a %s? This round's score is lost."), *Other.ToLower()), EntryCaption(Entry),
			[this] { Restart(true); });
		break;
	}
	case MatchMenuRestartEntry:
		Ask(Entry, TEXT("Restart"), TEXT("Start the match again from the beginning? This round's score is lost."), TEXT("Restart"), [this] { Restart(false); });
		break;
	case EScctMatchMenuButton::Resign:
		Ask(Entry, TEXT("Resign"), TEXT("Give up your remaining lives for this round?"), TEXT("Resign"), [this]
		{
			const FScctMatchMenuApi* Table = ScctFeature<FScctMatchMenuApi>();
			const bool bDone = Table != nullptr && Table->Resign() != 0;
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Resign %s"), bDone ? TEXT("sent (!resign)") : *(TEXT("refused: ") + MatchMenuTableError(Table)));
			OnResume.ExecuteIfBound();
		});
		break;
	case EScctMatchMenuButton::ReturnToLobby:
		Ask(Entry, EntryCaption(Entry),
			ScctUi::GameText(TEXT("BackToLobbyValidation"), TEXT("LabelCaption"), TEXT("Stop the match and bring everyone back to the lobby?")), EntryCaption(Entry), [this]
			{
				OnResume.ExecuteIfBound();
				// The visit's own request (main's way) when it may control the server, else the bridge's (an online host).
				UScctOnlineSubsystem* Visit = Online.Get();
				if (Visit != nullptr && Visit->HasFlag(EScctOnlineStatusFlags::CanControl))
				{
					Visit->BackToLobby();
					UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Return to Lobby asked of the server"));
					return;
				}
				const FScctMatchMenuApi* Table = ScctFeature<FScctMatchMenuApi>();
				const bool bDone = Table != nullptr && Table->ReturnToLobby() != 0;
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: Return to Lobby %s"), bDone ? TEXT("asked of the server") : *(TEXT("refused: ") + MatchMenuTableError(Table)));
			});
		break;
	case EScctMatchMenuButton::QuitSession:
		// SSMenu_ConfirmQuitLAN: leaving cannot be taken back, and the host of an online round ends it for everyone.
		Ask(Entry, EntryCaption(Entry),
			!bOnlineLevel ? TEXT("End the match and go back to the main menu?")
				: Context.IAmServer != 0 && bContextKnown ? TEXT("Leaving the game now will end this session. Are you sure?")
				: TEXT("Leave the server and go back to the menus? The match goes on without you."),
			bOnlineLevel ? TEXT("Leave") : TEXT("Quit match"), [this] { OnLeave.ExecuteIfBound(); });
		break;
	case MatchMenuQuitGameEntry:
		// GUIYesNoQuitPage.QuitButtonClick: leaves the session (telling the server), then the program quits.
		Ask(Entry, TEXT("Quit game"), TEXT("Quit OpenSCCT? You will leave the match and the game closes."), TEXT("Quit"), [this]
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
		break;
	default:
		break;
	}
}

void SScctMatchMenu::TickUnattended(float DeltaSeconds)
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
	if (!bUnattendedActed && !bMatchMenuActDoneThisRun && UnattendedSeconds >= 1.0f)
	{
		bUnattendedActed = true;
		bMatchMenuActDoneThisRun = true;
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
			{ TEXT("Leave"), EScctMatchMenuButton::QuitSession }, { TEXT("QuitSession"), EScctMatchMenuButton::QuitSession }, { TEXT("Quit"), MatchMenuQuitGameEntry },
			{ TEXT("ChangeSide"), MatchMenuChangeSideEntry }, { TEXT("Restart"), MatchMenuRestartEntry }, { TEXT("HudEdit"), MatchMenuHudEditEntry }, { TEXT("Feedback"), MatchMenuFeedbackEntry },
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
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctMatchMenu: -ScctMatchMenuAct: %s is not on the menu for this player; acting anyway"), *EntryCaption(*Entry));
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: -ScctMatchMenuAct presses %s"), *EntryCaption(*Entry).ToUpper());
		Act(*Entry);
	}
	else if (bUnattendedActed && bUnattendedYes && UnattendedSeconds >= 2.5f)
	{
		bUnattendedYes = false;
		if (Dialogs.IsValid() && Dialogs->IsOpen())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctMatchMenu: -ScctMatchMenuAct answers yes"));
			Dialogs->Answer(1);
		}
	}
}
