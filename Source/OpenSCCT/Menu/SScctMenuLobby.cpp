// The lobby of a server: the two sides, the map and its rules, the chat, the host's setup and the ready and launch
// buttons. Shown by the front end while the visit is in EScctOnlineState::Lobby (Menu/SScctFrontEnd.cpp).
//
// Launch feedback (the Godot client's OnlineLobby map line and the server's LaunchProblem): who lacks the setup's map
// or has another version of it (FScctOnlinePlayer::MapState), whether this game can show that map, and, above the
// buttons, why a launch would be refused now. Ready is off while the local player lacks the host's map.
//
// Moderation (LobbyRoomScreen's rows): for the host or an admin (CanModerate) every other person's row has MANAGE,
// which asks what to do (kick, ban, voice ban or unban) and then asks again as Godot does ("Do you want to kick NAME?").
//
// Session Options (Menu/SScctSessionOptions.h): every row of the setup with a draft and APPLY, shown in place of the
// lobby; RANDOM MAP draws a map that offers the mode.
//
// Unattended switches:
//   -ScctLobbyManage=<name>[,kick|ban|voiceban]  ask what to do with that player once in the lobby, and with an
//                                 action its question too (YES is never pressed: -ScctKickAfter does that)
//   -ScctLobbyTeamChat            the chat starts on the TEAM channel
//   -ScctLobbyOptions             open Session Options when the lobby is first shown (any player)
//   -ScctLobbyApply=<line>        the controller opens Session Options and puts an options line into the draft: MapName
//                                 and Mode (a mode key) or GameMode (a number) first, with their rules, then any setup
//                                 value ("?MapName=SteeE?Mode=DiskHunt?TimeLimit=5"); Tab=rules|alternate picks the tab
//   -ScctLobbyApplyAfter=S        seconds (on the clock) before APPLY is pressed, counted from when enough players are in
//                                 the lobby (default 4)
//   -ScctLobbyApplyWhen=N         how many players that is (default 1: the host alone)
//   -ScctLobbyShow=<what>[,...]   open a lobby dialog unattended: scoreboard (each new last-round scoreboard, once it is
//                                 in), briefing (once per run), plan (the briefing on its second page), armoury (the
//                                 COSMETICS button, pressed five seconds after the lobby is reached)
//   -ScctLobbyRecord=1|0          set "Record my matches" once the lobby is reached, as the switch would
//
// Extras for every player (Menu/SScctLobbyExtras.h, the Godot lobby's LobbyRoomScreen.Scoreboard, BriefingPanel and
// RankIcons): under the map, the last game's map and outcome with VIEW SCOREBOARD (the round's final scoreboard, both
// sides and a player's breakdown) once this visit has played a round; BRIEFING (the setup's map and mode for the
// player's side, the level picture and the plan); a rank badge in its emblem colour on each ranked player's row, its
// words in a tooltip; and the personal "Record my matches" switch with the server's answer under it (the same option
// as the Online tab's "Record online matches", which applies from the next round). The loadout summary and the
// strategic map are the row above (Menu/ScctOverlayPages.h). COSMETICS, beside Switch Team, opens the Armoury over
// the lobby (Menu/SScctArmouryOverlay.h, Godot's Cosmetics button): a look picked there is saved and the bridge
// sends it to the server, worn from the next round; closing it is back in the lobby.

#include "Menu/ScctMenuPage.h"
#include "Menu/SScctArmouryOverlay.h"
#include "Menu/ScctOverlayPages.h"
#include "Menu/ScctRankIcons.h"

