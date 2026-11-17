#include "Menu/SScctMouseAimPanel.h"

#include "Menu/SScctBindingsPanel.h"
#include "Menu/SScctConfirm.h"
#include "Menu/SScctOptionsPage.h"
#include "Menu/ScctMenuStyle.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "UObject/Package.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	constexpr float MouseAimLabelShare = 0.42f;

	const TCHAR* MouseAimSide(bool bMerc) { return bMerc ? TEXT("merc") : TEXT("spy"); }

	FLinearColor MouseAimColour(bool bMerc) { return ScctHudStyle::Accent(bMerc ? 1 : 0); }

	const TCHAR* const MouseAimResponseNames[] = { TEXT("Linear"), TEXT("CurveSoft"), TEXT("CurveMedium"), TEXT("CurveAggressive") };
	const TCHAR* const MouseAimResponseLabels[] = { TEXT("Linear"), TEXT("Soft curve"), TEXT("Medium curve"), TEXT("Aggressive curve") };

	bool SameLookOptions(const FScctLookOptions& A, const FScctLookOptions& B)
	{
		const auto Near = [](float X, float Y) { return FMath::IsNearlyEqual(X, Y, 1e-4f); };
		return A.bWalkHold == B.bWalkHold && A.bScopeHold == B.bScopeHold && A.bPerspectiveHold == B.bPerspectiveHold && A.bCloakHold == B.bCloakHold
			&& Near(A.FreeLook, B.FreeLook) && Near(A.Look, B.Look) && Near(A.Scope, B.Scope) && Near(A.ScopeVertical, B.ScopeVertical)
			&& Near(A.Vertical, B.Vertical) && Near(A.Cam, B.Cam) && A.LookResponse == B.LookResponse && A.GunLookResponse == B.GunLookResponse
			&& Near(A.ClutchOne, B.ClutchOne) && A.bClutchOneEnabled == B.bClutchOneEnabled && Near(A.ClutchTwo, B.ClutchTwo)
			&& A.bClutchTwoEnabled == B.bClutchTwoEnabled && A.bSwapLsLb == B.bSwapLsLb && A.bSwapRsRb == B.bSwapRsRb;
	}

	/** A multiplier as the rows show it: two decimals. */
	FString FormatMouseAimMultiplier(float Value)
	{
		return FString::Printf(TEXT("%.2f"), Value);
	}

	/** The draft, and the values the file had when last read or written (what leaving without saving goes back to). */
	TOptional<FScctMouseAimValues>& MouseAimDraftSlot()
	{
		static TOptional<FScctMouseAimValues> Held;
		return Held;
	}

	TOptional<FScctMouseAimValues>& MouseAimSavedSlot()
	{
		static TOptional<FScctMouseAimValues> Held;
		return Held;
	}

	UScctControlSettings& MouseAimSettings()
	{
		UScctControlSettings* Found = UScctControlSettings::Get();
		Found->EnsureLoaded();
		return *Found;
	}

	/** What the file has: what was in play when the section was first used (just after the files were read), or saved since. */
	const FScctMouseAimValues& MouseAimSaved()
	{
		if (!MouseAimSavedSlot().IsSet())
		{
			MouseAimSavedSlot() = FScctMouseAimValues::From(MouseAimSettings());
		}
		return MouseAimSavedSlot().GetValue();
	}

	/** Puts Values in play: the properties to the bridge, which clamps them; a running match takes them next frame. */
	void PutMouseAimInPlay(const FScctMouseAimValues& Values)
	{
		UScctControlSettings& Controls = MouseAimSettings();
		const bool bWheelChanged = Controls.bMouseWheelSpeed != Values.bMouseWheelSpeed;
		// The wheel's job is written into the key layouts in play, so key edits waiting to be applied go first:
		// otherwise applying them after would bring back the wheel's old commands.
		if (bWheelChanged && ScctBindings::HasEdits())
		{
			ScctBindings::Apply();
		}
		Values.To(Controls);
		if (!Controls.Apply())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("MouseAim: the settings could not be applied: %s"), *Controls.GetLastError());
		}
		if (bWheelChanged)
		{
			// The bridge rewrote the wheel's bindings; what shows keys looks them up again.
			Controls.NotifyRebound();
		}
	}

	FScctMouseAimField MakeMouseAimField(const TCHAR* Name, const TCHAR* Group, const TCHAR* SpyLabel, const TCHAR* MercLabel, const TCHAR* Tooltip, EScctMouseAimKind Kind)
	{
		FScctMouseAimField Field;
		Field.Name = Name;
		Field.Group = Group;
		Field.SpyLabel = SpyLabel;
		Field.MercLabel = MercLabel;
		Field.Tooltip = Tooltip;
		Field.Kind = Kind;
		Field.bSpy = SpyLabel[0] != TEXT('\0');
		Field.bMerc = MercLabel[0] != TEXT('\0');
		return Field;
	}

	TArray<FScctMouseAimField> BuildMouseAimFields()
	{
		TArray<FScctMouseAimField> Fields;
		using EKind = EScctMouseAimKind;
		const auto Sens = [&Fields](const TCHAR* Name, const TCHAR* Group, const TCHAR* Spy, const TCHAR* Merc, const TCHAR* Tip, float FScctLookOptions::* Member)
		{
			FScctMouseAimField& Field = Fields.Add_GetRef(MakeMouseAimField(Name, Group, Spy, Merc, Tip, EKind::Sensitivity));
			Field.Float = [Member](FScctMouseAimValues& V, bool bMerc) { return &(V.Mouse(bMerc).*Member); };
		};
		const auto LookBool = [&Fields](const TCHAR* Name, const TCHAR* Group, const TCHAR* Spy, const TCHAR* Merc, const TCHAR* Tip, EKind Kind, bool FScctLookOptions::* Member, bool bPad = false)
		{
			FScctMouseAimField& Field = Fields.Add_GetRef(MakeMouseAimField(Name, Group, Spy, Merc, Tip, Kind));
			Field.Bool = [Member, bPad](FScctMouseAimValues& V, bool bMerc) { return &((bPad ? V.Pad(bMerc) : V.Mouse(bMerc)).*Member); };
		};
		const auto Global = [&Fields](const TCHAR* Name, const TCHAR* Group, const TCHAR* Spy, const TCHAR* Merc, const TCHAR* Tip, EKind Kind, bool FScctMouseAimValues::* Member)
		{
			FScctMouseAimField& Field = Fields.Add_GetRef(MakeMouseAimField(Name, Group, Spy, Merc, Tip, Kind));
			Field.bShared = Field.bSpy && Field.bMerc;
			Field.Bool = [Member](FScctMouseAimValues& V, bool) { return &(V.*Member); };
		};

		const TCHAR* const Sensitivity = TEXT("Sensitivity");
		Sens(TEXT("FreeLook"), Sensitivity, TEXT("Free look sensitivity"), TEXT(""),
			TEXT("Mouse sensitivity with the gun holstered. Higher values turn farther for the same mouse movement."), &FScctLookOptions::FreeLook);
		Sens(TEXT("Look"), Sensitivity, TEXT("Gun look sensitivity"), TEXT("Look sensitivity"),
			TEXT("Mouse sensitivity for aiming (the spy: with the gun drawn). Higher values turn farther for the same mouse movement. Type an exact value beside the slider."),
			&FScctLookOptions::Look);
		Sens(TEXT("Vertical"), Sensitivity, TEXT("Vertical sensitivity"), TEXT("Vertical sensitivity"),
			TEXT("Kept with the side's look settings in the settings file, as the original keeps it. The original's mouse look does not read it, so it does not change how far the view tilts."),
			&FScctLookOptions::Vertical);
		Global(TEXT("InvertY"), Sensitivity, TEXT("Invert vertical mouse movement"), TEXT("Invert vertical mouse movement"),
			TEXT("Reverse vertical mouse movement. This setting is shared by the spy and the mercenary."), EKind::Toggle, &FScctMouseAimValues::bInvertY);
		Global(TEXT("InvertX"), Sensitivity, TEXT("Invert horizontal free look"), TEXT(""),
			TEXT("Reverse horizontal mouse movement while the gun is holstered."), EKind::Toggle, &FScctMouseAimValues::bInvertXSpy);
		{
			FScctMouseAimField& Field = Fields.Add_GetRef(MakeMouseAimField(TEXT("MouseScale"), Sensitivity, TEXT("Mouse scale"), TEXT("Mouse scale"),
				TEXT("A multiplier over every mouse count, shared by both sides; 1 is the original's turn per count. For a mouse whose counts differ from the original's. Type up to 100 beside the slider."),
				EKind::Sensitivity));
			Field.bShared = true;
			Field.Min = 0.01f;
			Field.Max = 100.0f;
			Field.SliderMax = 5.0f;
			Field.Float = [](FScctMouseAimValues& V, bool) { return &V.MouseScale; };
		}

		const TCHAR* const Scope = TEXT("Scope & cameras");
		Sens(TEXT("Scope"), Scope, TEXT("Binocular sensitivity"), TEXT("Scope sensitivity"),
			TEXT("Multiplies the look sensitivity while looking through the binoculars (spy) or the sniper scope (mercenary)."), &FScctLookOptions::Scope);
		Sens(TEXT("ScopeVertical"), Scope, TEXT(""), TEXT("Scope vertical multiplier"),
			TEXT("The sniper scope's vertical speed: 1 is the original's; 2.45 makes the scope move as far up and down as sideways."), &FScctLookOptions::ScopeVertical);
		Sens(TEXT("Cam"), Scope, TEXT("Remote camera sensitivity"), TEXT("Remote camera sensitivity"),
			TEXT("Mouse sensitivity in sticky cameras and the camera network (spy) or remote camera views (mercenary)."), &FScctLookOptions::Cam);

		const TCHAR* const Modifiers = TEXT("Modifiers");
		const auto Clutch = [&](bool bSecond)
		{
			const TCHAR* Prefix = bSecond ? TEXT("ClutchTwo") : TEXT("ClutchOne");
			const TCHAR* SpyName = bSecond ? TEXT("") : TEXT("sensitivity modifier");
			const TCHAR* MercName = bSecond ? TEXT("second modifier") : TEXT("sensitivity modifier");
			const FString SpyEnable = bSecond ? FString() : FString::Printf(TEXT("Enable %s"), SpyName);
			const FString MercEnable = FString::Printf(TEXT("Enable %s"), MercName);
			LookBool(*FString::Printf(TEXT("%sEnabled"), Prefix), Modifiers, *SpyEnable, *MercEnable,
				TEXT("A temporary sensitivity multiplier while its key is held."), EKind::Toggle,
				bSecond ? &FScctLookOptions::bClutchTwoEnabled : &FScctLookOptions::bClutchOneEnabled);

			FScctMouseAimField& Key = Fields.Add_GetRef(MakeMouseAimField(*FString::Printf(TEXT("%sKey"), Prefix), Modifiers,
				bSecond ? TEXT("") : TEXT("Sensitivity modifier key"), bSecond ? TEXT("Second modifier key") : TEXT("Sensitivity modifier key"),
				bSecond ? TEXT("Hold this key to apply the second multiplier. Both mercenary modifiers multiply together.")
					: TEXT("Hold this key to apply the multiplier. The key is shared by both sides; each side has its own strength."),
				EKind::Key));
			Key.bShared = !bSecond;
			Key.Text = [bSecond](FScctMouseAimValues& V, bool) { return bSecond ? &V.ClutchTwoKey : &V.ClutchOneKey; };

			FScctMouseAimField& Strength = Fields.Add_GetRef(MakeMouseAimField(Prefix, Modifiers,
				bSecond ? TEXT("") : TEXT("Sensitivity modifier strength"), bSecond ? TEXT("Second modifier strength") : TEXT("Sensitivity modifier strength"),
				TEXT("While the key is held: 0.5 halves the sensitivity, 1 keeps it and 2 doubles it. 0 does nothing."), EKind::Sensitivity));
			Strength.Min = 0.0f;
			Strength.Float = [bSecond](FScctMouseAimValues& V, bool bMerc) { return bSecond ? &V.Mouse(bMerc).ClutchTwo : &V.Mouse(bMerc).ClutchOne; };
		};
		Clutch(false);
		Clutch(true);

		const TCHAR* const Hold = TEXT("Hold or toggle");
		LookBool(TEXT("WalkHold"), Hold, TEXT("Walk"), TEXT("Walk"),
			TEXT("Hold: walk while the walk key is held. Toggle: each press switches between walking and running. Wheel bindings always toggle."),
			EKind::HoldToggle, &FScctLookOptions::bWalkHold);
		LookBool(TEXT("ScopeHold"), Hold, TEXT(""), TEXT("Sniper scope"),
			TEXT("Hold: look through the sniper scope while the key is held. Toggle: each press goes in or out. Wheel bindings always toggle."),
			EKind::HoldToggle, &FScctLookOptions::bScopeHold);
		LookBool(TEXT("PerspectiveHold"), Hold, TEXT("First person view"), TEXT(""),
			TEXT("Hold: first person until the key is let go. Toggle: each press switches the view. Also used by the controller."),
			EKind::HoldToggle, &FScctLookOptions::bPerspectiveHold);
		LookBool(TEXT("CloakHold"), Hold, TEXT("Cloak"), TEXT(""),
			TEXT("Hold: the camo suit is on while the key is held. Toggle: each press switches it. Also used by the controller."),
			EKind::HoldToggle, &FScctLookOptions::bCloakHold);

		const TCHAR* const View = TEXT("Wheel & view");
		Global(TEXT("MouseWheel"), View, TEXT("Mouse wheel"), TEXT("Mouse wheel"),
			TEXT("What the mouse wheel does, for both sides: change gadgets, or set walking and running. Wheel directions still on the other job's commands are moved over."),
			EKind::Wheel, &FScctMouseAimValues::bMouseWheelSpeed);
		Global(TEXT("RollFreeLook"), View, TEXT("Free camera while rolling"), TEXT(""),
			TEXT("Look around with the mouse or controller while rolling. The roll keeps its starting direction."), EKind::Toggle, &FScctMouseAimValues::bSpyRollFreeLook);

		const TCHAR* const Controller = TEXT("Controller");
		{
			FScctMouseAimField& Field = Fields.Add_GetRef(MakeMouseAimField(TEXT("LookResponse"), Controller, TEXT("Free look response"), TEXT("Look response"),
				TEXT("How the look stick's push becomes speed: linear, or a curve that is slower near the centre for fine aim."), EKind::Response));
			Field.Curve = [](FScctMouseAimValues& V, bool bMerc) { return &V.Pad(bMerc).LookResponse; };
		}
		{
			FScctMouseAimField& Field = Fields.Add_GetRef(MakeMouseAimField(TEXT("GunLookResponse"), Controller, TEXT("Gun look response"), TEXT(""),
				TEXT("The look stick's response with the gun drawn, apart from free look."), EKind::Response));
			Field.Curve = [](FScctMouseAimValues& V, bool bMerc) { return &V.Pad(bMerc).GunLookResponse; };
		}
		LookBool(TEXT("SwapLsLb"), Controller, TEXT("Swap left stick click and left bumper"), TEXT("Swap left stick click and left bumper"),
			TEXT("The left stick click and the left bumper trade places for this side."), EKind::Toggle, &FScctLookOptions::bSwapLsLb, true);
		LookBool(TEXT("SwapRsRb"), Controller, TEXT("Swap right stick click and right bumper"), TEXT("Swap right stick click and right bumper"),
			TEXT("The right stick click and the right bumper trade places for this side."), EKind::Toggle, &FScctLookOptions::bSwapRsRb, true);
		return Fields;
	}
}

