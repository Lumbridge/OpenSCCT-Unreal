#include "Menu/SScctResultsPanel.h"

#include "Engine/World.h"
#include "Hud/ScctHudSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/Paths.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	/** Offline the HUD's end banner plays first (the Godot results screen's 3.5 s), then the results come up. */
	constexpr float OfflineBannerSeconds = 4.0f;

	/** EGameMode values the panel words differently. */
	constexpr int32 ModeDeathmatch = 6;
	constexpr int32 ModeMercDeathmatch = 7;

	/** MatchResult.ReasonText, with the limit of a round that has no objectives named as such. */
	FString ReasonText(const SScctResultsPanel::FData& Data)
	{
		FString Text;
		switch (Data.Reason)
		{
		case 0:
			Text = Data.Mode == ModeMercDeathmatch ? TEXT("Kill limit reached") : Data.ObjectivesNeeded > 0 ? TEXT("Objectives completed") : TEXT("Score limit reached");
			break;
		case 1: Text = TEXT("Time is up"); break;
		case 2: Text = TEXT("No lives left"); break;
		default: Text = TEXT("Match stopped"); break;
		}
		if (Data.ObjectivesNeeded > 0)
		{
			Text += FString::Printf(TEXT("  -  objectives %d / %d"), Data.ObjectivesTaken, Data.ObjectivesNeeded);
		}
		return Text;
	}

	/** Reloaded's Merc Deathmatch line (MatchResult.DeathmatchLine): the player with the most kills, a draw, or none. */
	FString DeathmatchLine(const SScctResultsPanel::FData& Data)
	{
		int32 Best = 0;
		int32 Leaders = 0;
		FString Leader;
		for (const SScctResultsPanel::FRow& Row : Data.Rows)
		{
			if (Row.Kills > Best)
			{
				Best = Row.Kills;
				Leaders = 1;
				Leader = Row.Name;
			}
			else if (Row.Kills == Best && Best > 0)
			{
				++Leaders;
			}
		}
		return Best == 0 ? FString(TEXT("NO KILLS SCORED"))
			: Leaders > 1 ? FString::Printf(TEXT("DRAW AT %d KILLS"), Best)
			: FString::Printf(TEXT("%s WINS WITH %d KILLS"), *Leader.ToUpper(), Best);
	}
}

void SScctResultsPanel::Construct(const FArguments& InArgs)
{
	Online = InArgs._Online;
	World = InArgs._World;
	bOnlineLevel = InArgs._bOnlineLevel;
	OnLeave = InArgs._OnLeave;
	OnWantsInput = InArgs._OnWantsInput;
	OnGone = InArgs._OnGone;

	FParse::Value(FCommandLine::Get(), TEXT("ScctResultsAction="), UnattendedAction);
	FParse::Value(FCommandLine::Get(), TEXT("ScctResultsActionAfter="), UnattendedAfter);
	if (!bOnlineLevel)
	{
		FParse::Value(FCommandLine::Get(), TEXT("ScctEndMatch="), EndMatchAt);
	}

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(780.0f)
		.Visibility(TAttribute<EVisibility>::CreateLambda([this]
		{
			return !bShowing ? EVisibility::Collapsed : bTakesInput ? EVisibility::Visible : EVisibility::HitTestInvisible;
		}))
		[
			// Darker than the menus' glass: the HUD's own end banner is still drawn behind it.
			Ui::Glass(SAssignNew(Column, SVerticalBox), FMargin(28.0f), 0.86f)
		]
	];
}

void SScctResultsPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	LevelTime += InDeltaTime;

	const UScctOnlineSubsystem* Visit = Online.Get();
	const int32 Revision = bOnlineLevel && Visit != nullptr ? Visit->GetRevision() : 0;
	if (bShowing && bOnlineLevel && Visit != nullptr && Revision == SeenRevision && Visit->GetState() == EScctOnlineState::Results)
	{
		// Nothing new from the server: the lines stay as read (the countdown and the waiting line read live).
		ShownFor += InDeltaTime;
		RunUnattended(InDeltaTime);
		return;
	}
	FData Fresh;
	const bool bHave = bOnlineLevel ? ReadOnline(Fresh) : ReadOffline(Fresh);
	if (bHave && (!bShowing || (bOnlineLevel && Revision != SeenRevision)))
	{
		// Online the lines come again with each revision (a player readied, a demo line): only a changed table is
		// built again, so the buttons keep the focus and the leave question stays.
		const bool bFirst = !bShowing;
		SeenRevision = Revision;
		const bool bChanged = bFirst || Fresh.Rows.Num() != Data.Rows.Num() || Fresh.Winner != Data.Winner || Fresh.GamesLeft != Data.GamesLeft;
		Data = MoveTemp(Fresh);
		if (bChanged)
		{
			Fill();
		}
		if (bFirst)
		{
			bShowing = true;
			ShownFor = 0.0f;
			bUnattendedDone = false;
			bAskingLeave = false;
			UE_LOG(LogOpenSCCT, Log, TEXT("Results: showing (%s): winner %d, the local player played %d (%s), reason %d, objectives %d/%d, games left %d, %d line(s)"),
				bOnlineLevel ? TEXT("online") : TEXT("offline"), Data.Winner, Data.LocalTeam,
				Data.Winner > 1 || Data.bSpectator ? TEXT("no verdict") : Data.Winner == Data.LocalTeam ? TEXT("won") : TEXT("lost"),
				Data.Reason, Data.ObjectivesTaken, Data.ObjectivesNeeded, Data.GamesLeft, Data.Rows.Num());
			for (const FRow& Row : Data.Rows)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Results:   %s side %d score %d kills %d deaths %d objectives %d%s%s"), *Row.Name, Row.Team, Row.Score, Row.Kills,
					Row.Deaths, Row.Objectives, Row.bLocal ? TEXT(" (local)") : TEXT(""), Row.bLeftEarly ? TEXT(" (left early)") : TEXT(""));
			}
			OnWantsInput.ExecuteIfBound();
		}
	}
	else if (!bHave && bShowing)
	{
		bShowing = false;
		bAskingLeave = false;
		UE_LOG(LogOpenSCCT, Log, TEXT("Results: gone (the visit moved on)"));
		OnGone.ExecuteIfBound();
	}

	if (bShowing)
	{
		ShownFor += InDeltaTime;
	}
	RunUnattended(InDeltaTime);
}

bool SScctResultsPanel::ReadOnline(FData& Out) const
{
	const UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr || Visit->GetState() != EScctOnlineState::Results)
	{
		return false;
	}
	FScctOnlineResults Results;
	TArray<FScctOnlineResultRow> Rows;
	if (!Visit->GetResults(Results, Rows) || Rows.Num() == 0)
	{
		return false;
	}
	const TArray<FScctRoundResultExtra> Extras = Visit->GetResultExtras();
	Out.Winner = Results.WinningTeam;
	Out.Reason = Results.WinningReason;
	Out.ObjectivesTaken = Results.ObjectivesTaken;
	Out.ObjectivesNeeded = Results.ObjectivesNeeded;
	Out.GamesLeft = Results.GamesLeft;
	Out.Mode = Visit->GetStatus().Mode;
	Out.LocalTeam = Visit->GetResultsLocalTeam();
	Out.bSpectator = Out.LocalTeam != 0 && Out.LocalTeam != 1;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FScctOnlineResultRow& Source = Rows[Index];
		FRow& Row = Out.Rows.AddDefaulted_GetRef();
		Row.Name = Source.Name;
		Row.Team = Source.Team;
		Row.Score = Source.Score;
		Row.Kills = Source.Kills;
		Row.Deaths = Source.Deaths;
		Row.bLocal = Source.Id == Visit->GetStatus().LocalId;
		if (Extras.IsValidIndex(Index) && Extras[Index].Id == Source.Id)
		{
			Row.Objectives = Extras[Index].Objectives;
			Row.bLeftEarly = Extras[Index].LeftEarly != 0;
		}
	}
	return true;
}

