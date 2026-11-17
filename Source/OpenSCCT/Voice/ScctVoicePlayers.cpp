// UScctVoiceSubsystem's other players: the per-player volume and mute by name, what the lobby reads per player, the
// host's voice ban and the speaker overlay of a round (SScctVoiceOverlay). Kept apart from the capture and playback in
// ScctVoiceSubsystem.cpp.

#include "Voice/ScctVoiceSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Hud/ScctHudStyle.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"
#include "Voice/ScctVoiceSettings.h"
#include "Voice/SScctVoiceOverlay.h"

#define LOCTEXT_NAMESPACE "ScctVoice"

namespace
{
	/** Above the HUD and the match menu, so a player's line still takes the click while the menu is open. */
	constexpr int32 OverlayZOrder = 60;
	constexpr float VolumeStep = 0.1f;

	/** Godot's LobbyRoomModel.VolumeText: "name - volume n%". */
	FString VolumeText(const FString& Name, float Volume)
	{
		return FString::Printf(TEXT("%s - volume %d%%"), *Name, FMath::RoundToInt32(Volume * 100.0f));
	}

	/** A volume moved by Steps of 10%, on the 10% grid, from 0 to 2. */
	float StepVolume(float Volume, int32 Steps)
	{
		return FMath::Clamp(FMath::RoundToFloat((Volume + Steps * VolumeStep) * 10.0f) / 10.0f, 0.0f, 2.0f);
	}

	FString RowName(const FScctVoiceTalker& Row)
	{
		TCHAR Name[FScctVoiceTalker::NameCapacity];
		FMemory::Memcpy(Name, Row.Name, sizeof(Name));
		Name[FScctVoiceTalker::NameCapacity - 1] = TEXT('\0');
		return FString(Name);
	}

	bool IsSide(int32 Team) { return Team == 0 || Team == 1; }
}

void UScctVoiceSubsystem::InitPlayers()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	VolumeFile = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("VoiceVolumes.json"));

	// -ScctVoiceMute=Name[;Name]: those players at volume 0 (still received, played as silence).
	// -ScctVoiceVolume=Name=0.4[;Name=1.5]: those players at that volume.
	FString Text;
	TArray<FString> Entries;
	if (FParse::Value(CommandLine, TEXT("ScctVoiceMute="), Text, false))
	{
		Text.ParseIntoArray(Entries, TEXT(";"));
		for (FString& Entry : Entries)
		{
			Entry.TrimStartAndEndInline();
			if (!Entry.IsEmpty())
			{
				SwitchVolumes.Emplace(Entry, 0.0f);
			}
		}
	}
	Text.Reset();
	if (FParse::Value(CommandLine, TEXT("ScctVoiceVolume="), Text, false))
	{
		Text.ParseIntoArray(Entries, TEXT(";"));
		for (const FString& Entry : Entries)
		{
			FString Name;
			FString Value;
			if (Entry.Split(TEXT("="), &Name, &Value, ESearchCase::IgnoreCase, ESearchDir::FromEnd) && !Name.TrimStartAndEnd().IsEmpty())
			{
				SwitchVolumes.Emplace(Name.TrimStartAndEnd(), FCString::Atof(*Value));
			}
		}
	}

	Text.Reset();
	if (FParse::Value(CommandLine, TEXT("ScctVoiceOverlayInput="), Text, false))
	{
		Text.ParseIntoArray(Entries, TEXT(";"));
		for (const FString& Entry : Entries)
		{
			TArray<FString> Parts;
			Entry.TrimStartAndEnd().ParseIntoArray(Parts, TEXT(":"));
			if (Parts.Num() == 3)
			{
				OverlayInputs.Add({ FCString::Atof(*Parts[0]), FCString::Atoi(*Parts[1]), Parts[2].ToLower() });
			}
		}
		OverlayInputs.StableSort([](const FOverlayInput& A, const FOverlayInput& B) { return A.Seconds < B.Seconds; });
	}

	bForceOverlay = FParse::Param(CommandLine, TEXT("ScctVoiceOverlay"));
	if (bForceOverlay)
	{
		// Made-up talkers for a look at the overlay: a team mate on team talk, an enemy heard through hacked comms at
		// 60% and a muted enemy. Clicks and the wheel act on them as on real ones.
		FakeTalkers.Add({ TEXT("Archer"), 0, true, 1.0f, false });
		FakeTalkers.Add({ TEXT("Kestrel"), 1, false, 0.6f, false });
		FakeTalkers.Add({ TEXT("Rook"), 1, false, 1.0f, true });
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: the speaker overlay is shown everywhere with made-up talkers (-ScctVoiceOverlay)"));
	}
}