// ---- The values ----------------------------------------------------------------------------------------------------

FScctMouseAimValues FScctMouseAimValues::From(UScctControlSettings& Settings)
{
	FScctMouseAimValues Values;
	Values.SpyMouse = Settings.SpyMouse;
	Values.MercMouse = Settings.MercMouse;
	Values.SpyPad = Settings.SpyPad;
	Values.MercPad = Settings.MercPad;
	Values.bInvertY = Settings.bInvertY;
	Values.bInvertXSpy = Settings.bInvertXSpy;
	Values.bSpyRollFreeLook = Settings.bSpyRollFreeLook;
	Values.bMouseWheelSpeed = Settings.bMouseWheelSpeed;
	Values.MouseScale = Settings.MouseScale;
	Values.ClutchOneKey = Settings.GetClutchKey(1);
	Values.ClutchTwoKey = Settings.GetClutchKey(2);
	return Values;
}

void FScctMouseAimValues::To(UScctControlSettings& Settings) const
{
	Settings.SpyMouse = SpyMouse;
	Settings.MercMouse = MercMouse;
	Settings.SpyPad = SpyPad;
	Settings.MercPad = MercPad;
	Settings.bInvertY = bInvertY;
	Settings.bInvertXSpy = bInvertXSpy;
	Settings.bSpyRollFreeLook = bSpyRollFreeLook;
	Settings.bMouseWheelSpeed = bMouseWheelSpeed;
	Settings.MouseScale = MouseScale;
	for (int32 Which = 1; Which <= 2; ++Which)
	{
		const FString& Key = Which == 1 ? ClutchOneKey : ClutchTwoKey;
		if (Settings.GetClutchKey(Which) != Key && !Settings.SetClutchKey(Which, Key))
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("MouseAim: clutch %d key '%s' refused: %s"), Which, *Key, *Settings.GetLastError());
		}
	}
}