bool SScctResultsPanel::ReadOffline(FData& Out) const
{
	const UWorld* Level = World.Get();
	const UScctHudSubsystem* Hud = Level != nullptr ? Level->GetSubsystem<UScctHudSubsystem>() : nullptr;
	if (Hud == nullptr)
	{
		return false;
	}
	const FScctHudModel& Model = Hud->GetModel();
	const FScctHudRound& Round = Model.Round;
	if (!Model.bHasRound || Round.Ended == 0 || Model.EndAge < OfflineBannerSeconds)
	{
		return false;
	}

	// The HUD snapshot has no end reason: it is read from where the round stood when it ended, as the game decides it
	// (SGameInfo.sCheckEndGame: the clock first, then a side out of lives, then the objectives).
	Out.Winner = Round.WinningTeam >= 0 && Round.WinningTeam <= 1 ? Round.WinningTeam : 255;
	Out.Mode = Round.GameMode;
	Out.ObjectivesNeeded = Round.GameMode == ModeDeathmatch || Round.GameMode == ModeMercDeathmatch ? 0 : Round.NumObjectives;
	Out.ObjectivesTaken = FMath::Max(0, Round.NumObjectives - Round.ObjectivesLeft);
	Out.Reason = Out.Winner == 255 ? 255
		: Round.TimeLimit > 0 && Round.RemainingSeconds == 0 ? 1
		: Out.ObjectivesNeeded > 0 && Round.ObjectivesLeft == 0 && Out.Winner == 0 ? 0
		: Round.GameMode == ModeMercDeathmatch ? 0
		: 2;
	Out.LocalTeam = Model.LocalTeam;
	for (const FScctHudScoreRow& Source : Model.Scores)
	{
		if ((Source.Flags & EScctHudScoreFlags::Spectator) != 0)
		{
			continue;
		}
		FRow& Row = Out.Rows.AddDefaulted_GetRef();
		Row.Name = Model.Text(Source.NameId);
		Row.Team = Source.Team;
		Row.Score = Source.Score;
		Row.Kills = Source.Kills;
		Row.Deaths = Source.Deaths;
		// The original's score column counts objectives for a spy outside the deathmatches, else kills.
		Row.Objectives = Source.Team == 0 && Out.ObjectivesNeeded > 0 ? Source.ObjOrKills : Out.ObjectivesNeeded > 0 ? 0 : -1;
		Row.bLocal = (Source.Flags & EScctHudScoreFlags::Local) != 0;
	}
	return Out.Rows.Num() > 0;
}

