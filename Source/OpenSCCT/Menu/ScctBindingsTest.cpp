#include "Menu/ScctBindingsTest.h"

#include "Engine/World.h"
#include "Menu/ScctBindingsModel.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

bool UScctBindingsTest::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Steps;
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld() && FParse::Value(FCommandLine::Get(), TEXT("ScctBindTest="), Steps, false)
		&& Super::ShouldCreateSubsystem(Outer);
}

void UScctBindingsTest::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// A second in, so the bridge and the player's files are up as a player would find them.
	FTimerHandle Handle;
	InWorld.GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([] { ScctBindings::RunUnattendedTest(); }), 1.0f, false);
}
