#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ScctControlSettings.generated.h"

struct FScctControlSettings;
struct FScctLookSettings;

/** How a look stick's push becomes speed (the original's response curves). */
UENUM(BlueprintType)
enum class EScctLookResponse : uint8
{
	Linear = 0,
	CurveSoft = 1,
	CurveMedium = 2,
	CurveAggressive = 3,
};

/**
 * The look settings of one side on one device (spy or merc, mouse or controller). A multiplier of 1 is the
 * original game's speed. The values here are only what Load last read: the defaults live in the bridge.
 */
USTRUCT(BlueprintType)
struct FScctLookOptions
{
	GENERATED_BODY()

	/** Held (true) or toggled (false): the walk key, the merc's scope, the spy's first person view, the spy's cloak. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	bool bWalkHold = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	bool bScopeHold = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	bool bPerspectiveHold = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	bool bCloakHold = false;

	/** Spy: the look with the gun holstered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	float FreeLook = 1.0f;

	/** Spy: the look with the gun drawn. Merc: the look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	float Look = 1.0f;

	/** Binoculars or sniper scope, times the look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	float Scope = 1.0f;

	/** Merc: the sniper scope's vertical scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	float ScopeVertical = 1.0f;

	/** Kept for the settings file; the original's look does not read it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	float Vertical = 1.0f;

	/** Sticky camera and camera network. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	float Cam = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	EScctLookResponse LookResponse = EScctLookResponse::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	EScctLookResponse GunLookResponse = EScctLookResponse::Linear;

	/** A held clutch multiplies the look by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	float ClutchOne = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	bool bClutchOneEnabled = false;

	/** The merc's second clutch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	float ClutchTwo = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	bool bClutchTwoEnabled = false;

	/** Controller: the stick click and the bumper of that side trade places. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	bool bSwapLsLb = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCCT|Controls")
	bool bSwapRsRb = false;
};

/**
 * The player's control settings and key bindings, as Unreal sees them: sensitivity, inversions, the controller's
 * tuning, and what every key and pad button does on each side.
 *
 * One object for the process: UScctControlSettings::Get(). The player controller loads it before the first frame
 * of input; a settings menu changes the properties and calls Save, a bindings menu uses the binding calls and
 * SaveBindings.
 *
 * WHERE IT IS KEPT, AND WHY NOT IN AN INI. The bridge's controls own these values (they clamp them, hold the
 * defaults and apply them to the simulation's mouse look), and they read and write two JSON files with the same
 * field names as OpenSCCT's own settings, so a player's OpenSCCT settings carry over:
 *   <project>/Saved/Controls/controls.json   the settings (and the clutch keys, which only this file holds)
 *   <project>/Saved/Controls/bindings.json   both sides' key and pad bindings
 * This object is the Unreal mirror of the first file, filled through the controls table; it deliberately has no
 * config properties of its own, so there is never a second copy on disk to disagree with. It is a plain UObject
 * (used through its class default object, as developer settings are) and not a UDeveloperSettings, which would
 * add a module dependency for a project-settings page that could not be saved to an ini anyway.
 *
 * Every call needs the bridge (it is started if it is not up); a call that fails returns false and GetLastError
 * says why.
 */
UCLASS()
class OPENSCCT_API UScctControlSettings : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "OpenSCCT|Controls", meta = (DisplayName = "Get OpenSCCT Control Settings"))
	static UScctControlSettings* Get();

	/**
	 * Loads the settings and the bindings once per run; later calls do nothing. False when a file was refused.
	 * Every call below that hands something to the bridge or writes a file does this first, so nothing can act on
	 * (or save) the defaults in place of the player's files.
	 */
	bool EnsureLoaded();

	/** Reads controls.json (the defaults when there is none) and fills the properties from it. */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	bool Load();

	/**
	 * Hands the properties to the bridge, which clamps them, and reads the clamped values back. A match that is
	 * playing takes them on its next frame. Nothing is written to disk.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	bool Apply();

	/** Apply, then writes controls.json. */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	bool Save();

	/** Fills the properties from what the bridge holds now (after it changed a setting itself: ToggleAutoHack). */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	bool Refresh();

	/** Reads bindings.json (the default bindings when there is none). */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	bool LoadBindings();

	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	bool SaveBindings();

	/**
	 * What a key or pad control does. A key is named as bindings name it ("W", "Shift", "LeftMouse", "Ctrl+F",
	 * "Double+H") and its command is in the original's grammar ("Button bFire | Function Fire"). A pad control is
	 * "A", "LB+Y" or "B:Hold" and its action "Action:<id>", "Pad:<contextual>", "Clutch1", "Clutch2" or
	 * "Key:<key>". Empty when unbound.
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	FString GetBinding(bool bMerc, bool bPad, const FString& Key) const;

	/** Binds a key or pad control for one side; an empty command unbinds it. False for a name that is neither. */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	bool SetBinding(bool bMerc, bool bPad, const FString& Key, const FString& Command);

	/** Puts one side's keys and pad controls back to the defaults (not saved until SaveBindings). */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	void ResetBindings(bool bMerc);

	/** Every binding, as the JSON SaveBindings writes (for a bindings menu to list). */
	UFUNCTION(BlueprintCallable, Category = "OpenSCCT|Controls")
	FString GetBindingsJson() const;

	/** Why the last call here that returned false did. */
	UFUNCTION(BlueprintPure, Category = "OpenSCCT|Controls")
	const FString& GetLastError() const { return LastError; }

	/** <project>/Saved/Controls/controls.json and bindings.json. */
	static FString GetSettingsFile();
	static FString GetBindingsFile();

	/** Up and down look the other way round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bInvertY = false;

	/** The spy's free look turns the other way left and right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bInvertXSpy = false;

	/** The spy's camera may turn during a roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bSpyRollFreeLook = false;

	/** A multiplier over every mouse count; 1 is the original's turn per count. For mice whose counts differ. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	float MouseScale = 1.0f;

	/** Keep the 4:3 picture's height and add width (true), or stretch the 4:3 picture (false). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bWidescreen = true;

	/** The cap on the horizontal field of view, degrees (95 to 112). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	float MercFovCap = 112.0f;

	/** Hack enemy communications without holding the key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys")
	bool bAutoHackComms = false;

	/** The mouse wheel sets walk and run (true) or changes gadgets (false). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys")
	bool bMouseWheelSpeed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FScctLookOptions SpyMouse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FScctLookOptions MercMouse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	FScctLookOptions SpyPad;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	FScctLookOptions MercPad;

	/** The controller plays at all; then: its left stick moves, its right stick looks, its buttons act. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	bool bPadEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	bool bPadMove = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	bool bPadAim = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	bool bPadBinds = true;

	/** A part push of the left stick walks slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	bool bPadSlowWalk = true;

	/** Snap movement to eight directions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	bool bPadSpyMove8 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	bool bPadMercMove8 = false;

	/** Dead travel at the centre of each stick, 0 to 0.9. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	float PadDeadzoneLeft = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	float PadDeadzoneRight = 0.15f;

	/** The look stick's vertical speed against its sideways speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	float PadVerticalScale = 0.75f;

	/** The spy runs with the move stick at least this far out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	float PadRunThreshold = 0.95f;

	/** How far a trigger goes down before it counts as pressed, 0 to 0.99. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	float PadTriggerThreshold = 0.234f;

	/** A multiplier over the look stick's speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controller")
	float PadLookScale = 1.0f;

private:
	void ToBridge(FScctControlSettings& Out) const;
	void FromBridge(const FScctControlSettings& In);

	/** Records the controls table's last error (or Fallback when it has none) and returns false. */
	bool FailFromBridge(const TCHAR* Fallback);

	FString LastError;
	bool bLoaded = false;
};
