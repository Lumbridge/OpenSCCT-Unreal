#pragma once

#include "CoreMinimal.h"

// The C# bridge's audio check table: entry points the unattended audio checks use to force a state the game would
// reach only later. Mirrors Bridge/OpenScct.UnrealBridge/Events/AudioCheckExports.cs; change both together and
// bump the version. Fetched with ScctFeature<FScctAudioCheckApi>() (Runtime/ScctFeatures.h).

constexpr int32 ScctAudioCheckApiVersion = 1;

struct FScctAudioCheckApi
{
	int32 Version;

	/**
	 * Starts the named Sound ("Amb_Aqua.Ambiances.aqua_musicA") as the music, faded in over FadeSeconds, through the
	 * script's PlayMusic path: it arrives as an ordinary PlayMusic sound record. Returns 1 when it plays, 0 when no
	 * session runs or the sound is unknown or has no file.
	 */
	int32 (__cdecl* PlayMusic)(const TCHAR* Song, float FadeSeconds);
};
