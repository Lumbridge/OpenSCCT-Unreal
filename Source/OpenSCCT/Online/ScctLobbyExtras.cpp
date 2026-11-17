#include "Online/ScctLobbyExtras.h"

#include "Runtime/ScctFeatures.h"

namespace
{
	const FScctLobbyApi* LobbyExtrasTable()
	{
		const FScctLobbyApi* Table = ScctFeature<FScctLobbyApi>();
		return Table != nullptr && Table->Version >= 2 && Table->GetLastScoreboard != nullptr ? Table : nullptr;
	}

	/** Reads a text the bridge copies into a buffer and returns the full length of, growing the buffer once when it was cut. */
	template <typename TRead>
	FString LobbyExtrasText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(512);
		int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Length = Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return Length <= 0 ? FString() : FString(Buffer.GetData());
	}

	FString LobbyScoreText(const FScctLobbyApi* Table, EScctLobbyScoreboardText Kind, int32 A = 0, int32 B = 0)
	{
		return LobbyExtrasText([&](TCHAR* Buffer, int32 Capacity) { return Table->GetLastScoreboardText(static_cast<int32>(Kind), A, B, Buffer, Capacity); });
	}

	FString LobbyBriefText(const FScctLobbyApi* Table, EScctLobbyBriefingText Kind, int32 Side)
	{
		return LobbyExtrasText([&](TCHAR* Buffer, int32 Capacity) { return Table->GetBriefingText(static_cast<int32>(Kind), Side, Buffer, Capacity); });
	}
}

bool ScctLobbyExtras::IsAvailable()
{
	return LobbyExtrasTable() != nullptr;
}

bool ScctLobbyExtras::HasLastScoreboard(int32* OutSequence)
{
	const FScctLobbyApi* Table = LobbyExtrasTable();
	FScctLobbyScoreboardInfo Info = {};
	if (Table == nullptr || Table->GetLastScoreboard(&Info) == 0)
	{
		return false;
	}
	if (OutSequence != nullptr)
	{
		*OutSequence = Info.Sequence;
	}
	return true;
}

bool ScctLobbyExtras::ReadLastScoreboard(FScctLastScoreboard& Out)
{
	Out = FScctLastScoreboard();
	const FScctLobbyApi* Table = LobbyExtrasTable();
	if (Table == nullptr || Table->GetLastScoreboard(&Out.Info) == 0)
	{
		return false;
	}
	Out.Rows.SetNumZeroed(FMath::Max(Out.Info.RowCount, 0));
	const int32 Count = Table->GetLastScoreboardRows(Out.Rows.GetData(), Out.Rows.Num());
	Out.Rows.SetNum(FMath::Clamp(Count, 0, Out.Rows.Num()));
	for (FScctLobbyScoreRow& Row : Out.Rows)
	{
		Row.Name[UE_ARRAY_COUNT(Row.Name) - 1] = TEXT('\0');
	}
	Out.Title = LobbyScoreText(Table, EScctLobbyScoreboardText::Title);
	Out.Metadata = LobbyScoreText(Table, EScctLobbyScoreboardText::Metadata);
	Out.Timing = LobbyScoreText(Table, EScctLobbyScoreboardText::Timing);
	Out.Spectators = LobbyScoreText(Table, EScctLobbyScoreboardText::Spectators);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Columns = Side == 0 ? Out.Info.SpyColumns : Out.Info.MercColumns;
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			Out.Columns[Side].Add(LobbyScoreText(Table, EScctLobbyScoreboardText::ColumnLabel, Side, Column));
			Out.ColumnTips[Side].Add(LobbyScoreText(Table, EScctLobbyScoreboardText::ColumnDescription, Side, Column));
		}
	}
	for (int32 Row = 0; Row < Out.Rows.Num(); ++Row)
	{
		TArray<FString>& Cells = Out.Cells.AddDefaulted_GetRef();
		const int32 Side = Out.Rows[Row].Team == 1 ? 1 : 0;
		for (int32 Column = 0; Column < Out.Columns[Side].Num(); ++Column)
		{
			Cells.Add(LobbyScoreText(Table, EScctLobbyScoreboardText::Cell, Row, Column));
		}
	}
	return true;
}

