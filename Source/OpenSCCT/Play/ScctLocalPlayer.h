#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "ScctLocalPlayer.generated.h"

/**
 * The game's local player (Config/DefaultEngine.ini LocalPlayerClassName). Its only addition is the console: Get and
 * Set of a setting and the Godot console's words (Play/ScctConsoleCommands.h) are run before the engine's own words of
 * the same names (GET and SET of class properties, HELP, SAY), which would otherwise take them. Anything else goes on to
 * the engine as before, and a line the engine does not know either is answered as the Godot console answers it.
 */
UCLASS()
class OPENSCCT_API UScctLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
#if UE_ALLOW_EXEC_COMMANDS
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
#endif
};
