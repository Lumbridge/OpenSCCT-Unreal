#include "Menu/ScctSplashTrivia.h"

#include "Math/RandomStream.h"

namespace
{
	const TCHAR* const SplashTriviaLines[] =
	{
		TEXT("Chaos Theory came out in 2005."),
		TEXT("The first Splinter Cell came out in 2002, on the Xbox first."),
		TEXT("Ubisoft Montreal made the first Splinter Cell."),
		TEXT("Ubisoft Annecy made the Spies vs. Mercenaries mode."),
		TEXT("Spies vs. Mercenaries first appeared in Pandora Tomorrow."),
		TEXT("Amon Tobin composed the Chaos Theory soundtrack."),
		TEXT("Michael Ironside voiced Sam Fisher from the first game to Conviction."),
		TEXT("Eric Johnson voiced Sam Fisher in Blacklist."),
		TEXT("Chaos Theory gave Sam Fisher his combat knife."),
		TEXT("Chaos Theory added the sound meter: stay quieter than the room."),
		TEXT("Chaos Theory added a two-player co-op campaign."),
		TEXT("Sam Fisher works for Third Echelon, a secret arm of the NSA."),
		TEXT("Colonel Irving Lambert is Sam Fisher's handler."),
		TEXT("Anna Grimsdottir, Grim, is Third Echelon's technical expert."),
		TEXT("Sam Fisher's daughter is called Sarah."),
		TEXT("The SC-20K is Sam Fisher's modular assault rifle."),
		TEXT("The split jump has been in Splinter Cell since the first game."),
		TEXT("Double Agent followed Chaos Theory in 2006."),
		TEXT("Spies vs. Mercs came back in Blacklist in 2013."),
		TEXT("Chaos Theory came out on PC, Xbox, PS2 and GameCube."),
		TEXT("Chaos Theory was remastered in Splinter Cell Trilogy HD for the PS3."),
		TEXT("In Versus, the spies are Shadownet agents and the mercs work for ARGUS."),
		TEXT("The first Splinter Cell had no multiplayer at all."),
		TEXT("Amon Tobin's soundtrack also came out as his album Chaos Theory."),
		TEXT("Douglas Shetland, Sam's old friend, runs Displace International."),
		TEXT("Chaos Theory was the first Splinter Cell rated M in North America."),
		TEXT("Sam Fisher joined Rainbow Six Siege in 2020 as the operator Zero."),
		TEXT("Sam Fisher turns up in a mission in Ghost Recon Wildlands."),
		TEXT("The Splinter Cell novels came out under the pen name David Michaels."),
		TEXT("Liev Schreiber voices Sam Fisher in Netflix's Splinter Cell: Deathwatch."),
		TEXT("Ubisoft Toronto announced a remake of the first Splinter Cell in 2021."),
	};
}

int32 ScctSplashTrivia::Num()
{
	return UE_ARRAY_COUNT(SplashTriviaLines);
}

FString ScctSplashTrivia::Pick()
{
	static FRandomStream Stream(static_cast<int32>(FPlatformTime::Cycles()));
	static int32 Last = INDEX_NONE;
	const int32 Count = Num();
	int32 Index = Stream.RandRange(0, Count - 1);
	if (Count > 1 && Index == Last)
	{
		Index = (Index + 1 + Stream.RandRange(0, Count - 2)) % Count;
	}
	Last = Index;
	return SplashTriviaLines[Index];
}
