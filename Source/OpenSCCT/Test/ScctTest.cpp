#include "Test/ScctTest.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Play/ScctPlayerController.h"
#include "Play/ScctSessionSubsystem.h"
#include "Runtime/ScctFeatures.h"

DEFINE_LOG_CATEGORY(LogScctTest);

namespace
{
	struct FRegisteredTest
	{
		FString Name;
		FScctTestRegistry::FFactory Factory;
	};

	/** Made on first use: the registrations run as the module's statics are made, in no fixed order. */
	TArray<FRegisteredTest>& Registered()
	{
		static TArray<FRegisteredTest> Tests;
		return Tests;
	}
}

void FScctTestRegistry::Register(const TCHAR* Name, FFactory Factory)
{
	check(Name != nullptr && Factory);
	for (const FRegisteredTest& Test : Registered())
	{
		checkf(!Test.Name.Equals(Name, ESearchCase::IgnoreCase), TEXT("Two tests are named %s"), Name);
	}
	Registered().Add(FRegisteredTest{ Name, MoveTemp(Factory) });
}

TUniquePtr<IScctTest> FScctTestRegistry::Create(const FString& Name)
{
	for (const FRegisteredTest& Test : Registered())
	{
		if (Test.Name.Equals(Name, ESearchCase::IgnoreCase))
		{
			TUniquePtr<IScctTest> Instance = Test.Factory();
			if (Instance.IsValid())
			{
				Instance->Name = Test.Name;
			}
			return Instance;
		}
	}
	return nullptr;
}

TArray<FString> FScctTestRegistry::GetNames()
{
	TArray<FString> Names;
	for (const FRegisteredTest& Test : Registered())
	{
		Names.Add(Test.Name);
	}
	Names.Sort();
	return Names;
}

UWorld* FScctTestContext::GetWorld() const
{
	return GameInstance != nullptr ? GameInstance->GetWorld() : nullptr;
}

FString FScctTestContext::GetLevelName() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->PersistentLevel == nullptr)
	{
		return FString();
	}
	FString Name = World->GetMapName();
	Name.RemoveFromStart(World->StreamingLevelsPrefix);
	return Name;
}

UScctSessionSubsystem* FScctTestContext::GetSession() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UScctSessionSubsystem>() : nullptr;
}

AScctPlayerController* FScctTestContext::GetPlayer() const
{
	UWorld* World = GetWorld();
	return World != nullptr ? Cast<AScctPlayerController>(World->GetFirstPlayerController()) : nullptr;
}

bool FScctTestContext::IsMatchPlaying() const
{
	const UScctSessionSubsystem* Session = GetSession();
	return Session != nullptr && Session->IsPlaying();
}

FString FScctTestContext::GetSessionFailure() const
{
	const UScctSessionSubsystem* Session = GetSession();
	return Session != nullptr ? Session->GetFailure() : FString();
}

bool FScctTestContext::OpenLevel(const FString& URL, FString* OutError)
{
	UWorld* World = GetWorld();
	if (World == nullptr || GEngine == nullptr)
	{
		if (OutError != nullptr)
		{
			*OutError = TEXT("there is no game world to travel from");
		}
		return false;
	}

	// A level path that names no package would travel to the default map instead, and the test would wait there.
	FString Package = URL;
	int32 Options = INDEX_NONE;
	if (Package.FindChar(TEXT('?'), Options))
	{
		Package.LeftInline(Options);
	}
	if (Package.StartsWith(TEXT("/")) && !FPackageName::DoesPackageExist(Package))
	{
		if (OutError != nullptr)
		{
			*OutError = FString::Printf(TEXT("there is no level %s"), *Package);
		}
		return false;
	}

	const FString Travel = URL + GetTestOptions();
	UE_LOG(LogScctTest, Log, TEXT("Opening %s"), *Travel);
	GEngine->SetClientTravel(World, *Travel, TRAVEL_Absolute);
	return true;
}

FString FScctTestContext::GetTestMap(const FString& Fallback)
{
	FString Map;
	return FParse::Value(FCommandLine::Get(), TEXT("ScctTestMap="), Map) && !Map.IsEmpty() ? Map : Fallback;
}

FString FScctTestContext::GetTestOptions()
{
	FString Options;
	FParse::Value(FCommandLine::Get(), TEXT("ScctTestOptions="), Options, false);
	Options.TrimStartAndEndInline();
	if (!Options.IsEmpty() && !Options.StartsWith(TEXT("?")))
	{
		Options.InsertAt(0, TEXT('?'));
	}
	return Options;
}

bool FScctTestContext::HoldAction(const FString& Action, bool bHeld)
{
	AScctPlayerController* Player = GetPlayer();
	return Player != nullptr && Player->HoldScriptedAction(Action, bHeld);
}

bool FScctTestContext::RunConsoleCommand(const FString& Command)
{
	UWorld* World = GetWorld();
	if (World == nullptr || GEngine == nullptr)
	{
		return false;
	}
	UE_LOG(LogScctTest, Log, TEXT("Console: %s"), *Command);
	if (APlayerController* Player = World->GetFirstPlayerController())
	{
		Player->ConsoleCommand(Command, true);
		return true;
	}
	return GEngine->Exec(World, *Command);
}

const FScctTestApi* FScctTestContext::GetTestApi(FString* OutError) const
{
	return ScctFeature<FScctTestApi>(OutError);
}

FString FScctTestContext::GetTestApiError() const
{
	const FScctTestApi* Api = ScctFeature<FScctTestApi>();
	if (Api == nullptr)
	{
		return FString();
	}
	TCHAR Buffer[2048];
	Api->LastError(Buffer, UE_ARRAY_COUNT(Buffer));
	return FString(Buffer);
}
