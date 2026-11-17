#include "Hud/ScctChatSubsystem.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Hud/ScctHudSubsystem.h"
#include "Hud/SScctChatPanel.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Play/ScctGameMode.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctHudChatApi.h"
#include "Settings/ScctSettings.h"
#include "Widgets/Layout/SSafeZone.h"

#define LOCTEXT_NAMESPACE "ScctChat"

namespace
{
	/** Over the HUD (10), under the match menu (30). */
	constexpr int32 ChatZOrder = 20;

	/** A Talk press this soon after the composer closed is the key that closed it. */
	constexpr double ReopenGuardSeconds = 0.3;

	/** The bridge's team byte of a sender without a team (ChatK.NoTeam). */
	constexpr int32 NoTeam = 255;

	const TCHAR* ChannelName(EScctChatChannel Channel)
	{
		switch (Channel)
		{
		case EScctChatChannel::Team: return TEXT("team");
		case EScctChatChannel::System: return TEXT("system");
		default: return TEXT("all");
		}
	}
}

UScctChatSubsystem::UScctChatSubsystem()
	: Transcript(MakeShared<FScctChatTranscript>())
{
}

bool UScctChatSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UScctChatSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UScctChatSubsystem, STATGROUP_Tickables);
}

void UScctChatSubsystem::RegisterSettings()
{
	FScctSettingDesc Fade;
	Fade.Key = TEXT("Hud.ChatFadeSeconds");
	Fade.Page = TEXT("Hud");
	Fade.Group = LOCTEXT("Group", "Chat");
	Fade.Label = LOCTEXT("FadeLabel", "Chat lines stay for");
	Fade.Tooltip = LOCTEXT("FadeTip", "Seconds a new chat line stays at the lower left before it fades (the history keeps it).");
	Fade.Type = EScctSettingType::Float;
	Fade.Default = TEXT("8");
	Fade.Min = 2.0f;
	Fade.Max = 30.0f;
	Fade.Step = 1.0f;
	Fade.Order = 0;
	FScctSettings::Register(Fade);

	FScctSettingDesc Lines;
	Lines.Key = TEXT("Hud.ChatPassiveLines");
	Lines.Page = TEXT("Hud");
	Lines.Group = LOCTEXT("Group", "Chat");
	Lines.Label = LOCTEXT("LinesLabel", "Chat lines shown");
	Lines.Tooltip = LOCTEXT("LinesTip", "How many recent chat lines show at the lower left while the chat is closed; 0 shows none.");
	Lines.Type = EScctSettingType::Int;
	Lines.Default = TEXT("4");
	Lines.Min = 0.0f;
	Lines.Max = 8.0f;
	Lines.Step = 1.0f;
	Lines.Order = 1;
	FScctSettings::Register(Lines);
}

void UScctChatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSettings();

	if (UScctHudSubsystem* Hud = Collection.InitializeDependency<UScctHudSubsystem>())
	{
		HudMessageHandle = Hud->OnMessage.AddUObject(this, &UScctChatSubsystem::HandleHudMessage);
	}

	// The unattended switches become timed steps.
	const TCHAR* CommandLine = FCommandLine::Get();
	FString OpenOn;
	float At = 8.0f;
	FParse::Value(CommandLine, TEXT("ScctChatAt="), At);
	float NextFree = At;
	if (FParse::Value(CommandLine, TEXT("ScctChatOpen="), OpenOn))
	{
		const bool bTeam = OpenOn.Equals(TEXT("team"), ESearchCase::IgnoreCase);
		FString Say;
		FParse::Value(CommandLine, TEXT("ScctChatSay="), Say, false);
		TArray<FString> Lines;
		Say.ParseIntoArray(Lines, TEXT(";"));
		if (Lines.Num() == 0)
		{
			Steps.Add({ At, FStep::EKind::Open, bTeam });
			NextFree = At + 2.0f;
		}
		float Hold = 2.0f;
		FParse::Value(CommandLine, TEXT("ScctChatHold="), Hold);
		Hold = FMath::Max(Hold, 0.2f);
		float Time = At;
		for (FString Line : Lines)
		{
			bool bLineTeam = bTeam;
			if (Line.RemoveFromStart(TEXT("team:"), ESearchCase::IgnoreCase))
			{
				bLineTeam = true;
			}
			else if (Line.RemoveFromStart(TEXT("all:"), ESearchCase::IgnoreCase))
			{
				bLineTeam = false;
			}
			Steps.Add({ Time, FStep::EKind::Open, bLineTeam });
			Steps.Add({ Time + 0.1f, FStep::EKind::Type, bLineTeam, Line });
			Steps.Add({ Time + 0.1f + Hold, FStep::EKind::Submit, bLineTeam });
			Time += Hold + 1.5f;
			NextFree = Time;
		}
	}
	if (FParse::Param(CommandLine, TEXT("ScctChatHistory")) || FParse::Value(CommandLine, TEXT("ScctChatHistory="), NextFree))
	{
		FParse::Value(CommandLine, TEXT("ScctChatHistory="), NextFree);
		Steps.Add({ NextFree, FStep::EKind::History, false });
		FString Filter;
		if (FParse::Value(CommandLine, TEXT("ScctChatFilter="), Filter))
		{
			HistoryFilter = Filter.Equals(TEXT("team"), ESearchCase::IgnoreCase) ? 1 : (Filter.Equals(TEXT("system"), ESearchCase::IgnoreCase) ? 2 : 0);
		}
		FParse::Value(CommandLine, TEXT("ScctChatScroll="), HistoryScroll);
	}
}

