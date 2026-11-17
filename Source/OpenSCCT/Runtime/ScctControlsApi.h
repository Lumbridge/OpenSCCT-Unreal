#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

// The controls of the C# bridge: Unreal hands over the devices' state once a frame, the bridge applies the bindings
// (the original's key layouts, hold/tap/toggle rules, the controller) and writes the simulation's input itself, and
// gives back the camera with the mouse turn the simulation has not taken yet.
//
// These mirror Bridge/OpenScct.UnrealBridge/Controls/ControlsTypes.cs, BridgeKey.cs and ControlsExports.cs field for
// field; change both together and bump the version.
//
// Fetch the table once, as OpenSCCT.cpp fetches the bridge's:
//   Host.GetFunction(TEXT("OpenScct.UnrealBridge.ControlsExports, OpenScct.UnrealBridge"), TEXT("GetApi"))
// called with an FScctControlsApi*; it returns ScctControlsApiVersion.

constexpr int32 ScctControlsApiVersion = 1;

/** The length of FScctRawInput::Keys. */
constexpr int32 ScctKeyCount = 128;

/**
 * Indices into FScctRawInput::Keys. Named after the original's input keys (the IK_ names of its key layouts), which
 * are also the names bindings are stored under ("W", "Shift", "LeftMouse"; the digit row is "0".."9"; a pad
 * button's name is its control: "A", "LB", "DpadUp"). ScctKeyNames below gives the Unreal key of each.
 */
enum class EScctKey : uint8
{
	A = 0, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
	D0 = 26, D1, D2, D3, D4, D5, D6, D7, D8, D9,
	F1 = 36, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
	NumPad0 = 48, NumPad1, NumPad2, NumPad3, NumPad4, NumPad5, NumPad6, NumPad7, NumPad8, NumPad9,
	Shift = 58, // either Shift key; the same for Ctrl and Alt
	Ctrl, Alt, Space, Tab, Escape, Enter, Backspace, PageUp, PageDown, Home, End, Insert, Delete, Up, Down, Left, Right,
	Tilde = 76, CapsLock, Pause, NumLock, ScrollLock,
	GreyStar = 81, GreyPlus, GreyMinus, GreySlash, NumPadPeriod, // the numpad's * + - / .
	Semicolon = 86, EqualSign, Comma, Minus, Period, Slash, LeftBracket, Backslash, RightBracket, SingleQuote,
	LeftMouse = 96, RightMouse, MiddleMouse, MouseX1, MouseX2,
	MouseWheelUp = 101, MouseWheelDown, // no release: the byte is the notches turned since the last frame
	PadA = 103, PadB, PadX, PadY, PadLB, PadRB, PadLS, PadRS, PadDpadUp, PadDpadDown, PadDpadLeft, PadDpadRight,
	PadStart, PadBack, PadP1, PadP2, PadP3, PadP4, // P1..P4: back paddles, which Unreal has no keys for
	Last = PadP4,
};

/** Bits of a byte of FScctRawInput::Keys (the wheel codes hold a count instead). */
namespace EScctKeyState
{
	constexpr uint8 Down = 1;    // the key is down now
	constexpr uint8 Pressed = 2; // it went down since the last frame, even if it is up again
}

/** The Unreal keys read for one EScctKey: FKey names (EKeys::X.GetFName()), nullptr for none. */
struct FScctKeyNames
{
	const TCHAR* Primary;
	const TCHAR* Secondary;
};

