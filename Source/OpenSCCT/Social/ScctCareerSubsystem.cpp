#include "Social/ScctCareerSubsystem.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "Social/ScctSocialSubsystem.h"

TWeakObjectPtr<UScctCareerSubsystem> UScctCareerSubsystem::Current;

UScctCareerSubsystem* UScctCareerSubsystem::GetCurrent()
{
	return Current.Get();
}

FString UScctCareerSubsystem::ArchiveFolder()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Scoreboards"));
}

void UScctCareerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// The social subsystem runs the bridge's social Update, which keeps the account the stats come from.
	Collection.InitializeDependency<UScctSocialSubsystem>();
	Super::Initialize(Collection);
	Current = this;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctCareerSubsystem::Tick));
}

void UScctCareerSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	Api = nullptr;
	if (Current.Get() == this)
	{
		Current.Reset();
	}
	Super::Deinitialize();
}

bool UScctCareerSubsystem::Tick(float DeltaSeconds)
{
	if (Api == nullptr)
	{
		// The social table first: it brings the online bridge (the master and the account) up.
		if (ScctFeature<FScctSocialApi>() == nullptr || (Api = ScctFeature<FScctCareerApi>()) == nullptr)
		{
			return true;
		}
		const FString Folder = ArchiveFolder();
		IFileManager::Get().MakeDirectory(*Folder, true);
		Api->SetFolder(*Folder);
		UE_LOG(LogOpenSCCT, Log, TEXT("Career: the rounds played are saved to %s"), *Folder);
	}

	const FScctCareerInfo& Now = GetInfo();
	if (Now.Saved != LoggedSaved)
	{
		LoggedSaved = Now.Saved;
		UE_LOG(LogOpenSCCT, Log, TEXT("Career: %d scoreboard(s) saved this run"), Now.Saved);
	}
	return true;
}

const FScctCareerInfo& UScctCareerSubsystem::GetInfo() const
{
	if (Api != nullptr && InfoFrame != GFrameCounter)
	{
		InfoFrame = GFrameCounter;
		Api->GetInfo(&Info);
	}
	return Info;
}

FString UScctCareerSubsystem::GetString(EScctCareerString Kind, int32 Index, int32 Sub) const
{
	if (Api == nullptr)
	{
		return FString();
	}
	TCHAR Short[512];
	const int32 Length = Api->GetString(static_cast<int32>(Kind), Index, Sub, Short, UE_ARRAY_COUNT(Short));
	if (Length <= 0)
	{
		return FString();
	}
	if (Length < UE_ARRAY_COUNT(Short))
	{
		return FString(Short);
	}
	TArray<TCHAR> Long;
	Long.SetNumZeroed(Length + 1);
	Api->GetString(static_cast<int32>(Kind), Index, Sub, Long.GetData(), Long.Num());
	return FString(Long.GetData());
}

bool UScctCareerSubsystem::GetMatch(int32 Index, FScctCareerMatch& OutMatch) const
{
	OutMatch = {};
	return Api != nullptr && Api->GetMatch(Index, &OutMatch) != 0;
}

bool UScctCareerSubsystem::GetPlayer(int32 Index, int32 Row, FScctCareerPlayer& OutPlayer) const
{
	OutPlayer = {};
	return Api != nullptr && Api->GetPlayer(Index, Row, &OutPlayer) != 0;
}

bool UScctCareerSubsystem::GetAchievement(int32 Index, FScctCareerAchievement& OutAchievement) const
{
	OutAchievement = {};
	return Api != nullptr && Api->GetAchievement(Index, &OutAchievement) != 0;
}

void UScctCareerSubsystem::Command(EScctCareerCommand Kind, int32 Index)
{
	if (Api != nullptr)
	{
		Api->Command(static_cast<int32>(Kind), Index);
		InfoFrame = MAX_uint64;
	}
}
