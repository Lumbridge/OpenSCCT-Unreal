#include "Hub/ScctHubHud.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Hub/SScctHubHud.h"
#include "Hub/ScctHubPads.h"
#include "Hub/ScctHubSubsystem.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctPlayerController.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** Over the match HUD (10), under the in-match menu (30). */
	constexpr int32 HubHudZOrder = 12;

	/** Chat lines kept for the panel. */
	constexpr int32 ChatKept = 40;

	/** A sign starts fading here and is gone at SignFar (HubSignsView: 60 and 70 metres). */
	constexpr float SignFade = 6000.0f;
	constexpr float SignFar = 7000.0f;

	/** A line of a scale 1 sign stands this tall in the world (HubSignsView: about 0.36 m). */
	constexpr float SignLineHeight = 36.0f;

	/** The share of a board's height the text may use (HubSignsView.FitHeight). */
	constexpr float SignFitHeight = 0.74f;

	const TCHAR* ChannelName(EScctHubChatChannel Channel)
	{
		switch (Channel)
		{
		case EScctHubChatChannel::Side: return TEXT("Side");
		case EScctHubChatChannel::Nearby: return TEXT("Nearby");
		case EScctHubChatChannel::System: return TEXT("System");
		default: return TEXT("Everyone");
		}
	}

	FLinearColor ColourOf(uint32 Rgba)
	{
		return FLinearColor(FColor(static_cast<uint8>(Rgba >> 24), static_cast<uint8>(Rgba >> 16), static_cast<uint8>(Rgba >> 8), static_cast<uint8>(Rgba)));
	}

	/** Reads one of the table's lists: up to Capacity rows, the array sized to what was written. */
	template <typename TRow, typename TGetter>
	int32 ReadRows(TArray<TRow>& Rows, int32 Capacity, TGetter&& Getter)
	{
		Rows.SetNumZeroed(Capacity, EAllowShrinking::No);
		const int32 Count = Getter(Rows.GetData(), Capacity);
		Rows.SetNum(FMath::Clamp(Count, 0, Capacity), EAllowShrinking::No);
		return Count;
	}

	/** The hub HUD the console reaches (the first game instance's). */
	FScctHubHud* FindHud(UWorld* World)
	{
		UScctHubSubsystem* Hub = World != nullptr ? UScctHubSubsystem::Get(World) : nullptr;
		return Hub != nullptr ? Hub->GetHud() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs GHubCommand(
		TEXT("ScctHubCommand"),
		TEXT("ScctHubCommand [seconds] <command>: press the key the bindings put a hub command on (HubMerc, HubSpy, HubPlaza, HubPlayers, HubEmotes, HubTravel...) that long after the player stands in the hub, or now."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			FScctHubHud* Hud = FindHud(World);
			if (Hud == nullptr || Args.Num() == 0)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("ScctHubCommand: %s"), Hud == nullptr ? TEXT("no hub subsystem") : TEXT("give a command (ScctHubCommand 8 HubMerc)"));
				return;
			}
			const bool bTimed = Args.Num() >= 2 && FCString::IsNumeric(*Args[0]);
			Hud->QueueCommand(bTimed ? FCString::Atof(*Args[0]) : 0.0f, bTimed ? Args[1] : Args[0]);
		}));
}