void UScctChatSubsystem::Deinitialize()
{
	if (UScctHudSubsystem* Hud = GetWorld() != nullptr ? GetWorld()->GetSubsystem<UScctHudSubsystem>() : nullptr)
	{
		Hud->OnMessage.Remove(HudMessageHandle);
	}
	if (CommandSource.IsValid())
	{
		CommandSource->OnLocalCommand.Remove(CommandHandle);
	}
	if (Panel.IsValid() && Panel->HoldsInput())
	{
		GiveInputToGame();
	}
	RemovePanel();
	Super::Deinitialize();
}

bool UScctChatSubsystem::IsVisiting() const
{
	const UScctOnlineSubsystem* Online = UScctOnlineSubsystem::Get(GetWorld());
	return Online != nullptr && Online->GetState() != EScctOnlineState::Idle;
}

bool UScctChatSubsystem::EnsurePanel()
{
	if (ViewportContent.IsValid())
	{
		return true;
	}
	// Only a level that plays a match has the in-match chat; the front end has the lobby's own.
	UWorld* World = GetWorld();
	if (World == nullptr || Cast<AScctGameMode>(World->GetAuthGameMode()) == nullptr)
	{
		return false;
	}
	APlayerController* Player = World->GetFirstPlayerController();
	UGameViewportClient* Viewport = World->GetGameViewport();
	if (Player == nullptr || !Player->IsLocalController() || Viewport == nullptr || !FSlateApplication::IsInitialized())
	{
		return false;
	}

	TWeakObjectPtr<UScctChatSubsystem> WeakThis(this);
	SAssignNew(Panel, SScctChatPanel)
		.Transcript(Transcript)
		.LocalTeam_Lambda([WeakThis]()
		{
			const UScctHudSubsystem* Hud = WeakThis.IsValid() ? WeakThis->GetWorld()->GetSubsystem<UScctHudSubsystem>() : nullptr;
			return Hud != nullptr ? Hud->GetModel().LocalTeam : 0;
		})
		.Clock_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->Clock : 0.0; })
		.OnSubmit_UObject(this, &UScctChatSubsystem::HandleSubmit)
		.OnClosed_UObject(this, &UScctChatSubsystem::HandlePanelClosed);
	ViewportContent = SNew(SSafeZone).IsTitleSafe(true)[ Panel.ToSharedRef() ];
	Viewport->AddViewportWidgetContent(ViewportContent.ToSharedRef(), ChatZOrder);

	// A visit's chat is the connection's: what was said before this level opened is in the history (not shown passively).
	if (IsVisiting())
	{
		PullOnlineChat(true);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctChat: the chat is up (%s transcript, %d lines already)"), IsVisiting() ? TEXT("the online visit's") : TEXT("the match's"), Transcript->GetEntries().Num());
	return true;
}

void UScctChatSubsystem::RemovePanel()
{
	if (ViewportContent.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				Viewport->RemoveViewportWidgetContent(ViewportContent.ToSharedRef());
			}
		}
	}
	ViewportContent.Reset();
	Panel.Reset();
}

void UScctChatSubsystem::Tick(float DeltaTime)
{
	Clock += DeltaTime;
	if (!EnsurePanel())
	{
		return;
	}

	// Talk and TeamTalk come from the player's bindings.
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (Player != nullptr && CommandSource.Get() != Player)
	{
		if (CommandSource.IsValid())
		{
			CommandSource->OnLocalCommand.Remove(CommandHandle);
		}
		CommandSource = Player;
		CommandHandle = Player->OnLocalCommand.AddUObject(this, &UScctChatSubsystem::HandleLocalCommand);
	}

	if (IsVisiting())
	{
		PullOnlineChat(false);
	}
	RunUnattended(DeltaTime);
}