/** Indexed by EScctKey, up to EScctKey::Last. */
constexpr FScctKeyNames ScctKeyNames[] = {
	{ TEXT("A"), nullptr }, { TEXT("B"), nullptr }, { TEXT("C"), nullptr }, { TEXT("D"), nullptr },
	{ TEXT("E"), nullptr }, { TEXT("F"), nullptr }, { TEXT("G"), nullptr }, { TEXT("H"), nullptr },
	{ TEXT("I"), nullptr }, { TEXT("J"), nullptr }, { TEXT("K"), nullptr }, { TEXT("L"), nullptr },
	{ TEXT("M"), nullptr }, { TEXT("N"), nullptr }, { TEXT("O"), nullptr }, { TEXT("P"), nullptr },
	{ TEXT("Q"), nullptr }, { TEXT("R"), nullptr }, { TEXT("S"), nullptr }, { TEXT("T"), nullptr },
	{ TEXT("U"), nullptr }, { TEXT("V"), nullptr }, { TEXT("W"), nullptr }, { TEXT("X"), nullptr },
	{ TEXT("Y"), nullptr }, { TEXT("Z"), nullptr },
	// 26: the digit row (EKeys::Zero .. EKeys::Nine)
	{ TEXT("Zero"), nullptr }, { TEXT("One"), nullptr }, { TEXT("Two"), nullptr }, { TEXT("Three"), nullptr },
	{ TEXT("Four"), nullptr }, { TEXT("Five"), nullptr }, { TEXT("Six"), nullptr }, { TEXT("Seven"), nullptr },
	{ TEXT("Eight"), nullptr }, { TEXT("Nine"), nullptr },
	// 36: F1 .. F12
	{ TEXT("F1"), nullptr }, { TEXT("F2"), nullptr }, { TEXT("F3"), nullptr }, { TEXT("F4"), nullptr },
	{ TEXT("F5"), nullptr }, { TEXT("F6"), nullptr }, { TEXT("F7"), nullptr }, { TEXT("F8"), nullptr },
	{ TEXT("F9"), nullptr }, { TEXT("F10"), nullptr }, { TEXT("F11"), nullptr }, { TEXT("F12"), nullptr },
	// 48: NumPad0 .. NumPad9 (EKeys::NumPadZero .. EKeys::NumPadNine)
	{ TEXT("NumPadZero"), nullptr }, { TEXT("NumPadOne"), nullptr }, { TEXT("NumPadTwo"), nullptr },
	{ TEXT("NumPadThree"), nullptr }, { TEXT("NumPadFour"), nullptr }, { TEXT("NumPadFive"), nullptr },
	{ TEXT("NumPadSix"), nullptr }, { TEXT("NumPadSeven"), nullptr }, { TEXT("NumPadEight"), nullptr },
	{ TEXT("NumPadNine"), nullptr },
	// 58: Shift, Ctrl, Alt (either side)
	{ TEXT("LeftShift"), TEXT("RightShift") }, { TEXT("LeftControl"), TEXT("RightControl") },
	{ TEXT("LeftAlt"), TEXT("RightAlt") },
	// 61: Space, Tab, Escape, Enter, Backspace, PageUp, PageDown, Home, End, Insert, Delete, Up, Down, Left, Right
	{ TEXT("SpaceBar"), nullptr }, { TEXT("Tab"), nullptr }, { TEXT("Escape"), nullptr }, { TEXT("Enter"), nullptr },
	{ TEXT("BackSpace"), nullptr }, { TEXT("PageUp"), nullptr }, { TEXT("PageDown"), nullptr },
	{ TEXT("Home"), nullptr }, { TEXT("End"), nullptr }, { TEXT("Insert"), nullptr }, { TEXT("Delete"), nullptr },
	{ TEXT("Up"), nullptr }, { TEXT("Down"), nullptr }, { TEXT("Left"), nullptr }, { TEXT("Right"), nullptr },
	// 76: Tilde, CapsLock, Pause, NumLock, ScrollLock
	{ TEXT("Tilde"), nullptr }, { TEXT("CapsLock"), nullptr }, { TEXT("Pause"), nullptr },
	{ TEXT("NumLock"), nullptr }, { TEXT("ScrollLock"), nullptr },
	// 81: GreyStar, GreyPlus, GreyMinus, GreySlash, NumPadPeriod (EKeys::Multiply, Add, Subtract, Divide, Decimal)
	{ TEXT("Multiply"), nullptr }, { TEXT("Add"), nullptr }, { TEXT("Subtract"), nullptr },
	{ TEXT("Divide"), nullptr }, { TEXT("Decimal"), nullptr },
	// 86: Semicolon, EqualSign, Comma, Minus, Period, Slash, LeftBracket, Backslash, RightBracket, SingleQuote
	{ TEXT("Semicolon"), nullptr }, { TEXT("Equals"), nullptr }, { TEXT("Comma"), nullptr },
	{ TEXT("Hyphen"), nullptr }, { TEXT("Period"), nullptr }, { TEXT("Slash"), nullptr },
	{ TEXT("LeftBracket"), nullptr }, { TEXT("Backslash"), nullptr }, { TEXT("RightBracket"), nullptr },
	{ TEXT("Apostrophe"), nullptr },
	// 96: LeftMouse, RightMouse, MiddleMouse, MouseX1, MouseX2
	{ TEXT("LeftMouseButton"), nullptr }, { TEXT("RightMouseButton"), nullptr },
	{ TEXT("MiddleMouseButton"), nullptr }, { TEXT("ThumbMouseButton"), nullptr },
	{ TEXT("ThumbMouseButton2"), nullptr },
	// 101: MouseWheelUp, MouseWheelDown (one notch per frame at most through these keys)
	{ TEXT("MouseScrollUp"), nullptr }, { TEXT("MouseScrollDown"), nullptr },
	// 103: PadA, PadB, PadX, PadY, PadLB, PadRB, PadLS, PadRS
	{ TEXT("Gamepad_FaceButton_Bottom"), nullptr }, { TEXT("Gamepad_FaceButton_Right"), nullptr },
	{ TEXT("Gamepad_FaceButton_Left"), nullptr }, { TEXT("Gamepad_FaceButton_Top"), nullptr },
	{ TEXT("Gamepad_LeftShoulder"), nullptr }, { TEXT("Gamepad_RightShoulder"), nullptr },
	{ TEXT("Gamepad_LeftThumbstick"), nullptr }, { TEXT("Gamepad_RightThumbstick"), nullptr },
	// 111: PadDpadUp, PadDpadDown, PadDpadLeft, PadDpadRight, PadStart, PadBack
	{ TEXT("Gamepad_DPad_Up"), nullptr }, { TEXT("Gamepad_DPad_Down"), nullptr },
	{ TEXT("Gamepad_DPad_Left"), nullptr }, { TEXT("Gamepad_DPad_Right"), nullptr },
	{ TEXT("Gamepad_Special_Right"), nullptr }, { TEXT("Gamepad_Special_Left"), nullptr },
	// 117: PadP1 .. PadP4
	{ nullptr, nullptr }, { nullptr, nullptr }, { nullptr, nullptr }, { nullptr, nullptr },
};