FScctHubHud::FScctHubHud(UScctHubSubsystem* InOwner)
	: Owner(InOwner)
	, Model(MakeShared<FScctHubHudModel>())
{
	// -ScctHubOpen=chat@4,players@14
	FString Open;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctHubOpen="), Open, false))
	{
		TArray<FString> Panels;
		Open.ParseIntoArray(Panels, TEXT(","));
		for (const FString& Panel : Panels)
		{
			FString Name = Panel, Seconds;
			const bool bTimed = Panel.Split(TEXT("@"), &Name, &Seconds);
			FTimedCommand& Command = Timed.AddDefaulted_GetRef();
			Command.At = bTimed ? FCString::Atof(*Seconds) : 2.0f;
			Command.Command = TEXT("Open:") + Name.TrimStartAndEnd().ToLower();
		}
	}

	FString Chat;
	if (FParse::Value(FCommandLine::Get(), TEXT("ScctHubChat="), Chat, false))
	{
		FString Text = Chat, Seconds;
		if (Chat.Split(TEXT("@"), &Text, &Seconds, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && FCString::IsNumeric(*Seconds))
		{
			AutoChatAt = FCString::Atof(*Seconds);
		}
		else
		{
			Text = Chat;
		}
		AutoChat = Text;
	}
}

FScctHubHud::~FScctHubHud()
{
	Stop();
}

// ---- the frame --------------------------------------------------------------------------------------------------

void FScctHubHud::Tick(UWorld* World, float DeltaSeconds, bool bVerbose)
{
	if (World == nullptr)
	{
		return;
	}
	if (Api == nullptr)
	{
		FString Error;
		Api = ScctFeature<FScctHubSocialApi>(&Error);
		if (Api == nullptr)
		{
			if (!bReportedNoApi)
			{
				bReportedNoApi = true;
				UE_LOG(LogOpenSCCT, Error, TEXT("ScctHubHud: the bridge has no hub social table (%s); the hub plays without its HUD"), *Error);
			}
			return;
		}
	}

	AScctPlayerController* Player = Cast<AScctPlayerController>(World->GetFirstPlayerController());
	if (Player == nullptr)
	{
		return;
	}
	ShowWidget(World);
	Subscribe(Player);
	Model->Clock += DeltaSeconds;

	Api->GetState(&State);
	if (State.Valid == 0)
	{
		return;
	}
	Model->Team = State.Team;

	const FVector Eye = Player->PlayerCameraManager != nullptr ? Player->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	ReadTable(Eye);

	if (State.TravelMenuSerial != TravelSerial)
	{
		TravelSerial = State.TravelMenuSerial;
		if (TravelSerial > 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: the hub opened the travel menu"));
			OpenTravel();
		}
	}

	WatchCloseKeys();

	if (Owner != nullptr && Owner->HasFlag(EScctHubStatusFlags::LocalPawn))
	{
		StandingSeconds = FMath::Max(StandingSeconds, 0.0f) + DeltaSeconds;
	}
	RunUnattended(DeltaSeconds);
	if (bVerbose)
	{
		LogLine(DeltaSeconds);
	}
}

void FScctHubHud::ShowWidget(UWorld* World)
{
	if (Widget.IsValid() && WidgetWorld.Get() == World)
	{
		return;
	}
	Stop();
	UGameViewportClient* Viewport = World->GetGameViewport();
	if (Viewport == nullptr)
	{
		return;
	}
	Widget = SNew(SScctHubHud).Hud(AsShared()).Model(Model);
	Viewport->AddViewportWidgetContent(Widget.ToSharedRef(), HubHudZOrder);
	WidgetWorld = World;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: up (name tags, chat, player list, signs, hub keys)"));
}

void FScctHubHud::Stop()
{
	if (!Widget.IsValid())
	{
		return;
	}
	Model->bComposing = Model->bPlayerList = Model->bTravel = Model->bEmoteMenu = false;
	TakeKeys(false);
	if (UWorld* World = WidgetWorld.Get())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
		}
	}
	Widget.Reset();
	WidgetWorld.Reset();
	if (AScctPlayerController* Player = CommandSource.Get())
	{
		Player->OnLocalCommand.Remove(CommandHandle);
	}
	CommandSource.Reset();
	StandingSeconds = -1.0f;
	ChatSerial = 0;
	SignRevision = -1;
	Model->Chat.Reset();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: down"));
}

void FScctHubHud::Subscribe(AScctPlayerController* Player)
{
	if (CommandSource.Get() == Player)
	{
		return;
	}
	if (AScctPlayerController* Old = CommandSource.Get())
	{
		Old->OnLocalCommand.Remove(CommandHandle);
	}
	CommandSource = Player;
	CommandHandle = Player->OnLocalCommand.AddSP(this, &FScctHubHud::HandleLocalCommand);
}

