// The server browser: the part of UScctOnlineSubsystem that runs the bridge's browser table (Runtime/ScctBrowserApi.h),
// the master's list, the LAN search and the saved servers merged with a ping each. The search itself runs inside the
// online table's Update, which the subsystem calls every frame; the page (Menu/SScctServerBrowserPage.h) reads the
// rows when the browser's revision moves. A join from here is an ordinary visit afterwards.

#include "Online/ScctOnlineSubsystem.h"

#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	/** Rows the page reads at most; a master lists far fewer. */
	constexpr int32 MaxBrowserRows = 512;
}

const FScctBrowserApi* UScctOnlineSubsystem::ConnectBrowser() const
{
	return Api != nullptr ? ScctFeature<FScctBrowserApi>() : nullptr;
}

bool UScctOnlineSubsystem::OpenBrowser()
{
	// The online table first: it sets the master and the identity the browser's joins use.
	if (Connect() == nullptr)
	{
		return false;
	}
	const FScctBrowserApi* Browser = ConnectBrowser();
	if (Browser == nullptr)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("Online: the server browser table is not available"));
		return false;
	}

	const TArray<FString> Saved = GetSavedServers();
	const FString SavedLine = FString::Join(Saved, TEXT(";"));
	const bool bLoopback = IsLanLoopbackTest();
	const bool bOpened = Browser->Open(GetLanPort(), *SavedLine, bLoopback ? 1 : 0) != 0;
	UE_LOG(LogOpenSCCT, Log, TEXT("Online: server browser searching: master %s, LAN port %d%s, %d saved server(s)%s"), bMasterSet ? TEXT("set") : TEXT("none"),
		GetLanPort(), bLoopback ? TEXT(" (this machine only)") : TEXT(""), Saved.Num(), bOpened ? TEXT("") : *FString::Printf(TEXT(": %s"), *GetBrowserText(EScctBrowserText::Status)));
	return bOpened;
}

void UScctOnlineSubsystem::CloseBrowser()
{
	if (const FScctBrowserApi* Browser = ConnectBrowser())
	{
		Browser->Close();
	}
}

int32 UScctOnlineSubsystem::GetBrowserRevision() const
{
	const FScctBrowserApi* Browser = ConnectBrowser();
	return Browser != nullptr ? Browser->GetRevision() : -1;
}

void UScctOnlineSubsystem::GetBrowserRows(TArray<FScctBrowserRow>& OutRows) const
{
	OutRows.Reset();
	const FScctBrowserApi* Browser = ConnectBrowser();
	if (Browser == nullptr)
	{
		return;
	}

	const int32 Count = FMath::Clamp(Browser->GetRows(nullptr, 0), 0, MaxBrowserRows);
	OutRows.SetNumZeroed(Count);
	// The list can grow between the two calls; what does not fit waits for the next revision.
	const int32 Written = Browser->GetRows(OutRows.GetData(), OutRows.Num());
	OutRows.SetNum(FMath::Clamp(Written, 0, Count));
}

FString UScctOnlineSubsystem::GetBrowserText(EScctBrowserText Kind) const
{
	const FScctBrowserApi* Browser = ConnectBrowser();
	if (Browser == nullptr)
	{
		return FString();
	}

	TCHAR Short[512];
	const int32 Length = Browser->GetText(static_cast<int32>(Kind), Short, UE_ARRAY_COUNT(Short));
	if (Length < UE_ARRAY_COUNT(Short))
	{
		return FString(Short);
	}

	TArray<TCHAR> Long;
	Long.SetNumZeroed(Length + 1);
	Browser->GetText(static_cast<int32>(Kind), Long.GetData(), Long.Num());
	return FString(Long.GetData());
}

FString UScctOnlineSubsystem::GetBrowserPlayers(int32 Index) const
{
	const FScctBrowserApi* Browser = ConnectBrowser();
	if (Browser == nullptr || Index < 0)
	{
		return FString();
	}

	TCHAR Short[1024];
	const int32 Length = Browser->GetPlayers(Index, Short, UE_ARRAY_COUNT(Short));
	if (Length < UE_ARRAY_COUNT(Short))
	{
		return FString(Short);
	}

	TArray<TCHAR> Long;
	Long.SetNumZeroed(Length + 1);
	Browser->GetPlayers(Index, Long.GetData(), Long.Num());
	return FString(Long.GetData());
}

bool UScctOnlineSubsystem::JoinBrowserRow(int32 Index, const FString& Password, int32 PreferredTeam, FString& OutRefusal)
{
	OutRefusal.Reset();
	const FScctBrowserApi* Browser = ConnectBrowser();
	if (Browser == nullptr)
	{
		OutRefusal = TEXT("The server browser is not available.");
		return false;
	}

	Notice.Reset();
	Chat.Reset();
	const bool bStarted = Browser->Join(Index, Password.IsEmpty() ? nullptr : *Password, PreferredTeam) != 0;
	if (!bStarted)
	{
		OutRefusal = GetBrowserText(EScctBrowserText::Refusal);
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: the browser did not join row %d: %s"), Index, *OutRefusal);
	}
	else
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Online: joining the browser's row %d (side asked for: %d%s, connect mode %d)"), Index, PreferredTeam,
			Password.IsEmpty() ? TEXT("") : TEXT(", with a password"), GetConnectMode());
	}
	Touch();
	return bStarted;
}
