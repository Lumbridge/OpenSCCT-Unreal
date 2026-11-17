#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ScctFrontEndGameMode.generated.h"

class UScctFrontEndWidget;

/**
 * The game mode of the front end level (/Game/OpenSCCT/Maps/L_FrontEnd, made by Tools/Menu/build_front_end.py).
 * It puts the menu in the viewport, gives the mouse to it, and sets the backdrop: one of the imported maps is
 * streamed in behind the menu with a slowly moving camera; when none of the maps it knows a camera for has been
 * imported, the level's own small lit scene stays (its actors carry the tag ScctFallback).
 *
 * Unattended switch (off by default):
 *   -ScctMenuPage=offline|join|servers   open the menu on that page (for screenshots)
 */
UCLASS()
class OPENSCCT_API AScctFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AScctFrontEndGameMode();

	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ShowMenu();
	void MoveCamera();

	UPROPERTY(Transient)
	TObjectPtr<UScctFrontEndWidget> Menu;

	/** Where the camera rests; it sways a little around this. */
	FVector CameraLocation = FVector(-620.0, 0.0, 190.0);
	FRotator CameraRotation = FRotator(-6.0, 0.0, 0.0);
	float CameraSway = 4.0f;
	float Seconds = 0.0f;
};