void FScctHubHud::ReadTable(const FVector& Eye)
{
	// The name tags (farthest first: the order they are drawn in).
	TArray<FScctHubSocialTag> Tags;
	const float X = static_cast<float>(Eye.X), Y = static_cast<float>(Eye.Y), Z = static_cast<float>(Eye.Z);
	ReadRows(Tags, 64, [this, X, Y, Z](FScctHubSocialTag* Rows, int32 Capacity) { return Api->GetNameTags(X, Y, Z, Rows, Capacity); });
	Model->Tags.Reset(Tags.Num());
	for (const FScctHubSocialTag& Row : Tags)
	{
		FScctHubHudTag& Tag = Model->Tags.AddDefaulted_GetRef();
		Tag.World = FVector(Row.X, Row.Y, Row.Z);
		Tag.Distance = Row.Distance;
		Tag.Team = Row.Team;
		Tag.Name = Row.Name;
		Tag.Caption = Row.Caption;
		Tag.Bubble = Row.Bubble;
	}

	// The signs in sight; a mounted one's lines fit its board.
	TArray<FScctHubSocialSign> Signs;
	ReadRows(Signs, 64, [this, X, Y, Z](FScctHubSocialSign* Rows, int32 Capacity) { return Api->GetSigns(X, Y, Z, Rows, Capacity); });
	Model->Signs.Reset(Signs.Num());
	for (const FScctHubSocialSign& Row : Signs)
	{
		if ((Row.Flags & EScctHubSocialFlags::Visible) == 0 || Row.Distance >= SignFar)
		{
			continue;
		}
		FScctHubHudSign& Sign = Model->Signs.AddDefaulted_GetRef();
		Sign.World = FVector(Row.X, Row.Y, Row.Z);
		Sign.Text = Row.Text;
		Sign.Colour = ColourOf(Row.Rgba);
		Sign.Alpha = 1.0f - FMath::Clamp((Row.Distance - SignFade) / (SignFar - SignFade), 0.0f, 1.0f);
		Sign.LineHeight = SignLineHeight * FMath::Max(Row.Scale, 0.1f);
		if ((Row.Flags & EScctHubSocialFlags::Mounted) != 0 && Row.Height > 0.0f)
		{
			int32 Lines = 1;
			for (const TCHAR Char : Sign.Text)
			{
				Lines += Char == TEXT('\n') ? 1 : 0;
			}
			Sign.LineHeight = FMath::Min(Sign.LineHeight, Row.Height * SignFitHeight / Lines);
		}
	}

	ReadChat();

	PlayersTimer -= FApp::GetDeltaTime();
	if (PlayersTimer <= 0.0f)
	{
		PlayersTimer = Model->bPlayerList ? 0.5f : 2.0f;
		ReadPlayers();
	}

	TextTimer -= FApp::GetDeltaTime();
	if (TextTimer <= 0.0f)
	{
		TextTimer = 0.25f;
		TCHAR Prompt[512];
		Api->GetPrompt(X, Y, Z, Prompt, UE_ARRAY_COUNT(Prompt));
		Model->Prompt = Prompt;
		Model->Help = ReadText(EScctHubSocialText::Help);
		Model->EmoteMenu = ReadText(EScctHubSocialText::EmoteMenu);
		Model->Queue = ReadText(EScctHubSocialText::Queue);
		Model->HubName = ReadText(EScctHubSocialText::HubName);
		if (const FScctHubPads* Pads = Owner->GetPads())
		{
			// The pad panel: the queue line in its pad's state colour, the note about the last match.
			Model->QueueColour = Pads->GetQueueColour();
			Model->Note = Pads->ReadText(EScctHubPadsText::Note);
		}
		Model->bBusy = (State.Flags & (EScctHubSocialFlags::Running | EScctHubSocialFlags::InRound | EScctHubSocialFlags::OnPad)) != 0;
	}

	if (Model->bTravel || Model->Destinations.Num() != State.DestinationCount)
	{
		ReadDestinations();
	}
}

