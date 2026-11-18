#include "Social/ScctCrashContext.h"

#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	FString CrashContextLast;
	bool bCrashContextSet = false;
	bool bCrashFolderNamed = false;
	bool bCrashTestDone = false;

	FString CrashContextText(const FScctCrashApi& Api, EScctCrashText Kind)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(Api.GetText(static_cast<int32>(Kind), nullptr, 0) + 1);
		Api.GetText(static_cast<int32>(Kind), Buffer.GetData(), Buffer.Num());
		return FString(Buffer.GetData());
	}

	void CrashContextDrain(const FScctCrashApi& Api)
	{
		TArray<FString> Lines;
		CrashContextText(Api, EScctCrashText::Notices).ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Crash: %s"), *Line);
		}
	}
}

void ScctCrashContext::Update(const FString& Map, int32 Mode, int32 Side, const FString& Where)
{
	const FScctCrashApi* Api = ScctFeature<FScctCrashApi>();
	if (Api != nullptr && !bCrashFolderNamed)
	{
		bCrashFolderNamed = true;
		const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Crashes"));
		Api->Install(*Folder);
		UE_LOG(LogOpenSCCT, Log, TEXT("Crash: managed crash reports (crash-<utc>.log) go to %s"), *CrashContextText(*Api, EScctCrashText::Folder));
	}
	if (Api != nullptr)
	{
		CrashContextDrain(*Api);
	}

	const FString Key = FString::Printf(TEXT("%s|%d|%d|%s|%d"), *Map, Mode, Side, *Where, Api != nullptr ? 1 : 0);
	if (bCrashContextSet && Key == CrashContextLast)
	{
		return;
	}
	bCrashContextSet = true;
	CrashContextLast = Key;

	FString Playing;
	if (Api != nullptr)
	{
		Api->SetContext(*Map, Mode, Side, *Where);
		Playing = CrashContextText(*Api, EScctCrashText::Context);
	}
	else
	{
		Playing = Map.IsEmpty() ? Where : (Where.IsEmpty() ? Map : Map + TEXT(", ") + Where);
	}

	// An empty value takes the key out of the report.
	FGenericCrashContext::SetGameData(TEXT("ScctMap"), Map);
	FGenericCrashContext::SetGameData(TEXT("ScctMode"), Mode >= 0 ? FString::FromInt(Mode) : FString());
	FGenericCrashContext::SetGameData(TEXT("ScctSide"), Side == 0 ? TEXT("spy") : Side == 1 ? TEXT("merc") : TEXT(""));
	FGenericCrashContext::SetGameData(TEXT("ScctWhere"), Where.IsEmpty() ? TEXT("menus") : *Where);
	FGenericCrashContext::SetGameData(TEXT("ScctPlaying"), Playing.IsEmpty() ? TEXT("(menus)") : *Playing);
	UE_LOG(LogOpenSCCT, Log, TEXT("Crash: context is now '%s'"), Playing.IsEmpty() ? TEXT("(menus)") : *Playing);

	FString Test;
	if (!bCrashTestDone && !Map.IsEmpty() && FParse::Value(FCommandLine::Get(), TEXT("ScctCrashTest="), Test))
	{
		bCrashTestDone = true;
		if (Test.Equals(TEXT("native"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogOpenSCCT, Fatal, TEXT("The crash test of -ScctCrashTest=native (playing %s)."), *Playing);
		}
		else if (Api != nullptr)
		{
			const int32 Kind = Test.Equals(TEXT("report"), ESearchCase::IgnoreCase) ? 2 : 1;
			UE_LOG(LogOpenSCCT, Warning, TEXT("Crash: -ScctCrashTest=%s: %s"), *Test,
				Kind == 1 ? TEXT("throwing on a bridge worker thread; the process ends with a crash-<utc>.log") : TEXT("writing a report"));
			if (GLog != nullptr)
			{
				GLog->Flush();
			}
			Api->Test(Kind);
			CrashContextDrain(*Api);
		}
	}
}