bool FScctMouseAimValues::Equals(const FScctMouseAimValues& Other) const
{
	return SameLookOptions(SpyMouse, Other.SpyMouse) && SameLookOptions(MercMouse, Other.MercMouse) && SameLookOptions(SpyPad, Other.SpyPad) && SameLookOptions(MercPad, Other.MercPad)
		&& bInvertY == Other.bInvertY && bInvertXSpy == Other.bInvertXSpy && bSpyRollFreeLook == Other.bSpyRollFreeLook
		&& bMouseWheelSpeed == Other.bMouseWheelSpeed && FMath::IsNearlyEqual(MouseScale, Other.MouseScale, 1e-4f)
		&& ClutchOneKey == Other.ClutchOneKey && ClutchTwoKey == Other.ClutchTwoKey;
}

FString FScctMouseAimField::Show(const FScctMouseAimValues& Values, bool bForMerc) const
{
	FScctMouseAimValues& Mutable = const_cast<FScctMouseAimValues&>(Values);
	switch (Kind)
	{
	case EScctMouseAimKind::Sensitivity:
		return FormatMouseAimMultiplier(*Float(Mutable, bForMerc));
	case EScctMouseAimKind::Toggle:
		return *Bool(Mutable, bForMerc) ? TEXT("true") : TEXT("false");
	case EScctMouseAimKind::HoldToggle:
		return *Bool(Mutable, bForMerc) ? TEXT("Hold") : TEXT("Toggle");
	case EScctMouseAimKind::Wheel:
		return *Bool(Mutable, bForMerc) ? TEXT("Speed") : TEXT("Gadget");
	case EScctMouseAimKind::Response:
		return MouseAimResponseNames[FMath::Clamp(static_cast<int32>(*Curve(Mutable, bForMerc)), 0, 3)];
	case EScctMouseAimKind::Key:
	default:
		return *Text(Mutable, bForMerc);
	}
}