void SScctResultsPanel::Fill()
{
	Column->ClearChildren();
	const bool bDecided = Data.Winner == 0 || Data.Winner == 1;

	// "YOU WIN" / "YOU LOSE": the side the local player played this round (his own results line).
	const FString Verdict = !bDecided || Data.bSpectator ? FString(TEXT("ROUND OVER")) : Data.Winner == Data.LocalTeam ? FString(TEXT("YOU WIN")) : FString(TEXT("YOU LOSE"));
	Column->AddSlot().AutoHeight().HAlign(HAlign_Center)
	[
		SNew(STextBlock)
		.Text(Ui::ToText(Verdict))
		.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 34.0f, 260))
		.ColorAndOpacity(bDecided && !Data.bSpectator ? ScctHudStyle::Accent(Data.LocalTeam) : ScctHudStyle::Text())
	];

	const FString Headline = Data.Mode == ModeMercDeathmatch ? DeathmatchLine(Data)
		: Data.Winner == 0 ? FString(TEXT("SPIES WIN")) : Data.Winner == 1 ? FString(TEXT("MERCENARIES WIN")) : FString(TEXT("NO WINNER"));
	Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(Ui::ToText(Headline))
		.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 15.0f, 200))
		.ColorAndOpacity(Data.Mode == ModeMercDeathmatch ? ScctHudStyle::Accent(1) : ScctHudStyle::Accent(Data.Winner))
	];
	Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 6.0f, 0.0f, 0.0f)
	[
		Ui::Text(Ui::ToText(ReasonText(Data)), 12.0f, FSlateColor(ScctHudStyle::TextDim()))
	];

	if (bOnlineLevel)
	{
		// The series and the server's countdown, then who the next game waits for.
		const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
		const int32 GamesLeft = Data.GamesLeft;
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([Weak, GamesLeft]
			{
				const int32 Left = Weak.IsValid() ? FMath::CeilToInt32(Weak->GetStatus().Countdown) : 0;
				const FString Series = GamesLeft > 0 ? FString::Printf(TEXT("%d game%s left in the series"), GamesLeft, GamesLeft == 1 ? TEXT("") : TEXT("s"))
					: FString(TEXT("The series is over"));
				// Only a dedicated server counts down (OnlineServer.ResultsSeconds); a listen host waits for everyone.
				const TCHAR* Then = GamesLeft > 0 ? TEXT("next game") : TEXT("back to the lobby");
				return Ui::ToText(Left > 0 ? FString::Printf(TEXT("%s  -  %s in %d s"), *Series, Then, Left)
					: FString::Printf(TEXT("%s  -  %s once everyone is ready"), *Series, Then));
			}), 11.5f, FSlateColor(ScctHudStyle::Text()))
		];
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			Ui::Text(TAttribute<FText>::CreateLambda([Weak]
			{
				if (!Weak.IsValid())
				{
					return FText::GetEmpty();
				}
				TArray<FString> Waiting;
				for (const FScctOnlinePlayer& Player : Weak->GetPlayers())
				{
					if ((Player.Flags & (EScctOnlinePlayerFlags::Ready | EScctOnlinePlayerFlags::Spectator | EScctOnlinePlayerFlags::Bot)) == 0 && Player.Team != 255)
					{
						Waiting.Add(Player.Name);
					}
				}
				return Ui::ToText(Waiting.Num() == 0 ? FString(TEXT("Everyone is ready.")) : FString::Printf(TEXT("Waiting for %s."), *FString::Join(Waiting, TEXT(", "))));
			}), 11.5f, FSlateColor(ScctHudStyle::TextDim()))
		];
	}

	Column->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 10.0f)[ Ui::Rule() ];

	// The table: spies, then mercs, each by score.
	TArray<FRow> Rows = Data.Rows;
	Rows.StableSort([](const FRow& A, const FRow& B) { return A.Team != B.Team ? A.Team < B.Team : A.Score > B.Score; });
	const bool bObjectives = Data.ObjectivesNeeded > 0 || Rows.ContainsByPredicate([](const FRow& Row) { return Row.Objectives > 0; });
	const auto Cell = [](const FString& String, const FLinearColor& Colour, float Width) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(Width).HAlign(HAlign_Right)[ Ui::Text(Ui::ToText(String), 12.5f, FSlateColor(Colour)) ];
	};
	const auto Line = [&](const FString& Name, const FString& Side, const FString& Score, const FString& Kills, const FString& Deaths, const FString& Objectives,
		const FLinearColor& Colour)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[ Ui::Text(Ui::ToText(Name), 12.5f, FSlateColor(Colour)) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Cell(Side, Colour, 90.0f) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Cell(Score, Colour, 76.0f) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Cell(Kills, Colour, 70.0f) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Cell(Deaths, Colour, 76.0f) ];
		if (bObjectives)
		{
			Box->AddSlot().AutoWidth()[ Cell(Objectives, Colour, 104.0f) ];
		}
		Column->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ Box ];
	};
	Line(TEXT("PLAYER"), TEXT("SIDE"), TEXT("SCORE"), TEXT("KILLS"), TEXT("DEATHS"), TEXT("OBJECTIVES"), ScctHudStyle::TextFaint());
	for (const FRow& Row : Rows)
	{
		const FLinearColor Colour = Row.bLocal ? ScctHudStyle::Accent(Row.Team) : Row.bLeftEarly ? ScctHudStyle::TextFaint() : ScctHudStyle::Text();
		Line(Row.bLeftEarly ? Row.Name + TEXT("  (left)") : Row.Name, Row.Team == 0 ? TEXT("Spy") : Row.Team == 1 ? TEXT("Merc") : TEXT("-"),
			FString::FromInt(Row.Score), FString::FromInt(Row.Kills), FString::FromInt(Row.Deaths), Row.Objectives >= 0 ? FString::FromInt(Row.Objectives) : TEXT("-"), Colour);
	}

	if (bOnlineLevel)
	{
		// The personal demo (OnlineClient.DemoRecordingStatus): shown while the player records or a demo is still saved.
		const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
		Column->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SVerticalBox)
			.Visibility(TAttribute<EVisibility>::CreateLambda([Weak]
			{
				return Weak.IsValid() && (Weak->GetDemoFlags() & (EScctRoundDemoFlags::Recording | EScctRoundDemoFlags::PendingSave)) != 0 ? EVisibility::Visible : EVisibility::Collapsed;
			}))
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ Ui::Label(TEXT("MATCH DEMO")) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Ui::Text(TAttribute<FText>::CreateLambda([Weak] { return Weak.IsValid() ? Ui::ToText(Weak->GetRoundText(EScctRoundText::DemoStatus)) : FText::GetEmpty(); }),
					11.5f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Ui::Text(TAttribute<FText>::CreateLambda([Weak]
				{
					if (!Weak.IsValid())
					{
						return FText::GetEmpty();
					}
					const int32 Flags = Weak->GetDemoFlags();
					return (Flags & EScctRoundDemoFlags::PendingSave) != 0 ? Ui::ToText(TEXT("Stay connected until saving finishes. You can watch this match later from Demos."))
						: (Flags & EScctRoundDemoFlags::Saved) != 0 ? Ui::ToText(TEXT("Saved as ") + FPaths::GetCleanFilename(Weak->GetRoundText(EScctRoundText::LastDemoPath)))
						: FText::GetEmpty();
				}), 10.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
			]
		];
	}

	Column->AddSlot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)[ BuildButtons() ];
}

