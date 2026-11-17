#include "Play/ScctGameMode.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctViewPawn.h"

namespace
{
	/** A yes/no URL option: on when present, unless its value says otherwise. */
	bool ParseSwitch(const FString& Options, const TCHAR* Key)
	{
		if (!UGameplayStatics::HasOption(Options, Key))
		{
			return false;
		}
		const FString Value = UGameplayStatics::ParseOption(Options, Key);
		return !(Value == TEXT("0") || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase)
			|| Value.Equals(TEXT("no"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("off"), ESearchCase::IgnoreCase));
	}

	/** The bots' skill as the bridge counts it: 0 default, 1 easy, 2 normal, 3 hard, 4 expert. */
	int32 ParseDifficulty(const FString& Value)
	{
		static const TCHAR* const Names[] = { TEXT("default"), TEXT("easy"), TEXT("normal"), TEXT("hard"), TEXT("expert") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
		{
			if (Value.Equals(Names[Index], ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return FMath::Clamp(FCString::Atoi(*Value), 0, 4);
	}
}

AScctGameMode::AScctGameMode()
{
	DefaultPawnClass = AScctViewPawn::StaticClass();
	PlayerControllerClass = AScctPlayerController::StaticClass();
}

void AScctGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// A level is named after the OpenSCCT map it was imported from, unless the URL says otherwise.
	Match.Map = UGameplayStatics::ParseOption(Options, TEXT("ScctMap"));
	if (Match.Map.IsEmpty())
	{
		Match.Map = FPackageName::GetShortName(MapName);
		Match.Map.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
	}

	const FString Mode = UGameplayStatics::ParseOption(Options, TEXT("Mode"));
	if (!Mode.IsEmpty())
	{
		Match.Mode = Mode;
	}

	Match.bMerc = UGameplayStatics::ParseOption(Options, TEXT("Side")).Equals(TEXT("merc"), ESearchCase::IgnoreCase);
	Match.Bots = UGameplayStatics::GetIntOption(Options, TEXT("Bots"), Match.Bots);
	Match.Difficulty = ParseDifficulty(UGameplayStatics::ParseOption(Options, TEXT("Difficulty")));
	Match.bSidearms = ParseSwitch(Options, TEXT("Sidearms"));
	Match.bRifles = ParseSwitch(Options, TEXT("Rifles"));
	Match.bOfflineRules = ParseSwitch(Options, TEXT("Practice"));
	Match.PlayerName = UGameplayStatics::ParseOption(Options, TEXT("Name"));
}

void AScctGameMode::StartPlay()
{
	Super::StartPlay();

	if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->StartMatch(Match);
	}
}