#include "Framework/Application/SlateApplication.h"
#include "Demo/ScctDemoSubsystem.h"
#include "Engine/GameInstance.h"
#include "Menu/SScctLobbyExtras.h"
#include "Menu/SScctSessionOptions.h"
#include "Online/ScctLobbyExtras.h"
#include "Settings/ScctSettings.h"
#include "Menu/ScctBotNavPrompt.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/ScctContentDownloads.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctContentApi.h"
#include "Runtime/ScctOnlineApi.h"
#include "Engine/GameInstance.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"
#include "Voice/SScctVoiceLobbyControls.h"
#include "Voice/ScctVoiceSubsystem.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** FScctOnlinePlayer::MapState. */
	constexpr int32 LobbyMapUnknown = 0;
	constexpr int32 LobbyMapMissing = 2;
	constexpr int32 LobbyMapDiffers = 3;

	bool LobbyOnSide(const FScctOnlinePlayer& Player)
	{
		return Player.Team == 0 || Player.Team == 1;
	}

	enum class ELobbyModerate : uint8 { Kick, Ban, VoiceBan, VoiceUnban };

	const int32 LobbyTimeLimits[] = { 0, 5, 8, 10, 12, 15, 20, 25, 30, 45, 60 };

	int32 LobbyModeIndex(int32 ModeId)
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (UScctMenuSubsystem::ModeId(Index) == ModeId)
			{
				return Index;
			}
		}
		return 0;
	}


	const FScctOnlinePlayer* LobbyLocalPlayer(const UScctOnlineSubsystem& Visit)
	{
		const int32 LocalId = Visit.GetStatus().LocalId;
		return LocalId < 0 ? nullptr : Visit.GetPlayers().FindByPredicate([LocalId](const FScctOnlinePlayer& P) { return P.Id == LocalId; });
	}

	/** The local player hosts the server (a listen server: a dedicated one has no host). */
	bool LobbyLocalIsHost(const UScctOnlineSubsystem& Visit)
	{
		const FScctOnlinePlayer* Local = LobbyLocalPlayer(Visit);
		return Local != nullptr && (Local->Flags & EScctOnlinePlayerFlags::Host) != 0;
	}

	bool LobbyLocalOnSide(const UScctOnlineSubsystem& Visit)
	{
		const int32 Team = Visit.GetStatus().LocalTeam;
		return Team == 0 || Team == 1;
	}

	/** LAUNCH, or CANCEL while the launch counts down. */
	FString LobbyLaunchCaption(const UScctOnlineSubsystem& Visit)
	{
		return (Visit.GetStatus().Countdown > 0.0f
			? ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Control2ServerCancel"), TEXT("Cancel"))
			: ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Control2Server"), TEXT("Launch"))).ToUpper();
	}

	FLinearColor LobbyPingColour(int32 PingMs)
	{
		return PingMs < 80 ? FLinearColor(0.35f, 0.9f, 0.55f) : PingMs < 160 ? FLinearColor(0.95f, 0.8f, 0.3f) : ScctHudStyle::Danger();
	}

	class SScctMenuLobby : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMenuLobby) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			for (const FString& Map : UScctOnlineSubsystem::FindPlayableMaps())
			{
				PlayableMaps.Add(Map.ToLower());
			}
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			const FString Ready = ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Control2ClientReady"), TEXT("Ready"));
			const FString NotReady = ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Control2ClientNotReady"), TEXT("Not ready"));

			TSharedPtr<SScctButton> ReadyButton;
			ChildSlot
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
				SAssignNew(Switcher, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()
				[
				SNew(SHorizontalBox)
				// The sides.
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 22.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ ScctUi::Card(TEXT("Spies"), SAssignNew(SpyList, SVerticalBox), FMargin(18.0f, 16.0f)) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(10.0f, 0.0f, 0.0f, 0.0f)[ ScctUi::Card(TEXT("Mercenaries"), SAssignNew(MercList, SVerticalBox), FMargin(18.0f, 16.0f)) ]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 10.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim())
						.Text_Lambda([this] { return FText::FromString(Spectators); })
					]
					// The chat.
					+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 14.0f, 0.0f, 0.0f)
					[
						ScctUi::Card(TEXT("Chat"),
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().FillHeight(1.0f)
							[
								SAssignNew(ChatScroll, SScrollBox)
								+ SScrollBox::Slot()[ SAssignNew(ChatLines, SVerticalBox) ]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f)
								[
									// The menus' own field, as on the other pages (it kept Slate's default look).
									MakeChatBox()
								]
								+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SScctButton)
									.Look(EScctButtonLook::Chip)
									.Text_Lambda([this] { return FText::FromString(bTeamChat ? ScctUi::GameText(TEXT("Lobby_Create"), TEXT("ChatBox.txtTeam"), TEXT("Team")).Replace(TEXT("("), TEXT("")).Replace(TEXT(")"), TEXT("")).TrimStartAndEnd().ToUpper() : FString(TEXT("ALL"))); })
									.Description(FText::FromString(TEXT("Who reads what you write: everyone, or your side only.")))
									.OnPressed(FSimpleDelegate::CreateLambda([this] { bTeamChat = !bTeamChat; }))
								]
							], FMargin(18.0f, 16.0f))
					]
					// The personal demo recording (the Godot lobby's PersonalRecordDemos) and what the server says of it.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
					[
						BuildRecording()
					]
				]
				// The map, the setup and the buttons.
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(520.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(MapCard, SBox) ]
						// The setup scrolls in what is left, so the buttons under it are always on screen.
						+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 14.0f, 0.0f, 14.0f)
						[
							SNew(SScrollBox)
							.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
							+ SScrollBox::Slot()[ SAssignNew(SetupCard, SBox) ]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctUi::Accent()).Justification(ETextJustify::Center)
							.Visibility_Lambda([Weak] { return Weak.IsValid() && Weak->GetStatus().Countdown > 0.0f ? EVisibility::Visible : EVisibility::Collapsed; })
							.Text_Lambda([Weak] { return FText::FromString(Weak.IsValid() ? FString::Printf(TEXT("Starting in %d"), FMath::CeilToInt32(Weak->GetStatus().Countdown)) : FString()); })
						]
						// Why a launch would be refused now, and a reminder of a Session Options draft not applied.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
							.Visibility_Lambda([this] { return LaunchLine().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
							.Text_Lambda([this] { return FText::FromString(LaunchLine()); })
						]
						// What the match waits for (who is not ready yet), as Godot's lobby status line says it.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).Justification(ETextJustify::Center)
							.Visibility_Lambda([this, Weak] { return WaitingFor.IsEmpty() || (Weak.IsValid() && Weak->GetStatus().Countdown > 0.0f) ? EVisibility::Collapsed : EVisibility::Visible; })
							.Text_Lambda([this] { return FText::FromString(WaitingFor); })
						]
						// A player of a dedicated server may ask it to start (or stop its countdown) without waiting for
						// the start it makes on its own. The host of a listen server launches from the main button.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							SNew(SScctButton)
							.FillWidth(true)
							.Visibility_Lambda([Weak]
							{
								const UScctOnlineSubsystem* V = Weak.Get();
								return V != nullptr && V->HasFlag(EScctOnlineStatusFlags::CanLaunch) && !LobbyLocalIsHost(*V) && LobbyLocalOnSide(*V)
									? EVisibility::Visible : EVisibility::Collapsed;
							})
							.Text_Lambda([Weak] { return FText::FromString(Weak.IsValid() ? LobbyLaunchCaption(*Weak.Get()) : FString()); })
							.Description(FText::FromString(TEXT("Start the match now (or stop its countdown). The server starts it only when everyone on a side is ready, you too.")))
							.OnPressed(FSimpleDelegate::CreateLambda([Weak]
							{
								UScctOnlineSubsystem* V = Weak.Get();
								if (V == nullptr) { return; }
								// The server wants the one launching ready as well; a press during the countdown cancels it.
								if (V->GetStatus().Countdown <= 0.0f && !V->HasFlag(EScctOnlineStatusFlags::LocalReady)) { V->SetReady(true); }
								V->Launch();
							}))
						]
						// The kit the player takes into the round for the player's side (the spy's while spectating), the
						// Equipment page under the lobby's rules and the lobby map's strategic map (Menu/ScctOverlayPages.cpp).
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							ScctOverlayPages::MakeRow(Weak, [this] { OpenGear(); }, [this] { OpenStrategicMap(); })
						]
						// BRIEFING and, once a round was played, VIEW SCOREBOARD (Menu/SScctLobbyExtras.h).
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							BuildExtrasRow()
						]
						// The buttons wrap: a button that does not fit the column whole goes onto the next line and no
						// caption is ever cut. The last button of a line takes what is left of it, so LAUNCH / READY,
						// which comes last, is the widest: a line of its own when the three before it fill the first.
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, 8.0f))
							+ SWrapBox::Slot()
							[
								SNew(SScctButton)
								.Text(FText::FromString(ScctUi::GameText(TEXT("Lobby_Create"), TEXT("ButtonB"), TEXT("Leave")).ToUpper()))
								.Description(FText::FromString(TEXT("Leave the server and go back to the menus.")))
								.OnPressed(FSimpleDelegate::CreateLambda([this] { if (Host.Back) { Host.Back(); } }))
							]
							+ SWrapBox::Slot()
							[
								SNew(SScctButton)
								.Text(FText::FromString(ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Button2"), TEXT("Switch Team")).ToUpper()))
								.Description(FText::FromString(TEXT("Play on the other side.")))
								.OnPressed(FSimpleDelegate::CreateLambda([Weak]
								{
									if (UScctOnlineSubsystem* V = Weak.Get()) { V->SetTeam(V->GetStatus().LocalTeam == 0 ? 1 : 0); }
								}))
							]
							+ SWrapBox::Slot().FillEmptySpace(true)
							[
								SAssignNew(CosmeticsButton, SScctButton)
								.FillWidth(true)
								.Text(FText::FromString(TEXT("COSMETICS")))
								.Description(FText::FromString(TEXT("Open the Armoury over the lobby: pick the camo, tint and light colour of each side. Your look is sent to this server and worn from the next round.")))
								.OnPressed(FSimpleDelegate::CreateLambda([this] { OpenArmoury(); }))
							]
							+ SWrapBox::Slot().FillEmptySpace(true)
							[
								SAssignNew(ReadyButton, SScctButton)
								.Look(EScctButtonLook::Primary)
								.FillWidth(true)
								// Without the host's map (or with another version of it) the server refuses "ready".
								.IsEnabled_Lambda([this, Weak] { return LocalHasMap() || (Weak.IsValid() && Weak->HasFlag(EScctOnlineStatusFlags::LocalReady)); })
								.Text_Lambda([Weak, Ready, NotReady]
								{
									const UScctOnlineSubsystem* V = Weak.Get();
									if (V != nullptr && LobbyLocalIsHost(*V))
									{
										return FText::FromString(LobbyLaunchCaption(*V));
									}
									// The button says what pressing it does.
									return FText::FromString((V != nullptr && V->HasFlag(EScctOnlineStatusFlags::LocalReady) ? NotReady : Ready).ToUpper());
								})
								.Description(FText::FromString(TEXT("Say you are ready, or not ready any more (the host of a player-run server starts the match here instead). A match starts when everyone on a side is ready.")))
								.OnPressed(FSimpleDelegate::CreateLambda([this, Weak]
								{
									UScctOnlineSubsystem* V = Weak.Get();
									if (V == nullptr) { return; }
									// The host launches (the server does not wait for the host's ready). Everyone else,
									// on a dedicated server too, says ready or not ready, as Godot's lobby page does: a
									// dedicated server starts on its own once everyone on a side is ready, and the
									// LAUNCH button over this row asks it to start sooner.
									if (LobbyLocalIsHost(*V))
									{
										// A countdown already running is cancelled at once; a game this player hosts with bots
										// first offers the map's prebuilt bot navigation (the Godot lobby's Start).
										if (V->GetStatus().Countdown > 0.0f || !V->IsHosting() || !HasBots(V))
										{
											V->Launch();
											return;
										}
										const FString Map = V->GetSetupMap();
										ScctBotNavPrompt::OfferThenLaunch(Host.Dialogs, Map,
											[Weak, Map] { return Weak.IsValid() && Weak->GetState() == EScctOnlineState::Lobby && Weak->GetSetupMap() == Map && LobbyLocalIsHost(*Weak.Get()); },
											[Weak] { if (UScctOnlineSubsystem* Now = Weak.Get()) { Now->Launch(); } });
									}
									else { V->SetReady(!V->HasFlag(EScctOnlineStatusFlags::LocalReady)); }
								}))
							]
						]
					]
				]
				]
				// Session Options, in place of the lobby while open.
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(Options, SScctSessionOptions)
					.Online(Host.Online)
					.OnClose(FSimpleDelegate::CreateLambda([this] { ShowOptions(false); }))
				]
				]
				// The Equipment page and the strategic map open over the lobby.
				+ SOverlay::Slot()[ SAssignNew(PageLayer, SOverlay) ]
				// The match scoreboard and the briefing open over the lobby too.
				+ SOverlay::Slot()[ SAssignNew(ExtrasLayer, SOverlay) ]
			];
			DefaultFocus = ReadyButton;
			ReadUnattended();
			Refresh();
			if (bOpenOptionsAtStart)
			{
				ShowOptions(true);
			}
		}

		virtual FString GetTitle() const override
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			return Visit != nullptr && !Visit->GetServerName().IsEmpty() ? Visit->GetServerName() : ScctUi::GameText(TEXT("Lobby_Create"), TEXT("TitlePage"), TEXT("Lobby Room"));
		}

		virtual FString GetKicker() const override
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr) { return FString(); }
			const FScctOnlineStatus& Status = Visit->GetStatus();
			return FString::Printf(TEXT("Lobby · %d/%d players%s%s"), Status.PlayerCount, Status.MaxPlayers,
				Visit->HasFlag(EScctOnlineStatusFlags::Dedicated) ? TEXT(" · dedicated") : TEXT(""),
				Visit->HasFlag(EScctOnlineStatusFlags::Practice) ? TEXT(" · practice") : TEXT(""));
		}

		virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const override
		{
			OutKeyAndWhat.Add({ TEXT("T"), TEXT("Chat") });
			OutPadKeys.Add(FString());
		}

		/** Escape or B: closes Session Options (its draft kept), else the same question as Leave (Stay has the focus). */
		virtual bool HandleBack() override
		{
			// A dialog or a page over the lobby goes back first.
			if (ScctLobbyPanels::Back(ExtrasLayer) || SScctArmouryOverlay::Back(PageLayer) || ScctOverlayPages::Back(PageLayer))
			{
				return true;
			}
			if (bOptionsOpen)
			{
				ShowOptions(false);
				return true;
			}
			if (Host.Back)
			{
				Host.Back();
			}
			return true;
		}

		TSharedRef<SEditableTextBox> MakeChatBox()
		{
			ChatBox = ScctUi::TextBox(TEXT("Say something (Enter sends)"), FString(), false, FOnTextCommitted::CreateSP(this, &SScctMenuLobby::SendChat));
			ChatBox->SetClearKeyboardFocusOnCommit(false);
			ChatBox->SetHintText(TAttribute<FText>::CreateLambda([this]
			{
				return FText::FromString(!ChatNotice.IsEmpty() ? ChatNotice : bTeamChat ? FString(TEXT("Say something to your side (Tab: everyone)")) : FString(TEXT("Say something (Enter sends, Tab: your side only)")));
			}));
			// Tab switches the channel while typing, as Godot's ChatInput does (it would otherwise move the focus away).
			ChatBox->SetOnKeyDownHandler(FOnKeyDown::CreateLambda([this](const FGeometry&, const FKeyEvent& Key)
			{
				if (Key.GetKey() == EKeys::Tab && !Key.IsRepeat())
				{
					bTeamChat = !bTeamChat;
					ChatNotice.Reset();
					return FReply::Handled();
				}
				return FReply::Unhandled();
			}));
			return ChatBox.ToSharedRef();
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if (!bOptionsOpen && !ScctLobbyPanels::IsOpen(ExtrasLayer) && (Key == EKeys::T || Key == EKeys::Enter))
			{
				FSlateApplication::Get().SetKeyboardFocus(ChatBox, EFocusCause::SetDirectly);
				return true;
			}
			return false;
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SScctMenuPage::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			// The missing map's download, and the maps this game can play once one came in.
			ScctContentDownloads::GetLobbyFetch(Fetch);
			const int32 Generation = ScctContentDownloads::Generation();
			if (Generation != SeenGeneration)
			{
				if (SeenGeneration >= 0)
				{
					PlayableMaps.Reset();
					for (const FString& Map : UScctOnlineSubsystem::FindPlayableMaps())
					{
						PlayableMaps.Add(Map.ToLower());
					}
				}
				SeenGeneration = Generation;
			}
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit != nullptr && (Visit->GetRevision() != SeenRevision || Visit->GetChat().Num() != SeenChat))
			{
				Refresh();
			}
			RunUnattended();
		}

	private:
		void OpenGear() { ScctOverlayPages::OpenGear(PageLayer, Host); }
		void OpenStrategicMap() { ScctOverlayPages::OpenStrategicMap(PageLayer); }

		/** The Armoury over the lobby (Godot's LobbyRoomScreen.OpenArmoury); closing it gives the Cosmetics button the focus back. */
		void OpenArmoury()
		{
			if (!PageLayer.IsValid())
			{
				return;
			}
			const TWeakPtr<SScctButton> Button = CosmeticsButton;
			SScctArmouryOverlay::Open(PageLayer.ToSharedRef(), Host, TEXT("the lobby"), FSimpleDelegate::CreateLambda([Button]
			{
				if (const TSharedPtr<SScctButton> Pinned = Button.Pin())
				{
					FSlateApplication::Get().SetKeyboardFocus(Pinned, EFocusCause::SetDirectly);
				}
			}));
		}
		TSharedPtr<SScctButton> CosmeticsButton;
		/** -ScctLobbyShow=armoury, and when its press is due. */
		bool bShowArmoury = false;
		double ShowArmouryAt = 0.0;

		/** The Equipment page and the strategic map, over the lobby. */
		TSharedPtr<SOverlay> PageLayer;

		/** The match scoreboard and the briefing, over the lobby. */
		TSharedPtr<SOverlay> ExtrasLayer;

		// ---- extras for every player ----------------------------------------------------------------------------

		/** BRIEFING, and VIEW SCOREBOARD while this visit has a last round's scoreboard. */
		TSharedRef<SWidget> BuildExtrasRow()
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text_Lambda([this] { return FText::FromString(LastGame.IsEmpty() ? FString(TEXT("No game played on this server yet.")) : LastGame); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SScctButton)
					.Visibility_Lambda([this] { return LastGame.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					.Text(FText::FromString(TEXT("VIEW SCOREBOARD")))
					.Description(FText::FromString(TEXT("The last game's scoreboard: both sides' tables and each player's round totals.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { ScctLobbyPanels::OpenScoreboard(ExtrasLayer); }))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SScctButton)
					.Text(FText::FromString(ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Briefing"), TEXT("Briefing")).ToUpper()))
					.Description(FText::FromString(TEXT("The map and mode of the next match for your side: the main objective, the plan of the map and its legend.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { ScctLobbyPanels::OpenBriefing(ExtrasLayer); }))
				];
		}

		/** "Record my matches" and the server's answer (the personal demo; the Online tab's "Record online matches"). */
		TSharedRef<SWidget> BuildRecording()
		{
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SScctToggle)
					.Label(FText::FromString(TEXT("Record my matches")))
					.Description(FText::FromString(TEXT("Save the replay of each round you play here to Saved/Demos on this computer. Applies from the next round; stay connected after a round until its download finishes.")))
					.Get([] { return UScctDemoSubsystem::SavesOnline(); })
					.Set([](bool bOn) { SetRecording(bOn); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text_Lambda([Weak]
					{
						const UGameInstance* Game = Weak.IsValid() ? Weak->GetGameInstance() : nullptr;
						const UScctDemoSubsystem* Demos = Game != nullptr ? Game->GetSubsystem<UScctDemoSubsystem>() : nullptr;
						return FText::FromString(Demos != nullptr ? Demos->GetOnlineStatus() : FString());
					})
				];
		}

		static void SetRecording(bool bOn)
		{
			UScctDemoSubsystem::RegisterOptions();
			FScctSettings::SetBool(UScctDemoSubsystem::RecordOnlineSetting, bOn);
			FScctSettings::Save();
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby turns personal match recording %s"), bOn ? TEXT("on") : TEXT("off"));
		}

		/** The lobby's "Last game" lines and, once per new scoreboard, its rows in the log. */
		void RefreshLastGame()
		{
			int32 Sequence = 0;
			const bool bHas = ScctLobbyExtras::HasLastScoreboard(&Sequence);
			LastGame = bHas ? ScctLobbyExtras::LastScoreboardSummary().Replace(TEXT("\n"), TEXT("  ·  ")) : FString();
			if (bHas && Sequence != SeenScoreboard)
			{
				SeenScoreboard = Sequence;
				UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby has the last game's scoreboard: %s"), *LastGame);
			}
		}

		/** One log line with every player's rank whenever a rank or the players change (the badges the rows show). */
		void LogRanks(const UScctOnlineSubsystem& Visit)
		{
			TArray<FString> Parts;
			for (const FScctOnlinePlayer& Player : Visit.GetPlayers())
			{
				const FString Badge = ScctLobbyExtras::RankBadge(Player.Rank);
				Parts.Add(FString::Printf(TEXT("%s=%s"), Player.Name, Badge.IsEmpty() ? TEXT("none") : *FString::Printf(TEXT("%s (%s, #%s)"), *Badge,
					*ScctLobbyExtras::RankWords(Player.Rank), *ScctLobbyExtras::RankColour(Player.Rank).ToFColor(true).ToHex().Left(6))));
			}
			const FString Line = FString::Join(Parts, TEXT("; "));
			if (Line != LoggedRanks)
			{
				LoggedRanks = Line;
				UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby ranks: %s"), *Line);
			}
		}

		/** The lobby's "Last game" lines, empty before the visit's first round. */
		FString LastGame;
		int32 SeenScoreboard = 0;
		FString LoggedRanks;

		// The chat: the other team's chat line in the match HUD is separate (Play/ScctHudWidget).
		void SendChat(const FText& Text, ETextCommit::Type Commit)
		{
			if (Commit != ETextCommit::OnEnter) { return; }
			const FString Line = Text.ToString().TrimStartAndEnd();
			if (!Line.IsEmpty())
			{
				if (UScctOnlineSubsystem* Visit = Host.Online.Get())
				{
					// "/admin <password>" logs in as the server's admin; other slash lines are admin commands.
					if (Line.StartsWith(TEXT("/admin "))) { Visit->AdminLogin(Line.Mid(7)); }
					else if (Line.StartsWith(TEXT("/"))) { Visit->AdminCommand(Line.Mid(1)); }
					else
					{
						const int32 LocalTeam = Visit->GetStatus().LocalTeam;
						if (bTeamChat && LocalTeam != 0 && LocalTeam != 1)
						{
							// The server would send it to nobody: the line stays in the box.
							ChatNotice = TEXT("Join a side before sending team chat (Tab: everyone).");
							return;
						}
						ChatNotice.Reset();
						Visit->SendChat(Line, bTeamChat);
						if (bTeamChat)
						{
							UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby chat sent to the team only"));
						}
					}
				}
			}
			ChatBox->SetText(FText::GetEmpty());
		}

		// ---- Session Options -----------------------------------------------------------------------------------

		void ShowOptions(bool bOpen)
		{
			bOptionsOpen = bOpen;
			if (bOpen)
			{
				Options->Open();
				Switcher->SetActiveWidgetIndex(1);
				UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby shows Session Options"));
			}
			else
			{
				Switcher->SetActiveWidgetIndex(0);
				// The setup card is made again: the controller's steppers show the values the server now has.
				SetupBuilt = false;
				Refresh();
			}
		}

		// ---- launch feedback ------------------------------------------------------------------------------------

		static bool HasBots(const UScctOnlineSubsystem* Visit)
		{
			return Visit->GetStatus().Bots != 0 || Visit->GetPlayers().ContainsByPredicate([](const FScctOnlinePlayer& Player) { return (Player.Flags & EScctOnlinePlayerFlags::Bot) != 0; });
		}

		bool LocalHasMap() const
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			const FScctOnlinePlayer* Local = Visit != nullptr ? Visit->FindLocalPlayer() : nullptr;
			return Local == nullptr || (Local->MapState != LobbyMapMissing && Local->MapState != LobbyMapDiffers);
		}

		/** Who lacks the setup's map or has another version, and whether this game can show it. bProblem: it holds the launch. */
		FString MapLine(bool& bProblem) const
		{
			bProblem = false;
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr)
			{
				return FString();
			}
			const FString Map = Visit->GetSetupMap();
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const FString Title = Menu != nullptr ? Menu->GetMapTitle(Map) : Map;
			TArray<FString> Missing, Differs, Checking;
			int32 LocalState = -1;
			for (const FScctOnlinePlayer& Player : Visit->GetPlayers())
			{
				if (Player.Id == Visit->GetStatus().LocalId)
				{
					LocalState = Player.MapState;
					continue;
				}
				if (!LobbyOnSide(Player) || (Player.Flags & EScctOnlinePlayerFlags::Bot) != 0)
				{
					continue;
				}
				switch (Player.MapState)
				{
				case LobbyMapMissing: Missing.Add(Player.Name); break;
				case LobbyMapDiffers: Differs.Add(Player.Name); break;
				case LobbyMapUnknown: Checking.Add(Player.Name); break;
				default: break;
				}
			}

			TArray<FString> Lines;
			if (LocalState == LobbyMapMissing)
			{
				Lines.Add(FString::Printf(TEXT("You don't have %s, so you cannot be ready and the match cannot start with you on a side until you have it. Watching lets the others play."), *Map));
			}
			else if (LocalState == LobbyMapDiffers)
			{
				Lines.Add(FString::Printf(TEXT("Your copy of %s differs from the host's, so you cannot be ready and the match cannot start with you on a side. Watching lets the others play."), *Map));
			}
			else if (!Map.IsEmpty() && !PlayableMaps.Contains(Map.ToLower()))
			{
				Lines.Add(FString::Printf(TEXT("This game has no level for %s: a round on it would sit you out until it is over."), *Map));
			}
			if (!Missing.IsEmpty() || !Differs.IsEmpty())
			{
				FString Who;
				if (!Missing.IsEmpty())
				{
					Who += FString::Printf(TEXT("%s %s %s."), *FString::Join(Missing, TEXT(", ")), Missing.Num() == 1 ? TEXT("does not have") : TEXT("do not have"), *Map);
				}
				if (!Differs.IsEmpty())
				{
					Who += FString::Printf(TEXT("%s%s %s a different version of %s."), Who.IsEmpty() ? TEXT("") : TEXT(" "), *FString::Join(Differs, TEXT(", ")),
						Differs.Num() == 1 ? TEXT("has") : TEXT("have"), *Map);
				}
				Lines.Add(Who + TEXT(" The match cannot start until they have the host's map or watch."));
			}
			bProblem = !Lines.IsEmpty();
			if (!Checking.IsEmpty())
			{
				Lines.Add(FString::Printf(TEXT("Checking %s with %s..."), *Map, *FString::Join(Checking, TEXT(", "))));
			}
			if (Lines.IsEmpty())
			{
				Lines.Add(FString::Printf(TEXT("Everyone on a side has %s."), *Title));
			}
			return FString::Join(Lines, TEXT(" "));
		}

		/** Why a launch would be refused now (the server's LaunchProblem as the lobby can see it), or empty. */
		FString LaunchProblem() const
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr)
			{
				return FString();
			}
			if (Visit->HasFlag(EScctOnlineStatusFlags::PreparingBots))
			{
				return TEXT("The server is preparing its bots for this map.");
			}
			const FString Map = Visit->GetSetupMap();
			TArray<FString> NotReady;
			int32 OnSides = 0;
			for (const FScctOnlinePlayer& Player : Visit->GetPlayers())
			{
				if (!LobbyOnSide(Player))
				{
					continue;
				}
				++OnSides;
				if (Player.MapState == LobbyMapMissing) { return FString::Printf(TEXT("%s does not have %s."), Player.Name, *Map); }
				if (Player.MapState == LobbyMapDiffers) { return FString::Printf(TEXT("%s has a different version of %s."), Player.Name, *Map); }
				if (Player.MapState == LobbyMapUnknown) { return FString::Printf(TEXT("%s has not finished checking %s."), Player.Name, *Map); }
				if ((Player.Flags & (EScctOnlinePlayerFlags::Ready | EScctOnlinePlayerFlags::Host | EScctOnlinePlayerFlags::Bot)) == 0)
				{
					NotReady.Add(Player.Name);
				}
			}
			if (OnSides == 0)
			{
				return TEXT("Nobody is on a side.");
			}
			if (!NotReady.IsEmpty())
			{
				return FString::Printf(TEXT("Waiting for %s to be ready."), *FString::Join(NotReady, TEXT(", ")));
			}
			return Visit->HasFlag(EScctOnlineStatusFlags::CanStart) ? FString() : FString(TEXT("Not everyone on a side is ready."));
		}

		/** The line above the buttons: why a launch would be refused (for the one who may launch) and an unapplied draft. */
		FString LaunchLine() const
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			FString Line;
			if (Visit != nullptr && Visit->HasFlag(EScctOnlineStatusFlags::CanLaunch) && Visit->GetStatus().Countdown <= 0.0f)
			{
				const FString Problem = LaunchProblem();
				Line = Problem.IsEmpty() ? FString() : TEXT("Launch would be refused: ") + Problem;
			}
			if (Options.IsValid() && Options->IsDirty())
			{
				Line += (Line.IsEmpty() ? TEXT("") : TEXT("  ")) + FString(TEXT("Unapplied changes: open Session options to review them."));
			}
			return Line;
		}

		// ---- moderation -----------------------------------------------------------------------------------------

		/** MANAGE: what to do with the player, then the question that confirms it. */
		void Manage(int32 PlayerId)
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			const FScctOnlinePlayer* Target = Visit != nullptr ? Visit->FindPlayer(PlayerId) : nullptr;
			if (Target == nullptr || !Visit->CanModerate() || !Host.Dialogs.IsValid())
			{
				return;
			}
			const FString Name = Target->Name;
			const bool bVoiceBanned = (Target->Flags & EScctOnlinePlayerFlags::VoiceBanned) != 0;
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby moderation for '%s' (player %d)"), *Name, PlayerId);
			FScctDialog Dialog;
			Dialog.Title = TEXT("Manage player");
			Dialog.Body = FString::Printf(TEXT("%s%s"), *Name, bVoiceBanned ? TEXT(" (voice banned)") : TEXT(""));
			Dialog.Buttons.Add({ TEXT("Close"), EScctButtonLook::Secondary, nullptr });
			Dialog.Buttons.Add({ TEXT("Kick"), EScctButtonLook::Danger, [this, PlayerId, Name](const FString&) { Ask(PlayerId, Name, ELobbyModerate::Kick); } });
			Dialog.Buttons.Add({ TEXT("Ban"), EScctButtonLook::Danger, [this, PlayerId, Name](const FString&) { Ask(PlayerId, Name, ELobbyModerate::Ban); } });
			Dialog.Buttons.Add({ bVoiceBanned ? TEXT("Voice unban") : TEXT("Voice ban"), EScctButtonLook::Secondary,
				[this, PlayerId, Name, bVoiceBanned](const FString&) { Ask(PlayerId, Name, bVoiceBanned ? ELobbyModerate::VoiceUnban : ELobbyModerate::VoiceBan); } });
			Dialog.FocusButton = 0;
			Dialog.CancelButton = 0;
			Host.Dialogs->Show(Dialog);
		}

		/** LobbyRoomScreen.AskKick's questions; Yes sends it (the server checks the right again). */
		void Ask(int32 PlayerId, const FString& Name, ELobbyModerate Action)
		{
			static const TCHAR* const ActionNames[] = { TEXT("kick"), TEXT("ban"), TEXT("voice ban"), TEXT("voice unban") };
			UE_LOG(LogOpenSCCT, Log, TEXT("Menu: lobby asks whether to %s '%s'"), ActionNames[static_cast<int32>(Action)], *Name);
			const FString Question = Action == ELobbyModerate::Ban ? FString::Printf(TEXT("Do you want to ban %s from this server?"), *Name)
				: Action == ELobbyModerate::VoiceBan ? FString::Printf(TEXT("Do you want to ban %s from voice chat?"), *Name)
				: Action == ELobbyModerate::VoiceUnban ? FString::Printf(TEXT("Do you want to let %s use voice chat again?"), *Name)
				: FString::Printf(TEXT("Do you want to kick %s?"), *Name);
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			ScctUi::Confirm(Host.Dialogs, TEXT("Manage player"), Question, TEXT("Yes"), TEXT("No"), [Weak, PlayerId, Action]
			{
				UScctOnlineSubsystem* Visit = Weak.Get();
				if (Visit == nullptr) { return; }
				switch (Action)
				{
				case ELobbyModerate::Kick: Visit->Kick(PlayerId); break;
				case ELobbyModerate::Ban: Visit->Ban(PlayerId); break;
				case ELobbyModerate::VoiceBan: Visit->SetVoiceBanned(PlayerId, true); break;
				case ELobbyModerate::VoiceUnban: Visit->SetVoiceBanned(PlayerId, false); break;
				}
			}, Action != ELobbyModerate::VoiceUnban);
		}

		// ---- unattended -----------------------------------------------------------------------------------------

		void ReadUnattended()
		{
			// Once per run: the page is built again each time the lobby comes back after a round.
			static bool bSwitchesRead = false;
			if (bSwitchesRead)
			{
				return;
			}
			bSwitchesRead = true;
			const TCHAR* CommandLine = FCommandLine::Get();
			bOpenOptionsAtStart = FParse::Param(CommandLine, TEXT("ScctLobbyOptions"));
			bTeamChat = FParse::Param(CommandLine, TEXT("ScctLobbyTeamChat"));
			FString ManageSwitch;
			if (FParse::Value(CommandLine, TEXT("ScctLobbyManage="), ManageSwitch, false) && !ManageSwitch.IsEmpty())
			{
				if (!ManageSwitch.Split(TEXT(","), &PendingManage, &PendingManageAction))
				{
					PendingManage = ManageSwitch;
				}
			}
			FString Show;
			if (FParse::Value(CommandLine, TEXT("ScctLobbyShow="), Show, false))
			{
				TArray<FString> Parts;
				Show.ParseIntoArray(Parts, TEXT(","), true);
				for (const FString& Part : Parts)
				{
					LobbyShowScoreboard() |= Part.Equals(TEXT("scoreboard"), ESearchCase::IgnoreCase);
					LobbyShowBriefing() |= Part.Equals(TEXT("briefing"), ESearchCase::IgnoreCase) || Part.Equals(TEXT("plan"), ESearchCase::IgnoreCase);
					LobbyShowPlan() |= Part.Equals(TEXT("plan"), ESearchCase::IgnoreCase);
					bShowArmoury |= Part.Equals(TEXT("armoury"), ESearchCase::IgnoreCase);
				}
			}
			FString Record;
			if (FParse::Value(CommandLine, TEXT("ScctLobbyRecord="), Record, false) && !Record.IsEmpty())
			{
				PendingRecord = Record.ToBool() ? 1 : 0;
			}
			if (FParse::Value(CommandLine, TEXT("ScctLobbyApply="), PendingApply, false) && !PendingApply.IsEmpty())
			{
				ApplyAfter = 4.0f;
				FParse::Value(CommandLine, TEXT("ScctLobbyApplyAfter="), ApplyAfter);
				FParse::Value(CommandLine, TEXT("ScctLobbyApplyWhen="), ApplyWhen);
			}
		}

		void RunUnattended()
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr)
			{
				return;
			}
			// -ScctLobbyRecord: the switch, once the visit's lobby is reached.
			if (PendingRecord >= 0 && Visit->GetStatus().LocalId >= 0)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyRecord sets Record my matches %s"), PendingRecord != 0 ? TEXT("on") : TEXT("off"));
				SetRecording(PendingRecord != 0);
				PendingRecord = -1;
			}
			// -ScctLobbyShow: each new last-round scoreboard once it is in; the briefing once per run, when the setup is known.
			if (!ScctLobbyPanels::IsOpen(ExtrasLayer) && (!Host.Dialogs.IsValid() || !Host.Dialogs->IsOpen()))
			{
				int32 Sequence = 0;
				if (LobbyShowScoreboard() && ScctLobbyExtras::HasLastScoreboard(&Sequence) && Sequence != LobbyShownScoreboard())
				{
					LobbyShownScoreboard() = Sequence;
					UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyShow opens the match scoreboard"));
					ScctLobbyPanels::OpenScoreboard(ExtrasLayer);
				}
				else if (LobbyShowBriefing() && !Visit->GetSetupMap().IsEmpty() && Visit->GetStatus().LocalId >= 0)
				{
					LobbyShowBriefing() = false;
					UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyShow opens the briefing%s"), LobbyShowPlan() ? TEXT(" on its second page") : TEXT(""));
					if (ScctLobbyPanels::OpenBriefing(ExtrasLayer) && LobbyShowPlan())
					{
						if (IScctDriven* Flip = IScctDriven::Find(TEXT("NEXT")))
						{
							Flip->DrivenPress();
						}
					}
				}
			}
			// -ScctLobbyShow=armoury: the COSMETICS button is pressed five seconds after the lobby is reached, once.
			if (bShowArmoury && Visit->GetStatus().LocalId >= 0)
			{
				const double Now = FPlatformTime::Seconds();
				if (ShowArmouryAt <= 0.0)
				{
					ShowArmouryAt = Now + 5.0;
				}
				else if (Now >= ShowArmouryAt)
				{
					bShowArmoury = false;
					IScctDriven* Button = IScctDriven::Find(TEXT("COSMETICS"));
					UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyShow presses COSMETICS%s"), Button != nullptr ? TEXT("") : TEXT(": the lobby has no such button"));
					if (Button != nullptr)
					{
						Button->DrivenPress();
					}
				}
			}
			// The moderation question for a named player once he is in and this player may moderate.
			if (!PendingManage.IsEmpty() && Visit->CanModerate() && Host.Dialogs.IsValid() && !Host.Dialogs->IsOpen())
			{
				const FScctOnlinePlayer* Target = Visit->GetPlayers().FindByPredicate([this](const FScctOnlinePlayer& Player) { return PendingManage.Equals(Player.Name, ESearchCase::IgnoreCase); });
				if (Target != nullptr)
				{
					const bool bVoiceBanned = (Target->Flags & EScctOnlinePlayerFlags::VoiceBanned) != 0;
					if (PendingManageAction.Equals(TEXT("kick"), ESearchCase::IgnoreCase)) { Ask(Target->Id, Target->Name, ELobbyModerate::Kick); }
					else if (PendingManageAction.Equals(TEXT("ban"), ESearchCase::IgnoreCase)) { Ask(Target->Id, Target->Name, ELobbyModerate::Ban); }
					else if (PendingManageAction.Equals(TEXT("voiceban"), ESearchCase::IgnoreCase)) { Ask(Target->Id, Target->Name, bVoiceBanned ? ELobbyModerate::VoiceUnban : ELobbyModerate::VoiceBan); }
					else { Manage(Target->Id); }
					PendingManage.Reset();
				}
			}
			// The draft is filled once the server's setup is in and this player may change it, APPLY a moment later.
			if (!PendingApply.IsEmpty() && Visit->HasFlag(EScctOnlineStatusFlags::CanControl) && Options->HasServerSetup())
			{
				ShowOptions(true);
				Options->FillDraft(PendingApply);
				PendingApply.Reset();
			}
			if (ApplyAfter >= 0.0f && PendingApply.IsEmpty() && Options->IsDirty())
			{
				// Counted on the clock from when enough players are in (a loaded machine draws few frames).
				const double Now = FPlatformTime::Seconds();
				if (ApplyAt < 0.0 && Visit->GetStatus().PlayerCount >= ApplyWhen)
				{
					ApplyAt = Now + ApplyAfter;
					UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyApply: %d player(s) in the lobby, APPLY in %.0f s"), Visit->GetStatus().PlayerCount, ApplyAfter);
				}
				if (ApplyAt >= 0.0 && Now >= ApplyAt)
				{
					ApplyAfter = -1.0f;
					UE_LOG(LogOpenSCCT, Log, TEXT("Menu: -ScctLobbyApply presses APPLY"));
					Options->Apply();
				}
			}
		}

		void Refresh()
		{
			UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr) { return; }
			SeenRevision = Visit->GetRevision();
			SeenChat = Visit->GetChat().Num();
			const FScctOnlineStatus& Status = Visit->GetStatus();
			RefreshLastGame();
			LogRanks(*Visit);

			// The sides.
			SpyList->ClearChildren();
			MercList->ClearChildren();
			TArray<FString> Watching;
			int32 Counts[2] = { 0, 0 };
			for (const FScctOnlinePlayer& Player : Visit->GetPlayers())
			{
				if (Player.Team != 0 && Player.Team != 1)
				{
					Watching.Add(Player.Name);
					continue;
				}
				++Counts[Player.Team];
				(Player.Team == 0 ? SpyList : MercList)->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ PlayerRow(Player, Player.Id == Status.LocalId) ];
			}
			for (int32 Team = 0; Team < 2; ++Team)
			{
				if (Counts[Team] == 0)
				{
					(Team == 0 ? SpyList : MercList)->AddSlot().AutoHeight().Padding(4.0f, 8.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).Text(FText::FromString(TEXT("Nobody yet")))
					];
				}
			}
			Spectators = Watching.IsEmpty() ? FString() : TEXT("Watching: ") + FString::Join(Watching, TEXT(", "));

			// Who the match waits for: the players on a side who are not ready (bots always are, and the host of a
			// listen server launches instead).
			TArray<FString> NotReady;
			for (const FScctOnlinePlayer& Player : Visit->GetPlayers())
			{
				if ((Player.Team == 0 || Player.Team == 1) && (Player.Flags & (EScctOnlinePlayerFlags::Ready | EScctOnlinePlayerFlags::Bot | EScctOnlinePlayerFlags::Host)) == 0)
				{
					NotReady.Add(Player.Id == Status.LocalId ? FString(TEXT("you")) : Player.Name);
				}
			}
			WaitingFor = !LobbyLocalOnSide(*Visit) ? FString(TEXT("Pick a side to play (Switch Team)."))
				: NotReady.IsEmpty() ? FString(TEXT("Everyone is ready."))
				: TEXT("Not ready yet: ") + FString::Join(NotReady, TEXT(", "));

			// The chat.
			ChatLines->ClearChildren();
			for (const FScctOnlineChatEntry& Entry : Visit->GetChat())
			{
				const FLinearColor SenderColour = Entry.Team == 0 ? FLinearColor(0.35f, 0.85f, 0.45f) : Entry.Team == 1 ? FLinearColor(0.95f, 0.45f, 0.35f) : ScctHudStyle::Text();
				ChatLines->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(Entry.bSystem ? ScctUi::Accent() : SenderColour)
						.Text(FText::FromString(Entry.bSystem ? FString(TEXT("* ")) : FString::Printf(TEXT("%s%s: "), Entry.bTeamOnly ? TEXT("(team) ") : TEXT(""), *Entry.Sender)))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(Entry.bSystem ? ScctHudStyle::TextDim() : ScctHudStyle::Text()).AutoWrapText(true).Text(FText::FromString(Entry.Text))
					]
				];
			}
			ChatScroll->ScrollToEnd();

			MapCard->SetContent(BuildMap(Visit));
			// The setup's controls are rebuilt only when who may change it changes, so the focus stays on them.
			const bool bControl = Visit->HasFlag(EScctOnlineStatusFlags::CanControl);
			if (!SetupBuilt || bControl != bBuiltControl)
			{
				SetupBuilt = true;
				bBuiltControl = bControl;
				SetupCard->SetContent(BuildSetup(Visit));
			}
		}

		TSharedRef<SWidget> PlayerRow(const FScctOnlinePlayer& Player, bool bLocal)
		{
			const bool bReady = (Player.Flags & EScctOnlinePlayerFlags::Ready) != 0;
			const bool bBot = (Player.Flags & EScctOnlinePlayerFlags::Bot) != 0;
			FString Badges;
			if ((Player.Flags & EScctOnlinePlayerFlags::Host) != 0) { Badges += TEXT("HOST  "); }
			if ((Player.Flags & EScctOnlinePlayerFlags::Admin) != 0) { Badges += TEXT("ADMIN  "); }
			if (bBot) { Badges += TEXT("BOT  "); }
			if (Player.MapState == 2) { Badges += TEXT("NO MAP  "); }
			else if (Player.MapState == 3) { Badges += TEXT("OTHER MAP VERSION  "); }
			const FString Rank = ScctLobbyExtras::RankBadge(Player.Rank);
			const FLinearColor RankColour = ScctLobbyExtras::RankColour(Player.Rank);
			// Voice chat's part of the row (Voice/SScctVoiceLobbyControls): speaking mark, volume, mute, the host's voice ban.
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			const TWeakObjectPtr<UScctVoiceSubsystem> VoiceChat = Visit != nullptr && Visit->GetGameInstance() != nullptr ? Visit->GetGameInstance()->GetSubsystem<UScctVoiceSubsystem>() : nullptr;
			const FScctOnlinePlayer* LocalRow = Visit != nullptr ? Visit->GetPlayers().FindByPredicate([Visit](const FScctOnlinePlayer& P) { return P.Id == Visit->GetStatus().LocalId; }) : nullptr;
			const bool bLocalAdmin = LocalRow != nullptr && (LocalRow->Flags & (EScctOnlinePlayerFlags::Host | EScctOnlinePlayerFlags::Admin)) != 0;
			return SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(bLocal ? ScctUi::Accent().CopyWithNewOpacity(0.16f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.04f))
				.Padding(FMargin(12.0f, 8.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(SBox).WidthOverride(8.0f).HeightOverride(8.0f)
						[
							SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(bReady ? FLinearColor(0.35f, 0.9f, 0.55f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.18f))
						]
					]
					// The rank, when the player has one (a guest and a bot have none, and leave no gap).
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, Rank.IsEmpty() ? 0.0f : 10.0f, 0.0f)
					[
						// RankIcons' badge: the emblem (Menu/ScctRankIcons) and the level beside it in the emblem's
						// colour; the rank's words in the tooltip.
						SNew(SBox).Visibility(Rank.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
						.ToolTipText(FText::FromString(ScctLobbyExtras::RankWords(Player.Rank)))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 5.0f, 0.0f)
							[
								ScctRankIcons::Icon(ScctRankIcons::ForPacked(Player.Rank, 22.0f), 22.0f)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								ScctUi::Label(Rank, RankColour, 11.0f)
							]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(Player.Name))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f)
					[
						ScctUi::Label(Badges.TrimEnd(), ScctHudStyle::TextDim(), 10.0f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SScctVoiceLobbyControls).Voice(VoiceChat).PlayerName(FString(Player.Name)).PlayerId(Player.Id).Team(Player.Team)
						.Local(bLocal).Bot(bBot).CanBan(bLocalAdmin && !bLocal).VoiceBanned((Player.Flags & EScctOnlinePlayerFlags::VoiceBanned) != 0)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(bBot ? ScctHudStyle::TextDim() : LobbyPingColour(Player.PingMs))
						.Text(FText::FromString(bBot ? FString(TEXT("--")) : FString::Printf(TEXT("%d ms"), Player.PingMs)))
					]
					// The host or an admin manages every other person (kick, ban, voice ban).
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SBox).Visibility(!bLocal && !bBot && Host.Online.IsValid() && Host.Online->CanModerate() ? EVisibility::Visible : EVisibility::Collapsed)
						[
							SNew(SScctButton)
							.Look(EScctButtonLook::Chip)
							.Text(FText::FromString(TEXT("MANAGE")))
							.Description(FText::FromString(TEXT("Kick, ban or voice-ban this player (you run this server or are its admin).")))
							.OnPressed(FSimpleDelegate::CreateLambda([this, Id = Player.Id] { Manage(Id); }))
						]
					]
				];
		}

		TSharedRef<SWidget> BuildMap(const UScctOnlineSubsystem* Visit)
		{
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			const FScctOnlineStatus& Status = Visit->GetStatus();
			const FString Map = Visit->GetSetupMap();
			const FString Infinite = ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Infini"), TEXT("Unlimited"));
			const auto Lives = [&Infinite](int32 Value) { return Value <= 0 ? Infinite : FString::FromInt(Value); };
			TArray<FString> Rules;
			Rules.Add(Status.TimeLimit > 0 ? FString::Printf(TEXT("%d min"), Status.TimeLimit) : Infinite + TEXT(" time"));
			Rules.Add(FString::Printf(TEXT("Spy lives %s"), *Lives(Status.SpyLives)));
			Rules.Add(FString::Printf(TEXT("Merc lives %s"), *Lives(Status.MercLives)));
			if (Status.RespawnSeconds > 0) { Rules.Add(FString::Printf(TEXT("Respawn %d s"), Status.RespawnSeconds)); }
			if (Status.GamesInARow > 1) { Rules.Add(FString::Printf(TEXT("%d games in a row"), Status.GamesInARow)); }
			if (Status.Bots != 0) { Rules.Add(TEXT("Bots")); }
			const FString ModeName = Menu != nullptr ? Menu->GetModeName(LobbyModeIndex(Status.Mode)) : Visit->GetModeName();
			return ScctUi::Card(TEXT("Next match"),
				SNew(SVerticalBox)
				// On a short screen (the page under 780 units high: 1280x720, 1600x900) the picture gives its room to
				// the setup under the card, which the second line of buttons would otherwise squeeze to its heading.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox)
					.Visibility_Lambda([this]
					{
						const float Height = GetTickSpaceGeometry().GetLocalSize().Y;
						return Height > 0.0f && Height < 780.0f ? EVisibility::Collapsed : EVisibility::Visible;
					})
					[
						ScctUi::Picture(Menu != nullptr ? Menu->GetMapPicture(Map, Status.LocalTeam == 1) : nullptr, 472.0f, 130.0f)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctHudStyle::Text()).AutoWrapText(true).Text(FText::FromString(Menu != nullptr ? Menu->GetMapTitle(Map) : Map))
				]
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Label(ModeName.ToUpper(), ScctUi::Accent(), 12.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).Text(FText::FromString(FString::Join(Rules, TEXT("  ·  "))))
				]
				// Who has the setup's map.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).Font(ScctUi::Small()).AutoWrapText(true)
					.ColorAndOpacity_Lambda([this] { bool bProblem = false; MapLine(bProblem); return FSlateColor(bProblem ? ScctHudStyle::Danger() : ScctHudStyle::Good()); })
					.Text_Lambda([this] { bool bProblem = false; return FText::FromString(MapLine(bProblem)); })
				]
				// The missing map from OpenSCCT-Content: its line, a progress bar and Download or Stop.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[ BuildMapFetch() ], FMargin(24.0f, 20.0f));
		}

		/** The Godot lobby's map fetch row (OnlineLobby.MapFetch): shown while this player lacks the setup's map. */
		TSharedRef<SWidget> BuildMapFetch()
		{
			return SNew(SVerticalBox)
				.Visibility_Lambda([this] { return Fetch.IsShown() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(ScctUi::Small()).AutoWrapText(true)
					.ColorAndOpacity_Lambda([this] { return FSlateColor(Fetch.IsProblem() ? ScctHudStyle::Danger() : ScctHudStyle::Text()); })
					.Text_Lambda([this] { return FText::FromString(Fetch.Line); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SBox).HeightOverride(6.0f).Visibility_Lambda([this] { return Fetch.IsRunning() ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SProgressBar).Percent_Lambda([this] { return TOptional<float>(Fetch.Fraction); }).FillColorAndOpacity(ScctUi::Accent())
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SScctButton)
						.Look(EScctButtonLook::Chip)
						.Visibility_Lambda([this] { return Fetch.CanDownload() ? EVisibility::Visible : EVisibility::Collapsed; })
						.Text_Lambda([this] { return FText::FromString(Fetch.State == static_cast<int32>(EScctContentDownloadState::Failed) ? TEXT("RETRY") : TEXT("DOWNLOAD")); })
						.Description(FText::FromString(TEXT("Download the host's version of this map from OpenSCCT-Content. It is kept for next time.")))
						.OnPressed(FSimpleDelegate::CreateLambda([] { ScctContentDownloads::StartLobbyFetch(); }))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SScctButton)
						.Look(EScctButtonLook::Chip)
						.Visibility_Lambda([this] { return Fetch.IsRunning() ? EVisibility::Visible : EVisibility::Collapsed; })
						.Text(FText::FromString(TEXT("STOP")))
						.Description(FText::FromString(TEXT("Stop the download. A half-downloaded map is never installed.")))
						.OnPressed(FSimpleDelegate::CreateLambda([] { ScctContentDownloads::CancelLobbyFetch(); }))
					]
				];
		}

		/** Session Options (every row, read-only for the others) and, for the controller, a random map. */
		TSharedRef<SWidget> SetupButtons(bool bControl)
		{
			const TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SScctButton)
					.Look(EScctButtonLook::Chip)
					.Text(FText::FromString(bControl ? TEXT("SESSION OPTIONS") : TEXT("MATCH SETTINGS")))
					.Description(FText::FromString(bControl ? TEXT("Every rule of the next match, the alternate rules too: change several, then apply them together.") : TEXT("Every rule of the next match, as the host set them.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { ShowOptions(true); }))
				];
			if (bControl && Options.IsValid() && Options->HasMaps())
			{
				Box->AddSlot().AutoWidth()
				[
					SNew(SScctButton)
					.Look(EScctButtonLook::Chip)
					.Text(FText::FromString(TEXT("RANDOM MAP")))
					.Description(FText::FromString(TEXT("Another map that has this mode, with its own rules.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { Options->RandomMap(); }))
				];
			}
			return Box;
		}

		TSharedRef<SWidget> BuildSetup(UScctOnlineSubsystem* Visit)
		{
			if (!Visit->HasFlag(EScctOnlineStatusFlags::CanControl))
			{
				return ScctUi::Card(TEXT("Setup"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
						.Text(FText::FromString(TEXT("The host chooses the map and the rules. Type /admin and the server's admin password in the chat to take control.")))
					]
					+ SVerticalBox::Slot().AutoHeight()[ SetupButtons(false) ]);
			}
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Visit;
			const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
			Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ SetupButtons(true) ];
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			TArray<FString> MapIds, MapNames;
			if (Menu != nullptr)
			{
				for (const FScctMenuMapInfo& Map : Menu->GetMaps()) { MapIds.Add(Map.Id); MapNames.Add(Map.Title); }
			}
			if (MapIds.Num() > 0)
			{
				Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(SScctStepper)
					.Label(FText::FromString(TEXT("Map")))
					.Description(FText::FromString(TEXT("The map of the next match (the maps installed here).")))
					.Options(MapNames)
					.Get([Weak, MapIds] { return Weak.IsValid() ? FMath::Max(MapIds.IndexOfByPredicate([&](const FString& Id) { return Id.Equals(Weak->GetSetupMap(), ESearchCase::IgnoreCase); }), 0) : 0; })
					.Set([Weak, MapIds](int32 Value) { if (Weak.IsValid() && MapIds.IsValidIndex(Value)) { Weak->SetMatch(MapIds[Value], Weak->GetStatus().Mode); } })
				];
			}
			TArray<FString> LobbyModeChoices;
			for (int32 Index = 0; Index < 4; ++Index) { LobbyModeChoices.Add(Menu != nullptr ? Menu->GetModeName(Index) : UScctMenuSubsystem::ModeKey(Index)); }
			Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("Mode.Caption"), TEXT("Game Mode"))))
				.Description(FText::FromString(TEXT("The mode of the next match. A mode the map does not have is refused by the server.")))
				.Options(LobbyModeChoices)
				.Get([Weak] { return Weak.IsValid() ? LobbyModeIndex(Weak->GetStatus().Mode) : 0; })
				.Set([Weak](int32 Value) { if (Weak.IsValid()) { Weak->SetMatch(Weak->GetSetupMap(), UScctMenuSubsystem::ModeId(Value)); } })
			];
			const auto Option = [&Rows, Weak](const FString& Caption, const TCHAR* Description, const TCHAR* Name, TArray<int32> Values, TFunction<FString(int32)> Show)
			{
				const FString Key = Name;
				// A value the server was given some other way (its config, the console) is offered too, in its place.
				const int32 Current = Weak.IsValid() ? Weak->GetSetupOption(*Key, Values[0]) : Values[0];
				if (!Values.Contains(Current))
				{
					Values.Add(Current);
					Values.Sort();
				}
				TArray<FString> Shown;
				for (const int32 Value : Values) { Shown.Add(Show(Value)); }
				Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(SScctStepper)
					.Label(FText::FromString(Caption))
					.Description(FText::FromString(Description))
					.Wrap(false)
					.Options(Shown)
					.Get([Weak, Key, Values] { const int32 Now = Weak.IsValid() ? Weak->GetSetupOption(*Key, Values[0]) : Values[0]; return FMath::Max(Values.IndexOfByKey(Now), 0); })
					.Set([Weak, Key, Values](int32 Index) { if (Weak.IsValid()) { Weak->SetSetupOptions(FString::Printf(TEXT("?%s=%d"), *Key, Values[Index])); } })
				];
			};
			const FString Infinite = ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Infini"), TEXT("Unlimited"));
			Option(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("TimeLimitCaption"), TEXT("Time Limit")), TEXT("Minutes of the match."), TEXT("TimeLimit"),
				TArray<int32>(LobbyTimeLimits, UE_ARRAY_COUNT(LobbyTimeLimits)), [Infinite](int32 V) { return V == 0 ? Infinite : FString::Printf(TEXT("%d min"), V); });
			Option(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("SpyLivesCaption"), TEXT("Spy Lives")), TEXT("Lives of each spy."), TEXT("LivesATT"),
				{ 0, 1, 2, 3, 4, 5, 6, 8, 10 }, [Infinite](int32 V) { return V == 0 ? Infinite : FString::FromInt(V); });
			Option(ScctUi::GameLabel(TEXT("Create_Match"), TEXT("MercLivesCaption"), TEXT("Merc Lives")), TEXT("Lives of each mercenary."), TEXT("LivesDEF"),
				{ 0, 1, 2, 3, 4, 5, 6, 8, 10 }, [Infinite](int32 V) { return V == 0 ? Infinite : FString::FromInt(V); });
			Option(TEXT("Bots"), TEXT("Server bots: none, a number on each side, or enough to fill the empty places."), TEXT("SvmBots"),
				{ 0, 1, 2 }, [](int32 V) { return V == 0 ? FString(TEXT("Off")) : V == 1 ? FString(TEXT("Per side")) : FString(TEXT("Fill")); });
			Option(TEXT("Bot skill"), TEXT("How well the server's bots play."), TEXT("SvmBotSkill"),
				{ 0, 1, 2, 3 }, [](int32 V) { static const TCHAR* Names[] = { TEXT("Easy"), TEXT("Normal"), TEXT("Hard"), TEXT("Expert") }; return FString(Names[FMath::Clamp(V, 0, 3)]); });
			Option(TEXT("Games in a row"), TEXT("Matches played before the lobby comes back."), TEXT("GamesInARow"),
				{ 1, 2, 3, 4, 5 }, [](int32 V) { return FString::FromInt(V); });
			return ScctUi::Card(TEXT("Setup (you control it)"), Rows, FMargin(12.0f, 14.0f, 12.0f, 10.0f));
		}

		TSharedPtr<SVerticalBox> SpyList;
		TSharedPtr<SVerticalBox> MercList;
		TSharedPtr<SVerticalBox> ChatLines;
		TSharedPtr<SScrollBox> ChatScroll;
		TSharedPtr<SEditableTextBox> ChatBox;
		TSharedPtr<SBox> MapCard;
		TSharedPtr<SBox> SetupCard;
		FString Spectators;
		FString WaitingFor;
		int32 SeenRevision = -1;
		int32 SeenChat = -1;
		bool bTeamChat = false;
		bool SetupBuilt = false;
		bool bBuiltControl = false;

		// Launch feedback: the maps this game can play (an imported level or a runtime map folder), lower case.
		TSet<FString> PlayableMaps;

		// The setup's map from OpenSCCT-Content when this player lacks it, read each frame.
		FScctLobbyMapFetch Fetch;
		int32 SeenGeneration = -1;

		// Session Options, and whether it is shown in place of the lobby.
		TSharedPtr<SWidgetSwitcher> Switcher;
		TSharedPtr<SScctSessionOptions> Options;
		bool bOptionsOpen = false;

		/** Said in the chat line's hint about the last send (a team line without a side). */
		FString ChatNotice;

		// Unattended. What -ScctLobbyShow still opens is kept for the run: the page is built again after each round.
		static bool& LobbyShowScoreboard() { static bool bValue = false; return bValue; }
		static bool& LobbyShowBriefing() { static bool bValue = false; return bValue; }
		static bool& LobbyShowPlan() { static bool bValue = false; return bValue; }
		static int32& LobbyShownScoreboard() { static int32 Value = 0; return Value; }
		int32 PendingRecord = -1;
		bool bOpenOptionsAtStart = false;
		FString PendingManage;
		FString PendingManageAction;
		FString PendingApply;
		float ApplyAfter = -1.0f;
		int32 ApplyWhen = 1;
		double ApplyAt = -1.0;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeLobby(const FScctMenuHost& Host)
{
	return SNew(SScctMenuLobby, Host);
}