void UScctChatSubsystem::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	if (!bPressed || IsOpen() || FPlatformTime::Seconds() - ClosedAt < ReopenGuardSeconds)
	{
		return;
	}
	if (Name == TEXT("Talk"))
	{
		Open(false);
	}
	else if (Name == TEXT("TeamTalk"))
	{
		Open(true);
	}
}

bool UScctChatSubsystem::IsOpen() const
{
	return Panel.IsValid() && Panel->HoldsInput();
}

bool UScctChatSubsystem::Open(bool bTeam)
{
	AScctPlayerController* Player = Cast<AScctPlayerController>(GetWorld()->GetFirstPlayerController());
	if (!EnsurePanel() || Player == nullptr)
	{
		return false;
	}
	if (Panel->IsOpen())
	{
		Panel->SetTeam(bTeam);
		return true;
	}
	if (Player->IsMenuOpen() && InputHolder.Get() != Player)
	{
		// The match menu has the keys; the chat waits for it to close.
		return false;
	}

	// The keys and the mouse leave the simulation (everything held is let go) and the box takes the keyboard.
	InputHolder = Player;
	Player->SetMenuOpen(true);
	Panel->SetChannelKeys(Player->FindKeysBoundTo(TEXT("Talk")), Player->FindKeysBoundTo(TEXT("TeamTalk")));
	Panel->Open(bTeam);
	if (!FApp::IsUnattended())
	{
		Player->SetShowMouseCursor(true);
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Panel->GetFocusTarget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Player->SetInputMode(Mode);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctChat: the composer opened on %s"), bTeam ? TEXT("team") : TEXT("all"));
	return true;
}

void UScctChatSubsystem::Close()
{
	if (Panel.IsValid())
	{
		Panel->Close();
	}
}

void UScctChatSubsystem::HandlePanelClosed()
{
	ClosedAt = FPlatformTime::Seconds();
	GiveInputToGame();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctChat: the composer closed, the keys are the game's again"));
}

void UScctChatSubsystem::GiveInputToGame()
{
	AScctPlayerController* Player = InputHolder.Get();
	InputHolder.Reset();
	if (Player == nullptr)
	{
		return;
	}
	Player->SetMenuOpen(false);
	if (!FApp::IsUnattended())
	{
		// As the game mode gives the match its input: game only, no cursor.
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetShowMouseCursor(false);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UScctChatSubsystem::HandleSubmit(const FString& Text, bool bTeam)
{
	Send(Text, bTeam);
}

bool UScctChatSubsystem::Send(const FString& Text, bool bTeam)
{
	const FString Line = Text.TrimStartAndEnd().Left(SScctChatPanel::MaxChars);
	if (Line.IsEmpty())
	{
		return false;
	}
	// The session's own connection; in a visit without that table, the visit's.
	if (const FScctHudChatApi* Chat = ScctFeature<FScctHudChatApi>())
	{
		if (Chat->Send(*Line, bTeam ? 1 : 0) == 1)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctChat: sent %s \"%s\""), bTeam ? TEXT("team") : TEXT("all"), *Line);
			return true;
		}
	}
	if (UScctOnlineSubsystem* Online = UScctOnlineSubsystem::Get(GetWorld()); Online != nullptr && IsVisiting())
	{
		Online->SendChat(Line, bTeam);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctChat: sent %s \"%s\" (through the visit)"), bTeam ? TEXT("team") : TEXT("all"), *Line);
		return true;
	}
	UE_LOG(LogOpenSCCT, Warning, TEXT("ScctChat: not sent, no session has a connection that joined its server: %s \"%s\""), bTeam ? TEXT("team") : TEXT("all"), *Line);
	return false;
}

void UScctChatSubsystem::Receive(EScctChatChannel Channel, const FString& Sender, int32 SenderTeam, const FString& Text, double Now)
{
	if (Transcript->Add(Channel, Sender, SenderTeam, Text, Now))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctChat: received %s %s\"%s\"%s"), ChannelName(Channel),
			Sender.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("<%s> "), *Sender), *Text, Now >= 0.0 ? TEXT("") : TEXT(" (from before this level)"));
	}
	else
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctChat: dropped the echo of %s \"%s\""), ChannelName(Channel), *Text);
	}
}

