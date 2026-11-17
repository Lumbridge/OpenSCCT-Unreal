#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UScctVoiceSubsystem;

/**
 * The voice part of one lobby row (Godot's LobbyRoomScreen): a speaking mark lit while the player is heard (the local
 * player's while he transmits), and on another player's row his "Voice volume" and "Mute" in one small word: the
 * percentage (the wheel moves it by 10% from 0 to 200%) or MUTED (a click mutes and unmutes), and for the host or an
 * admin "Voice Ban" / "Voice Unban" (a second click within four seconds confirms). Bots show nothing. Everything shown is read from UScctVoiceSubsystem as it is drawn, so the row stays right however
 * seldom the lobby is filled again. The lobby screen adds one per player row:
 *
 *   SNew(SScctVoiceLobbyControls).Voice(VoiceSubsystem).PlayerName(Name).PlayerId(Id).Team(Team)
 *       .Local(bLocal).Bot(bBot).CanBan(bLocalIsHostOrAdmin && !bLocal).VoiceBanned(bBanned)
 */
class OPENSCCT_API SScctVoiceLobbyControls : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctVoiceLobbyControls) : _PlayerId(0), _Team(-1), _Local(false), _Bot(false), _CanBan(false), _VoiceBanned(false) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UScctVoiceSubsystem>, Voice)
		SLATE_ARGUMENT(FString, PlayerName)
		SLATE_ARGUMENT(int32, PlayerId)
		SLATE_ARGUMENT(int32, Team)
		SLATE_ARGUMENT(bool, Local)
		SLATE_ARGUMENT(bool, Bot)
		SLATE_ARGUMENT(bool, CanBan)
		SLATE_ARGUMENT(bool, VoiceBanned)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
	void Step(int32 Steps) const;

	TWeakObjectPtr<UScctVoiceSubsystem> Voice;
	FString PlayerName;
	int32 PlayerId = 0;
	bool bLocal = false;
};
