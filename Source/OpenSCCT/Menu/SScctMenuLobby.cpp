// The lobby of a server: the two sides, the map and its rules, the chat, the host's setup and the ready and launch
// buttons. Shown by the front end while the visit is in EScctOnlineState::Lobby (Menu/SScctFrontEnd.cpp).

#include "Menu/ScctMenuPage.h"

#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Runtime/ScctOnlineApi.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
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

	FString LobbyRank(int32 Packed)
	{
		if (Packed == 0)
		{
			return FString();
		}
		const int32 Level = Packed & 0xFF;
		const int32 Prestige = (Packed >> 8) & 0xFF;
		return Prestige > 0 ? FString::Printf(TEXT("P%d · %d"), Prestige, Level) : FString::Printf(TEXT("%d"), Level);
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
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Host.Online;
			const FString Ready = ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Control2ClientReady"), TEXT("Ready"));
			const FString NotReady = ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Control2ClientNotReady"), TEXT("Not ready"));

			TSharedPtr<SScctButton> ReadyButton;
			ChildSlot
			[
				SNew(SHorizontalBox)
				// The sides.
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 22.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ ScctUi::Card(ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 0, TEXT("Spy")) + TEXT("s"), SAssignNew(SpyList, SVerticalBox), FMargin(18.0f, 16.0f)) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(10.0f, 0.0f, 0.0f, 0.0f)[ ScctUi::Card(ScctUi::GameTextItem(TEXT("Ending_Menu"), TEXT("TeamName"), 1, TEXT("Mercenary")) + TEXT("ies"), SAssignNew(MercList, SVerticalBox), FMargin(18.0f, 16.0f)) ]
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
									SAssignNew(ChatBox, SEditableTextBox)
									.HintText(FText::FromString(TEXT("Say something (Enter sends)")))
									.ClearKeyboardFocusOnCommit(false)
									.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SScctMenuLobby::SendChat))
								]
								+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SScctButton)
									.Look(EScctButtonLook::Chip)
									.Text_Lambda([this] { return FText::FromString(bTeamChat ? ScctUi::GameText(TEXT("Lobby_Create"), TEXT("ChatBox.txtTeam"), TEXT("Team")).ToUpper() : FString(TEXT("ALL"))); })
									.Description(FText::FromString(TEXT("Who reads what you write: everyone, or your side only.")))
									.OnPressed(FSimpleDelegate::CreateLambda([this] { bTeamChat = !bTeamChat; }))
								]
							], FMargin(18.0f, 16.0f))
					]
				]
				// The map, the setup and the buttons.
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(520.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(MapCard, SBox) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)[ SAssignNew(SetupCard, SBox) ]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							SNew(STextBlock).Font(ScctUi::H2()).ColorAndOpacity(ScctUi::Accent()).Justification(ETextJustify::Center)
							.Visibility_Lambda([Weak] { return Weak.IsValid() && Weak->GetStatus().Countdown > 0.0f ? EVisibility::Visible : EVisibility::Collapsed; })
							.Text_Lambda([Weak] { return FText::FromString(Weak.IsValid() ? FString::Printf(TEXT("Starting in %d"), FMath::CeilToInt32(Weak->GetStatus().Countdown)) : FString()); })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
							[
								SNew(SScctButton)
								.Text(FText::FromString(ScctUi::GameText(TEXT("Lobby_Create"), TEXT("ButtonB"), TEXT("Leave")).ToUpper()))
								.Description(FText::FromString(TEXT("Leave the server and go back to the menus.")))
								.OnPressed(FSimpleDelegate::CreateLambda([this] { if (Host.Back) { Host.Back(); } }))
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
							[
								SNew(SScctButton)
								.Text(FText::FromString(ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Button2"), TEXT("Switch Team")).ToUpper()))
								.Description(FText::FromString(TEXT("Play on the other side.")))
								.OnPressed(FSimpleDelegate::CreateLambda([Weak]
								{
									if (UScctOnlineSubsystem* V = Weak.Get()) { V->SetTeam(V->GetStatus().LocalTeam == 0 ? 1 : 0); }
								}))
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f)
							[
								SAssignNew(ReadyButton, SScctButton)
								.Look(EScctButtonLook::Primary)
								.FillWidth(true)
								.Text_Lambda([Weak, Ready, NotReady]
								{
									const UScctOnlineSubsystem* V = Weak.Get();
									if (V != nullptr && V->HasFlag(EScctOnlineStatusFlags::CanLaunch))
									{
										return FText::FromString(V->GetStatus().Countdown > 0.0f
											? ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Control2ServerCancel"), TEXT("Cancel")).ToUpper()
											: ScctUi::GameText(TEXT("Lobby_Create"), TEXT("Control2Server"), TEXT("Launch")).ToUpper());
									}
									// The button says what pressing it does.
									return FText::FromString((V != nullptr && V->HasFlag(EScctOnlineStatusFlags::LocalReady) ? NotReady : Ready).ToUpper());
								})
								.Description(FText::FromString(TEXT("Say you are ready, or (when you run the server) start the match. A match starts when everyone on a side is ready.")))
								.OnPressed(FSimpleDelegate::CreateLambda([Weak]
								{
									UScctOnlineSubsystem* V = Weak.Get();
									if (V == nullptr) { return; }
									if (V->HasFlag(EScctOnlineStatusFlags::CanLaunch)) { V->Launch(); }
									else { V->SetReady(!V->HasFlag(EScctOnlineStatusFlags::LocalReady)); }
								}))
							]
						]
					]
				]
			];
			DefaultFocus = ReadyButton;
			Refresh();
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

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if (Key == EKeys::T || Key == EKeys::Enter)
			{
				FSlateApplication::Get().SetKeyboardFocus(ChatBox, EFocusCause::SetDirectly);
				return true;
			}
			return false;
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SScctMenuPage::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit != nullptr && (Visit->GetRevision() != SeenRevision || Visit->GetChat().Num() != SeenChat))
			{
				Refresh();
			}
		}

	private:
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
					else { Visit->SendChat(Line, bTeamChat); }
				}
			}
			ChatBox->SetText(FText::GetEmpty());
		}

		void Refresh()
		{
			UScctOnlineSubsystem* Visit = Host.Online.Get();
			if (Visit == nullptr) { return; }
			SeenRevision = Visit->GetRevision();
			SeenChat = Visit->GetChat().Num();
			const FScctOnlineStatus& Status = Visit->GetStatus();

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
			const FString Rank = LobbyRank(Player.Rank);
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
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(SBox).WidthOverride(46.0f)
						[
							ScctUi::Label(Rank, ScctUi::Accent(), 11.0f)
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
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(bBot ? ScctHudStyle::TextDim() : LobbyPingColour(Player.PingMs))
						.Text(FText::FromString(bBot ? FString(TEXT("--")) : FString::Printf(TEXT("%d ms"), Player.PingMs)))
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
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Picture(Menu != nullptr ? Menu->GetMapPicture(Map, Status.LocalTeam == 1) : nullptr, 472.0f, 210.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 2.0f)
				[
					SNew(STextBlock).Font(ScctUi::H1()).ColorAndOpacity(ScctHudStyle::Text()).Text(FText::FromString(Menu != nullptr ? Menu->GetMapTitle(Map) : Map))
				]
				+ SVerticalBox::Slot().AutoHeight()[ ScctUi::Label(ModeName.ToUpper(), ScctUi::Accent(), 12.0f) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true).Text(FText::FromString(FString::Join(Rules, TEXT("  ·  "))))
				], FMargin(24.0f, 20.0f));
		}

		TSharedRef<SWidget> BuildSetup(UScctOnlineSubsystem* Visit)
		{
			if (!Visit->HasFlag(EScctOnlineStatusFlags::CanControl))
			{
				return ScctUi::Card(TEXT("Setup"),
					SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
					.Text(FText::FromString(TEXT("The host chooses the map and the rules. Type /admin and the server's admin password in the chat to take control."))));
			}
			const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Visit;
			const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
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
			TArray<FString> Modes;
			for (int32 Index = 0; Index < 4; ++Index) { Modes.Add(Menu != nullptr ? Menu->GetModeName(Index) : UScctMenuSubsystem::ModeKey(Index)); }
			Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SScctStepper)
				.Label(FText::FromString(ScctUi::GameText(TEXT("Create_Match"), TEXT("Mode.Caption"), TEXT("Game Mode"))))
				.Description(FText::FromString(TEXT("The mode of the next match. A mode the map does not have is refused by the server.")))
				.Options(Modes)
				.Get([Weak] { return Weak.IsValid() ? LobbyModeIndex(Weak->GetStatus().Mode) : 0; })
				.Set([Weak](int32 Value) { if (Weak.IsValid()) { Weak->SetMatch(Weak->GetSetupMap(), UScctMenuSubsystem::ModeId(Value)); } })
			];
			const auto Option = [&Rows, Weak](const FString& Caption, const TCHAR* Description, const TCHAR* Name, TArray<int32> Values, TFunction<FString(int32)> Show)
			{
				TArray<FString> Shown;
				for (const int32 Value : Values) { Shown.Add(Show(Value)); }
				const FString Key = Name;
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
			Option(ScctUi::GameText(TEXT("Create_Match"), TEXT("TimeLimitCaption"), TEXT("Time Limit")), TEXT("Minutes of the match."), TEXT("TimeLimit"),
				TArray<int32>(LobbyTimeLimits, UE_ARRAY_COUNT(LobbyTimeLimits)), [Infinite](int32 V) { return V == 0 ? Infinite : FString::Printf(TEXT("%d min"), V); });
			Option(ScctUi::GameText(TEXT("Create_Match"), TEXT("SpyLivesCaption"), TEXT("Spy Lives")), TEXT("Lives of each spy."), TEXT("LivesATT"),
				{ 0, 1, 2, 3, 4, 5, 6, 8, 10 }, [Infinite](int32 V) { return V == 0 ? Infinite : FString::FromInt(V); });
			Option(ScctUi::GameText(TEXT("Create_Match"), TEXT("MercLivesCaption"), TEXT("Merc Lives")), TEXT("Lives of each mercenary."), TEXT("LivesDEF"),
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
		int32 SeenRevision = -1;
		int32 SeenChat = -1;
		bool bTeamChat = false;
		bool SetupBuilt = false;
		bool bBuiltControl = false;
	};
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeLobby(const FScctMenuHost& Host)
{
	return SNew(SScctMenuLobby, Host);
}
