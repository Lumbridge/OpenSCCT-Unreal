#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctControlsApi.h"
#include "Runtime/ScctFeatures.h"

// What the Options screen's mouse and aim section needs from the C# bridge beyond the controls table
// (Runtime/ScctControlsApi.h): the two clutch keys, which FScctControlSettings does not carry, and the default
// settings a reset goes back to (Bridge/OpenScct.UnrealBridge/Controls/MouseAimExports.cs).
//
// This mirrors MouseAimExports.cs field for field; change both together and bump the version. Strings are UTF-16
// and zero terminated; a text handed back is copied as far as it fits and its full length is returned. Nothing here
// needs a session.

constexpr int32 ScctMouseAimApiVersion = 1;

struct FScctMouseAimApi
{
	int32 Version;

	/** The key (binding name: "Alt", "MiddleMouse", "Ctrl+V") of clutch 1 or 2 in the settings in use; length 0 for none. */
	int32 (__cdecl* GetClutchKey)(int32 Which, TCHAR* Buffer, int32 Capacity);

	/** Why Key cannot be a clutch key (a wheel direction, a double tap, not a key); length 0 when it can. */
	int32 (__cdecl* CheckClutchKey)(const TCHAR* Key, TCHAR* Buffer, int32 Capacity);

	/**
	 * Sets the key of clutch 1 or 2 ("" for none) in the settings in use; a running match reads it from its next
	 * frame, and the controls table's SaveSettings writes it. Returns 1, or 0 with the reason in LastError.
	 */
	int32 (__cdecl* SetClutchKey)(int32 Which, const TCHAR* Key);

	/** The settings a new player starts with. */
	void (__cdecl* GetDefaults)(FScctControlSettings* Settings);

	/** The last failure inside this table. */
	int32 (__cdecl* LastError)(TCHAR* Buffer, int32 Capacity);
};

// The size the C# side has (Bridge/Probes/BindingsProbe prints it).
static_assert(sizeof(FScctMouseAimApi) == 48, "FScctMouseAimApi must match MouseAimApi (64-bit)");

// Fetched like every other feature table (Runtime/ScctFeatures.h), declared here so the shared list stays as it is.
template <> struct TScctFeatureInfo<FScctMouseAimApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.MouseAimExports"); }
	static constexpr int32 Version = ScctMouseAimApiVersion;
};
