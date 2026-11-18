#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "ScctLocalPlayer.generated.h"

/**
 * The game's local player (Config/DefaultEngine.ini LocalPlayerClassName). Its only addition: the console's Get and Set
 * of a setting (Play/ScctConsoleCommands.h) are run before the engine's own GET and SET of class properties, which
 * would otherwise take every command of those names. Anything else goes on as before.
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