void UScctVoiceSubsystem::StartPlayers()
{
	if (bPlayersStarted)
	{
		return;
	}
	bPlayersStarted = true;
	Api->SetVolumeFile(*VolumeFile);
	FString Saved;
	FFileHelper::LoadFileToString(Saved, *VolumeFile);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: per-player volumes kept in %s (%s)"), *VolumeFile,
		Saved.IsEmpty() ? TEXT("nothing saved yet") : *Saved.TrimStartAndEnd());
	for (const TPair<FString, float>& Switch : SwitchVolumes)
	{
		const float Before = GetPlayerVoice(Switch.Key).Volume;
		SetPlayerVolume(Switch.Key, Switch.Value);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: %s's voice from %d%% to %d%% (command line)"), *Switch.Key,
			FMath::RoundToInt32(Before * 100.0f), FMath::RoundToInt32(GetPlayerVoice(Switch.Key).Volume * 100.0f));
	}
}

// ---- the lobby's per-player row -------------------------------------------------------------------------------

FScctVoicePlayer UScctVoiceSubsystem::GetPlayerVoice(const FString& Name) const
{
	FScctVoicePlayer Player = {};
	Player.Volume = 1.0f;
	if (Api != nullptr && !Name.IsEmpty())
	{
		Api->GetPlayerVoice(*Name, &Player);
	}
	return Player;
}

void UScctVoiceSubsystem::SetPlayerVolume(const FString& Name, float Volume)
{
	if (Api != nullptr && !Name.IsEmpty())
	{
		Api->SetPlayerVolume(*Name, FMath::Clamp(Volume, 0.0f, 2.0f));
	}
}

void UScctVoiceSubsystem::SetPlayerMuted(const FString& Name, bool bMuted)
{
	if (Api != nullptr && !Name.IsEmpty())
	{
		Api->SetPlayerMuted(*Name, bMuted ? 1 : 0);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: %s %s"), *Name, bMuted ? TEXT("muted") : TEXT("unmuted"));
	}
}

bool UScctVoiceSubsystem::SetVoiceBanned(int32 PlayerId, bool bBanned)
{
	const bool bSent = Api != nullptr && Api->SetVoiceBanned(PlayerId, bBanned ? 1 : 0) != 0;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: voice %s of player %d %s"), bBanned ? TEXT("ban") : TEXT("unban"), PlayerId,
		bSent ? TEXT("sent to the server") : TEXT("not sent (no visit)"));
	return bSent;
}

FText UScctVoiceSubsystem::GetMicrophoneLine() const
{
	if (Context.Visit == 0)
	{
		return FText::GetEmpty();
	}
	if (!FScctSettings::GetBool(ScctVoiceSettings::EnabledKey))
	{
		return LOCTEXT("MicLineOff", "Voice chat is off (Options, Voice).");
	}
	if (FScctSettings::GetBool(ScctVoiceSettings::MicMutedKey))
	{
		return LOCTEXT("MicLineMuted", "Your microphone is muted");
	}
	const FText State = Status.Transmitting != 0 ? LOCTEXT("MicTransmitting", "transmitting") : LOCTEXT("MicSilent", "silent");
	const FText Mode = FScctSettings::GetBool(ScctVoiceSettings::PushToTalkKey) ? LOCTEXT("MicPush", "push to talk") : LOCTEXT("MicActivated", "voice activated");
	return FText::Format(LOCTEXT("MicLine", "Mic {0}, {1}"), State, Mode);
}

// ---- the speaker overlay --------------------------------------------------------------------------------------