static_assert(UE_ARRAY_COUNT(ScctKeyNames) == static_cast<int32>(EScctKey::Last) + 1, "ScctKeyNames must have one entry per EScctKey");

/** The devices as read in one rendered frame. */
struct FScctRawInput
{
	uint8 Keys[ScctKeyCount]; // EScctKeyState bits per EScctKey; for the wheel codes, notches since the last frame
	float MouseX;             // raw mouse counts since the last frame, right positive (no smoothing or acceleration)
	float MouseY;             // raw mouse counts since the last frame, up (mouse pushed away) positive
	float LeftX, LeftY;       // left stick, -1 to 1, right and up positive, no deadzone applied
	float RightX, RightY;     // right stick, the same
	float LeftTrigger, RightTrigger; // 0 (released) to 1
	float DeltaSeconds;       // the length of the frame, seconds
	int32 Flags;              // EScctRawInputFlags
};

namespace EScctRawInputFlags
{
	/**
	 * The game has the input: the window has focus, the pointer is captured and no menu or text field takes the
	 * keys. Without it nothing is read and everything held is released; keys still down when it comes back are
	 * ignored until they are let go.
	 */
	constexpr int32 Play = 1;
}

/**
 * The local player's camera as drawn this frame: the view between the last two ticks, turned by the mouse counts
 * the simulation has not taken yet (no frame of delay). Use it in place of FScctBridgeView.
 */