// ---- The model -----------------------------------------------------------------------------------------------------

const TArray<FScctMouseAimField>& ScctMouseAim::Fields()
{
	static const TArray<FScctMouseAimField> All = BuildMouseAimFields();
	return All;
}

const FScctMouseAimField* ScctMouseAim::FindField(const FString& Name)
{
	return Fields().FindByPredicate([&Name](const FScctMouseAimField& Field) { return Field.Name.Equals(Name, ESearchCase::IgnoreCase); });
}

FScctMouseAimValues ScctMouseAim::InPlay()
{
	return FScctMouseAimValues::From(MouseAimSettings());
}

void ScctMouseAim::Begin()
{
	MouseAimSaved();
	if (!MouseAimDraftSlot().IsSet() || !HasEdits())
	{
		MouseAimDraftSlot() = InPlay();
	}
}

FScctMouseAimValues& ScctMouseAim::Draft()
{
	if (!MouseAimDraftSlot().IsSet())
	{
		Begin();
	}
	return MouseAimDraftSlot().GetValue();
}

bool ScctMouseAim::SetField(bool bMerc, const FString& Name, const FString& Value, FString& OutError)
{
	const FScctMouseAimField* Field = FindField(Name);
	if (Field == nullptr)
	{
		OutError = FString::Printf(TEXT("there is no mouse and aim setting '%s'"), *Name);
		return false;
	}
	if (!Field->IsOn(bMerc))
	{
		OutError = FString::Printf(TEXT("the %s has no '%s'"), bMerc ? TEXT("mercenary") : TEXT("spy"), *Field->Name);
		return false;
	}

	FScctMouseAimValues& Values = Draft();
	const FString Text = Value.TrimStartAndEnd();
	switch (Field->Kind)
	{
	case EScctMouseAimKind::Sensitivity:
		if (!Text.IsNumeric())
		{
			OutError = FString::Printf(TEXT("'%s' is not a number"), *Text);
			return false;
		}
		*Field->Float(Values, bMerc) = FMath::Clamp(FCString::Atof(*Text), Field->Min, Field->Max);
		break;
	case EScctMouseAimKind::Toggle:
		*Field->Bool(Values, bMerc) = Text.ToBool();
		break;
	case EScctMouseAimKind::HoldToggle:
		*Field->Bool(Values, bMerc) = Text.Equals(TEXT("Hold"), ESearchCase::IgnoreCase) || Text.ToBool();
		break;
	case EScctMouseAimKind::Wheel:
		*Field->Bool(Values, bMerc) = Text.Equals(TEXT("Speed"), ESearchCase::IgnoreCase) || Text.ToBool();
		break;
	case EScctMouseAimKind::Response:
	{
		int32 Index = INDEX_NONE;
		for (int32 Each = 0; Each < UE_ARRAY_COUNT(MouseAimResponseNames); ++Each)
		{
			if (Text.Equals(MouseAimResponseNames[Each], ESearchCase::IgnoreCase) || Text.Equals(MouseAimResponseLabels[Each], ESearchCase::IgnoreCase))
			{
				Index = Each;
			}
		}
		if (Index == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("'%s' is not a response curve (Linear, CurveSoft, CurveMedium, CurveAggressive)"), *Text);
			return false;
		}
		*Field->Curve(Values, bMerc) = static_cast<EScctLookResponse>(Index);
		break;
	}
	case EScctMouseAimKind::Key:
	{
		const FString Refused = MouseAimSettings().CheckClutchKey(Text);
		if (!Refused.IsEmpty())
		{
			OutError = Refused;
			return false;
		}
		*Field->Text(Values, bMerc) = Text;
		break;
	}
	}
	return true;
}