TSharedRef<SWidget> SScctResultsPanel::BuildButtons()
{
	const auto Clicked = [this](void (SScctResultsPanel::*Action)())
	{
		return FOnClicked::CreateLambda([this, Action] { (this->*Action)(); return FReply::Handled(); });
	};

	if (!bOnlineLevel)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Ui::Button(Ui::ToText(TEXT("REMATCH")), Clicked(&SScctResultsPanel::Rematch), true, Ui::Accent(), 180.0f) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Ui::Button(Ui::ToText(TEXT("MAIN MENU")), Clicked(&SScctResultsPanel::MainMenu), false, Ui::Accent(), 180.0f) ];
	}

	const TWeakObjectPtr<UScctOnlineSubsystem> Weak = Online;
	const int32 GamesLeft = Data.GamesLeft;

	// NEXT: ready for the next game of the series, or for the lobby once the series is over (ResultsScreen.Rematch).
	TSharedRef<SButton> NextButton = Ui::Button(TAttribute<FText>::CreateLambda([Weak, GamesLeft]
	{
		const bool bReady = Weak.IsValid() && Weak->HasFlag(EScctOnlineStatusFlags::LocalReady);
		const bool bControl = Weak.IsValid() && Weak->HasFlag(EScctOnlineStatusFlags::CanControl);
		return Ui::ToText(bReady ? TEXT("READY") : GamesLeft > 0 ? TEXT("NEXT ROUND") : bControl ? TEXT("RETURN TO LOBBY") : TEXT("CONTINUE TO LOBBY"));
	}), Clicked(&SScctResultsPanel::Next), true, Ui::Accent(), 190.0f);
	NextButton->SetEnabled(TAttribute<bool>::CreateLambda([Weak] { return Weak.IsValid() && !Weak->HasFlag(EScctOnlineStatusFlags::LocalReady); }));

	// BACK TO LOBBY: the player who controls the session sends everyone there at once, without waiting for the others to
	// be ready (the rest of the series is dropped). Once the series is over it differs from NEXT only by not waiting.
	TSharedRef<SButton> LobbyButton = Ui::Button(Ui::ToText(GamesLeft > 0 ? TEXT("END SERIES, TO LOBBY") : TEXT("EVERYONE TO LOBBY NOW")),
		Clicked(&SScctResultsPanel::ToLobby), false, Ui::Accent(), 220.0f);

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bAskingLeave ? EVisibility::Collapsed : EVisibility::Visible; }))
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ NextButton ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility(TAttribute<EVisibility>::CreateLambda([Weak] { return Weak.IsValid() && Weak->HasFlag(EScctOnlineStatusFlags::CanControl) ? EVisibility::Visible : EVisibility::Collapsed; }))
				[
					LobbyButton
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()[ Ui::Button(Ui::ToText(TEXT("LEAVE")), Clicked(&SScctResultsPanel::AskLeave), false, Ui::Accent(), 150.0f) ]
		]
		// LEAVE is asked again, with a warning while the match demo is still being saved (ResultsScreen.ToMainMenu).
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SVerticalBox)
			.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bAskingLeave ? EVisibility::Visible : EVisibility::Collapsed; }))
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([Weak]
				{
					const bool bPending = Weak.IsValid() && (Weak->GetDemoFlags() & EScctRoundDemoFlags::PendingSave) != 0;
					return Ui::ToText(bPending ? TEXT("Your match demo is still being saved. Leaving now cancels the download. Leave the session?") : TEXT("Leave the session?"));
				}), 12.5f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					Ui::Button(Ui::ToText(TEXT("LEAVE")), FOnClicked::CreateLambda([this]
					{
						UE_LOG(LogOpenSCCT, Log, TEXT("Results: leaving the session"));
						OnLeave.ExecuteIfBound();
						return FReply::Handled();
					}), true, ScctHudStyle::Danger(), 150.0f)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					Ui::Button(Ui::ToText(TEXT("STAY")), FOnClicked::CreateLambda([this] { bAskingLeave = false; return FReply::Handled(); }), false, Ui::Accent(), 150.0f)
				]
			]
		];
}

