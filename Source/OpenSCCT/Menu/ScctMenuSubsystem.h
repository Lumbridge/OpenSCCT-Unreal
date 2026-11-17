#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctMenuSubsystem.generated.h"

class AScctPlayerController;

/**
 * The menus' own interface commands in a match. The player's bindings raise them on
 * AScctPlayerController::OnLocalCommand; this subscribes there rather than in the game mode:
 *
 *   ChangeGear   (I by default) opens the in-match menu on the Equipment page (Menu/SScctGearPage.h), as the
 *                Godot client's PlayMenu.OpenGear does; closing the page goes back to the game
 *   DisplayStrategicMap (F7 by default) opens the strategic map of the round (Menu/SScctStrategicMapPage.h) when
 *                the player's state allows it (SPlayerController.DisplayStrategicMap: not a tutorial map, not working
 *                an object), as the Godot client's PlayMenu.OpenStrategicMap does; the same key closes it again
 *                (read here, as the controls raise no commands while a menu is open), as do Escape and BACK
 *
 * Unattended strategic map, in a match:
 *   -ScctStrategicMap[=S]        raises DisplayStrategicMap S seconds (3 by default) after the key first may open it
 *   -ScctStrategicMapFloorAt=S   S seconds after it opened, presses UP (DOWN from the top floor)
 *   -ScctStrategicMapCloseAt=S   S seconds after it opened, closes it as a press of its own key does
 *   While it is open the log has its floor, the marker's U, V and the player's point every 2 s ("ScctStrategicMap: ...").
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

	/** Opens the strategic map on its own over the match. False when the player's state or the level does not allow it. */
	bool OpenStrategicMap();

private:
	void OnFrame(float DeltaSeconds);
	void TickStrategicMap(AScctPlayerController* Player, float DeltaSeconds);
	class UScctMatchMenuWidget* FindMatchMenu() const;
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

	/** The strategic map's own key while it is open: the keys bound to DisplayStrategicMap, armed once all are up. */
	TArray<FKey> MapKeys;
	bool bMapKeysArmed = false;
	bool bMapWasOpen = false;
	float MapOpenSeconds = 0.0f;
	float MapLogTimer = 0.0f;

	/** -ScctStrategicMap and its follow-ups (negative: not asked for, or done). */
	float MapOpenAt = -1.0f;
	float MapAllowedSeconds = 0.0f;
	float MapFloorAt = -1.0f;
	float MapCloseAt = -1.0f;
};
