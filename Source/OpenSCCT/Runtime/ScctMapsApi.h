#pragma once

#include "CoreMinimal.h"

// Where this client finds its maps: the map folders of the command line and the executable, the folder a map is
// in, and the maps whose render files a level can be built from. Mirrors
// Bridge/OpenScct.UnrealBridge/Online/MapsExports.cs field for field; change both together and bump the version.
//
// Text is UTF-16, zero terminated.

constexpr int32 ScctMapsApiVersion = 1;

/** The texts GetText reads. */
enum class EScctMapsText : int32
{
	Roots = 0,          // every folder searched for maps, in order, one per line (LF between them)
	RenderableMaps = 1, // the maps with render/world.glb, one per line, in the catalog's order
};

struct FScctMapsApi
{
	int32 Version;

	/** Sets the folders searched before the defaults (First: -ScctMaps) and after them (Last: maps next to the executable); lists separated by newlines or semicolons. Returns how many of them exist, -1 on failure. */
	int32 (__cdecl* SetFolders)(const TCHAR* First, const TCHAR* Last);

	/** Copies the folder of a map into Buffer. Returns its full length in characters, 0 when no folder has it. */
	int32 (__cdecl* GetMapFolder)(const TCHAR* Map, TCHAR* Buffer, int32 Capacity);

	/** Copies a text (EScctMapsText) into Buffer. Returns its full length in characters. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(TCHAR) == 2, "The bridge's text is UTF-16");
static_assert(sizeof(FScctMapsApi) == 8 + 3 * sizeof(void*), "FScctMapsApi does not match MapsApi");