void FScctHubHud::ReadChat()
{
	if (State.ChatSerial == ChatSerial)
	{
		return;
	}
	TArray<FScctHubSocialChatLine> Lines;
	const int32 After = ChatSerial;
	ReadRows(Lines, 64, [this, After](FScctHubSocialChatLine* Rows, int32 Capacity) { return Api->GetChat(After, Rows, Capacity); });
	for (const FScctHubSocialChatLine& Line : Lines)
	{
		FScctHubHudChat& Entry = Model->Chat.AddDefaulted_GetRef();
		Entry.Serial = Line.Serial;
		Entry.Channel = static_cast<EScctHubChatChannel>(FMath::Clamp(Line.Channel, 0, 3));
		Entry.Team = Line.Team;
		Entry.Name = Line.Name;
		Entry.Text = Line.Text;
		Entry.At = Model->Clock;
		ChatSerial = FMath::Max(ChatSerial, Line.Serial);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: chat #%d [%s] %s%s%s"), Line.Serial, ChannelName(Entry.Channel), *Entry.Name, Entry.Name.IsEmpty() ? TEXT("") : TEXT(": "), *Entry.Text);
	}
	ChatSerial = FMath::Max(ChatSerial, State.ChatSerial);
	if (Model->Chat.Num() > ChatKept)
	{
		Model->Chat.RemoveAt(0, Model->Chat.Num() - ChatKept);
	}
}

void FScctHubHud::ReadPlayers()
{
	TArray<FScctHubSocialPlayer> Rows;
	ReadRows(Rows, 64, [this](FScctHubSocialPlayer* Out, int32 Capacity) { return Api->GetPlayers(Out, Capacity); });
	TArray<int32> Ids;
	Model->Players.Reset(Rows.Num());
	for (const FScctHubSocialPlayer& Row : Rows)
	{
		FScctHubHudPlayer& Player = Model->Players.AddDefaulted_GetRef();
		Player.PlayerId = Row.PlayerId;
		Player.Team = Row.Team;
		Player.PingMs = Row.PingMs;
		Player.Flags = Row.Flags;
		Player.Name = Row.Name;
		Ids.Add(Row.PlayerId * 2 + (Row.Team == 1 ? 1 : 0));
	}
	if (Ids != RosterIds)
	{
		RosterIds = MoveTemp(Ids);
		++Model->RosterRevision;
	}
}

void FScctHubHud::ReadDestinations()
{
	TArray<FScctHubSocialDestination> Rows;
	ReadRows(Rows, 32, [this](FScctHubSocialDestination* Out, int32 Capacity) { return Api->GetDestinations(Out, Capacity); });
	bool bChanged = Rows.Num() != Model->Destinations.Num();
	TArray<FScctHubHudDestination> Destinations;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		FScctHubHudDestination& Destination = Destinations.AddDefaulted_GetRef();
		Destination.Id = Rows[Index].Id;
		Destination.Title = Rows[Index].Title;
		Destination.BestMs = Rows[Index].BestMs;
		bChanged |= !bChanged && (Model->Destinations[Index].Id != Destination.Id || Model->Destinations[Index].BestMs != Destination.BestMs);
	}
	if (bChanged)
	{
		Model->Destinations = MoveTemp(Destinations);
		++Model->DestinationRevision;
	}
}

FString FScctHubHud::ReadText(EScctHubSocialText Kind) const
{
	TCHAR Buffer[1024];
	Api->GetText(static_cast<int32>(Kind), Buffer, UE_ARRAY_COUNT(Buffer));
	return FString(Buffer);
}

bool FScctHubHud::Project(const FVector& World, float Scale, FVector2D& OutLocal) const
{
	const AScctPlayerController* Player = CommandSource.Get();
	FVector2D Pixel;
	if (Player == nullptr || !Player->ProjectWorldLocationToScreen(World, Pixel, false))
	{
		return false;
	}
	OutLocal = Pixel / FMath::Max(Scale, 0.01f);
	return true;
}

// ---- the keys ---------------------------------------------------------------------------------------------------

