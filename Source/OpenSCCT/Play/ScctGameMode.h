#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Play/ScctSessionSubsystem.h"
#include "ScctGameMode.generated.h"

class UScctMatchMenuWidget;

/**
 * The game mode of a playable OpenSCCT level. It starts the OpenSCCT match for the level; the rules themselves
 * run in the simulation. URL options, all optional:
 *   ?ScctMap=AquaE      the OpenSCCT map (default: the level's name)
 *   ?Mode=Story         Story, DiskHunt, Deathmatch or MercDeathMatch
 *   ?Side=spy|merc      the local player's side (default spy)
 *   ?Bots=3             how many bots fill the match
 *   ?Difficulty=hard    the bots' skill: easy, normal, hard or expert (or 1 to 4); default normal
 *   ?Sidearms=1         the mercs carry OpenSCCT's sidearms (G, SwapWeapon, draws it)
 *   ?Sidearm=magnum44   the local merc's own: suppressed, automatic, magnum44, desert_eagle, unsuppressed (the
 *                       default) or none
 *   ?Rifles=1           the mercs may pick OpenSCCT's additional rifles
 *   ?Practice=1         practice rules: no time limit, unlimited lives, 5 s respawn
 *   ?Name=Sam           the local player's name (Unreal's own player-name option; default "Player")
 *   ?TimeLimit=10       the player's own rules over the map's: minutes (0 for none), ?SpyLives=3 and ?MercLives=3
 *                       (0 for unlimited), ?Respawn=5 seconds
 *   ?ScctOnline=1       the level presents the round of the online visit (Online/ScctOnlineSubsystem.h): no local
 *                       match is started, the other options are not used. Without it a level opened directly
 *                       starts a local match, and a visit that was under way is left.
 * A switch counts as on when it is given without a value or with anything but 0, false, no or off.
 *
 * Keys: none of its own. The in-match menu (Menu/ScctMatchMenuWidget.h) opens on the interface command ShowMenu
 * as the player's bindings raise it (AScctPlayerController::OnLocalCommand; Escape by default). Quit (F11) does
 * nothing, as in OpenSCCT's Godot client: the way out of a match is the menu.
 * While it is open the controls hold the player's keys and mouse back, so the bindings raise nothing; the menu
 * then closes on the keys bound to ShowMenu, read from Unreal directly, and on Escape whatever is bound.
 *
 * Unattended switch (off by default):
 *   -ScctMatchMenu=S    open the in-match menu S seconds after the level started (for screenshots)
 *   -ScctLeaveMatchAfter=S  S seconds after the level started, do what the in-match menu's last button does: back
 *                       to the main menu from a local match, leave the server from an online round
 *   -ScctLeaveMatchTimes=N  -ScctLeaveMatchAfter counts in the first N levels of the run (default 1)
 */
UCLASS()
class OPENSCCT_API AScctGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AScctGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Opens or closes the in-match menu and hands the keys and the mouse to it or back to the game. */
	void SetMenuOpen(bool bOpen);

	/** The player controller's OnLocalCommand. */
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);

	/** Gives the keyboard and the mouse to the match (the front end's menu, or the in-match menu, had them). */
	void GiveInputToGame(APlayerController* Player);

	FScctLocalMatchOptions Match;

	/** The level was opened for the online visit's round. */
	bool bOnline = false;

	UPROPERTY(Transient)
	TObjectPtr<UScctMatchMenuWidget> MatchMenu;

	/** The keys that close the open menu: those bound to ShowMenu when it opened, and Escape. */
	TArray<FKey> CloseKeys;

	/** Every close key has been up since the menu opened, so the next press of one closes it. */
	bool bCloseKeysArmed = false;

	/** The controller whose interface commands are listened to. */
	TWeakObjectPtr<class AScctPlayerController> CommandSource;

	/** -ScctMatchMenu: seconds until the menu is opened once; negative when not asked for or done. */
	float OpenMenuIn = -1.0f;

	/** -ScctLeaveMatchAfter: seconds until the match is left once; negative when not asked for or done. */
	float LeaveMatchIn = -1.0f;
};
