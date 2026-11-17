#include "Online/ScctMatchChat.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace Ui = ScctMenuStyle;

namespace
{
	/** Lines of the transcript shown above the open line. */
	constexpr int32 OpenLines = 8;

	/** A server line stays this long while the line is closed, the last second fading. */
	constexpr double ServerLineSeconds = 10.0;
	constexpr int32 ClosedLines = 4;

	/** OpenSCCT's ChatK.MaxChars: the server cuts a longer line. */
	constexpr int32 MaxChars = 100;

	/** The round's chat line is drawn above the HUD (10) and below the in-match menu (30). */
	constexpr int32 ChatLayer = 20;

	bool IsServerLine(const FScctOnlineChatEntry& Entry)
	{
		// Relayed during a round, the server's own lines carry the sender "Server" (OnlineClient.OnChat).
		return Entry.bSystem || Entry.Sender == TEXT("Server");
	}
}

/** The widgets of the chat line: the recent lines above the channel and the text field. */
class SScctMatchChat : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctMatchChat) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctOnlineSubsystem>, Online)
		SLATE_EVENT(FSimpleDelegate, OnSend)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Online = InArgs._Online;
		OnSend = InArgs._OnSend;
		OnClose = InArgs._OnClose;
		SeenReceived = Online.IsValid() ? Online->GetChatReceived() : 0;

		Line = Ui::TextBox(FString(), FString(), false, FOnTextCommitted::CreateSP(this, &SScctMatchChat::HandleCommitted));
		Line->SetHintText(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(bTeam ? TEXT("Say something to your team") : TEXT("Say something to everyone")); }));
		Line->SetOnKeyDownHandler(FOnKeyDown::CreateSP(this, &SScctMatchChat::HandleKey));

		ChildSlot
		[
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(40.0f, 0.0f, 0.0f, 300.0f)
			[
				SNew(SBox)
				.WidthOverride(620.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						// The transcript: a glass panel while typing, bare lines (server notices) otherwise.
						SNew(SBox)
						.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return Lines.IsValid() && Lines->NumSlots() > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								SNew(SBox)
								.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bOpen ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
								[
									Ui::Glass(SNew(SBox), FMargin(0.0f), 0.55f)
								]
							]
							+ SOverlay::Slot().Padding(14.0f, 10.0f)[ SAssignNew(Lines, SVerticalBox) ]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.Visibility(TAttribute<EVisibility>::CreateLambda([this] { return bOpen ? EVisibility::Visible : EVisibility::Collapsed; }))
						[
							Ui::Glass(
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
									[
										SNew(SBox).WidthOverride(52.0f)
										[
											SNew(STextBlock)
											.Font(ScctHudStyle::Label(10.0f))
											.Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(bTeam ? TEXT("TEAM") : TEXT("ALL")); }))
											.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(bTeam ? ScctHudStyle::Accent(LocalSide()) : ScctHudStyle::Text()); }))
										]
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f)[ Line.ToSharedRef() ]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(62.0f, 5.0f, 0.0f, 0.0f)
								[
									Ui::Text(TAttribute<FText>::CreateLambda([this]
									{
										return Ui::ToText(Notice.IsEmpty() ? FString(TEXT("TAB  everyone / team      ENTER  send      ESC  close")) : Notice);
									}), 9.5f, TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(Notice.IsEmpty() ? ScctHudStyle::TextFaint() : ScctHudStyle::Danger()); }))
								],
								FMargin(12.0f, 9.0f), 0.62f)
						]
					]
				]
			]
		];
	}

	void SetOpen(bool bInOpen, bool bInTeam)
	{
		bOpen = bInOpen;
		bTeam = bInTeam;
		Notice.Reset();
		Line->SetText(FText::GetEmpty());
		bDirty = true;
	}

	bool IsOpen() const { return bOpen; }
	bool IsTeam() const { return bTeam; }
	TSharedPtr<SEditableTextBox> GetLine() const { return Line; }
	FString GetDraft() const { return Line->GetText().ToString(); }
	void SetDraft(const FString& Text) { Line->SetText(Ui::ToText(Text.Left(MaxChars))); }
	void SetNotice(const FString& Text) { Notice = Text; }

	/** Called before Slate lays out the frame: new lines are taken, expired ones dropped, the list made again if it changed. */
	void Refresh()
	{
		const UScctOnlineSubsystem* Visit = Online.Get();
		if (Visit == nullptr)
		{
			return;
		}
		const double Now = FPlatformTime::Seconds();
		const TArray<FScctOnlineChatEntry>& Chat = Visit->GetChat();
		const int32 New = FMath::Min(Visit->GetChatReceived() - SeenReceived, Chat.Num());
		SeenReceived = Visit->GetChatReceived();
		for (int32 Index = Chat.Num() - FMath::Max(New, 0); Index < Chat.Num(); ++Index)
		{
			if (IsServerLine(Chat[Index]))
			{
				Notices.Add({ Chat[Index].Text, Now + ServerLineSeconds });
				bDirty = true;
			}
		}
		if (New > 0 && bOpen)
		{
			bDirty = true;
		}
		const int32 Before = Notices.Num();
		Notices.RemoveAll([Now](const FNotice& Entry) { return Entry.Until <= Now; });
		bDirty |= Notices.Num() != Before;
		if (!bDirty)
		{
			return;
		}
		bDirty = false;

		Lines->ClearChildren();
		if (bOpen)
		{
			// The transcript: everyone's lines and the server's, team lines marked in their side's colour.
			for (int32 Index = FMath::Max(0, Chat.Num() - OpenLines); Index < Chat.Num(); ++Index)
			{
				const FScctOnlineChatEntry& Entry = Chat[Index];
				const bool bServer = IsServerLine(Entry);
				const FLinearColor Colour = bServer ? ScctHudStyle::TextDim() : Entry.bTeamOnly ? ScctHudStyle::Accent(FMath::Clamp(Entry.Team, 0, 1)) : ScctHudStyle::Text();
				const FString Text = bServer ? Entry.Text : FString::Printf(TEXT("%s%s: %s"), Entry.bTeamOnly ? TEXT("(Team) ") : TEXT(""), *Entry.Sender, *Entry.Text);
				Lines->AddSlot().AutoHeight().Padding(0.0f, 1.0f)[ Ui::Text(Ui::ToText(Text), 11.5f, FSlateColor(Colour), ScctHudStyle::EWeight::Regular, true) ];
			}
			return;
		}
		// Closed: the server's last notices, fading out.
		for (int32 Index = FMath::Max(0, Notices.Num() - ClosedLines); Index < Notices.Num(); ++Index)
		{
			const double Until = Notices[Index].Until;
			Lines->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				Ui::Text(Ui::ToText(Notices[Index].Text), 11.5f, TAttribute<FSlateColor>::CreateLambda([Until]
				{
					const float Alpha = FMath::Clamp(static_cast<float>(Until - FPlatformTime::Seconds()), 0.0f, 1.0f);
					return FSlateColor(ScctHudStyle::Fade(ScctHudStyle::TextDim(), Alpha));
				}), ScctHudStyle::EWeight::Regular, true)
			];
		}
	}

