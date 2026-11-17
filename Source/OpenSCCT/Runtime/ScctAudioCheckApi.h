#pragma once

#include "CoreMinimal.h"

// The C# bridge's audio check table: entry points the unattended audio checks use to force a state the game would
// reach only later. Mirrors Bridge/OpenScct.UnrealBridge/Events/AudioCheckExports.cs; change both together and
// bump the version. Fetched with ScctFeature<FScctAudioCheckApi>() (Runtime/ScctFeatures.h).

constexpr int32 ScctAudioCheckApiVersion = 2;

struct FScctAudioCheckApi
{
	int32 Version;

	/**
	 * Starts the named Sound ("Amb_Aqua.Ambiances.aqua_musicA") as the music, faded in over FadeSeconds, through the
	 * script's PlayMusic path: it arrives as an ordinary PlayMusic sound record. Returns 1 when it plays, 0 when no
	 * session runs or the sound is unknown or has no file.
	 */
	int32 (__cdecl* PlayMusic)(const TCHAR* Song, float FadeSeconds);

	/**
	 * Version 2. Plays the named Sound at X, Y, Z with the radius given (0: the default hearer radius) as a PlaySound
	 * call of no actor: a test emitter that arrives as an ordinary Play record (a looping sound loops until the session
	 * ends). Returns the call's id, 0 when no session runs or the sound is unknown or has no file.
	 */
	int32 (__cdecl* PlaySoundAt)(const TCHAR* Sound, float X, float Y, float Z, float Radius);
};