void SScctResultsPanel::Next()
{
	UScctOnlineSubsystem* Visit = Online.Get();
	if (Visit == nullptr)
	{
		return;
	}
	// The server starts the next game (or goes back to the lobby) once every player on a side is ready, or when its
	// results countdown runs out.
	UE_LOG(LogOpenSCCT, Log, TEXT("Results: Next, ready for the %s"), Data.GamesLeft > 0 ? TEXT("next game of the series") : TEXT("lobby"));
	Visit->SetReady(true);
}

void SScctResultsPanel::ToLobby()
{
	if (UScctOnlineSubsystem* Visit = Online.Get())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Results: Back to lobby, everyone"));
		Visit->BackToLobby();
	}
}

void SScctResultsPanel::AskLeave()
{
	bAskingLeave = true;
}

void SScctResultsPanel::Rematch()
{
	UWorld* Level = World.Get();
	if (Level == nullptr)
	{
		return;
	}
	// The same level with the options it was opened with: the same map, mode, side, bots and rules.
	const FString Options = FString::Join(Level->URL.Op, TEXT("?"));
	UE_LOG(LogOpenSCCT, Log, TEXT("Results: rematch, reopening %s?%s"), *Level->URL.Map, *Options);
	UGameplayStatics::OpenLevel(Level, FName(*Level->URL.Map), true, Options);
}

void SScctResultsPanel::MainMenu()
{
	UE_LOG(LogOpenSCCT, Log, TEXT("Results: main menu"));
	OnLeave.ExecuteIfBound();
}

void SScctResultsPanel::RunUnattended(float DeltaSeconds)
{
	// Once a run too, so the match a rematch opens plays on.
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
	if (UnattendedAction.IsEmpty() || bUnattendedDone || bPressedThisRun || !bShowing || ShownFor < UnattendedAfter)
	{
		return;
	}
	bUnattendedDone = true;
	bPressedThisRun = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("-ScctResultsAction: pressing %s"), *UnattendedAction);
	if (UnattendedAction.Equals(TEXT("next"), ESearchCase::IgnoreCase) && bOnlineLevel)
	{
		Next();
	}
	else if (UnattendedAction.Equals(TEXT("lobby"), ESearchCase::IgnoreCase) && bOnlineLevel)
	{
		ToLobby();
	}
	else if (UnattendedAction.Equals(TEXT("leave"), ESearchCase::IgnoreCase) && bOnlineLevel)
	{
		OnLeave.ExecuteIfBound();
	}
	else if (UnattendedAction.Equals(TEXT("rematch"), ESearchCase::IgnoreCase) && !bOnlineLevel)
	{
		Rematch();
	}
	else if (UnattendedAction.Equals(TEXT("menu"), ESearchCase::IgnoreCase) && !bOnlineLevel)
	{
		MainMenu();
	}
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctResultsAction: '%s' is not a button of the %s results"), *UnattendedAction, bOnlineLevel ? TEXT("online") : TEXT("offline"));
	}
}
