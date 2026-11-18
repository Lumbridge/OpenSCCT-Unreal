#pragma once

#include "CoreMinimal.h"
#include "Runtime/ScctFeatures.h"

// What the menus' map lists ask about the catalog beyond the hostable maps (Runtime/ScctHostApi.h SetupText Maps):
// the groups a list can be narrowed to and the words its search box looks through. Mirrors
// Bridge/OpenScct.UnrealBridge/Online/MapBrowserExports.cs; change both together and bump the version.
// Text is UTF-16, zero terminated.

constexpr int32 ScctMapBrowserApiVersion = 1;

/** FScctMapBrowserApi::GetText's kinds. */
enum class EScctMapBrowserText : int32
{
	Groups = 0,  // the groups, one per line: "name TAB title TAB description TAB map ids (comma separated)"; the first is every installed map
	Details = 1, // one host map per line: "Id TAB search words TAB description TAB credit"
	Training = 2, // installed training map ids, one per line
};

struct FScctMapBrowserApi
{
	int32 Version;

	/** Copies a text (EScctMapBrowserText) into Buffer. Returns its full length in characters. */
	int32 (__cdecl* GetText)(int32 Kind, TCHAR* Buffer, int32 Capacity);
};

static_assert(sizeof(FScctMapBrowserApi) == 16, "FScctMapBrowserApi must match MapBrowserApi (64-bit)");

template <> struct TScctFeatureInfo<FScctMapBrowserApi>
{
	static const TCHAR* TypeName() { return TEXT("OpenScct.UnrealBridge.MapBrowserExports"); }
	static constexpr int32 Version = ScctMapBrowserApiVersion;
};