TArray<FScctVoiceOverlayLine> UScctVoiceSubsystem::BuildOverlayLines() const
{
	TArray<FScctVoiceOverlayLine> Lines;
	// Without a known side (a local match with the switch) the made-up round is seen as a spy.
	const int32 LocalTeam = IsSide(Context.LocalTeam) ? Context.LocalTeam : (bForceOverlay ? 0 : Context.LocalTeam);
	const FLinearColor Own = ScctHudStyle::Accent(LocalTeam);
	if (Status.MicMuted != 0 || FScctSettings::GetBool(ScctVoiceSettings::MicMutedKey) || bForceOverlay)
	{
		Lines.Add({ LOCTEXT("OverlayMuted", "Your microphone is muted").ToString(), FString(), Own });
	}
	else if (Status.Transmitting != 0)
	{
		Lines.Add({ Status.Channel != 0 ? TEXT("You - Speaking (team)") : TEXT("You - Speaking"), FString(), Own });
	}

	auto AddTalker = [&Lines, LocalTeam, this](const FString& Name, int32 Team, bool bTeamChannel, float Volume, bool bMuted)
	{
		// An enemy is heard in a round only through hacked comms (or talking to him downed, which is the same link).
		const bool bHacked = (Context.InRound != 0 || bForceOverlay) && IsSide(Team) && IsSide(LocalTeam) && Team != LocalTeam;
		const TCHAR* Mark = bMuted ? TEXT(" (muted)") : bHacked ? TEXT(" (hacked)") : bTeamChannel ? TEXT(" (team)") : TEXT("");
		FLinearColor Colour = ScctHudStyle::Accent(Team);
		if (bMuted)
		{
			Colour = ScctHudStyle::Fade(Colour, 0.6f);
		}
		Lines.Add({ VolumeText(Name, Volume) + Mark, Name, Colour });
	};

	for (const FScctVoiceTalker& Row : Talkers)
	{
		if (Row.Speaking != 0 || Row.Muted != 0)
		{
			AddTalker(RowName(Row), Row.Team, Row.Channel != 0, Row.Volume, Row.Muted != 0);
		}
	}
	for (const FFakeTalker& Fake : FakeTalkers)
	{
		AddTalker(Fake.Name, Fake.Team, Fake.bTeamChannel, Fake.Volume, Fake.bMuted);
	}
	return Lines;
}

void UScctVoiceSubsystem::UpdateOverlay()
{
	// A round of an online visit, as Godot's ShowOverlay: the lobby lists its talkers itself.
	const bool bShow = bForceOverlay || (Context.Visit != 0 && Context.InRound != 0);
	ShowOverlay(bShow);
	if (!Overlay.IsValid())
	{
		return;
	}

	const TArray<FScctVoiceOverlayLine> Lines = BuildOverlayLines();
	Overlay->SetLines(Lines);
	const UGameInstance* GameInstance = GetGameInstance();
	const APlayerController* Player = GameInstance != nullptr ? GameInstance->GetFirstLocalPlayerController() : nullptr;
	// The cursor is free while a menu over the match has it; then a click or the wheel on a line is the player's.
	Overlay->SetInteractive(Player != nullptr && Player->ShouldShowMouseCursor());

	FString Text;
	for (const FScctVoiceOverlayLine& Line : Lines)
	{
		Text += (Text.IsEmpty() ? TEXT("") : TEXT(" | ")) + Line.Text;
	}
	if (Text != LastOverlayText)
	{
		LastOverlayText = Text;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: overlay: %s"), Text.IsEmpty() ? TEXT("(nothing)") : *Text);
	}
	RunOverlayInputs();
}

