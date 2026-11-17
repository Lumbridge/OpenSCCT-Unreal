// The lobby's moderation and the admin console: the part of UScctOnlineSubsystem behind the player rows' Kick, Ban
// and Voice Ban (Runtime/ScctLobbyApi.h, the Godot client's OnlineLobby) and the lobby's Admin box (the online table's
// AdminLogin and AdminCommand on a dedicated server, the host's console on the player's own server). The server
// decides; what it did comes back with the players and as system chat lines, which the menus already follow.

#include "Online/ScctOnlineSubsystem.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** Seconds between the commands of -ScctAdmin, so each answer is in the log before the next command. */
	constexpr float AdminCommandEvery = 1.0f;

	/** The admin login is answered within this; after it the commands are sent anyway (the server says why it refuses). */
	constexpr float AdminLoginWait = 4.0f;
}

const FScctLobbyApi* UScctOnlineSubsystem::ConnectLobby() const
{
	return Api != nullptr ? ScctFeature<FScctLobbyApi>() : nullptr;
}

const FScctOnlinePlayer* UScctOnlineSubsystem::FindPlayer(int32 PlayerId) const
{
	return Players.FindByPredicate([PlayerId](const FScctOnlinePlayer& Player) { return Player.Id == PlayerId; });
}

const FScctOnlinePlayer* UScctOnlineSubsystem::FindLocalPlayer() const
{
	return Status.LocalId >= 0 ? FindPlayer(Status.LocalId) : nullptr;
}

bool UScctOnlineSubsystem::CanModerate() const
{
	// SessionModel.CanModerate: the host or an admin.
	const FScctOnlinePlayer* Local = FindLocalPlayer();
	return Local != nullptr && (Local->Flags & (EScctOnlinePlayerFlags::Host | EScctOnlinePlayerFlags::Admin)) != 0;
}

bool UScctOnlineSubsystem::IsLocalAdmin() const
{
	const FScctOnlinePlayer* Local = FindLocalPlayer();
	return Local != nullptr && (Local->Flags & EScctOnlinePlayerFlags::Admin) != 0;
}

bool UScctOnlineSubsystem::CanActOn(int32 PlayerId, const TCHAR* What) const
{
	// The server refuses these too; checked here so a refused request is said in the log instead of vanishing.
	const FScctOnlinePlayer* Target = FindPlayer(PlayerId);
	const TCHAR* Why = ConnectLobby() == nullptr ? TEXT("the bridge has no lobby table")
		: !CanModerate() ? TEXT("only the host or an admin may")
		: Target == nullptr ? TEXT("no such player in the session")
		: PlayerId == Status.LocalId ? TEXT("that is the local player")
		: (Target->Flags & EScctOnlinePlayerFlags::Bot) != 0 ? TEXT("bots cannot be")
		: nullptr;
	if (Why != nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Online: %s of player %d not sent: %s"), What, PlayerId, Why);
		return false;
	}
	return true;
}