void UScctChatSubsystem::HandleHudMessage(const FScctHudMessage& Message)
{
	if (Message.Category != EScctHudMessageCategory::Chat && Message.Category != EScctHudMessageCategory::TeamChat)
	{
		return;
	}
	// A visit's lines come from its own transcript, which has them all (the lobby's too); the round's copy would repeat them.
	if (IsVisiting())
	{
		return;
	}
	const UScctHudSubsystem* Hud = GetWorld()->GetSubsystem<UScctHudSubsystem>();
	const FString Sender = Hud != nullptr ? Hud->GetModel().Text(Message.ActorNameId) : FString();
	const bool bServer = Message.ActorPlayerId == 0 && (Message.ActorTeam == NoTeam || Message.ActorTeam < 0);
	const EScctChatChannel Channel = bServer ? EScctChatChannel::System
		: (Message.Category == EScctHudMessageCategory::TeamChat ? EScctChatChannel::Team : EScctChatChannel::All);
	const FString Text = FString(Message.Text).TrimStartAndEnd();
	if (!Text.IsEmpty())
	{
		Receive(Channel, bServer ? FString() : Sender, Message.ActorTeam, Text, Clock);
	}
}

void UScctChatSubsystem::PullOnlineChat(bool bSeed)
{
	const UScctOnlineSubsystem* Online = UScctOnlineSubsystem::Get(GetWorld());
	if (Online == nullptr)
	{
		return;
	}
	const TArray<FScctOnlineChatEntry>& Lines = Online->GetChat();
	auto IsLast = [this](const FScctOnlineChatEntry& Line)
	{
		return bOnlineHasLast && Line.bSystem == bOnlineLastSystem && Line.bTeamOnly == bOnlineLastTeam && Line.Sender == OnlineLastSender && Line.Text == OnlineLastText;
	};

	// The visit keeps its last lines and drops the oldest when full: new lines are the ones after the count seen while
	// it grows, and after the last line seen once it is full.
	int32 Start = 0;
	if (bOnlineSeeded)
	{
		if (Lines.Num() > OnlineSeenCount)
		{
			Start = OnlineSeenCount;
		}
		else if (Lines.Num() == OnlineSeenCount && Lines.Num() > 0)
		{
			if (IsLast(Lines.Last()))
			{
				return;
			}
			Start = 0;
			for (int32 Index = Lines.Num() - 2; Index >= 0; --Index)
			{
				if (IsLast(Lines[Index]))
				{
					Start = Index + 1;
					break;
				}
			}
		}
		else if (Lines.Num() == 0)
		{
			OnlineSeenCount = 0;
			return;
		}
	}
	bOnlineSeeded = true;

	const double Now = bSeed ? -1.0 : Clock;
	for (int32 Index = Start; Index < Lines.Num(); ++Index)
	{
		const FScctOnlineChatEntry& Line = Lines[Index];
		const EScctChatChannel Channel = Line.bSystem ? EScctChatChannel::System : (Line.bTeamOnly ? EScctChatChannel::Team : EScctChatChannel::All);
		Receive(Channel, Line.bSystem ? FString() : Line.Sender, Line.Team, Line.Text, Now);
	}
	OnlineSeenCount = Lines.Num();
	if (Lines.Num() > 0)
	{
		const FScctOnlineChatEntry& Last = Lines.Last();
		bOnlineHasLast = true;
		bOnlineLastSystem = Last.bSystem;
		bOnlineLastTeam = Last.bTeamOnly;
		OnlineLastSender = Last.Sender;
		OnlineLastText = Last.Text;
	}
}

void UScctChatSubsystem::RunUnattended(float DeltaTime)
{
	if (Steps.Num() == 0)
	{
		return;
	}
	ChatClock += DeltaTime;
	for (FStep& Step : Steps)
	{
		if (Step.bDone || ChatClock < Step.Time)
		{
			continue;
		}
		Step.bDone = true;
		switch (Step.Kind)
		{
		case FStep::EKind::Open:
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctChatOpen: opening the composer on %s at %.1f s"), Step.bTeam ? TEXT("team") : TEXT("all"), ChatClock);
			Open(Step.bTeam);
			break;
		case FStep::EKind::Type:
			if (Panel.IsValid() && Panel->IsOpen())
			{
				Panel->SetDraft(Step.Text);
			}
			break;
		case FStep::EKind::Submit:
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctChatSay: Enter on \"%s\""), Panel.IsValid() ? *Panel->GetDraft() : TEXT(""));
			if (Panel.IsValid())
			{
				Panel->Submit();
			}
			break;
		case FStep::EKind::History:
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctChatHistory: opening the history at %.1f s (%d lines)"), ChatClock, Transcript->GetEntries().Num());
			if (Open(false))
			{
				Panel->SetFilter(static_cast<SScctChatPanel::EFilter>(HistoryFilter));
				if (HistoryScroll > 0)
				{
					Panel->ScrollBy(HistoryScroll);
					UE_LOG(LogOpenSCCT, Log, TEXT("-ScctChatScroll: the history scrolled back %d lines"), HistoryScroll);
				}
			}
			break;
		}
		break;
	}
}

#undef LOCTEXT_NAMESPACE
