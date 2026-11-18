#include "Play/ScctLocalPlayer.h"

#include "Play/ScctConsoleCommands.h"

#if UE_ALLOW_EXEC_COMMANDS
bool UScctLocalPlayer::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (ScctConsoleCommands::ExecSettingCommand(Cmd, InWorld, Ar) || ScctConsoleCommands::ExecGameCommand(Cmd, InWorld, Ar))
	{
		return true;
	}
	if (Super::Exec(InWorld, Cmd, Ar))
	{
		return true;
	}
	if (FString(Cmd).TrimStartAndEnd().IsEmpty())
	{
		return false;
	}
	ScctConsoleCommands::SayUnknown(Cmd, Ar);
	return true;
}
#endif
