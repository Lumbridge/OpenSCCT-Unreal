#include "Online/ScctRejoinSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** Seconds an unattended rejoin waits after the offer, so the menu's picture of it can be taken. */
	constexpr float ScctAutoRejoinDelaySeconds = 4.0f;
}

UScctRejoinSubsystem* UScctRejoinSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* Instance = World != nullptr ? World->GetGameInstance() : nullptr;
	return Instance != nullptr ? Instance->GetSubsystem<UScctRejoinSubsystem>() : nullptr;
}

void UScctRejoinSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UScctOnlineSubsystem>();
	Super::Initialize(Collection);

	const TCHAR* CommandLine = FCommandLine::Get();
	bAuto = FParse::Param(CommandLine, TEXT("ScctAutoRejoin"));
	// A test script's round must never be offered over another script's menus.
	bEnabled = !FApp::IsUnattended() || bAuto || FParse::Param(CommandLine, TEXT("ScctRejoin"));
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctRejoinSubsystem::TickRejoin));
}

void UScctRejoinSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	Api = nullptr;
	Super::Deinitialize();
}

const FScctRejoinApi* UScctRejoinSubsystem::Connect()
{
	if (Api != nullptr)
	{
		return Api;
	}

	// The online visit brings the bridge up; the rejoin table is asked for only once it is.
	UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>();
	if (Online == nullptr || !Online->EnsureReady())
	{
		return nullptr;
	}

	FString Error;
	Api = ScctFeature<FScctRejoinApi>(&Error);
	if (Api == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Rejoin: the bridge has no rejoin table: %s"), *Error);
		return nullptr;
	}

	const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Online/rejoin.json"));
	Api->SetFile(bEnabled ? *File : nullptr);
	UE_LOG(LogOpenSCCT, Log, TEXT("Rejoin: %s"), bEnabled ? *FString::Printf(TEXT("a dropped round is remembered in %s"), *File)
		: TEXT("off in an unattended run (-ScctRejoin or -ScctAutoRejoin turns it on)"));
	return Api;
}

void UScctRejoinSubsystem::Check()
{
	bCheckAsked = true;
	if (Connect() != nullptr)
	{
		Api->Check();
	}
}

bool UScctRejoinSubsystem::GetOffer(FScctRejoinChoice& Out) const
{
	Out = Choice;
	return Choice.Offered != 0;
}

bool UScctRejoinSubsystem::Rejoin()
{
	if (Api == nullptr || Choice.Offered == 0)
	{
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("Rejoin: rejoining the round on '%s' (%s, %s)"), Choice.ServerName, Choice.Map, Choice.Where);
	const bool bStarted = Api->Rejoin() != 0;
	if (!bStarted)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Rejoin: the join did not start"));
	}
	return bStarted;
}

void UScctRejoinSubsystem::Dismiss()
{
	if (Api != nullptr && Choice.Offered != 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Rejoin: the round on '%s' is not offered again"), Choice.ServerName);
		Api->Dismiss();
	}
}

bool UScctRejoinSubsystem::TickRejoin(float DeltaSeconds)
{
	if (Api == nullptr)
	{
		// The record must be kept from a round's start: the file is handed over as soon as a visit brought the bridge
		// up. An unattended rejoin checks by itself once a level plays.
		const UScctOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UScctOnlineSubsystem>();
		const UWorld* World = GetGameInstance()->GetWorld();
		const bool bAutoCheck = bAuto && !bCheckAsked && World != nullptr && World->HasBegunPlay();
		if (bAutoCheck)
		{
			Check();
		}
		else if (Online == nullptr || (!Online->IsVisiting() && !bCheckAsked))
		{
			return true;
		}
		if (Connect() == nullptr)
		{
			return true;
		}
	}

	TCHAR Line[512];
	for (int32 Taken = 0; Taken < 16; ++Taken)
	{
		const int32 Length = Api->TakeLine(Line, UE_ARRAY_COUNT(Line));
		if (Length < 0)
		{
			break;
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("Rejoin: %s"), Line);
	}

	const int32 OldRevision = Choice.Revision;
	Api->GetRejoin(&Choice);
	if (Choice.Revision != OldRevision && Choice.Offered != 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Rejoin: offering the round on '%s' (%s, %s%s%s): the main menu shows REJOIN"), Choice.ServerName, Choice.Map, Choice.Where,
			Choice.FromAccount != 0 ? TEXT(", from the account") : TEXT(""), Choice.HasPassword != 0 ? TEXT(", password kept") : TEXT(""));
		AutoWait = 0.0f;
	}

	if (bAuto && !bAutoDone && Choice.Offered != 0)
	{
		AutoWait += DeltaSeconds;
		if (AutoWait >= ScctAutoRejoinDelaySeconds)
		{
			bAutoDone = true;
			UE_LOG(LogOpenSCCT, Log, TEXT("Rejoin: -ScctAutoRejoin: rejoining"));
			Rejoin();
		}
	}
	return true;
}
