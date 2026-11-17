// Around a round of the visit: the part of UScctOnlineSubsystem that reads the bridge's round table
// (Runtime/ScctRoundApi.h): the waiting list while the round loads, what the results screen adds to the results, the
// personal demo's state and the loading picture, and the controller's way back to the lobby.

#include "Online/ScctOnlineSubsystem.h"

#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** Lines read at most: a session holds far fewer players. */
	constexpr int32 MaxRoundLines = 64;
}

const FScctRoundApi* UScctOnlineSubsystem::ConnectRound() const
{
	return Api != nullptr ? ScctFeature<FScctRoundApi>() : nullptr;
}

void UScctOnlineSubsystem::BackToLobby()
{
	if (Api == nullptr || GetState() == EScctOnlineState::Idle)
	{
		return;
	}
	// The server refuses it unless this player controls the session; nothing changes then.
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: asking for everyone back to the lobby (state %d, may control %d)"), Status.State,
		HasFlag(EScctOnlineStatusFlags::CanControl) ? 1 : 0);
	Api->BackToLobby();
}

bool UScctOnlineSubsystem::GetRoundWaitList(TArray<FScctRoundWaitLine>& OutLines) const
{
	OutLines.Reset();
	const FScctRoundApi* Round = ConnectRound();
	if (Round == nullptr || GetState() != EScctOnlineState::Loading)
	{
		return false;
	}
	OutLines.SetNumZeroed(MaxRoundLines);
	const int32 Count = Round->GetWaitList(OutLines.GetData(), OutLines.Num());
	if (Count < 0)
	{
		OutLines.Reset();
		return false;
	}
	OutLines.SetNum(FMath::Min(Count, MaxRoundLines));
	return true;
}

TArray<FScctRoundResultExtra> UScctOnlineSubsystem::GetResultExtras() const
{
	TArray<FScctRoundResultExtra> Extras;
	if (const FScctRoundApi* Round = ConnectRound())
	{
		Extras.SetNumZeroed(MaxRoundLines);
		const int32 Count = Round->GetResultExtras(Extras.GetData(), Extras.Num());
		Extras.SetNum(FMath::Clamp(Count, 0, MaxRoundLines));
	}
	return Extras;
}

int32 UScctOnlineSubsystem::GetDemoFlags() const
{
	const FScctRoundApi* Round = ConnectRound();
	return Round != nullptr ? Round->GetDemoFlags() : 0;
}

FString UScctOnlineSubsystem::GetRoundText(EScctRoundText Kind) const
{
	const FScctRoundApi* Round = ConnectRound();
	if (Round == nullptr)
	{
		return FString();
	}
	TCHAR Buffer[1024];
	const int32 Length = Round->GetText(static_cast<int32>(Kind), Buffer, UE_ARRAY_COUNT(Buffer));
	if (Length < UE_ARRAY_COUNT(Buffer))
	{
		return FString(Buffer);
	}
	TArray<TCHAR> Long;
	Long.SetNumZeroed(Length + 1);
	Round->GetText(static_cast<int32>(Kind), Long.GetData(), Long.Num());
	return FString(Long.GetData());
}

int32 UScctOnlineSubsystem::GetResultsLocalTeam() const
{
	// RoundResultSide.LocalTeam: the lobby keeps each player's lobby side, which a Revenge round swaps for its second
	// game, so "You Win" / "You Lose" comes from the local player's own line of the results.
	FScctOnlineResults Results;
	TArray<FScctOnlineResultRow> Rows;
	if (GetResults(Results, Rows))
	{
		for (const FScctOnlineResultRow& Row : Rows)
		{
			if (Row.Id == Status.LocalId)
			{
				return Row.Team;
			}
		}
	}
	return Status.LocalTeam >= 0 ? Status.LocalTeam : 0;
}
