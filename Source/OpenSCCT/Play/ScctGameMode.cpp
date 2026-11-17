#include "Play/ScctGameMode.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Play/ScctViewPawn.h"

AScctGameMode::AScctGameMode()
{
	DefaultPawnClass = AScctViewPawn::StaticClass();
	PlayerControllerClass = AScctPlayerController::StaticClass();
}

void AScctGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// A level is named after the OpenSCCT map it was imported from, unless the URL says otherwise.
	ScctMap = UGameplayStatics::ParseOption(Options, TEXT("ScctMap"));
	if (ScctMap.IsEmpty())
	{
		ScctMap = FPackageName::GetShortName(MapName);
		ScctMap.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
	}

	const FString Mode = UGameplayStatics::ParseOption(Options, TEXT("Mode"));
	if (!Mode.IsEmpty())
	{
		ScctMode = Mode;
	}

	bMerc = UGameplayStatics::ParseOption(Options, TEXT("Side")).Equals(TEXT("merc"), ESearchCase::IgnoreCase);
	Bots = UGameplayStatics::GetIntOption(Options, TEXT("Bots"), Bots);
}

void AScctGameMode::StartPlay()
{
	Super::StartPlay();

	if (UScctSessionSubsystem* Session = GetWorld()->GetSubsystem<UScctSessionSubsystem>())
	{
		Session->StartLocalMatch(ScctMap, ScctMode, bMerc, Bots);
	}
}