private:
	int32 LocalSide() const
	{
		return Online.IsValid() ? FMath::Clamp(Online->GetStatus().LocalTeam, 0, 1) : 0;
	}

	void HandleCommitted(const FText& Text, ETextCommit::Type Commit)
	{
		if (!bOpen)
		{
			return;
		}
		if (Commit == ETextCommit::OnEnter)
		{
			OnSend.ExecuteIfBound();
		}
		else if (Commit == ETextCommit::OnUserMovedFocus || Commit == ETextCommit::OnCleared)
		{
			// The focus went elsewhere (a click outside, the window lost): the line closes as Escape does.
			OnClose.ExecuteIfBound();
		}
	}

	FReply HandleKey(const FGeometry& Geometry, const FKeyEvent& Key)
	{
		if (Key.GetKey() == EKeys::Escape)
		{
			OnClose.ExecuteIfBound();
			return FReply::Handled();
		}
		if (Key.GetKey() == EKeys::Tab)
		{
			if (!Key.IsRepeat())
			{
				bTeam = !bTeam;
				Notice.Reset();
				UE_LOG(LogOpenSCCT, Log, TEXT("Match chat: channel %s"), bTeam ? TEXT("TEAM") : TEXT("ALL"));
			}
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	struct FNotice
	{
		FString Text;
		double Until = 0.0;
	};

	TWeakObjectPtr<UScctOnlineSubsystem> Online;
	FSimpleDelegate OnSend;
	FSimpleDelegate OnClose;
	TSharedPtr<SEditableTextBox> Line;
	TSharedPtr<SVerticalBox> Lines;
	TArray<FNotice> Notices;
	FString Notice;
	int32 SeenReceived = 0;
	bool bOpen = false;
	bool bTeam = false;
	bool bDirty = true;
};

// ---- the widget ---------------------------------------------------------------------------------------------------

TSharedRef<SWidget> UScctMatchChatWidget::RebuildWidget()
{
	SAssignNew(Chat, SScctMatchChat)
		.Online(UScctOnlineSubsystem::Get(this))
		.OnSend(FSimpleDelegate::CreateWeakLambda(this, [this] { Send(); }))
		.OnClose(FSimpleDelegate::CreateWeakLambda(this, [this] { Close(); }));
	return Chat.ToSharedRef();
}

void UScctMatchChatWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Chat.Reset();
}