void UScctVoiceSubsystem::RunOverlayInputs()
{
	while (OverlayInputs.Num() > 0 && OverlayInputs[0].Seconds <= ReadyTime && Overlay.IsValid() && FSlateApplication::IsInitialized())
	{
		const FOverlayInput Input = OverlayInputs[0];
		OverlayInputs.RemoveAt(0);
		FVector2D Centre;
		if (!Overlay->GetLineScreenCentre(Input.Line, Centre))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctVoice: -ScctVoiceOverlayInput: the overlay has no line %d laid out"), Input.Line);
			continue;
		}

		// As the mouse would: moved onto the line, then the button or the wheel, all through Slate's own routing.
		FSlateApplication& Slate = FSlateApplication::Get();
		const FModifierKeysState Modifiers;
		const TSet<FKey> NoButtons;
		Slate.ProcessMouseMoveEvent(FPointerEvent(Slate.CursorPointerIndex, Centre, Centre, NoButtons, EKeys::Invalid, 0.0f, Modifiers));
		bool bHandled = false;
		if (Input.Action == TEXT("click"))
		{
			TSet<FKey> Pressed;
			Pressed.Add(EKeys::LeftMouseButton);
			const FPointerEvent Down(Slate.CursorPointerIndex, Centre, Centre, Pressed, EKeys::LeftMouseButton, 0.0f, Modifiers);
			bHandled = Slate.ProcessMouseButtonDownEvent(nullptr, Down);
			Slate.ProcessMouseButtonUpEvent(FPointerEvent(Slate.CursorPointerIndex, Centre, Centre, NoButtons, EKeys::LeftMouseButton, 0.0f, Modifiers));
		}
		else if (Input.Action == TEXT("up") || Input.Action == TEXT("down"))
		{
			const FPointerEvent Wheel(Slate.CursorPointerIndex, Centre, Centre, NoButtons, EKeys::Invalid, Input.Action == TEXT("up") ? 1.0f : -1.0f, Modifiers);
			bHandled = Slate.ProcessMouseWheelOrGestureEvent(Wheel, nullptr);
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: -ScctVoiceOverlayInput %s on line %d '%s' at (%.0f, %.0f), cursor %s: %s"), *Input.Action, Input.Line,
			Overlay->GetLines().IsValidIndex(Input.Line) ? *Overlay->GetLines()[Input.Line].Text : TEXT("?"), Centre.X, Centre.Y,
			Overlay->IsInteractive() ? TEXT("free") : TEXT("the game's"), bHandled ? TEXT("handled") : TEXT("not handled"));
	}
}

void UScctVoiceSubsystem::ShowOverlay(bool bShow)
{
	UGameViewportClient* Viewport = GEngine != nullptr ? GEngine->GameViewport.Get() : nullptr;
	const bool bOnViewport = Overlay.IsValid() && OverlayViewport.Get() == Viewport && Viewport != nullptr && Overlay->GetParentWidget().IsValid();
	if (bShow && bOnViewport)
	{
		return;
	}
	if (Overlay.IsValid())
	{
		if (UGameViewportClient* Old = OverlayViewport.Get())
		{
			Old->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
		if (!bShow)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: speaker overlay hidden"));
		}
		Overlay.Reset();
		OverlayViewport.Reset();
		LastOverlayText.Reset();
	}
	if (!bShow || Viewport == nullptr)
	{
		return;
	}

	// Put on again after a map load took the viewport's widgets away.
	Overlay = SNew(SScctVoiceOverlay)
		.OnToggleMute(FScctVoiceOverlayMute::CreateUObject(this, &UScctVoiceSubsystem::ToggleOverlayMute))
		.OnAdjustVolume(FScctVoiceOverlayVolume::CreateUObject(this, &UScctVoiceSubsystem::AdjustOverlayVolume));
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), OverlayZOrder);
	OverlayViewport = Viewport;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: speaker overlay shown"));
}

void UScctVoiceSubsystem::ToggleOverlayMute(const FString& Player)
{
	if (FFakeTalker* Fake = FakeTalkers.FindByPredicate([&Player](const FFakeTalker& F) { return F.Name == Player; }))
	{
		Fake->bMuted = !Fake->bMuted;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: %s %s (overlay, made up)"), *Player, Fake->bMuted ? TEXT("muted") : TEXT("unmuted"));
		return;
	}
	SetPlayerMuted(Player, GetPlayerVoice(Player).Muted == 0);
}

void UScctVoiceSubsystem::AdjustOverlayVolume(const FString& Player, int32 Steps)
{
	if (FFakeTalker* Fake = FakeTalkers.FindByPredicate([&Player](const FFakeTalker& F) { return F.Name == Player; }))
	{
		Fake->Volume = StepVolume(Fake->Volume, Steps);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: %s's voice at %d%% (overlay, made up)"), *Player, FMath::RoundToInt32(Fake->Volume * 100.0f));
		return;
	}
	const float Volume = StepVolume(GetPlayerVoice(Player).Volume, Steps);
	SetPlayerVolume(Player, Volume);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctVoice: %s's voice at %d%% (overlay)"), *Player, FMath::RoundToInt32(Volume * 100.0f));
}

#undef LOCTEXT_NAMESPACE