bool ScctMouseAim::HasEdits()
{
	return MouseAimDraftSlot().IsSet() && !MouseAimDraftSlot()->Equals(InPlay());
}

bool ScctMouseAim::HasUnsaved()
{
	return MouseAimSavedSlot().IsSet() && !MouseAimSavedSlot()->Equals(InPlay());
}

void ScctMouseAim::Apply()
{
	if (!HasEdits())
	{
		return;
	}
	PutMouseAimInPlay(MouseAimDraftSlot().GetValue());
	// The bridge's clamped values are what the draft continues from.
	MouseAimDraftSlot() = InPlay();
	UE_LOG(LogOpenSCCT, Log, TEXT("MouseAim: applied; a running match turns with them from its next frame. Spy: %s. Merc: %s"), *Describe(false), *Describe(true));
}

void ScctMouseAim::Save()
{
	Apply();
	if (!HasUnsaved())
	{
		return;
	}
	if (MouseAimSettings().Save())
	{
		MouseAimSavedSlot() = InPlay();
		UE_LOG(LogOpenSCCT, Log, TEXT("MouseAim: saved to %s"), *UScctControlSettings::GetSettingsFile());
	}
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("MouseAim: not saved: %s"), *MouseAimSettings().GetLastError());
	}
}

void ScctMouseAim::Discard()
{
	MouseAimDraftSlot().Reset();
	if (HasUnsaved())
	{
		PutMouseAimInPlay(MouseAimSavedSlot().GetValue());
		UE_LOG(LogOpenSCCT, Log, TEXT("MouseAim: changes thrown away; the saved mouse and aim settings are in play again"));
	}
}

