#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScctProfileOptions.generated.h"

/**
 * The player's choices the simulation reads in play, and his saved gear (gaps 130 and 169).
 *
 * The bridge installs its level services when it loads (Bridge/OpenScct.UnrealBridge/Loadout, the Godot client's
 * GearServices): every spawn of the local player reads the saved kit, the server's answer to a gear change is shown
 * as a HUD message line, and the simulation asks a profile for its options. This subsystem feeds them:
 *
 *   Saved/Controls/loadout.json   the saved kit (beside controls.json), named to the bridge and read once a run
 *   Audio.ObjectiveSounds         "Objective sounds" on the Audio page: the objective jingles play
 *   SelectedMouse                 what the mouse wheel does follows the controls' wheel setting
 *                                 (UScctControlSettings::bMouseWheelSpeed, which the bridge's controls hold)
 *   OfflineSession                on for a local match (its zone tips show), off for a round on a remote server
 *   DisplayHelpInGame             on for a Visit a Map play: the level URL's ?Help option (the front end has no
 *                                 Visit a Map entry yet), or -ScctHelpInGame
 *
 * Help paths read DisplayHelpInGame when the level loads, so the options go to the bridge as the world begins play,
 * before the game mode starts the match.
 *
 * Each frame of a match it logs the kit the local pawn ends up with ("ScctLoadout: installed spy kit [...]") and the
 * server's answers to gear changes. For unattended checks:
 *   -ScctLoadout=spy:SBase.SGadFlashBang,Svm.CloneGadget,...;merc:...;weapon:<path>;sidearm:<id>
 *       sets the saved kit (the slots named, in order) and saves loadout.json before the match starts;
 *   -ScctGearChange=<seconds>[,spy:...;merc:...]  that long into the match, sets the kit given (if any), saves it and
 *       asks the server to put it on now, as the gear picker's OK does.
 */
UCLASS()
class OPENSCCT_API UScctProfileOptions : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Hands the current choices to the bridge (also on each change of an option this owns). */
	void PushOptions();

	/** <project>/Saved/Controls/loadout.json. */
	static FString GetLoadoutFile();

	/**
	 * Applies a kit text ("spy:A,B,C,D;merc:...;weapon:W;sidearm:S") onto the saved kit, puts it in use and saves it.
	 * Returns false (with the reason in the log) when the bridge refused it.
	 */
	static bool ApplyLoadoutText(const FString& Text);

private:
	void OnSettingChanged(FName Key);
	void OnFrame(float DeltaSeconds);
	void OnSessionChanged(int32 OldId, int32 NewId);

	FDelegateHandle SettingChangedHandle;
	FDelegateHandle FrameHandle;
	FDelegateHandle SessionChangedHandle;

	bool bOffline = true;
	bool bHelpInGame = false;

	/** The last kit logged, and the one seen while it settles. */
	FString LoggedKit;
	FString SettlingKit;
	float SettlingFor = 0.0f;
	bool bLoggedOptions = false;
	int32 LoggedTipId = 0;

	float MatchSeconds = 0.0f;
	float GearChangeAt = -1.0f;
	FString GearChangeKit;
};