bool UScctMatchChatWidget::IsOpen() const
{
	return Chat.IsValid() && Chat->IsOpen();
}

void UScctMatchChatWidget::Open(bool bTeam)
{
	APlayerController* Player = GetOwningPlayer();
	if (!Chat.IsValid() || Player == nullptr)
	{
		return;
	}
	if (Chat->IsOpen())
	{
		return;
	}
	Chat->SetOpen(true, bTeam);
	Chat->Refresh();

	// The keyboard goes to the line: the viewport loses the focus, so the controls see no play input and the pawn
	// lets go of what it held; the mouse stays hidden (nothing here is clicked).
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Chat->GetLine());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(Mode);
	FSlateApplication::Get().SetKeyboardFocus(Chat->GetLine(), EFocusCause::SetDirectly);
	UE_LOG(LogOpenSCCT, Log, TEXT("Match chat: line opened (%s)"), bTeam ? TEXT("TEAM") : TEXT("ALL"));
}

void UScctMatchChatWidget::Close()
{
	if (!Chat.IsValid() || !Chat->IsOpen())
	{
		return;
	}
	Chat->SetOpen(false, Chat->IsTeam());
	Chat->Refresh();

	// Back to the game, as AScctGameMode::GiveInputToGame does: an unattended run does not capture the mouse.
	if (APlayerController* Player = GetOwningPlayer())
	{
		if (FApp::IsUnattended())
		{
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}
		else
		{
			Player->SetInputMode(FInputModeGameOnly());
		}
		Player->SetShowMouseCursor(false);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Match chat: line closed"));
}

void UScctMatchChatWidget::SetDraft(const FString& Text)
{
	if (Chat.IsValid())
	{
		Chat->SetDraft(Text);
	}
}

bool UScctMatchChatWidget::Send()
{
	UScctOnlineSubsystem* Visit = UScctOnlineSubsystem::Get(this);
	if (!Chat.IsValid() || !Chat->IsOpen() || Visit == nullptr)
	{
		return false;
	}
	const FString Text = Chat->GetDraft().TrimStartAndEnd();
	const bool bTeam = Chat->IsTeam();
	const int32 Side = Visit->GetStatus().LocalTeam;
	if (bTeam && Side != 0 && Side != 1)
	{
		// Watching: the server would send it to nobody (LobbyRoomScreen.SendChat's rule).
		Chat->SetNotice(TEXT("Join a team before sending team chat."));
		return false;
	}
	if (!Text.IsEmpty())
	{
		Visit->SendChat(Text, bTeam);
		UE_LOG(LogOpenSCCT, Log, TEXT("Match chat: sent to %s: %s"), bTeam ? TEXT("the team") : TEXT("everyone"), *Text);
	}
	Close();
	return !Text.IsEmpty();
}

void UScctMatchChatWidget::Refresh()
{
	if (Chat.IsValid())
	{
		Chat->Refresh();
	}
}

// ---- the subsystem ------------------------------------------------------------------------------------------------

namespace
{
	FAutoConsoleCommandWithWorldAndArgs OpenChatCommand(
		TEXT("scct.OpenChat"),
		TEXT("Opens the chat line of an online round: scct.OpenChat [team]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UScctMatchChatSubsystem* Chat = World != nullptr ? World->GetSubsystem<UScctMatchChatSubsystem>() : nullptr;
			const bool bTeam = Args.Num() > 0 && Args[0].Equals(TEXT("team"), ESearchCase::IgnoreCase);
			if (Chat == nullptr || !Chat->OpenChat(bTeam))
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("scct.OpenChat: no online round is being played in this level"));
			}
		}));
}

