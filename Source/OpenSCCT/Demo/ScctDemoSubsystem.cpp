#include "Demo/ScctDemoSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"
#include "Settings/ScctSettings.h"

#define LOCTEXT_NAMESPACE "ScctDemo"

const TCHAR* const UScctDemoSubsystem::RecordOfflineSetting = TEXT("Online.RecordOfflineDemos");
const TCHAR* const UScctDemoSubsystem::RecordOnlineSetting = TEXT("Online.RecordOnlineDemos");

namespace
{
	/** -ScctRecord: both kinds of match are saved this run, whatever the options say. */
	bool ScctDemoForcedOn()
	{
		static const bool bForced = FParse::Param(FCommandLine::Get(), TEXT("ScctRecord"));
		return bForced;
	}

	/** A text of any length from a call that returns its full length. */
	FString ScctDemoReadText(const FScctDemoApi& Api, EScctDemoText Kind)
	{
		TCHAR Short[512];
		const int32 Length = Api.GetText(static_cast<int32>(Kind), Short, UE_ARRAY_COUNT(Short));
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
		Api.GetText(static_cast<int32>(Kind), Long.GetData(), Long.Num());
		return FString(Long.GetData());
	}
}

UScctDemoSubsystem* UScctDemoSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UScctDemoSubsystem>() : nullptr;
}

FString UScctDemoSubsystem::DemoFolder()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Demos"));
}

bool UScctDemoSubsystem::SavesOffline()
{
	return ScctDemoForcedOn() || FScctSettings::GetBool(RecordOfflineSetting);
}

bool UScctDemoSubsystem::SavesOnline()
{
	return ScctDemoForcedOn() || FScctSettings::GetBool(RecordOnlineSetting);
}

bool UScctDemoSubsystem::PushChoice()
{
	// Another subsystem may ask before this one started: the options must exist for their values to be read.
	RegisterOptions();
	const FScctDemoApi* Api = ScctFeature<FScctDemoApi>();
	if (Api == nullptr)
	{
		return false;
	}

	const int32 Flags = (SavesOffline() ? EScctDemoConfigure::Offline : 0) | (SavesOnline() ? EScctDemoConfigure::Online : 0);
	const FString Folder = DemoFolder();
	if (Flags != 0)
	{
		IFileManager::Get().MakeDirectory(*Folder, true);
	}
	Api->Configure(*Folder, Flags);
	return true;
}

void UScctDemoSubsystem::SetRecordOffline(bool bRecord)
{
	FScctSettings::SetBool(RecordOfflineSetting, bRecord);
	FScctSettings::Save();
}

void UScctDemoSubsystem::SetRecordOnline(bool bRecord)
{
	FScctSettings::SetBool(RecordOnlineSetting, bRecord);
	FScctSettings::Save();
}

void UScctDemoSubsystem::RegisterOptions()
{
	static bool bRegistered = false;
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;

	// The Godot client's personal recording preference (DemoRecordingText, OnlineSettings [demos] record_matches),
	// split in its offline and online halves. After the hosting options on the Online tab.
	const FText Group = LOCTEXT("GroupDemos", "Demos");
	int32 Order = 600;
	auto Add = [&Group, &Order](const TCHAR* Key, const FText& Label, const FText& Tooltip)
	{
		FScctSettingDesc Desc;
		Desc.Key = Key;
		Desc.Page = TEXT("Online");
		Desc.Group = Group;
		Desc.Label = Label;
		Desc.Tooltip = Tooltip;
		Desc.Type = EScctSettingType::Bool;
		Desc.Default = TEXT("false");
		Desc.Order = Order++;
		FScctSettings::Register(Desc);
	};
	Add(RecordOfflineSetting, LOCTEXT("RecordOffline", "Record offline matches"),
		LOCTEXT("RecordOfflineTip", "Save every match you play against bots on this computer as a demo in Saved/Demos. Applies from the next match."));
	Add(RecordOnlineSetting, LOCTEXT("RecordOnline", "Record online matches"),
		LOCTEXT("RecordOnlineTip", "Save online match replays to Saved/Demos on this computer, whoever hosts. Applies to upcoming rounds. Stay connected after a round until the download finishes."));
}

void UScctDemoSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterOptions();
	SettingHandle = FScctSettings::OnChanged().AddUObject(this, &UScctDemoSubsystem::HandleSettingChanged);
	if (ScctDemoForcedOn())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: -ScctRecord, offline matches and online rounds are saved to %s this run"), *DemoFolder());
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScctDemoSubsystem::Tick), 0.25f);
}

void UScctDemoSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickerHandle);
	FScctSettings::OnChanged().Remove(SettingHandle);
	// A match still recording is finished by its session's stop; its size is not known here any more.
	Super::Deinitialize();
}

void UScctDemoSubsystem::HandleSettingChanged(FName Key)
{
	if (Key == FName(RecordOfflineSetting) || Key == FName(RecordOnlineSetting))
	{
		PushChoice();
		UE_LOG(LogOpenSCCT, Log, TEXT("Demo: offline matches %s, online rounds %s"),
			SavesOffline() ? TEXT("saved") : TEXT("not saved"), SavesOnline() ? TEXT("saved") : TEXT("not saved"));
	}
}

FString UScctDemoSubsystem::Text(EScctDemoText Kind) const
{
	const FScctDemoApi* Api = ScctFeature<FScctDemoApi>();
	return Api != nullptr ? ScctDemoReadText(*Api, Kind) : FString();
}

void UScctDemoSubsystem::ReportFile(const TCHAR* What, const FString& Path) const
{
	const int64 Size = IFileManager::Get().FileSize(*Path);
	UE_LOG(LogOpenSCCT, Log, TEXT("Demo: %s %s (%lld bytes)"), What, *Path, Size);
}

bool UScctDemoSubsystem::Tick(float DeltaSeconds)
{
	// The bridge comes up when the first match or the menus need it; the choice is handed over as soon as it is.
	if (!bPushed)
	{
		bPushed = PushChoice();
		if (!bPushed)
		{
			return true;
		}
	}

	const FScctDemoApi* Api = ScctFeature<FScctDemoApi>();
	FScctDemoInfo Now = {};
	if (Api == nullptr || Api->GetInfo(&Now) == 0)
	{
		return true;
	}

	// The finished file of a match that ended: its size once the host has closed it (it is written as it plays).
	const FString Recording = (Now.Flags & EScctDemoFlags::RecordingOffline) != 0 ? ScctDemoReadText(*Api, EScctDemoText::CurrentOfflineDemo) : FString();
	if (!PendingFinished.IsEmpty() && Recording != PendingFinished)
	{
		ReportFile(TEXT("offline match saved to"), PendingFinished);
		PendingFinished.Reset();
	}

	if (Now.Revision == Info.Revision)
	{
		return true;
	}
	Info = Now;

	if (Recording != SeenRecording)
	{
		SeenRecording = Recording;
		if (!Recording.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Demo: this match is recording to %s"), *Recording);
			PendingFinished = Recording;
		}
	}

	const FString OnlineDemo = (Now.Flags & EScctDemoFlags::HasOnlineDemo) != 0 ? ScctDemoReadText(*Api, EScctDemoText::LastOnlineDemo) : FString();
	if (OnlineDemo != SeenOnlineDemo)
	{
		SeenOnlineDemo = OnlineDemo;
		if (!OnlineDemo.IsEmpty())
		{
			ReportFile(TEXT("online round saved to"), OnlineDemo);
		}
	}

	// The visit's status as the server answers it ("checking...", "saved", "downloading...").
	if ((Now.Flags & (EScctDemoFlags::VisitRecords | EScctDemoFlags::PendingSave)) != 0)
	{
		// A download's percentage moves every few hundred milliseconds: the log takes a new kind of status, or each
		// further quarter of the download.
		const FString Status = ScctDemoReadText(*Api, EScctDemoText::OnlineStatus);
		FString Shape = Status;
		int32 Percent = -1;
		for (int32 Index = 0; Index < Shape.Len(); ++Index)
		{
			if (FChar::IsDigit(Shape[Index]))
			{
				Percent = FMath::Max(Percent, 0) * 10 + (Shape[Index] - TEXT('0'));
				Shape[Index] = TEXT('#');
			}
		}
		const int32 Quarter = Percent >= 0 ? Percent / 25 : -1;
		if (Shape != SeenOnlineStatus || Quarter != SeenOnlineQuarter)
		{
			SeenOnlineStatus = Shape;
			SeenOnlineQuarter = Quarter;
			UE_LOG(LogOpenSCCT, Log, TEXT("Demo: online: %s"), *Status);
		}
	}

	OnChanged.Broadcast();
	return true;
}

#undef LOCTEXT_NAMESPACE
