#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// The controller's extras from the C# bridge beyond the controls table (Runtime/ScctControlsApi.h): the vibration
// settings, which FScctControlSettings does not carry, the match's rumbles to play, and the capture of a pad binding
// gesture (Bridge/OpenScct.UnrealBridge/Controls/PadExports.cs, PadRumble.cs, PadCapture.cs).
//
// This mirrors PadExports.cs field for field; change both together and bump the version. Strings are UTF-16 and zero
// terminated; a text handed back is copied as far as it fits and its full length is returned.

constexpr int32 ScctPadApiVersion = 1;

/** Why a rumble was or was not played (BridgeRumbleResult). */
namespace EScctRumbleResult
{
	constexpr int32 Played = 0;
	constexpr int32 VibrationOff = 1;
	constexpr int32 ControllerOff = 2;
	constexpr int32 Outranked = 3;
	constexpr int32 RoundEnded = 4;
}

/** The states of a pad binding capture (PadCaptureState). */
namespace EScctPadCapture
{
	constexpr int32 Releasing = 0; // waiting for every control to be let go before the gesture
	constexpr int32 Listening = 1; // waiting for the gesture; the text is what is held so far
	constexpr int32 Captured = 2;  // the text is the gesture's control name ("A", "LS+Y", "B:Hold")
	constexpr int32 Refused = 3;   // the text says why the combination cannot be bound
}

/** One rumble of the local player's Vibration_* notifications (BridgeRumble). */
struct FScctPadRumble
{
	int32 Priority;
	float Low;      // the low-frequency (left, large) motor, 0 to 1, already times Strength
	float High;     // the high-frequency (right, small) motor, 0 to 1, already times Strength
	float Seconds;
	float Strength; // the strength setting the motors were scaled by
	int32 Play;     // 1: play it; 0: held back (Result says why)
	int32 Result;   // EScctRumbleResult
	TCHAR Name[32]; // "Vibration_Blast"
};

struct FScctPadApi
{
	int32 Version;

	/** The vibration settings in use: on (1) or off, and the strength 0 to 1. */
	int32 (__cdecl* GetRumble)(int32* Enabled, float* Strength);

	/** Sets them in the settings in use; the controls table's SaveSettings writes them. */
	int32 (__cdecl* SetRumble)(int32 Enabled, float Strength);

	/** Starts the rumble of the running session (call each frame: cheap). 1 when a session runs. */
	int32 (__cdecl* Begin)();

	/** Takes the waiting rumbles, oldest first. Returns the count written. */
	int32 (__cdecl* DrainRumbles)(FScctPadRumble* Rumbles, int32 Capacity);

	/** Raises a Vibration_* notification on the local player's controller through the simulation (a check). */
	int32 (__cdecl* RaiseForCheck)(const TCHAR* What, float A);

	/** Starts a pad binding capture. */
	void (__cdecl* CaptureBegin)();

	/** One frame of the capture: Down is the pad controls held now, LF between them. Writes the state's text. */
	int32 (__cdecl* CaptureUpdate)(const TCHAR* Down, float DeltaSeconds, TCHAR* Buffer, int32 Capacity, int32* State);

	/** The Options screen's TEST rumble (the blast's) at the strength in use. */
	void (__cdecl* TestRumble)(FScctPadRumble* Rumble);

	/** The last failure inside this table. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctPadRumble) == 92, "FScctPadRumble must match BridgeRumble");
static_assert(sizeof(FScctPadApi) == 80, "FScctPadApi must match PadApi (64-bit)");

template <> struct TScctFeatureInfo<FScctPadApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.PadExports"); }
	static constexpr int32 Version = ScctPadApiVersion;
};