struct FScctLookView
{
	float X, Y, Z;           // simulation units = Unreal centimetres, same axes
	float Pitch, Yaw, Roll;  // degrees, -180 to 180
	float Fov;               // the simulation's FovAngle, degrees: horizontal over a 4:3 picture, merc aim zoom included
	float HorizontalFov;     // degrees across a screen of the aspect given to GetView (widescreen rule and FOV cap applied); past 16:9 it is the 16:9 angle (the original stretches that picture)
	float VerticalFov;       // degrees up and down on that screen: give the camera this one
	float AimPitch, AimYaw;  // degrees: the controller's rotation plus the pending mouse turn (the spy's arm aims along it)
	int32 Valid;             // 1 when there is a view to draw
	int32 SpyFirstPerson;    // 1 when the spy is seen through his own eyes
	int32 Cut;               // 1 when this view does not continue the last one (respawn, change of camera, a cinematic sequence's camera); it stays 1 for every frame drawn until the next tick
	int32 Occluded;          // 1 when no collision-free eye position exists: cover the world this frame
	int32 LookAhead;         // 1 while the mouse turn is drawn ahead of the simulation
	float RifleAimBlend;     // the merc's rifle aim zoom, 0 (hip) to 1 (aimed), eased as the FOV is
};

/** An interface command a binding raised (ShowMenu, Talk, DisplaySessionStatus, PushToTalk...). */
struct FScctLocalCommand
{
	int32 NameId;  // the command's name: FScctControlsApi::GetName (stable for the life of the process)
	int32 Arg;     // the number after the command in the binding, 0 when none; for ToggleAutoHack the new setting
	int32 Pressed; // 1 on the press, 0 on the release (held commands: push to talk, held scoreboard, team ping)
};

/** Look settings of one side and device. Multipliers: 1 is the original's speed. */
struct FScctLookSettings
{
	int32 WalkHold, ScopeHold, PerspectiveHold, CloakHold; // 1 hold, 0 toggle: walk key, merc scope, spy first person, spy cloak
	float FreeLook;      // spy: look with the gun holstered (default 1.4 mouse, 1.6 pad)
	float Look;          // spy: look with the gun drawn; merc: the look (default 1.0 mouse; pad 1.2 spy, 1.8 merc)
	float Scope;         // binoculars or sniper scope, times the look (default 0.75)
	float ScopeVertical; // merc: the sniper scope's vertical scale (default 2.45 = 1:1)
	float Vertical;      // kept for the settings file; the original's look does not read it
	float Cam;           // sticky camera and camera network (default 1.0; spy pad 1.8)
	int32 LookResponse, GunLookResponse; // 0 Linear, 1 CurveSoft, 2 CurveMedium, 3 CurveAggressive
	float ClutchOne;     // a held clutch multiplies the look by this
	int32 ClutchOneEnabled;
	float ClutchTwo;     // the merc's second clutch
	int32 ClutchTwoEnabled;
	int32 SwapLsLb, SwapRsRb; // pad: the stick click and the bumper of that side trade places
};

/** The control settings. Booleans are 0 or 1. Values out of range are clamped by SetSettings. */
struct FScctControlSettings
{
	int32 InvertY;          // up and down look the other way round (default 0)
	int32 InvertXSpy;       // the spy's free look turns the other way left and right (default 0)
	int32 SpyRollFreeLook;  // the spy's camera may turn during a roll (default 0)
	int32 AutoHackComms;    // hack enemy communications without holding the key (default 0)
	int32 MouseWheelSpeed;  // 0 the wheel changes gadgets (default), 1 it sets walk and run
	int32 Widescreen;       // 1 keeps the 4:3 vertical view and adds width (default), 0 stretches the 4:3 picture
	float MercFovCap;       // the cap on the horizontal field of view, degrees, 95 to 112 (default 112)
	float MouseScale;       // a multiplier over every mouse count (default 1 = the original's turn per count)
	FScctLookSettings SpyKbm, MercKbm, SpyPad, MercPad;
	int32 PadEnabled, PadMove, PadAim, PadBinds; // the controller plays; its left stick moves, right stick looks, buttons act (default 1)
	int32 PadSlowWalk;      // a part push of the left stick walks slower (default 1)
	int32 PadSpyMove8, PadMercMove8; // snap movement to eight directions (default 0)
	float PadDeadzoneLeft, PadDeadzoneRight; // 0 to 0.9 of the stick's travel (default 0.25 and 0.15)
	float PadVerticalScale;    // the look stick's vertical speed against its sideways speed (default 0.75)
	float PadRunThreshold;     // the spy runs with the move stick at least this far out (default 0.95)
	float PadTriggerThreshold; // how far a trigger goes down before it is pressed, 0 to 0.99 (default 0.234)
	float PadLookScale;        // a multiplier over the look stick's speed (default 1)
};

