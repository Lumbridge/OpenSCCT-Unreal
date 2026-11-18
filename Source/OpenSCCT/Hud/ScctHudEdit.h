#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctHudEdit.generated.h"

class AScctPlayerController;
class SScctHudEditOverlay;
class SWidget;

/**
 * The HUD layout's edit mode in a match (Godot's HudEditorPanel, done over the live HUD instead of a preview): the
 * groups of Hud/ScctHudLayout.h are outlined with their names, size and opacity, and the player arranges them for
 * the side being played.
 *
 *   drag (left button)    moves the group under the mouse; its edges snap to the safe area's edges and its centre
 *                         to the safe area's centre within 10 units, and a guide shows the snap
 *   wheel                 sizes the group under the mouse (or the last one picked), 25 % to 300 % in 5 % steps
 *   Shift + wheel         its opacity, 10 % to 100 %
 *   arrow keys            nudge the picked group by one unit (Shift: ten)
 *   H                     hides or shows the picked group (a hidden group stays faint while editing)
 *   R / Ctrl + R          puts the picked group / every group of the side back to the stock layout
 *   Esc or Enter          saves the layout to the user settings file and gives the game its keys back
 * A group with nothing on screen right now (an empty kill feed, no prompt) has no outline; the help panel names them.
 *
 * It opens from the match menu's Edit HUD layout entry, the console's scct.HudEdit [on|off] (no argument: toggles), or
 * -ScctHudEdit. While open it holds the keys and the mouse as the chat composer does (the player's controller is told
 * a menu is open, so everything held is let go). It does not open while a menu or the chat has the keys.
 *
 * Unattended switches (for runs with nobody at the keyboard):
 *   -ScctHudEdit                    open the edit mode -ScctHudEditAt seconds into the match (default 3)
 *   -ScctHudEditDrag=<group>:DX,DY[,W]  then a second later drag that group by DX,DY units of the safe area and turn
 *                                   the wheel W steps over it, through the code the mouse runs (snapping included)
 *   -ScctHudEditExitAt=S            leave (and save) S seconds into the match
 * Every open, drag, size, hide, reset and save writes a "ScctHudEdit:" line to the log.
 */
UCLASS()
class OPENSCCT_API UScctHudEditSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	bool IsEditing() const { return bEditing; }

	/** Opens the edit mode; false when it cannot now (no match HUD, a menu or the chat has the keys). */
	bool Start();
	/** Saves the layout and closes. */
	void Stop();

	/** The side the HUD is drawn for (0 spy, 1 merc): the layout being edited. */
	int32 GetSide() const;

	// UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void RemoveOverlay();
	void RunUnattended();

	bool bEditing = false;
	double Clock = 0.0;
	TSharedPtr<SScctHudEditOverlay> Overlay;
	TWeakObjectPtr<AScctPlayerController> InputHolder;

	// The unattended switches, timed from the subsystem's start.
	bool bDebugOpen = false;
	float DebugAt = 3.0f;
	FString DebugDragGroup;
	FVector2f DebugDrag = FVector2f::ZeroVector;
	int32 DebugWheel = 0;
	float DebugExitAt = -1.0f;
	int32 DebugStep = 0;
};