FString ScctLobbyExtras::LastScoreboardSummary()
{
	const FScctLobbyApi* Table = LobbyExtrasTable();
	return Table != nullptr && Table->GetLastScoreboard(nullptr) != 0 ? LobbyScoreText(Table, EScctLobbyScoreboardText::Summary) : FString();
}

TArray<FScctScoreDetail> ScctLobbyExtras::ReadDetails(int32 Row)
{
	TArray<FScctScoreDetail> Details;
	const FScctLobbyApi* Table = LobbyExtrasTable();
	if (Table == nullptr)
	{
		return Details;
	}
	TArray<FString> Lines;
	LobbyScoreText(Table, EScctLobbyScoreboardText::Details, Row).ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("\t"), false);
		if (Parts.Num() == 3)
		{
			Details.Add({ Parts[0], Parts[1], Parts[2] });
		}
	}
	return Details;
}

bool ScctLobbyExtras::ReadBriefing(FScctLobbyBriefing& Out, int32 Side)
{
	Out = FScctLobbyBriefing();
	const FScctLobbyApi* Table = LobbyExtrasTable();
	if (Table == nullptr)
	{
		return false;
	}
	Out.Title = LobbyBriefText(Table, EScctLobbyBriefingText::Title, Side);
	if (Out.Title.IsEmpty())
	{
		return false;
	}
	Out.PageTitle = LobbyBriefText(Table, EScctLobbyBriefingText::PageTitle, Side);
	Out.Objective = LobbyBriefText(Table, EScctLobbyBriefingText::Objective, Side);
	Out.ObjectiveName = LobbyBriefText(Table, EScctLobbyBriefingText::ObjectiveName, Side);
	Out.MapBrief = LobbyBriefText(Table, EScctLobbyBriefingText::MapBrief, Side);
	Out.ModeBrief = LobbyBriefText(Table, EScctLobbyBriefingText::ModeBrief, Side);
	Out.Legend = LobbyBriefText(Table, EScctLobbyBriefingText::Legend, Side);
	Out.Picture = LobbyBriefText(Table, EScctLobbyBriefingText::Picture, Side);
	Out.Plan = LobbyBriefText(Table, EScctLobbyBriefingText::Plan, Side);
	Out.Marks.SetNumZeroed(64);
	const int32 Count = Table->GetBriefingMarks(Out.Marks.GetData(), Out.Marks.Num());
	if (Count > Out.Marks.Num())
	{
		Out.Marks.SetNumZeroed(Count);
		Table->GetBriefingMarks(Out.Marks.GetData(), Out.Marks.Num());
	}
	Out.Marks.SetNum(FMath::Clamp(Count, 0, Out.Marks.Num()));
	return true;
}

FString ScctLobbyExtras::RankBadge(int32 PackedRank)
{
	const FScctLobbyApi* Table = LobbyExtrasTable();
	return Table == nullptr || PackedRank == 0 ? FString()
		: LobbyExtrasText([&](TCHAR* Buffer, int32 Capacity) { return Table->RankWords(PackedRank, 0, Buffer, Capacity); });
}

FString ScctLobbyExtras::RankWords(int32 PackedRank)
{
	const FScctLobbyApi* Table = LobbyExtrasTable();
	return Table == nullptr || PackedRank == 0 ? FString()
		: LobbyExtrasText([&](TCHAR* Buffer, int32 Capacity) { return Table->RankWords(PackedRank, 1, Buffer, Capacity); });
}

FLinearColor ScctLobbyExtras::RankColour(int32 PackedRank)
{
	const FScctLobbyApi* Table = LobbyExtrasTable();
	if (Table == nullptr || PackedRank == 0)
	{
		return FLinearColor::White;
	}
	const int32 Rgb = Table->RankColour(PackedRank);
	return FLinearColor(FColor((Rgb >> 16) & 0xFF, (Rgb >> 8) & 0xFF, Rgb & 0xFF));
}