struct FScctControlsApi
{
	int32 Version;

	/**
	 * The devices' state of this frame. Call once a frame, before the bridge's Update. The bridge then writes the
	 * simulation's input itself: do not also call the bridge's SetInput, or Command for anything a binding covers.
	 */
	void (__cdecl* Frame)(const FScctRawInput* Raw);

	/**
	 * The camera to draw this frame; call after Update. Aspect: the picture's width over height (0 for 16:9).
	 * Clearance: the radius, in cm, of the sphere round the camera that holds its near plane (0 for the default).
	 * Returns 1 with a view.
	 */
	int32 (__cdecl* GetView)(FScctLookView* View, float Aspect, float Clearance);

	/** Moves the interface commands raised since the last call into Commands, oldest first. Returns the count. */
	int32 (__cdecl* TakeLocalCommands)(FScctLocalCommand* Commands, int32 Capacity);

	/** Copies the name behind a command name id. Returns its full length in characters, 0 for an unknown id. */
	int32 (__cdecl* GetName)(int32 NameId, TCHAR* Buffer, int32 Capacity);

	/** The side whose bindings play: 0 spy, 1 merc, -1 with no session. */
	int32 (__cdecl* GetSide)();

	/** 1 while the controller did something in the last frame (show its button prompts). */
	int32 (__cdecl* IsPadActive)();

	/** The settings in use. Both work with no session; a running one takes a change on its next frame. */
	void (__cdecl* GetSettings)(FScctControlSettings* Settings);
	void (__cdecl* SetSettings)(const FScctControlSettings* Settings);

	/** The settings as a JSON file (a missing file loads the defaults). Return 1, or 0 with LastError set. */
	int32 (__cdecl* LoadSettings)(const TCHAR* Path);
	int32 (__cdecl* SaveSettings)(const TCHAR* Path);

	/** Both sides' bindings as a JSON file (a missing file loads the defaults). Return 1, or 0 with LastError set. */
	int32 (__cdecl* LoadBindings)(const TCHAR* Path);
	int32 (__cdecl* SaveBindings)(const TCHAR* Path);

	/**
	 * Copies what a key or pad control is bound to. bMerc: 0 the spy's layout, 1 the merc's. bPad: 0 a key by its
	 * binding name ("W", "Ctrl+F", "Double+H"), whose command is in the original's binding grammar
	 * ("Button bFire | Function Fire | GadgetQuickChoice 0"); 1 a pad control ("A", "LB+Y", "B:Hold"), whose action
	 * is "Action:<id>", "Pad:<contextual>", "Clutch1", "Clutch2" or "Key:<key>". Returns the full length, 0 when unbound.
	 */
	int32 (__cdecl* GetBinding)(int32 bMerc, int32 bPad, const TCHAR* Key, TCHAR* Buffer, int32 Capacity);

	/** Binds a key or pad control (an empty command unbinds). Returns 1, or 0 for a name that is neither. */
	int32 (__cdecl* SetBinding)(int32 bMerc, int32 bPad, const TCHAR* Key, const TCHAR* Command);

	/** Puts one side's keys and pad controls back to the defaults. */
	void (__cdecl* ResetBindings)(int32 bMerc);

	/** Copies every binding as the JSON a save writes (for a bindings menu to list). Returns the full length. */
	int32 (__cdecl* GetBindingsJson)(TCHAR* Buffer, int32 Capacity);

	/** Copies the binding name of an EScctKey ("W", "Shift", "LeftMouse"; a pad button's control name). Returns the full length. */
	int32 (__cdecl* GetKeyName)(int32 Key, TCHAR* Buffer, int32 Capacity);

