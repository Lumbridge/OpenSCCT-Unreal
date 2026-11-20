#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Styling/SlateBrush.h"
#include "ScctFrontEndGameMode.generated.h"

class SBorder;
class UTexture2D;
class UScctFrontEndWidget;
class UScctMenuBackdrop;
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
 *
 * Once that still is up, the live backdrop takes over when the player has it on (Menu/ScctMenuBackdrop.h): a match
 * played by bots behind the menu, on a map that changes every few minutes.
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Where the camera rests while no match gives it a view. The live backdrop holds its last picture with it when
	 * its match stops without another to show.
	 */
	void SetStillCamera(const FVector& Location, const FRotator& Rotation, float Sway);

private:
	void ShowMenu();
	void MoveCamera();
	void RevealBackdrop();
	void RemoveLoadingCover();
	bool ShowLocalMapPicture();

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LocalMapPicture;
	FSlateBrush LocalMapBrush;
	TSharedPtr<SWidget> LocalMapBackdrop;

	/** Covers the first rendered frame, then masks the temporary scene underneath the interactive menu. */
	TSharedPtr<SBorder> LoadingCover;

	UPROPERTY(Transient)
	TObjectPtr<UScctFrontEndWidget> Menu;

	/** The live backdrop's director; it does nothing until the still below is settled. */
	UPROPERTY(Transient)
	TObjectPtr<UScctMenuBackdrop> LiveBackdrop;

	/** The still that was shown, for the live backdrop to start from: its map and its level (none: the level's own scene). */
	UPROPERTY(Transient)
	TObjectPtr<ULevelStreamingDynamic> StillLevel;
	FString StillMap;

	/** Where the camera rests; it sways a little around this. */
	FVector CameraLocation = FVector(-620.0, 0.0, 190.0);
	FRotator CameraRotation = FRotator(-6.0, 0.0, 0.0);
	float CameraSway = 4.0f;
	float Seconds = 0.0f;

	/**
	 * The imported map streaming in behind the menu. It loads hidden under the cover and takes its place, with
	 * its camera, once it is visible and has had time to compile shaders.
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
