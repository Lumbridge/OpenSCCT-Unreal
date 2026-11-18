#include "Play/ScctLocalPlayer.h"

#include "Play/ScctConsoleCommands.h"

#if UE_ALLOW_EXEC_COMMANDS
bool UScctLocalPlayer::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (ScctConsoleCommands::ExecSettingCommand(Cmd, InWorld, Ar))
	{
		return true;
	}
	return Super::Exec(InWorld, Cmd, Ar);
}
#endif
