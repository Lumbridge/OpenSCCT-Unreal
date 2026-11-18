// The Match history page (the match history of the Godot client's DemosScreen and its MatchHistoryScreen over
// Social/ScctCareerSubsystem.h): every round played on this PC, saved by the scoreboard capture, joined with the
// signed-in account's history on the master, newest first. Each round says its map, who won (practice, unfinished and
// left-early rounds labelled), when, the mode and length, and the server; its scoreboard folds open, with the timeline
// the master recorded for a round it knows, and a replay saved on this PC can be watched from it.

#include "Menu/ScctCareerPages.h"

#include "Demo/ScctDemoPlayback.h"
#include "Framework/Application/SlateApplication.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Widgets/Layout/SScrollBox.h"

namespace
{
	/** Rounds drawn at first, and added by SHOW MORE (the master's next page is asked for when they run out). */
	constexpr int32 ScctHistoryPageSize = 20;

	/** Timeline events drawn at first, and added by SHOW MORE EVENTS. */
	constexpr int32 ScctHistoryEventStep = 100;

	FString ScctHistoryMapTitle(const FString& Map)
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		const FString Title = Menu != nullptr ? Menu->GetMapTitle(Map) : FString();
		return Title.IsEmpty() ? Map : Title;
	}

	class SScctMatchHistoryPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctMatchHistoryPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			if (UScctCareerSubsystem* Career = ScctCareerUi::Career())
			{
				Career->Command(EScctCareerCommand::RefreshHistory);
			}

			TSharedPtr<SScctButton> First;
			const TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
			const auto Action = [&Actions](const TSharedRef<SWidget>& Button) { Actions->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Button ]; };
			Action(SAssignNew(First, SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("REFRESH")))
				.Description(FText::FromString(TEXT("Reads the saved scoreboards again and asks the master for your account's latest rounds.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { Refresh(); })));
			Action(SNew(SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("PLAYER STATS")))
				.Description(FText::FromString(TEXT("Your career totals by category.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(TEXT("stats")); })));
			Action(SNew(SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("DEMOS")))
				.Description(FText::FromString(TEXT("The demos on this computer and the master's recent online ones.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(TEXT("demos")); })));
			DefaultFocus = First;

			ChildSlot
			[
				SNew(SBox).MaxDesiredWidth(1180.0f).HAlign(HAlign_Left)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
						.Text_Lambda([] { return FText::FromString(ScctCareerUi::Str(EScctCareerString::HistoryStatus)); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::Danger()).AutoWrapText(true)
						.Visibility_Lambda([] { return ScctCareerUi::Str(EScctCareerString::HistoryError).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
						.Text_Lambda([] { return FText::FromString(ScctCareerUi::Str(EScctCareerString::HistoryError)); })
					]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SAssignNew(Scroll, SScrollBox)
						.ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
						+ SScrollBox::Slot()[ SAssignNew(List, SVerticalBox) ]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)[ Actions ]
				]
			];
			Rebuild();
			RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctMatchHistoryPage::Watch));
		}

		virtual FString GetTitle() const override { return TEXT("Match history"); }
		virtual FString GetKicker() const override { return TEXT("Every round you played"); }

		virtual void ShowSub(const FString& Sub) override
		{
			// "history/first": the newest round's scoreboard open (for pictures).
			if (Sub.Equals(TEXT("first"), ESearchCase::IgnoreCase))
			{
				bOpenFirst = true;
				Rebuild();
			}
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if (Key == EKeys::R)
			{
				Refresh();
				return true;
			}
			if (!Scroll.IsValid())
			{
				return false;
			}
			const float Page = FMath::Max(Scroll->GetCachedGeometry().GetLocalSize().Y * 0.8f, 200.0f);
			if (Key == EKeys::PageDown) { Scroll->SetScrollOffset(Scroll->GetScrollOffset() + Page); return true; }
			if (Key == EKeys::PageUp) { Scroll->SetScrollOffset(FMath::Max(0.0f, Scroll->GetScrollOffset() - Page)); return true; }
			return false;
		}

		virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const override
		{
			OutKeyAndWhat.Add({ TEXT("R"), TEXT("Refresh") });
			OutKeyAndWhat.Add({ TEXT("PG UP / DN"), TEXT("Scroll") });
			OutPadKeys.Add(TEXT("Y"));
			OutPadKeys.Add(TEXT("RS"));
		}

	private:
		void Refresh()
		{
			if (UScctCareerSubsystem* Career = ScctCareerUi::Career())
			{
				Career->Command(EScctCareerCommand::RefreshHistory);
			}
		}

		EActiveTimerReturnType Watch(double, float)
		{
			if (ScctCareerUi::Revision() != ShownRevision)
			{
				Rebuild();
			}
			return EActiveTimerReturnType::Continue;
		}

		/** A round's key across rebuilds: its master id, else its start and map. */
		static FString KeyOf(int32 Index, const FScctCareerMatch& Match)
		{
			return Match.MatchId != 0 ? FString::Printf(TEXT("m%lld"), Match.MatchId)
				: FString::Printf(TEXT("%lld:%s"), Match.StartedUnix, *ScctCareerUi::Str(EScctCareerString::MatchMap, Index));
		}

		void Rebuild()
		{
			UScctCareerSubsystem* Career = ScctCareerUi::Career();
			ShownRevision = ScctCareerUi::Revision();
			List->ClearChildren();
			Toggles.Reset();
			const int32 Count = Career != nullptr ? Career->GetInfo().Matches : 0;
			if (Count == 0)
			{
				List->AddSlot().AutoHeight()
				[
					ScctUi::Card(TEXT("Your scoreboards will appear here"), ScctCareerUi::Line(
						TEXT("Play a round online or offline: its final scoreboard is saved on this PC, and a round you leave early is kept marked as such. ")
						TEXT("Signed in, your account's rounds on the master are listed too."), ScctUi::Body(), ScctHudStyle::Text()))
				];
				return;
			}

			for (int32 Index = 0; Index < FMath::Min(Count, Shown); ++Index)
			{
				FScctCareerMatch Match;
				if (!Career->GetMatch(Index, Match))
				{
					continue;
				}
				const FString Key = KeyOf(Index, Match);
				if (bOpenFirst && Index == 0)
				{
					bOpenFirst = false;
					Open.Add(Key);
				}
				if (Open.Contains(Key) && (Match.Flags & EScctCareerMatchFlags::Online) != 0 && (Match.Flags & EScctCareerMatchFlags::DetailsLoaded) == 0
					&& !ScctCareerUi::Flag(EScctCareerFlags::DetailsLoading) && !AskedDetails.Contains(Key))
				{
					AskedDetails.Add(Key);
					Career->Command(EScctCareerCommand::LoadDetails, Index);
				}
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ MatchCard(Index, Match, Key) ];
			}

			if (Count > Shown || ScctCareerUi::Flag(EScctCareerFlags::HistoryMore))
			{
				List->AddSlot().AutoHeight().HAlign(HAlign_Left)
				[
					SNew(SScctButton).Look(EScctButtonLook::Secondary)
					.Text(FText::FromString(ScctCareerUi::Flag(EScctCareerFlags::HistoryLoading) ? TEXT("LOADING…") : TEXT("SHOW MORE")))
					.Description(FText::FromString(TEXT("Older rounds (the master's next page when these run out).")))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Count]
					{
						if (Shown >= Count)
						{
							if (UScctCareerSubsystem* C = ScctCareerUi::Career()) { C->Command(EScctCareerCommand::MoreHistory); }
						}
						Shown += ScctHistoryPageSize;
						Rebuild();
					}))
				];
			}
		}

		TSharedRef<SWidget> MatchCard(int32 Index, const FScctCareerMatch& Match, const FString& Key)
		{
			const bool bOpen = Open.Contains(Key);
			const bool bLeft = (Match.Flags & EScctCareerMatchFlags::LeftEarly) != 0 || (Match.Flags & EScctCareerMatchFlags::Partial) != 0;
			const FString Outcome = ScctCareerUi::Str(EScctCareerString::MatchOutcome, Index);
			const FLinearColor OutcomeColour = bLeft ? ScctHudStyle::TextDim() : Match.Winner >= 0 ? ScctHudStyle::Accent(Match.Winner) : ScctHudStyle::Text();
			FString Tags;
			if ((Match.Flags & EScctCareerMatchFlags::Saved) != 0) { Tags += TEXT("SAVED ON THIS PC"); }
			if ((Match.Flags & EScctCareerMatchFlags::Online) != 0) { Tags += Tags.IsEmpty() ? TEXT("ON YOUR ACCOUNT") : TEXT("  ·  ON YOUR ACCOUNT"); }
			if (Match.LocalTeam == 0 || Match.LocalTeam == 1) { Tags += Match.LocalTeam == 0 ? TEXT("  ·  YOU PLAYED SPY") : TEXT("  ·  YOU PLAYED MERCENARY"); }

			const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			Box->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ ScctCareerUi::Line(ScctHistoryMapTitle(ScctCareerUi::Str(EScctCareerString::MatchMap, Index)).ToUpper(), ScctUi::H2(), ScctHudStyle::Text(), false) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ScctCareerUi::Line(Tags, ScctUi::Caps(10.5f), ScctHudStyle::TextFaint(), false) ]
			];
			Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[ ScctCareerUi::Line(Outcome, ScctUi::Body(true), OutcomeColour) ];
			Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				ScctCareerUi::Line(ScctCareerUi::Str(EScctCareerString::MatchMetadata, Index) + TEXT("  ·  ") + ScctCareerUi::Str(EScctCareerString::MatchServer, Index), ScctUi::Small(), ScctHudStyle::TextDim())
			];
			Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)[ ScctCareerUi::Line(ScctCareerUi::Str(EScctCareerString::MatchReplay, Index), ScctUi::Small(), ScctHudStyle::TextFaint()) ];

			const TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
			TSharedPtr<SScctButton> Toggle;
			Buttons->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SAssignNew(Toggle, SScctButton).Look(EScctButtonLook::Chip).Selected(bOpen)
				.Text(FText::FromString(bOpen ? TEXT("HIDE SCOREBOARD") : TEXT("VIEW SCOREBOARD")))
				.Description(FText::FromString(TEXT("The round's players with their score, kills and deaths, and its timeline when the master recorded one.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this, Key]
				{
					if (Open.Contains(Key)) { Open.Remove(Key); } else { Open.Add(Key); }
					Rebuild();
					// The rebuilt card's button keeps the focus.
					if (const TSharedPtr<SScctButton>* Again = Toggles.Find(Key); Again != nullptr && Again->IsValid())
					{
						FSlateApplication::Get().SetKeyboardFocus(*Again, EFocusCause::SetDirectly);
					}
				}))
			];
			Toggles.Add(Key, Toggle);
			if ((Match.Flags & EScctCareerMatchFlags::ReplayPlayable) != 0)
			{
				const FString Path = ScctCareerUi::Str(EScctCareerString::MatchReplayPath, Index);
				Buttons->AddSlot().AutoWidth()
				[
					SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(TEXT("WATCH REPLAY")))
					.Description(FText::FromString(TEXT("Plays the demo of this round saved on this computer.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Path] { WatchReplay(Path); }))
				];
			}
			Box->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[ Buttons ];
			if (bOpen)
			{
				Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ Details(Index, Match, Key) ];
			}
			return ScctUi::Card(FString(), Box, FMargin(20.0f, 14.0f));
		}

		TSharedRef<SWidget> Details(int32 Index, const FScctCareerMatch& Match, const FString& Key)
		{
			UScctCareerSubsystem* Career = ScctCareerUi::Career();
			const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ ScctCareerUi::Line(ScctCareerUi::Str(EScctCareerString::MatchReason, Index), ScctUi::Small(), ScctHudStyle::TextDim()) ];

			// The players, spies first, as a table.
			const auto Row = [&Box](const FString& Name, const FString& Score, const FString& Kills, const FString& Deaths, const FLinearColor& Colour, bool bHead)
			{
				const auto Cell = [&](const FString& Text) { return ScctCareerUi::Line(Text, bHead ? ScctUi::Caps(11.0f) : ScctUi::Body(), Colour, false); };
				Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[ Cell(Name) ]
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(90.0f).HAlign(HAlign_Right)[ Cell(Score) ] ]
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(90.0f).HAlign(HAlign_Right)[ Cell(Kills) ] ]
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(90.0f).HAlign(HAlign_Right)[ Cell(Deaths) ] ]
				];
			};
			Row(TEXT("PLAYER"), TEXT("SCORE"), TEXT("KILLS"), TEXT("DEATHS"), ScctHudStyle::TextFaint(), true);
			for (int32 Player = 0; Player < Match.Players; ++Player)
			{
				FScctCareerPlayer P;
				if (!Career->GetPlayer(Index, Player, P))
				{
					continue;
				}
				FString Name = FString::Printf(TEXT("%s · %s"), P.Team == 1 ? TEXT("Merc") : TEXT("Spy"), *Career->GetString(EScctCareerString::PlayerName, Index, Player));
				if ((P.Flags & EScctCareerPlayerFlags::Bot) != 0) { Name += TEXT(" [Bot]"); }
				if ((P.Flags & EScctCareerPlayerFlags::LeftEarly) != 0) { Name += TEXT(" (left early)"); }
				if ((P.Flags & EScctCareerPlayerFlags::Local) != 0) { Name += TEXT("  — you"); }
				Row(Name, FString::FromInt(P.Score), FString::FromInt(P.Kills), FString::FromInt(P.Deaths),
					(P.Flags & EScctCareerPlayerFlags::Local) != 0 ? ScctHudStyle::Text() : ScctHudStyle::Accent(P.Team == 1 ? 1 : 0), false);
			}
			if (Match.Players == 0)
			{
				Box->AddSlot().AutoHeight()[ ScctCareerUi::Line(TEXT("No player scores were recorded."), ScctUi::Small(), ScctHudStyle::TextDim()) ];
			}

			// The master's timeline of a round it knows.
			if ((Match.Flags & EScctCareerMatchFlags::Online) == 0)
			{
				return Box;
			}
			Box->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 6.0f)[ ScctUi::Label(TEXT("MATCH TIMELINE")) ];
			if ((Match.Flags & EScctCareerMatchFlags::DetailsLoaded) == 0)
			{
				const FString Error = ScctCareerUi::Str(EScctCareerString::DetailsError);
				Box->AddSlot().AutoHeight()[ ScctCareerUi::Line(!Error.IsEmpty() ? Error : TEXT("Asking the master for this round's details…"), ScctUi::Small(), Error.IsEmpty() ? ScctHudStyle::TextDim() : ScctHudStyle::Danger()) ];
				return Box;
			}
			if ((Match.Flags & EScctCareerMatchFlags::HasTimeline) == 0)
			{
				Box->AddSlot().AutoHeight()[ ScctCareerUi::Line(TEXT("Timeline unavailable for this match."), ScctUi::Small(), ScctHudStyle::TextDim()) ];
				return Box;
			}
			if ((Match.Flags & EScctCareerMatchFlags::TimelineTruncated) != 0)
			{
				Box->AddSlot().AutoHeight()[ ScctCareerUi::Line(TEXT("Some events were omitted because this match reached the recording limit."), ScctUi::Small(), ScctHudStyle::TextDim()) ];
			}
			if (Match.Events == 0)
			{
				Box->AddSlot().AutoHeight()[ ScctCareerUi::Line(TEXT("No events recorded."), ScctUi::Small(), ScctHudStyle::TextDim()) ];
				return Box;
			}
			const int32 Limit = EventsShown.FindRef(Key) > 0 ? EventsShown.FindRef(Key) : ScctHistoryEventStep;
			for (int32 Event = 0; Event < FMath::Min(Match.Events, Limit); ++Event)
			{
				Box->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(64.0f)[ ScctCareerUi::Line(Career->GetString(EScctCareerString::EventTime, Index, Event), ScctUi::Small(), ScctHudStyle::TextDim(), false) ] ]
					+ SHorizontalBox::Slot().FillWidth(1.0f)[ ScctCareerUi::Line(Career->GetString(EScctCareerString::EventText, Index, Event), ScctUi::Small(), ScctHudStyle::Text()) ]
				];
			}
			if (Match.Events > Limit)
			{
				Box->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(TEXT("SHOW MORE EVENTS")))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Key, Limit] { EventsShown.Add(Key, Limit + ScctHistoryEventStep); Rebuild(); }))
				];
			}
			return Box;
		}

		void WatchReplay(const FString& Path)
		{
			const UScctOnlineSubsystem* Visit = Host.Online.Get();
			UGameInstance* Instance = Visit != nullptr ? Visit->GetGameInstance() : nullptr;
			UScctDemoPlayback* Demo = Instance != nullptr ? Instance->GetSubsystem<UScctDemoPlayback>() : nullptr;
			if (Demo == nullptr || Path.IsEmpty())
			{
				return;
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("Match history: watching %s"), *Path);
			if (!Demo->Watch(Path))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Match history: the replay could not be played: %s"), *Demo->GetError());
			}
		}

		TSharedPtr<SScrollBox> Scroll;
		TSharedPtr<SVerticalBox> List;
		TSet<FString> Open;
		TMap<FString, TSharedPtr<SScctButton>> Toggles;
		TSet<FString> AskedDetails;
		TMap<FString, int32> EventsShown;
		int32 Shown = ScctHistoryPageSize;
		int32 ShownRevision = -2;
		bool bOpenFirst = false;
	};
}

TSharedRef<SScctMenuPage> ScctCareerPages::MakeMatchHistory(const FScctMenuHost& Host)
{
	return SNew(SScctMatchHistoryPage, Host);
}