bool UScctMatchChatSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UScctMatchChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* CommandLine = FCommandLine::Get();
	FString Open;
	if (FParse::Value(CommandLine, TEXT("ScctOpenChat="), Open, false) && !Open.IsEmpty())
	{
		FString Seconds = Open, Channel;
		Open.Split(TEXT(","), &Seconds, &Channel);
		OpenAfter = FMath::Max(0.0f, FCString::Atof(*Seconds));
		bOpenTeam = Channel.Equals(TEXT("team"), ESearchCase::IgnoreCase);
	}
	FParse::Value(CommandLine, TEXT("ScctChatDraft="), Draft, false);
	FParse::Value(CommandLine, TEXT("ScctChatSendAfter="), SendAfter);
}

void UScctMatchChatSubsystem::Deinitialize()
{
	if (CommandSource.IsValid())
	{
		CommandSource->OnLocalCommand.Remove(CommandHandle);
	}
	if (IsValid(Widget))
	{
		Widget->RemoveFromParent();
	}
	Widget = nullptr;
	Super::Deinitialize();
}

TStatId UScctMatchChatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctMatchChatSubsystem, STATGROUP_Tickables);
}

bool UScctMatchChatSubsystem::InRound() const
{
	const UScctOnlineSubsystem* Visit = UScctOnlineSubsystem::Get(GetWorld());
	return Visit != nullptr && GetWorld()->URL.HasOption(UScctOnlineSubsystem::OnlineOption) && Visit->GetState() == EScctOnlineState::Match;
}

bool UScctMatchChatSubsystem::OpenChat(bool bTeam)
{
	if (!InRound() || !IsValid(Widget))
	{
		return false;
	}
	Widget->Open(bTeam);
	if (!Draft.IsEmpty())
	{
		Widget->SetDraft(Draft);
	}
	return true;
}

void UScctMatchChatSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	// ChatInput.OnLocalCommand: Talk for everyone, TeamTalk for the side.
	if (bPressed && (Name == TEXT("Talk") || Name == TEXT("TeamTalk")))
	{
		OpenChat(Name == TEXT("TeamTalk"));
	}
}

void UScctMatchChatSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (World == nullptr || !World->URL.HasOption(UScctOnlineSubsystem::OnlineOption))
	{
		// A local match has no server to talk to.
		return;
	}
	AScctPlayerController* Player = Cast<AScctPlayerController>(World->GetFirstPlayerController());
	if (Player == nullptr || !Player->IsLocalController())
	{
		return;
	}
	if (!IsValid(Widget))
	{
		Widget = CreateWidget<UScctMatchChatWidget>(Player, UScctMatchChatWidget::StaticClass());
		if (!IsValid(Widget))
		{
			return;
		}
		Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Widget->AddToViewport(ChatLayer);
	}
	if (CommandSource.Get() != Player)
	{
		CommandSource = Player;
		CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctMatchChatSubsystem::HandleLocalCommand);
	}

	const bool bInRound = InRound();
	if (!bInRound && Widget->IsOpen())
	{
		// The round is over (results, travel, a lost connection): nothing to say it to.
		Widget->Close();
	}
	Widget->Refresh();

	// Unattended: open the line a while into the round, type the draft, press Enter.
	RoundSeconds = bInRound ? RoundSeconds + DeltaTime : 0.0f;
	if (OpenAfter >= 0.0f && bInRound && RoundSeconds >= OpenAfter)
	{
		OpenAfter = -1.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("Match chat: -ScctOpenChat opens the line %.0f s into the round"), RoundSeconds);
		if (OpenChat(bOpenTeam))
		{
			OpenSeconds = 0.0f;
		}
	}
	if (OpenSeconds >= 0.0f && SendAfter >= 0.0f)
	{
		OpenSeconds += DeltaTime;
		if (OpenSeconds >= SendAfter)
		{
			OpenSeconds = -1.0f;
			UE_LOG(LogOpenSCCT, Log, TEXT("Match chat: -ScctChatSendAfter presses Enter"));
			Widget->Send();
		}
	}
}