void FScctHubHud::HandleLocalCommand(FName Name, int32 Arg, bool bPressed)
{
	if (!bPressed || !Widget.IsValid() || Api == nullptr || State.Valid == 0)
	{
		return;
	}
	const FString Command = Name.ToString();
	if (Command == TEXT("Talk") || Command == TEXT("TeamTalk"))
	{
		OpenComposer(Command == TEXT("TeamTalk") ? EScctHubChatChannel::Side : EScctHubChatChannel::Everyone);
		return;
	}
	if (Command == TEXT("ShowMenu"))
	{
		// The emote menu closes on the menu key (the in-match menu opens as well).
		if (Model->bEmoteMenu)
		{
			ToggleEmoteMenu();
		}
		return;
	}
	if (Command.StartsWith(TEXT("Hub"), ESearchCase::CaseSensitive))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: hub command %s from the bindings"), *Command);
		RunCommand(Command);
	}
}

void FScctHubHud::RunCommand(const FString& Command)
{
	if (Command == TEXT("HubPlayers"))
	{
		TogglePlayerList();
	}
	else if (Command == TEXT("HubEmotes"))
	{
		ToggleEmoteMenu();
	}
	else if (Command == TEXT("HubTravel"))
	{
		Model->bTravel ? CloseTravel() : OpenTravel();
	}
	else if (Command == TEXT("HubArmoury"))
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: the Armoury is not in this client yet"));
	}
	else if (Command.StartsWith(TEXT("HubEmote")))
	{
		// 1 to 8 choose an emote only while the menu is up (they select gadgets otherwise).
		if (Model->bEmoteMenu)
		{
			Api->RunCommand(*Command);
			ToggleEmoteMenu();
		}
	}
	else if (Api->RunCommand(*Command) == 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: %s does nothing here"), *Command);
	}
}

void FScctHubHud::TakeKeys(bool bTake)
{
	if (bKeysTaken == bTake)
	{
		return;
	}
	bKeysTaken = bTake;
	AScctPlayerController* Player = CommandSource.Get();
	if (Player == nullptr)
	{
		return;
	}
	// The hub goes on underneath: the panel takes the mouse and the keys, the controls let go of what was held.
	Player->SetMenuOpen(bTake);
	UGameViewportClient* Viewport = Player->GetWorld() != nullptr ? Player->GetWorld()->GetGameViewport() : nullptr;
	if (bTake)
	{
		Player->SetShowMouseCursor(true);
		if (!FApp::IsUnattended())
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			Player->SetInputMode(Mode);
		}
	}
	else
	{
		if (FApp::IsUnattended())
		{
			if (Viewport != nullptr)
			{
				Viewport->SetIgnoreInput(false);
			}
		}
		else
		{
			Player->SetInputMode(FInputModeGameOnly());
		}
		Player->SetShowMouseCursor(false);
	}
}

void FScctHubHud::WatchCloseKeys()
{
	// While a panel holds the keys the controls raise no commands, so its own key and Escape are read here (as the
	// in-match menu does). The press that opened it must be seen up first.
	if (!Model->bPlayerList && !Model->bTravel)
	{
		return;
	}
	const AScctPlayerController* Player = CommandSource.Get();
	if (Player == nullptr || Model->bComposing)
	{
		return;
	}
	bool bAnyDown = false;
	bool bAnyPressed = false;
	for (const FKey& Key : CloseKeys)
	{
		bAnyDown |= Player->IsInputKeyDown(Key);
		bAnyPressed |= Player->WasInputKeyJustPressed(Key);
	}
	if (bCloseKeysArmed && bAnyPressed)
	{
		Model->bPlayerList ? TogglePlayerList() : CloseTravel();
	}
	else if (!bAnyDown && !bAnyPressed)
	{
		bCloseKeysArmed = true;
	}
}

// ---- the panels -------------------------------------------------------------------------------------------------