void ScctMouseAim::ResetSide(bool bMerc)
{
	UScctControlSettings* Defaults = NewObject<UScctControlSettings>(GetTransientPackage());
	if (!MouseAimSettings().GetDefaults(*Defaults))
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("MouseAim: no defaults to reset to: %s"), *MouseAimSettings().GetLastError());
		return;
	}
	const FScctMouseAimValues Stock = FScctMouseAimValues::From(*Defaults);
	FScctMouseAimValues& Values = Draft();
	// Everything this side's page shows: the side's mouse group whole, its controller curves and swaps, and the
	// settings both sides share.
	Values.Mouse(bMerc) = bMerc ? Stock.MercMouse : Stock.SpyMouse;
	FScctLookOptions& Pad = Values.Pad(bMerc);
	const FScctLookOptions& StockPad = bMerc ? Stock.MercPad : Stock.SpyPad;
	Pad.LookResponse = StockPad.LookResponse;
	Pad.GunLookResponse = StockPad.GunLookResponse;
	Pad.bSwapLsLb = StockPad.bSwapLsLb;
	Pad.bSwapRsRb = StockPad.bSwapRsRb;
	Values.bInvertY = Stock.bInvertY;
	Values.MouseScale = Stock.MouseScale;
	Values.bMouseWheelSpeed = Stock.bMouseWheelSpeed;
	Values.ClutchOneKey = FString();
	if (bMerc)
	{
		Values.ClutchTwoKey = FString();
	}
	else
	{
		Values.bInvertXSpy = Stock.bInvertXSpy;
		Values.bSpyRollFreeLook = Stock.bSpyRollFreeLook;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("MouseAim: %s mouse and aim edited back to the defaults (not applied yet)"), MouseAimSide(bMerc));
}

FString ScctMouseAim::Describe(bool bMerc)
{
	const FScctMouseAimValues Values = InPlay();
	FString Text;
	for (const FScctMouseAimField& Field : Fields())
	{
		if (Field.IsOn(bMerc))
		{
			Text += FString::Printf(TEXT("%s%s %s"), Text.IsEmpty() ? TEXT("") : TEXT(", "), *Field.Name, *Field.Show(Values, bMerc));
		}
	}
	return Text;
}

// ---- The panel -----------------------------------------------------------------------------------------------------

void SScctMouseAimPanel::RegisterSections()
{
	static bool bRegistered = false;
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;

	for (const bool bSide : { false, true })
	{
		FScctOptionsSection Section;
		Section.Build = [bSide]() -> TSharedRef<SWidget> { return SNew(SScctMouseAimPanel).bMerc(bSide); };
		// The two pages share one draft, so each callback is safe to run twice (the second finds nothing to do).
		Section.HasEdits = [] { return ScctMouseAim::HasEdits(); };
		Section.HasUnsaved = [] { return ScctMouseAim::HasUnsaved(); };
		Section.Apply = [] { ScctMouseAim::Apply(); };
		Section.Save = [] { ScctMouseAim::Save(); };
		Section.Discard = [] { ScctMouseAim::Discard(); };
		Section.ResetToDefaults = [bSide] { ScctMouseAim::ResetSide(bSide); };
		SScctOptionsPage::RegisterSection(bSide ? FName(TEXT("Mercenary")) : FName(TEXT("Spy")), Section);
	}
}

void SScctMouseAimPanel::Construct(const FArguments& InArgs)
{
	bMerc = InArgs._bMerc;
	ScctMouseAim::Begin();

	ChildSlot
	[
		SAssignNew(List, SVerticalBox)
	];

	// -ScctMouseAimEdit=<side>,<field>,<value>[;...]: edits as if the player had made them, when the page first
	// opens (not applied: APPLY, SAVE or -ScctOptionsSave do that). Once per run, for screenshots.
	static bool bEdited = false;
	FString Edits;
	if (!bEdited && FParse::Value(FCommandLine::Get(), TEXT("ScctMouseAimEdit="), Edits, false))
	{
		bEdited = true;
		TArray<FString> Entries;
		Edits.ParseIntoArray(Entries, TEXT(";"));
		for (const FString& Entry : Entries)
		{
			TArray<FString> Parts;
			Entry.ParseIntoArray(Parts, TEXT(","), false);
			FString Error;
			if (Parts.Num() < 3)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctMouseAimEdit: '%s' is not <side>,<field>,<value>"), *Entry);
			}
			else if (ScctMouseAim::SetField(Parts[0].TrimStartAndEnd().Equals(TEXT("merc"), ESearchCase::IgnoreCase), Parts[1].TrimStartAndEnd(), Parts[2], Error))
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("-ScctMouseAimEdit: %s %s edited to %s (not applied)"), *Parts[0].TrimStartAndEnd(), *Parts[1].TrimStartAndEnd(), *Parts[2].TrimStartAndEnd());
			}
			else
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctMouseAimEdit: %s"), *Error);
			}
		}
	}

	Refresh();
}

