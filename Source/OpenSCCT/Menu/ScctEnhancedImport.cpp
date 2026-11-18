#include "Menu/ScctEnhancedImport.h"
#include "Menu/ScctMapBrowser.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Runtime/ScctEnhancedImportApi.h"
#include "Settings/ScctProfileOptions.h"
#include "Settings/ScctSettingsConsole.h"
#include "Widgets/SWindow.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <shobjidl.h>
#include "Windows/HideWindowsPlatformTypes.h"
// The Windows headers rename DialogBox to DialogBoxW, which breaks the menu kit's DialogBox member in a file that shares
// this unity translation unit.
#undef DialogBox
#endif

namespace
{
	template <typename TRead>
	FString EnhancedImportReadText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(4096);
		const int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return FString(Buffer.GetData());
	}

	bool EnhancedImportGameIsUp()
	{
		if (GEngine == nullptr)
		{
			return false;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (World != nullptr && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && World->HasBegunPlay())
			{
				return true;
			}
		}
		return false;
	}

	/** -ScctImportEnhanced=<folder>|profile: once the game is up and the bridge has the table, one import, logged. */
	struct FEnhancedImportOnStart
	{
		FEnhancedImportOnStart()
		{
			// Made as the module loads: the ticker waits for the engine, a game world that has begun play and the bridge.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				static bool bRead = false;
				static FString Folder;
				static double GiveUpAt = 0.0;
				if (!bRead)
				{
					bRead = true;
					if (!FParse::Value(FCommandLine::Get(), TEXT("ScctImportEnhanced="), Folder, false) || Folder.IsEmpty())
					{
						return false;
					}
					GiveUpAt = FPlatformTime::Seconds() + 180.0;
				}
				if (!EnhancedImportGameIsUp() || ScctFeature<FScctEnhancedImportApi>() == nullptr)
				{
					if (FPlatformTime::Seconds() > GiveUpAt)
					{
						UE_LOG(LogOpenSCCT, Warning, TEXT("ScctEnhancedImport: -ScctImportEnhanced gave up: the bridge's import table never came up"));
						return false;
					}
					return true;
				}
				const bool bProfile = Folder.Equals(TEXT("profile"), ESearchCase::IgnoreCase);
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctEnhancedImport: -ScctImportEnhanced from %s"), bProfile ? TEXT("this PC's profile") : *Folder);
				ScctEnhancedImport::Import(bProfile ? FString() : Folder, bProfile);
				return false;
			}), 0.5f);
		}
	} EnhancedImportOnStart;
}

FString ScctEnhancedImport::UsualProfileFolder()
{
	const FScctEnhancedImportApi* Api = ScctFeature<FScctEnhancedImportApi>();
	return Api == nullptr ? FString() : EnhancedImportReadText([Api](TCHAR* Buffer, int32 Capacity) { return Api->GetUsualProfileFolder(Buffer, Capacity); });
}

