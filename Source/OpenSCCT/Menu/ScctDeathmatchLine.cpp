#include "Menu/ScctDeathmatchLine.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	const TArray<int32>& DeathmatchLineForcedKills()
	{
		static const TArray<int32> Forced = []
		{
			TArray<int32> Kills;
			FString Text;
			if (FParse::Value(FCommandLine::Get(), TEXT("ScctResultsKills="), Text, false))
			{
				TArray<FString> Parts;
				Text.ParseIntoArray(Parts, TEXT(","));
				for (const FString& Part : Parts)
				{
					Kills.Add(FMath::Max(FCString::Atoi(*Part), 0));
				}
			}
			return Kills;
		}();
		return Forced;
	}
}

void FScctDeathmatchLine::Add(const FString& Name, int32 Kills, bool bLocal)
{
	// MatchResult.DeathmatchLine: the first line with more kills leads; a later line with as many makes it a draw.
	if (Kills > Best)
	{
		Best = Kills;
		Leaders = 1;
		Leader = Name;
		bAnyLocalLeader = bLocal;
	}
	else if (Kills == Best && Best > 0)
	{
		++Leaders;
		bAnyLocalLeader |= bLocal;
	}
}

void FScctDeathmatchLine::Finish()
{
	bShown = true;
	bLocalLeads = Best > 0 && bAnyLocalLeader;
	Text = Best == 0 ? FString(TEXT("No kills scored"))
		: Leaders > 1 ? FString::Printf(TEXT("Draw at %d kills"), Best)
		: FString::Printf(TEXT("%s wins with %d kills"), *Leader, Best);
}

int32 FScctDeathmatchLine::ForcedKills(int32 Index, int32 Kills)
{
	const TArray<int32>& Forced = DeathmatchLineForcedKills();
	return Forced.IsValidIndex(Index) ? Forced[Index] : Kills;
}

bool FScctDeathmatchLine::IsForced()
{
	return DeathmatchLineForcedKills().Num() > 0;
}