void SScctMouseAimPanel::Refresh()
{
	List->ClearChildren();
	const FLinearColor Colour = MouseAimColour(bMerc);
	if (ScctFeature<FScctControlsApi>() == nullptr)
	{
		List->AddSlot().AutoHeight()
		[
			Ui::Text(Ui::ToText(TEXT("The mouse settings need the bridge, which is not running.")), 12.0f, FSlateColor(ScctHudStyle::TextDim()))
		];
		return;
	}

	List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Ui::Label(bMerc ? TEXT("Mercenary mouse & aim") : TEXT("Spy mouse & aim"), Colour) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 14.0f, 0.0f)
		[
			Ui::Button(Ui::ToText(TEXT("RESET MOUSE")), FOnClicked::CreateLambda([this] { AskReset(); return FReply::Handled(); }), false, Colour, 120.0f)
		]
	];
	List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		Ui::Text(TAttribute<FText>::CreateLambda([this]
		{
			return Ui::ToText(Status.IsEmpty() ? FString(TEXT("A multiplier of 1 is the original game's speed. Changes take effect when applied, in a match too. Hover a setting for what it does.")) : Status);
		}), 11.5f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
	];

	FString Group;
	for (const FScctMouseAimField& Field : ScctMouseAim::Fields())
	{
		if (!Field.IsOn(bMerc))
		{
			continue;
		}
		if (Field.Group != Group)
		{
			Group = Field.Group;
			List->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 6.0f)[ Ui::Label(Group, Ui::Accent()) ];
		}

		const FString Name = Field.Name;
		const bool bSide = bMerc;
		const FString Tooltip = Field.bShared ? Field.Tooltip + TEXT(" (Shared by both sides.)") : Field.Tooltip;
		List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 14.0f, 5.0f)
		[
			SNew(SBox)
			.MinDesiredHeight(32.0f)
			.ToolTipText(Ui::ToText(Tooltip))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(MouseAimLabelShare).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					// An edited row's label is lit until the edit is applied.
					Ui::Text(Ui::ToText(Field.Label(bMerc)), 13.0f, TAttribute<FSlateColor>::CreateLambda([Name, bSide]
					{
						const FScctMouseAimField* Found = ScctMouseAim::FindField(Name);
						const bool bEdited = Found != nullptr && Found->Show(ScctMouseAim::Draft(), bSide) != Found->Show(ScctMouseAim::InPlay(), bSide);
						return FSlateColor(bEdited ? ScctHudStyle::Accent(1) : ScctHudStyle::Text());
					}))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f - MouseAimLabelShare).VAlign(VAlign_Center)[ MakeControl(Field) ]
			]
		];
	}
}

TSharedRef<SWidget> SScctMouseAimPanel::MakeControl(const FScctMouseAimField& Field)
{
	// The fields live for the whole run; the controls find theirs by name, as the options page does.
	const FString Name = Field.Name;
	const bool bSide = bMerc;
	const auto Find = [Name] { return ScctMouseAim::FindField(Name); };
	const auto Set = [this, Name, bSide](const FString& Value)
	{
		FString Error;
		if (!ScctMouseAim::SetField(bSide, Name, Value, Error))
		{
			Status = Error;
		}
	};

	switch (Field.Kind)
	{
	case EScctMouseAimKind::Sensitivity:
	{
		const auto Current = [Find, bSide]
		{
			const FScctMouseAimField* Found = Find();
			return Found != nullptr ? *Found->Float(ScctMouseAim::Draft(), bSide) : 0.0f;
		};
		const TSharedRef<SEditableTextBox> Exact = Ui::TextBox(FString(), FormatMouseAimMultiplier(Current()), false,
			FOnTextCommitted::CreateLambda([Set](const FText& Text, ETextCommit::Type) { Set(Text.ToString()); }));
		Exact->SetText(TAttribute<FText>::CreateLambda([Current] { return Ui::ToText(FormatMouseAimMultiplier(Current())); }));
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.MinValue(Field.Min)
				.MaxValue(Field.SliderMax)
				.StepSize(0.05f)
				.MouseUsesStep(true)
				.IsFocusable(false)
				.SliderBarColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.25f))
				.SliderHandleColor(MouseAimColour(bMerc))
				.Value(TAttribute<float>::CreateLambda(Current))
				.OnValueChanged(FOnFloatValueChanged::CreateLambda([Set](float Value) { Set(FString::SanitizeFloat(FMath::RoundToFloat(Value * 100.0f) / 100.0f)); }))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox).WidthOverride(76.0f)[ Exact ]
			];
	}

	case EScctMouseAimKind::Toggle:
		return SNew(SCheckBox)
			.IsFocusable(false)
			.IsChecked(TAttribute<ECheckBoxState>::CreateLambda([Find, bSide]
			{
				const FScctMouseAimField* Found = Find();
				return Found != nullptr && *Found->Bool(ScctMouseAim::Draft(), bSide) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}))
			.OnCheckStateChanged(FOnCheckStateChanged::CreateLambda([Set](ECheckBoxState State) { Set(State == ECheckBoxState::Checked ? TEXT("true") : TEXT("false")); }));

	case EScctMouseAimKind::HoldToggle:
	case EScctMouseAimKind::Wheel:
	case EScctMouseAimKind::Response:
	{
		// A choice with arrows either side, as the options page draws one.
		const EScctMouseAimKind Kind = Field.Kind;
		const auto Index = [Find, bSide, Kind]() -> int32
		{
			const FScctMouseAimField* Found = Find();
			if (Found == nullptr)
			{
				return 0;
			}
			return Kind == EScctMouseAimKind::Response ? static_cast<int32>(*Found->Curve(ScctMouseAim::Draft(), bSide)) : (*Found->Bool(ScctMouseAim::Draft(), bSide) ? 1 : 0);
		};
		const auto Choices = [Kind]() -> TArray<FString>
		{
			switch (Kind)
			{
			case EScctMouseAimKind::HoldToggle: return { TEXT("Toggle"), TEXT("Hold") };
			case EScctMouseAimKind::Wheel: return { TEXT("Changes gadgets"), TEXT("Walk and run") };
			default: return { MouseAimResponseLabels[0], MouseAimResponseLabels[1], MouseAimResponseLabels[2], MouseAimResponseLabels[3] };
			}
		};
		const auto Values = [Kind]() -> TArray<FString>
		{
			switch (Kind)
			{
			case EScctMouseAimKind::HoldToggle: return { TEXT("Toggle"), TEXT("Hold") };
			case EScctMouseAimKind::Wheel: return { TEXT("Gadget"), TEXT("Speed") };
			default: return { MouseAimResponseNames[0], MouseAimResponseNames[1], MouseAimResponseNames[2], MouseAimResponseNames[3] };
			}
		};
		const auto Step = [Index, Values, Set](int32 By)
		{
			const TArray<FString> All = Values();
			Set(All[((Index() + By) % All.Num() + All.Num()) % All.Num()]);
		};
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Ui::Button(Ui::ToText(TEXT("<")), FOnClicked::CreateLambda([Step] { Step(-1); return FReply::Handled(); })) ]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(10.0f, 0.0f)
			[
				Ui::Text(TAttribute<FText>::CreateLambda([Index, Choices]
				{
					const TArray<FString> All = Choices();
					return Ui::ToText(All.IsValidIndex(Index()) ? All[Index()] : FString());
				}), 13.0f)
			]
			+ SHorizontalBox::Slot().AutoWidth()[ Ui::Button(Ui::ToText(TEXT(">")), FOnClicked::CreateLambda([Step] { Step(1); return FReply::Handled(); })) ];
	}

	case EScctMouseAimKind::Key:
	default:
	{
		const TWeakPtr<SScctMouseAimPanel> Weak = SharedThis(this);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Ui::Button(TAttribute<FText>::CreateLambda([Find, bSide]
				{
					const FScctMouseAimField* Found = Find();
					const FString Key = Found != nullptr ? *Found->Text(ScctMouseAim::Draft(), bSide) : FString();
					return Ui::ToText(Key.IsEmpty() ? FString(TEXT("-")) : ScctBindings::DisplayName(Key));
				}), FOnClicked::CreateLambda([Weak, Find]
				{
					const TSharedPtr<SScctMouseAimPanel> Panel = Weak.Pin();
					if (Panel.IsValid() && Find() != nullptr)
					{
						Panel->CaptureClutch(*Find());
					}
					return FReply::Handled();
				}), false, MouseAimColour(bMerc), 178.0f)
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f);
	}
	}
}