FScctEnhancedImportResult ScctEnhancedImport::Import(const FString& Folder, bool bWithoutInstall)
{
	FScctEnhancedImportResult Result;
	FString Error;
	const FScctEnhancedImportApi* Api = ScctFeature<FScctEnhancedImportApi>(&Error);
	if (Api == nullptr)
	{
		Result.Error = Error.IsEmpty() ? FString(TEXT("The bridge has no import table.")) : Error;
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctEnhancedImport: %s"), *Result.Error);
		return Result;
	}

	// The import lays its values over the controls in play: those must be the player's own files, not the defaults.
	UScctControlSettings* Controls = UScctControlSettings::Get();
	Controls->EnsureLoaded();

	const FString Text = EnhancedImportReadText([Api, &Folder, bWithoutInstall](TCHAR* Buffer, int32 Capacity)
	{
		return Api->Import(*Folder, bWithoutInstall ? 1 : 0, Buffer, Capacity);
	});
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	TArray<TPair<FString, FString>> Sets;
	for (const FString& Line : Lines)
	{
		FString Kind, Rest;
		if (!Line.Split(TEXT(" "), &Kind, &Rest))
		{
			continue;
		}
		if (Kind == TEXT("error")) { Result.Error = Rest; }
		else if (Kind == TEXT("source")) { Result.Source = Rest; }
		else if (Kind == TEXT("note")) { Result.Notes.Add(Rest); }
		else if (Kind == TEXT("controls")) { Result.ControlLines.Add(Rest); }
		else if (Kind == TEXT("loadout")) { Result.Loadout = Rest; }
		else if (Kind == TEXT("set"))
		{
			FString Key, Value;
			if (Rest.Split(TEXT("="), &Key, &Value))
			{
				Sets.Emplace(Key, Value);
			}
		}
	}
	if (!Result.Error.IsEmpty() || Result.Source.IsEmpty())
	{
		if (Result.Error.IsEmpty())
		{
			Result.Error = TEXT("The bridge gave no answer.");
		}
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctEnhancedImport: nothing imported: %s"), *Result.Error);
		return Result;
	}
	Result.bOk = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctEnhancedImport: importing from %s"), *Result.Source);

	// The bridge has put the controls and both key layouts in play: the mirror reads them back, and both files are written.
	Controls->Refresh();
	Controls->Save();
	Controls->SaveBindings();
	Controls->NotifyRebound();
	for (const FString& Line : Result.ControlLines)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctEnhancedImport: controls: %s"), *Line);
	}

	for (const TPair<FString, FString>& Set : Sets)
	{
		if (Set.Key == TEXT("Maps.ActiveMapPlaylist"))
		{
			ScctMapBrowser::SetActiveGroup(Set.Value);
			Result.Applied.Add(FString::Printf(TEXT("Map playlist = %s"), *Set.Value));
			continue;
		}
		FString Now, Why;
		if (FScctSettingsConsole::Set(Set.Key, Set.Value, Now, Why))
		{
			Result.Applied.Add(FString::Printf(TEXT("%s = %s"), *FScctSettingsConsole::CanonicalKey(Set.Key), *Now));
			UE_LOG(LogOpenSCCT, Log, TEXT("ScctEnhancedImport: set %s = %s (Enhanced gave %s)"), *Set.Key, *Now, *Set.Value);
		}
		else
		{
			Result.Refused.Add(FString::Printf(TEXT("%s: %s"), *Set.Key, *Why));
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctEnhancedImport: %s not set: %s"), *Set.Key, *Why);
		}
	}

	if (!Result.Loadout.IsEmpty())
	{
		Result.bLoadoutApplied = UScctProfileOptions::ApplyLoadoutText(Result.Loadout);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctEnhancedImport: gear %s: %s"), Result.bLoadoutApplied ? TEXT("saved") : TEXT("refused by the bridge"), *Result.Loadout);
	}

	FScctSettingsConsole::Save();
	for (const FString& Note : Result.Notes)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctEnhancedImport: %s"), *Note);
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctEnhancedImport: done: %d settings set, %d refused, gear %s"), Result.Applied.Num(), Result.Refused.Num(),
		Result.Loadout.IsEmpty() ? TEXT("not in the profile") : Result.bLoadoutApplied ? TEXT("saved") : TEXT("refused"));
	return Result;
}

FString ScctEnhancedImport::Summary(const FScctEnhancedImportResult& Result)
{
	if (!Result.bOk)
	{
		return Result.Error;
	}
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("From %s:"), *Result.Source));
	for (const FString& Note : Result.Notes)
	{
		Lines.Add(TEXT("•  ") + Note);
	}
	if (!Result.Loadout.IsEmpty())
	{
		Lines.Add(Result.bLoadoutApplied ? TEXT("•  The profile's gear is your saved kit now.") : TEXT("•  The profile's gear could not be saved."));
	}
	Lines.Add(FString::Printf(TEXT("%d settings were set%s. Everything is saved and already in force."), Result.Applied.Num(),
		Result.Refused.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%d could not be: see the log)"), Result.Refused.Num())));
	return FString::Join(Lines, TEXT("\n"));
}

bool ScctEnhancedImport::PickFolder(const FString& Title, FString& OutFolder)
{
	if (FApp::IsUnattended())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctEnhancedImport: the folder picker is not shown in an unattended run"));
		return false;
	}
#if PLATFORM_WINDOWS
	HWND Owner = nullptr;
	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow(); Window.IsValid() && Window->GetNativeWindow().IsValid())
		{
			Owner = static_cast<HWND>(Window->GetNativeWindow()->GetOSWindowHandle());
		}
	}
	bool bPicked = false;
	const HRESULT Init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	IFileOpenDialog* Dialog = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog))) && Dialog != nullptr)
	{
		DWORD Options = 0;
		Dialog->GetOptions(&Options);
		Dialog->SetOptions(Options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
		Dialog->SetTitle(*Title);
		if (SUCCEEDED(Dialog->Show(Owner)))
		{
			IShellItem* Item = nullptr;
			if (SUCCEEDED(Dialog->GetResult(&Item)) && Item != nullptr)
			{
				PWSTR Path = nullptr;
				if (SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &Path)) && Path != nullptr)
				{
					OutFolder = Path;
					bPicked = true;
					CoTaskMemFree(Path);
				}
				Item->Release();
			}
		}
		Dialog->Release();
	}
	if (SUCCEEDED(Init))
	{
		CoUninitialize();
	}
	return bPicked;
#else
	return false;
#endif
}
