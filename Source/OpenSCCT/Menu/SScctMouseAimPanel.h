#pragma once

#include "CoreMinimal.h"
#include "Play/ScctControlSettings.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

/**
 * The mouse and aim settings the Options screen edits, as one value: both sides' mouse and controller look groups,
 * the inversions, the mouse scale, the wheel's job and the two clutch keys. A copy of what UScctControlSettings
 * holds (and the bridge, for the clutch keys), so a draft can be compared with what is in play and with the file.
 */
struct FScctMouseAimValues
{
	FScctLookOptions SpyMouse;
	FScctLookOptions MercMouse;
	FScctLookOptions SpyPad;
	FScctLookOptions MercPad;
	bool bInvertY = false;
	bool bInvertXSpy = false;
	bool bSpyRollFreeLook = false;
	bool bMouseWheelSpeed = false;
	float MouseScale = 1.0f;
	FString ClutchOneKey;
	FString ClutchTwoKey;

	/** What UScctControlSettings (and the bridge's clutch keys) hold now. */
	static FScctMouseAimValues From(UScctControlSettings& Settings);

	/** Writes these values into the properties; the clutch keys are set in the bridge at once. */
	void To(UScctControlSettings& Settings) const;

	bool Equals(const FScctMouseAimValues& Other) const;

	FScctLookOptions& Mouse(bool bMerc) { return bMerc ? MercMouse : SpyMouse; }
	FScctLookOptions& Pad(bool bMerc) { return bMerc ? MercPad : SpyPad; }
};

/** How a field of the section is edited. */
enum class EScctMouseAimKind : uint8
{
	Sensitivity, // a multiplier: slider and an exact value
	Toggle,      // a box to tick
	HoldToggle,  // a bool shown as Toggle / Hold
	Wheel,       // a bool shown as the wheel's two jobs
	Response,    // a look response curve
	Key,         // a clutch key, captured
};

/**
 * One setting of the section: its name (what -ScctMouseAimEdit and -ScctLookTest call it), its label and tooltip
 * on each side, which sides show it, and where its value is in FScctMouseAimValues. The panel draws its rows from
 * these and the unattended switches set values through them, so both go the same way.
 */
struct FScctMouseAimField
{
	FString Name;
	FString Group;
	FString SpyLabel;
	FString MercLabel;
	FString Tooltip;
	EScctMouseAimKind Kind = EScctMouseAimKind::Toggle;
	bool bSpy = true;
	bool bMerc = true;
	/** Both sides read the one value (invert Y, mouse scale, the wheel, the first clutch key). */
	bool bShared = false;
	float Min = 0.05f;
	float Max = 10.0f;
	/** The slider's top; a typed value may go up to Max. */
	float SliderMax = 10.0f;

	TFunction<float*(FScctMouseAimValues&, bool bMerc)> Float;
	TFunction<bool*(FScctMouseAimValues&, bool bMerc)> Bool;
	TFunction<EScctLookResponse*(FScctMouseAimValues&, bool bMerc)> Curve;
	TFunction<FString*(FScctMouseAimValues&, bool bMerc)> Text;

	const FString& Label(bool bForMerc) const { return bForMerc ? MercLabel : SpyLabel; }
	bool IsOn(bool bForMerc) const { return bForMerc ? bMerc : bSpy; }
	/** The field's value in Values as text ("1.40", "true", "Hold", "CurveSoft", "Ctrl+V"). */
	FString Show(const FScctMouseAimValues& Values, bool bForMerc) const;
};

/**
 * The mouse and aim section's model: one draft for both pages, held with the Options screen's other edits. APPLY
 * hands it to UScctControlSettings::Apply (the bridge clamps it and a running match takes it on its next frame,
 * as ScctInvertY and ScctMouseScale do), SAVE also writes Saved/Controls/controls.json, and leaving without saving
 * puts back what the file had.
 */
namespace ScctMouseAim
{
	/** Every field, in the order the panel draws them. */
	const TArray<FScctMouseAimField>& Fields();
	const FScctMouseAimField* FindField(const FString& Name);

	/** Starts editing from what is in play, unless edits are already held (a rebuilt screen). */
	void Begin();
	FScctMouseAimValues& Draft();
	FScctMouseAimValues InPlay();

	/**
	 * Sets a field of the draft by name for a side, from text as Show writes it ("2.0", "true", "Hold", "Speed",
	 * "CurveSoft", a key name or "" for none); a number is held to the field's range. False with the reason.
	 */
	bool SetField(bool bMerc, const FString& Name, const FString& Value, FString& OutError);

	bool HasEdits();
	bool HasUnsaved();
	void Apply();
	void Save();
	void Discard();
	/** Puts what the side's page shows back to the defaults in the draft (the shared settings included). */
	void ResetSide(bool bMerc);

	/** One line of a side's settings in play, for the log. */
	FString Describe(bool bMerc);
}

/**
 * Mouse and aim: the first section of the Spy and Mercenary options pages (Godot OptionsMenu.MouseAim.cs and the
 * controller's per-side tuning). Grouped as the Godot client groups them:
 *   Sensitivity      free look (spy), look or gun look, vertical, invert Y (shared), invert X (spy), mouse scale (shared)
 *   Scope & cameras  binoculars or scope, the scope's vertical multiplier (merc), remote cameras
 *   Modifiers        the clutches: on or off, the key held (captured as a binding is), and the strength
 *   Hold or toggle   walk, the sniper scope (merc), first person (spy), cloak (spy)
 *   Wheel & view     what the mouse wheel does (shared), free camera while rolling (spy)
 *   Controller       the side's look response curves and the stick-click / bumper swaps
 * A sensitivity has a slider and a box for an exact value. An edited row's label is lit until applied; RESET MOUSE
 * asks, then puts the page's settings back to the defaults.
 *
 * Unattended switches (with -ScctOptions=Spy or Mercenary):
 *   -ScctMouseAimEdit=<side>,<field>,<value>[;...]  edit fields as if the player had, when the page opens (not applied)
 * and in a match (Menu/ScctMouseAimTest.h): -ScctLookTest=<side>,<field>,<value>.
 */
class SScctMouseAimPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SScctMouseAimPanel) : _bMerc(false) {}
		SLATE_ARGUMENT(bool, bMerc)
	SLATE_END_ARGS()

	/** Registers the panel as a section of the Spy and Mercenary pages (once). */
	static void RegisterSections();

	void Construct(const FArguments& InArgs);

	/** Draws the rows again from the draft. */
	void Refresh();

private:
	TSharedRef<SWidget> MakeControl(const FScctMouseAimField& Field);
	void AskReset();
	void CaptureClutch(const FScctMouseAimField& Field);

	bool bMerc = false;
	TSharedPtr<SVerticalBox> List;
	FString Status;
};