void SScctMouseAimPanel::CaptureClutch(const FScctMouseAimField& Field)
{
	const FString Name = Field.Name;
	const bool bSide = bMerc;
	const bool bHasKey = !Field.Text(ScctMouseAim::Draft(), bMerc)->IsEmpty();
	const TWeakPtr<SScctMouseAimPanel> Weak = SharedThis(this);
	const auto Then = [Weak, Name, bSide](const FString& Key)
	{
		FString Error;
		const bool bDone = ScctMouseAim::SetField(bSide, Name, Key, Error);
		UE_LOG(LogOpenSCCT, Log, TEXT("MouseAim: %s %s %s"), MouseAimSide(bSide), *Name, bDone ? *FString::Printf(TEXT("captured as '%s' (not applied)"), *Key) : *Error);
		if (const TSharedPtr<SScctMouseAimPanel> Panel = Weak.Pin())
		{
			Panel->Status = bDone ? (Key.IsEmpty() ? FString(TEXT("The modifier has no key now.")) : FString::Printf(TEXT("The modifier is held on %s."), *ScctBindings::DisplayName(Key))) : Error;
		}
	};
	ScctBindings::OpenCapture(Field.Label(bMerc), Field.Tooltip, MouseAimColour(bMerc), bHasKey,
		[](const FString& Key) { return UScctControlSettings::Get()->CheckClutchKey(Key); },
		Then, [Then] { Then(FString()); });
}

void SScctMouseAimPanel::AskReset()
{
	const TSharedPtr<SOverlay> Host = SScctOptionsPage::ModalHost();
	if (!Host.IsValid())
	{
		return;
	}
	const TWeakPtr<SScctMouseAimPanel> Weak = SharedThis(this);
	const bool bSide = bMerc;
	SScctConfirm::Open(Host.ToSharedRef(),
		FString::Printf(TEXT("Put the %s's mouse sensitivity, modifiers, hold or toggle choices and controller curves back to the defaults? Invert Y, the mouse scale, the wheel and the modifier key are shared, so they change for both sides. Nothing changes until you apply."),
			bMerc ? TEXT("mercenary") : TEXT("spy")),
		TEXT("RESET MOUSE"), TEXT("CANCEL"), FSimpleDelegate::CreateLambda([Weak, bSide]
		{
			ScctMouseAim::ResetSide(bSide);
			if (const TSharedPtr<SScctMouseAimPanel> Panel = Weak.Pin())
			{
				Panel->Status = TEXT("Edited back to the defaults; apply to use them.");
			}
		}));
}
