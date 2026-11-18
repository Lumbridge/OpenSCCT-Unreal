#include "Hud/ScctHudSubsystem.h"

#include "Hud/ScctHudModel.h"
#include "Online/ScctLobbyExtras.h"
#include "OpenSCCT.h"
#include "Runtime/ScctLiveScoreboardApi.h"

// The HUD subsystem's part for the running round's scoreboard (Runtime/ScctLiveScoreboardApi.h): the bridge's board
// into the model while the scoreboard or the mission status page is up, and log lines for the unattended checks. The
// board is asked for every frame from the session's start, shown or not, because the bridge notes when each of the
// server's snapshots arrives and counts the "Next update" line on from there.

namespace
{
	FString LiveBoardText(const FScctLiveScoreboardApi* Table, EScctLiveScoreboardText Kind, int32 A = 0, int32 B = 0)
	{
		TCHAR Buffer[256];
		const int32 Length = Table->GetText(static_cast<int32>(Kind), A, B, Buffer, UE_ARRAY_COUNT(Buffer));
		if (Length <= 0)
		{
			return FString();
		}
		if (Length < static_cast<int32>(UE_ARRAY_COUNT(Buffer)))
		{
			return FString(Length, Buffer);
		}
		TArray<TCHAR> Long;
		Long.SetNumZeroed(Length + 1);
		Table->GetText(static_cast<int32>(Kind), A, B, Long.GetData(), Long.Num());
		Long.Last() = TEXT('\0');
		return FString(Long.GetData());
	}
}

void UScctHudSubsystem::ReadLiveBoard()
{
	FScctHudLiveBoard& Board = Model->LiveBoard;
	const FScctLiveScoreboardApi* Table = ScctFeature<FScctLiveScoreboardApi>();
	FScctLiveScoreboardInfo Info = {};
	if (Table == nullptr || Table->GetBoard(&Info) == 0)
	{
		Board = FScctHudLiveBoard();
		return;
	}

	if (Info.RoundSerial != ScoreboardRound)
	{
		if (ScoreboardRound != 0) ClearScoreboardInput();
		ScoreboardRound = Info.RoundSerial;
	}
	// Closed: nothing to read. The rows are read again when it opens.
	const bool bUp = Model->bScoreboardHeld || Model->bStatusPage || Model->ScoreboardShown > 0.01f || Model->StatusShown > 0.01f;
	if (!bUp)
	{
		Board.bValid = false;
		bLiveBoardLogged = false;
		return;
	}

	if (!Board.bValid || Board.Info.Revision != Info.Revision)
	{
		Board.Info = Info;
		Board.Rows.SetNumZeroed(FMath::Max(Info.RowCount, 0));
		const int32 Count = Table->GetRows(Board.Rows.GetData(), Board.Rows.Num());
		Board.Rows.SetNum(FMath::Clamp(Count, 0, Board.Rows.Num()));
		Board.RankTags.Reset();
		Board.Cells.Reset();
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Columns = Side == 0 ? Info.SpyColumns : Info.MercColumns;
			Board.Columns[Side].Reset();
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				Board.Columns[Side].Add(LiveBoardText(Table, EScctLiveScoreboardText::ColumnLabel, Side, Column));
			}
		}
		for (int32 Row = 0; Row < Board.Rows.Num(); ++Row)
		{
			FScctLobbyScoreRow& Line = Board.Rows[Row];
			Line.Name[UE_ARRAY_COUNT(Line.Name) - 1] = TEXT('\0');
			Board.RankTags.Add(ScctLobbyExtras::RankBadge(Line.Rank));
			TArray<FString>& Cells = Board.Cells.AddDefaulted_GetRef();
			const int32 Side = Line.Team == 1 ? 1 : 0;
			for (int32 Column = 0; Column < Board.Columns[Side].Num(); ++Column)
			{
				Cells.Add(LiveBoardText(Table, EScctLiveScoreboardText::Cell, Row, Column));
			}
		}
		Board.Title = LiveBoardText(Table, EScctLiveScoreboardText::Title);
		Board.Spectators = LiveBoardText(Table, EScctLiveScoreboardText::Spectators);
		Board.Omitted = LiveBoardText(Table, EScctLiveScoreboardText::Omitted);
		Board.bValid = true;
	}
	Board.Clock = LiveBoardText(Table, EScctLiveScoreboardText::Clock);
	Board.Timing = LiveBoardText(Table, EScctLiveScoreboardText::Timing);

	// For the unattended checks: the whole board when it comes up, then the timing line every two seconds.
	if (!bLiveBoardLogged)
	{
		bLiveBoardLogged = true;
		LiveBoardLogAt = Model->Clock + 2.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: live scoreboard up: \"%s\" %s, flags 0x%x, first team %d, %d rows, %d omitted, local participant %d; %s"),
			*Board.Title, *Board.Clock, Info.Flags, Info.FirstTeam, Board.Rows.Num(), Info.Omitted, Info.LocalParticipantId, *Board.Timing);
		for (int32 Row = 0; Row < Board.Rows.Num(); ++Row)
		{
			const FScctLobbyScoreRow& Line = Board.Rows[Row];
			const int32 Side = Line.Team == 1 ? 1 : 0;
			FString Cells;
			for (int32 Column = 0; Column < Board.Cells[Row].Num(); ++Column)
			{
				Cells += FString::Printf(TEXT(" %s=%s"), *Board.Columns[Side][Column], *Board.Cells[Row][Column]);
			}
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: live scoreboard row: '%s' team %d flags 0x%x rank '%s':%s"), Line.Name, Line.Team, Line.Flags, *Board.RankTags[Row], *Cells);
		}
	}
	else if (Model->Clock >= LiveBoardLogAt)
	{
		LiveBoardLogAt = Model->Clock + 2.0f;
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctHud: live scoreboard %s revision %d: %s"), *Board.Clock, Info.Revision, *Board.Timing);
	}
}
