#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ScctFrontEndGameMode.generated.h"

class UScctFrontEndWidget;
class ULevelStreamingDynamic;

/**
 * The game mode of the front end level (/Game/OpenSCCT/Maps/L_FrontEnd, made by Tools/Menu/build_front_end.py).
 * It puts the menu in the viewport, gives the mouse to it, and sets the backdrop: one of the imported maps is
 * streamed in behind the menu with a slowly moving camera; when none of the maps it knows a camera for has been
 * imported, the level's own small lit scene stays (its actors carry the tag ScctFallback).
 *
 * Unattended switches (off by default):
 *   -ScctMenuPage=offline|join|servers|options   open the menu on that page (for screenshots)
 *   -ScctMenuPage=start                  open the offline page and press its START (each time the menu is shown)
 *   -ScctOptions=<Page>                  open the settings page on that tab (video, audio, hud, online...; Menu/SScctSettingsPanel.h)
 *   -ScctConfirmTest                     open the QUIT question (a screenshot of the confirmation box)
 */
UCLASS()
class OPENSCCT_API AScctFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AScctFrontEndGameMode();

	virtual void StartPlay() override;
	virtual void StartToLeaveMap() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ShowMenu();
	void MoveCamera();
	void RevealBackdrop();

	UPROPERTY(Transient)
	TObjectPtr<UScctFrontEndWidget> Menu;

	/** Where the camera rests; it sways a little around this. */
	FVector CameraLocation = FVector(-620.0, 0.0, 190.0);
	FRotator CameraRotation = FRotator(-6.0, 0.0, 0.0);
	float CameraSway = 4.0f;
	float Seconds = 0.0f;

	/**
	 * The imported map streaming in behind the menu. It loads hidden while the level's own scene stays on screen,
	 * and takes its place (with its camera) once it is loaded and its shaders are ready, so the menu never sits on
	 * a black or untextured picture while the map loads.
	 */
	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> BackdropLevel;
	const TCHAR* BackdropMap = nullptr;
	FVector BackdropLocation = FVector::ZeroVector;
	FRotator BackdropRotation = FRotator::ZeroRotator;
	float BackdropSway = 0.0f;
	/** When the map started loading (wall clock): past a limit it is shown even with shaders still compiling. */
	double BackdropStarted = 0.0;
};