bool UScctOnlineSubsystem::Kick(int32 PlayerId)
{
	if (!CanActOn(PlayerId, TEXT("kick")) || ConnectLobby()->Kick(PlayerId) == 0)
	{
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: asked the server to kick '%s' (player %d)"), FindPlayer(PlayerId)->Name, PlayerId);
	return true;
}

bool UScctOnlineSubsystem::Ban(int32 PlayerId)
{
	if (!CanActOn(PlayerId, TEXT("ban")) || ConnectLobby()->Ban(PlayerId) == 0)
	{
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: asked the server to ban '%s' (player %d)"), FindPlayer(PlayerId)->Name, PlayerId);
	return true;
}

bool UScctOnlineSubsystem::SetVoiceBanned(int32 PlayerId, bool bBanned)
{
	if (!CanActOn(PlayerId, bBanned ? TEXT("voice ban") : TEXT("voice unban")) || ConnectLobby()->SetVoiceBanned(PlayerId, bBanned ? 1 : 0) == 0)
	{
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: asked the server to %s '%s' (player %d)"), bBanned ? TEXT("ban from voice chat") : TEXT("lift the voice ban of"),
		FindPlayer(PlayerId)->Name, PlayerId);
	return true;
}

FString UScctOnlineSubsystem::RankText(int32 PackedRank) const
{
	const FScctLobbyApi* Lobby = ConnectLobby();
	if (Lobby == nullptr || PackedRank == 0)
	{
		return FString();
	}
	TCHAR Buffer[128];
	Lobby->RankText(PackedRank, Buffer, UE_ARRAY_COUNT(Buffer));
	Buffer[UE_ARRAY_COUNT(Buffer) - 1] = TEXT('\0');
	return Buffer;
}

void UScctOnlineSubsystem::AdminLogin(const FString& Password)
{
	if (Api == nullptr || Password.IsEmpty())
	{
		return;
	}
	// The password itself never goes into the log.
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: admin login sent"));
	Api->AdminLogin(*Password);
}

bool UScctOnlineSubsystem::AdminCommand(const FString& Command)
{
	const FString Trimmed = Command.TrimStartAndEnd();
	if (Api == nullptr || Trimmed.IsEmpty() || GetState() == EScctOnlineState::Idle)
	{
		return false;
	}
	if (IsHosting())
	{
		// The player's own server: its console answers the host without a login.
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: host console command: %s"), *Trimmed);
		return HostCommand(Trimmed);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: admin command: %s"), *Trimmed);
	Api->AdminCommand(*Trimmed);
	return true;
}

// ---- unattended ---------------------------------------------------------------------------------------------------

void UScctOnlineSubsystem::ReadModerationSwitches()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	const auto ReadAction = [this, CommandLine](const TCHAR* Switch, const TCHAR* Action)
	{
		FString Value, Name, Seconds;
		if (FParse::Value(CommandLine, Switch, Value, false) && !Value.IsEmpty())
		{
			FAutoModeration& Entry = AutoModeration.AddDefaulted_GetRef();
			Entry.Action = Action;
			if (Value.Split(TEXT(","), &Name, &Seconds, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				Entry.Name = Name.TrimStartAndEnd();
				Entry.After = FMath::Max(0.0f, FCString::Atof(*Seconds));
			}
			else
			{
				Entry.Name = Value.TrimStartAndEnd();
			}
		}
	};
	ReadAction(TEXT("ScctKickAfter="), TEXT("kick"));
	ReadAction(TEXT("ScctBanAfter="), TEXT("ban"));
	ReadAction(TEXT("ScctVoiceBanAfter="), TEXT("voiceban"));

	FParse::Value(CommandLine, TEXT("ScctChatTeam="), AutoTeamChat, false);
	FParse::Value(CommandLine, TEXT("ScctChatWhen="), AutoChatWhen);

	FString Admin;
	if (FParse::Value(CommandLine, TEXT("ScctAdmin="), Admin, false) && !Admin.IsEmpty())
	{
		TArray<FString> Parts;
		Admin.ParseIntoArray(Parts, TEXT(";"), true);
		if (Parts.Num() > 0)
		{
			AutoAdminPassword = Parts[0];
			Parts.RemoveAt(0);
			AutoAdminCommands = MoveTemp(Parts);
		}
	}
}

void UScctOnlineSubsystem::RunUnattendedModeration(float DeltaSeconds)
{
	if (GetState() != EScctOnlineState::Lobby || Status.LocalId < 0)
	{
		return;
	}

	if (!bAutoTeamChatSent && !AutoTeamChat.IsEmpty() && Status.PlayerCount >= AutoChatWhen)
	{
		bAutoTeamChatSent = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: -ScctChatTeam: saying '%s' to the %s only (%d players in the lobby)"), *AutoTeamChat,
			Status.LocalTeam == 0 ? TEXT("spies") : Status.LocalTeam == 1 ? TEXT("mercs") : TEXT("(no side)"), Status.PlayerCount);
		SendChat(AutoTeamChat, true);
	}

	for (FAutoModeration& Entry : AutoModeration)
	{
		if (Entry.bDone)
		{
			continue;
		}
		const FScctOnlinePlayer* Target = Players.FindByPredicate([&Entry](const FScctOnlinePlayer& Player) { return Entry.Name.Equals(Player.Name, ESearchCase::IgnoreCase); });
		if (Target == nullptr)
		{
			continue;
		}
		Entry.Seen = FMath::Max(Entry.Seen, 0.0f) + DeltaSeconds;
		if (Entry.Seen < Entry.After)
		{
			continue;
		}
		Entry.bDone = true;
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: -Scct%sAfter: '%s' has been in the lobby %.0f s"), Entry.Action == TEXT("kick") ? TEXT("Kick") : Entry.Action == TEXT("ban") ? TEXT("Ban") : TEXT("VoiceBan"),
			Target->Name, Entry.Seen);
		const int32 Id = Target->Id;
		if (Entry.Action == TEXT("kick"))
		{
			Kick(Id);
		}
		else if (Entry.Action == TEXT("ban"))
		{
			Ban(Id);
		}
		else
		{
			SetVoiceBanned(Id, (Target->Flags & EScctOnlinePlayerFlags::VoiceBanned) == 0);
		}
	}

	if (AutoAdminPassword.IsEmpty() && AutoAdminCommands.IsEmpty())
	{
		return;
	}
	AutoAdminWait -= DeltaSeconds;
	if (!bAutoAdminLoggedIn)
	{
		bAutoAdminLoggedIn = true;
		if (!IsHosting() && !AutoAdminPassword.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Online: -ScctAdmin: logging in as an admin of '%s'%s"), *ServerName,
				HasFlag(EScctOnlineStatusFlags::Dedicated) ? TEXT(" (dedicated)") : TEXT(""));
			AdminLogin(AutoAdminPassword);
			AutoAdminWait = AdminLoginWait;
		}
		return;
	}
	if (AutoAdminCommands.IsEmpty() || (AutoAdminWait > 0.0f && !IsLocalAdmin() && !IsHosting()))
	{
		return;
	}
	if (AutoAdminWait > AdminCommandEvery)
	{
		// Logged in early: the first command goes now, the rest a second apart.
		AutoAdminWait = 0.0f;
	}
	if (AutoAdminWait <= 0.0f)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: -ScctAdmin: %s"), IsLocalAdmin() ? TEXT("logged in as an admin") : IsHosting() ? TEXT("the host's console") : TEXT("not an admin (the server will say why)"));
		AdminCommand(AutoAdminCommands[0]);
		AutoAdminCommands.RemoveAt(0);
		AutoAdminWait = AdminCommandEvery;
	}
}