void FScctHubHud::OpenComposer(EScctHubChatChannel Channel)
{
	if (Model->bPlayerList || Model->bTravel || Model->bComposing)
	{
		return;
	}
	Model->bComposing = true;
	Model->Channel = Channel;
	Model->bEmoteMenu = false;
	TakeKeys(true);
	if (Widget.IsValid())
	{
		Widget->FocusComposer();
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: chat composer open on %s"), ChannelName(Channel));
}

void FScctHubHud::CloseComposer()
{
	if (!Model->bComposing)
	{
		return;
	}
	Model->bComposing = false;
	TakeKeys(Model->bPlayerList || Model->bTravel);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: chat composer closed"));
}

void FScctHubHud::SendChat(const FString& Text)
{
	const FString Line = Text.TrimStartAndEnd();
	if (Api != nullptr && !Line.IsEmpty())
	{
		Api->SendChat(static_cast<int32>(Model->Channel), *Line);
	}
}

void FScctHubHud::CycleChannel()
{
	Model->Channel = Model->Channel == EScctHubChatChannel::Everyone ? EScctHubChatChannel::Side
		: Model->Channel == EScctHubChatChannel::Side ? EScctHubChatChannel::Nearby : EScctHubChatChannel::Everyone;
}

void FScctHubHud::TogglePlayerList()
{
	if (Model->bPlayerList)
	{
		Model->bPlayerList = false;
		TakeKeys(Model->bComposing || Model->bTravel);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: player list closed"));
		return;
	}
	if (Model->bComposing || Model->bTravel)
	{
		return;
	}
	Model->bPlayerList = true;
	Model->bEmoteMenu = false;
	ReadPlayers();
	PlayersTimer = 0.5f;
	const AScctPlayerController* Player = CommandSource.Get();
	CloseKeys = Player != nullptr ? Player->FindKeysBoundTo(TEXT("HubPlayers")) : TArray<FKey>();
	CloseKeys.AddUnique(EKeys::Escape);
	bCloseKeysArmed = false;
	TakeKeys(true);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: player list open (%d players)"), Model->Players.Num());
	for (const FScctHubHudPlayer& Row : Model->Players)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud:   %s '%s' %d ms%s%s"), Row.Team == 1 ? TEXT("merc") : TEXT("spy"), *Row.Name, Row.PingMs,
			(Row.Flags & EScctHubSocialFlags::Me) != 0 ? TEXT(" (you)") : TEXT(""), (Row.Flags & EScctHubSocialFlags::SignedIn) != 0 ? TEXT(" signed in") : TEXT(" guest"));
	}
}

void FScctHubHud::ToggleEmoteMenu()
{
	if (!Model->bEmoteMenu && (Model->bComposing || Model->bPlayerList || Model->bTravel))
	{
		return;
	}
	Model->bEmoteMenu = !Model->bEmoteMenu;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: emote menu %s"), Model->bEmoteMenu ? TEXT("open") : TEXT("closed"));
}

void FScctHubHud::OpenTravel()
{
	if (Model->bTravel || Model->bComposing)
	{
		return;
	}
	if (Model->bPlayerList)
	{
		TogglePlayerList();
	}
	Model->bEmoteMenu = false;
	Model->bTravel = true;
	ReadDestinations();
	const AScctPlayerController* Player = CommandSource.Get();
	CloseKeys = Player != nullptr ? Player->FindKeysBoundTo(TEXT("HubTravel")) : TArray<FKey>();
	CloseKeys.AddUnique(EKeys::Escape);
	bCloseKeysArmed = false;
	TakeKeys(true);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: travel menu open (%d districts)"), Model->Destinations.Num());
}

void FScctHubHud::CloseTravel()
{
	if (!Model->bTravel)
	{
		return;
	}
	Model->bTravel = false;
	TakeKeys(Model->bComposing || Model->bPlayerList);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: travel menu closed"));
}

void FScctHubHud::Travel(const FString& Id)
{
	if (Api != nullptr)
	{
		Api->Travel(*Id);
	}
	CloseTravel();
}

void FScctHubHud::SetChatMuted(const FString& Name, bool bMuted)
{
	if (Api != nullptr)
	{
		Api->MuteChat(*Name, bMuted ? 1 : 0);
		PlayersTimer = 0.0f;
	}
}

float FScctHubHud::GetVoiceVolume(const FString& Name) const
{
	return Owner != nullptr ? Owner->GetVoiceVolume(Name) : 1.0f;
}