	/** Copies the last error of a settings or bindings call. Returns its full length. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};

// The sizes the C# side has (the probe prints them: Bridge/Probes/ControlsProbe). A mismatch here means the two
// files have drifted apart.
static_assert(sizeof(FScctRawInput) == 168, "FScctRawInput must match BridgeRawInput");
static_assert(sizeof(FScctLookView) == 68, "FScctLookView must match BridgeLookView");
static_assert(sizeof(FScctLocalCommand) == 12, "FScctLocalCommand must match BridgeLocalCommand");
static_assert(sizeof(FScctLookSettings) == 72, "FScctLookSettings must match BridgeLookSettings");
static_assert(sizeof(FScctControlSettings) == 372, "FScctControlSettings must match BridgeControlSettings");
static_assert(sizeof(FScctControlsApi) == 152, "FScctControlsApi must match ControlsApi (64-bit)");

namespace FScctControls
{
	/** The Unreal keys of ScctKeyNames, made once (an invalid FKey where there is none). */
	inline const TArray<TPair<FKey, FKey>>& Keys()
	{
		static const TArray<TPair<FKey, FKey>> Table = []
		{
			TArray<TPair<FKey, FKey>> Made;
			for (const FScctKeyNames& Names : ScctKeyNames)
			{
				Made.Emplace(
					Names.Primary != nullptr ? FKey(FName(Names.Primary)) : FKey(),
					Names.Secondary != nullptr ? FKey(FName(Names.Secondary)) : FKey());
			}
			return Made;
		}();
		return Table;
	}

	/**
	 * Fills the keys, buttons, wheel, sticks and triggers of Raw from a player controller's input state. The caller
	 * sets MouseX, MouseY (raw counts, Y up positive), DeltaSeconds and Flags.
	 *
	 * Mouse counts: APlayerController::GetInputMouseDelta is NOT raw. It is the value after Unreal's axis
	 * sensitivity (0.07 by default), mouse smoothing and FOV scaling. Give the bridge the device's own counts
	 * (the player input's raw value of EKeys::MouseX / MouseY), or switch those three off in the input settings.
	 */
	inline void ReadDevices(const APlayerController& Controller, FScctRawInput& Raw)
	{
		const TArray<TPair<FKey, FKey>>& Table = Keys();
		for (int32 Code = 0; Code < Table.Num(); ++Code)
		{
			uint8 State = 0;
			for (const FKey* Key : { &Table[Code].Key, &Table[Code].Value })
			{
				if (!Key->IsValid())
				{
					continue;
				}
				if (Controller.IsInputKeyDown(*Key))
				{
					State |= EScctKeyState::Down;
				}
				if (Controller.WasInputKeyJustPressed(*Key))
				{
					State |= EScctKeyState::Pressed;
				}
			}

			const bool bWheel = Code == static_cast<int32>(EScctKey::MouseWheelUp) || Code == static_cast<int32>(EScctKey::MouseWheelDown);
			// The wheel's byte is a count of notches: one when the wheel key was pressed this frame.
			Raw.Keys[Code] = bWheel ? ((State & EScctKeyState::Pressed) != 0 ? 1 : 0) : State;
		}

		// Codes past the last key are spare and must read 0.
		for (int32 Code = Table.Num(); Code < ScctKeyCount; ++Code)
		{
			Raw.Keys[Code] = 0;
		}

		// Several notches in one frame: the wheel keys only say "turned", the wheel axis holds how far (up positive).
		const int32 Notches = FMath::Clamp(FMath::RoundToInt(Controller.GetInputAnalogKeyState(EKeys::MouseWheelAxis)), -100, 100);
		if (Notches > 1)
		{
			Raw.Keys[static_cast<int32>(EScctKey::MouseWheelUp)] = static_cast<uint8>(Notches);
		}
		else if (Notches < -1)
		{
			Raw.Keys[static_cast<int32>(EScctKey::MouseWheelDown)] = static_cast<uint8>(-Notches);
		}

		Raw.LeftX = Controller.GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
		Raw.LeftY = Controller.GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
		Raw.RightX = Controller.GetInputAnalogKeyState(EKeys::Gamepad_RightX);
		Raw.RightY = Controller.GetInputAnalogKeyState(EKeys::Gamepad_RightY);
		Raw.LeftTrigger = Controller.GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);
		Raw.RightTrigger = Controller.GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
	}
}
