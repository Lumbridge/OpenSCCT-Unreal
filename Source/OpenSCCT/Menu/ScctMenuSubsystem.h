#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctMenuSubsystem.generated.h"

class AScctPlayerController;

/**
 * The menus' own interface commands in a match. The player's bindings raise them on
 * AScctPlayerController::OnLocalCommand; this subscribes there rather than in the game mode:
 *
 *   ChangeGear   (I by default) opens the in-match menu on the Equipment page (Menu/SScctGearPage.h), as the
 *                Godot client's PlayMenu.OpenGear does; closing the page goes back to the game
 *
 * Unattended, in a match (the side's kit must be on the pawn; -ScctGearAt=S waits S seconds more, 2 by default, so
 * a gear change still lands inside the swap window after a spawn):
 *   -ScctGear[=spy|merc]       opens the Equipment page, on that side's tab (else the side played)
 *   -ScctGearApply=<side>,...  opens it, picks the gear given and applies (SScctGearPage::RunScript)
 *   -ScctGearKey               raises the ChangeGear command on the controller, as the binding does
 */
UCLASS()
class OPENSCCT_API UScctMenuSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Opens the in-match menu on the Equipment page. False when this level has no in-match menu. */
	bool OpenGear(int32 Side = -1);

private:
	void OnFrame(float DeltaSeconds);
	void HandleLocalCommand(FName Name, int32 Arg, bool bPressed);

	FDelegateHandle FrameHandle;
	FDelegateHandle CommandHandle;
	TWeakObjectPtr<AScctPlayerController> CommandSource;

	/** -ScctGear / -ScctGearApply still to do, and how long the kit has been on the pawn. */
	bool bGearPending = false;
	/** -ScctGearKey: raise the ChangeGear command instead of opening the page directly. */
	bool bGearByCommand = false;
	int32 GearSide = -1;
	FString GearScript;
	float GearDelay = 2.0f;
	float KitSeconds = 0.0f;
};