void FScctHubHud::SetVoiceVolume(const FString& Name, float Volume)
{
	if (Owner != nullptr)
	{
		Owner->SetVoiceVolume(Name, Volume);
	}
}

bool FScctHubHud::IsVoiceMuted(const FString& Name) const
{
	return Owner != nullptr && Owner->IsVoiceMuted(Name);
}

void FScctHubHud::SetVoiceMuted(const FString& Name, bool bMuted)
{
	if (Owner != nullptr)
	{
		Owner->SetVoiceMuted(Name, bMuted);
	}
}

// ---- unattended -------------------------------------------------------------------------------------------------

void FScctHubHud::QueueCommand(float DelaySeconds, const FString& Command)
{
	FTimedCommand& Entry = Timed.AddDefaulted_GetRef();
	Entry.At = FMath::Max(DelaySeconds, 0.0f);
	Entry.Command = Command.TrimStartAndEnd();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubCommand: %s queued for %.1f s after the player stands in the hub"), *Entry.Command, Entry.At);
}

void FScctHubHud::RunUnattended(float DeltaSeconds)
{
	if (StandingSeconds < 0.0f)
	{
		return;
	}
	for (FTimedCommand& Entry : Timed)
	{
		if (Entry.bDone || StandingSeconds < Entry.At)
		{
			continue;
		}
		Entry.bDone = true;
		if (Entry.Command.StartsWith(TEXT("Open:")))
		{
			const FString Panel = Entry.Command.Mid(5);
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHubOpen: %s at %.1f s in the hub"), *Panel, StandingSeconds);
			if (Panel == TEXT("players") && !Model->bPlayerList) { TogglePlayerList(); }
			else if (Panel == TEXT("chat")) { OpenComposer(EScctHubChatChannel::Everyone); }
			else if (Panel == TEXT("travel")) { OpenTravel(); }
			else if (Panel == TEXT("emotes") && !Model->bEmoteMenu) { ToggleEmoteMenu(); }
			continue;
		}

		// Through the player's own bindings when a key carries the command: the press takes the same way as a real one.
		AScctPlayerController* Player = CommandSource.Get();
		if (Player != nullptr && !Player->IsMenuOpen() && Player->FindKeysBoundTo(Entry.Command).Num() > 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubCommand: pressing the key of %s at %.1f s in the hub"), *Entry.Command, StandingSeconds);
			Player->TapBoundCommand(Entry.Command);
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubCommand: running %s at %.1f s in the hub (no free key for it)"), *Entry.Command, StandingSeconds);
			RunCommand(Entry.Command);
		}
	}

	if (!bAutoChatDone && !AutoChat.IsEmpty() && StandingSeconds >= AutoChatAt)
	{
		bAutoChatDone = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("-ScctHubChat: saying '%s' to everyone"), *AutoChat);
		if (Api != nullptr)
		{
			Api->SendChat(static_cast<int32>(EScctHubChatChannel::Everyone), *AutoChat);
		}
	}
}

void FScctHubHud::LogLine(float DeltaSeconds)
{
	LogTimer -= DeltaSeconds;
	if (LogTimer > 0.0f)
	{
		return;
	}
	LogTimer = 3.0f;
	FString Tags;
	for (const FScctHubHudTag& Tag : Model->Tags)
	{
		Tags += FString::Printf(TEXT(" %s(%s %.0f)"), *Tag.Name, Tag.Team == 1 ? TEXT("merc") : TEXT("spy"), Tag.Distance);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctHubHud: team %d, %d players, %d tags read, %d drawn:%s | %d signs in sight, %d drawn | %d chat lines | prompt '%s' | panels%s%s%s%s"),
		State.Team, State.PlayerCount, Model->Tags.Num(), Model->TagsDrawn, *Tags, Model->Signs.Num(), Model->SignsDrawn, Model->Chat.Num(), *Model->Prompt,
		Model->bComposing ? TEXT(" chat") : TEXT(""), Model->bPlayerList ? TEXT(" players") : TEXT(""), Model->bTravel ? TEXT(" travel") : TEXT(""),
		Model->bEmoteMenu ? TEXT(" emotes") : TEXT(""));
}
