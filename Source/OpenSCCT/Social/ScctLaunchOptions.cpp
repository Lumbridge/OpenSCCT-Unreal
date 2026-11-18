#include "Social/ScctLaunchOptions.h"

#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"
#include "Settings/ScctSettingsForced.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "ScctLaunchOptions"

namespace
{
	const FName LaunchThrottleStopKey(TEXT("Performance.ThrottleStop"));

	struct FScctLaunchParsed
	{
		bool bRead = false;
		FString Content;
		FString GameRoot;
		FString MapDownloads;
		TArray<FString> Maps;
		FString Priority;
		FString Affinity;
		TArray<TPair<FString, FString>> Settings;
	};

	FScctLaunchParsed& LaunchParsed()
	{
		static FScctLaunchParsed Parsed;
		if (Parsed.bRead)
		{
			return Parsed;
		}
		Parsed.bRead = true;

		const TCHAR* Stream = FCommandLine::Get();
		FString Token;
		while (FParse::Token(Stream, Token, false))
		{
			Token.ReplaceInline(TEXT("\""), TEXT(""));
			const auto Take = [&Token](const TCHAR* Prefix, FString& Out)
			{
				const int32 Length = FCString::Strlen(Prefix);
				if (Token.Len() > Length && Token.StartsWith(Prefix, ESearchCase::IgnoreCase))
				{
					Out = Token.Mid(Length).TrimStartAndEnd();
					return !Out.IsEmpty();
				}
				return false;
			};

			FString Value;
			if (Take(TEXT("--svm-content="), Value))
			{
				Parsed.Content = FPaths::ConvertRelativePathToFull(Value);
			}
			else if (Take(TEXT("--svm-game-root="), Value))
			{
				Parsed.GameRoot = FPaths::ConvertRelativePathToFull(Value);
			}
			else if (Take(TEXT("--svm-map-downloads="), Value))
			{
				Parsed.MapDownloads = FPaths::ConvertRelativePathToFull(Value);
			}
			else if (Take(TEXT("--svm-maps="), Value))
			{
				Parsed.Maps.AddUnique(FPaths::ConvertRelativePathToFull(Value));
			}
			else if (Take(TEXT("--priority="), Value))
			{
				Parsed.Priority = Value.ToLower();
			}
			else if (Take(TEXT("--affinity="), Value))
			{
				Parsed.Affinity = Value;
			}
			else if (Take(TEXT("--svm-setting="), Value))
			{
				FString Key, Text;
				if (Value.Split(TEXT("="), &Key, &Text) && !Key.TrimStartAndEnd().IsEmpty())
				{
					Parsed.Settings.Emplace(Key.TrimStartAndEnd(), Text.TrimStartAndEnd());
				}
				else
				{
					UE_LOG(LogOpenSCCT, Warning, TEXT("Launch: --svm-setting '%s' is not Key=Value"), *Value);
				}
			}
		}
		return Parsed;
	}

#if PLATFORM_WINDOWS
	/** The priority class of a name (ProcessOptions.PriorityOf; Reloaded's "medium" is normal), or 0. */
	uint32 LaunchPriorityOf(const FString& Name)
	{
		if (Name == TEXT("idle") || Name == TEXT("low")) { return IDLE_PRIORITY_CLASS; }
		if (Name == TEXT("below") || Name == TEXT("belownormal") || Name == TEXT("below_normal")) { return BELOW_NORMAL_PRIORITY_CLASS; }
		if (Name == TEXT("normal") || Name == TEXT("medium")) { return NORMAL_PRIORITY_CLASS; }
		if (Name == TEXT("above") || Name == TEXT("abovenormal") || Name == TEXT("above_normal")) { return ABOVE_NORMAL_PRIORITY_CLASS; }
		if (Name == TEXT("high")) { return HIGH_PRIORITY_CLASS; }
		return 0;
	}

	bool LaunchDigits(const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return false;
		}
		for (const TCHAR C : Text)
		{
			if (!FChar::IsDigit(C))
			{
				return false;
			}
		}
		return true;
	}

	/** A CPU mask from "0xF", "15" or "0,2-3" (ProcessOptions.AffinityOf); 0 when unreadable or naming no CPU here. */
	uint64 LaunchAffinityOf(const FString& Text, uint64 Available)
	{
		uint64 Mask = 0;
		if (Text.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
		{
			const FString Hex = Text.Mid(2);
			if (Hex.IsEmpty() || Hex.Len() > 16)
			{
				return 0;
			}
			for (const TCHAR C : Hex)
			{
				if (!FChar::IsHexDigit(C))
				{
					return 0;
				}
			}
			Mask = FCString::Strtoui64(*Hex, nullptr, 16);
		}
		else if (Text.Contains(TEXT(",")) || Text.Contains(TEXT("-")))
		{
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, TEXT(","), true);
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
				FString From = Part, To;
				if (Part.Split(TEXT("-"), &From, &To))
				{
					From.TrimStartAndEndInline();
					To.TrimStartAndEndInline();
				}
				else
				{
					To = From;
				}
				if (!LaunchDigits(From) || !LaunchDigits(To))
				{
					return 0;
				}
				for (int32 Cpu = FCString::Atoi(*From); Cpu <= FCString::Atoi(*To) && Cpu < 64; ++Cpu)
				{
					Mask |= 1ull << Cpu;
				}
			}
		}
		else if (LaunchDigits(Text))
		{
			Mask = FCString::Strtoui64(*Text, nullptr, 10);
		}
		else
		{
			return 0;
		}
		return Mask & Available;
	}

	void LaunchApplyProcess(const FScctLaunchParsed& Parsed)
	{
		const HANDLE Process = GetCurrentProcess();
		if (!Parsed.Priority.IsEmpty())
		{
			if (const uint32 Class = LaunchPriorityOf(Parsed.Priority))
			{
				if (SetPriorityClass(Process, Class) != 0)
				{
					UE_LOG(LogOpenSCCT, Log, TEXT("Launch: priority %s"), *Parsed.Priority);
				}
				else
				{
					UE_LOG(LogOpenSCCT, Warning, TEXT("Launch: could not set priority %s (error %u)"), *Parsed.Priority, static_cast<uint32>(GetLastError()));
				}
			}
			else
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Launch: unknown --priority '%s' (idle, low, below, normal, medium, above, high)"), *Parsed.Priority);
			}
		}

		if (!Parsed.Affinity.IsEmpty())
		{
			DWORD_PTR ProcessMask = 0, SystemMask = 0;
			GetProcessAffinityMask(Process, &ProcessMask, &SystemMask);
			const uint64 Mask = LaunchAffinityOf(Parsed.Affinity, static_cast<uint64>(SystemMask));
			if (Mask == 0)
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Launch: unreadable --affinity '%s' (a mask such as 0xF, or CPUs such as 0,2-3)"), *Parsed.Affinity);
			}
			else if (SetProcessAffinityMask(Process, static_cast<DWORD_PTR>(Mask)) != 0)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("Launch: affinity %s (mask 0x%llx)"), *Parsed.Affinity, Mask);
			}
			else
			{
				UE_LOG(LogOpenSCCT, Warning, TEXT("Launch: could not set affinity %s (error %u)"), *Parsed.Affinity, static_cast<uint32>(GetLastError()));
			}
		}
	}

	/** SetProcessInformation(ProcessPowerThrottling) with execution speed throttling controlled and off (Windows 10 1709 on). */
	void LaunchStopPowerThrottling()
	{
#if defined(PROCESS_POWER_THROTTLING_EXECUTION_SPEED)
		PROCESS_POWER_THROTTLING_STATE State = {};
		State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
		State.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
		State.StateMask = 0;
		if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State)) != 0)
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("Launch: power throttling off"));
		}
		else
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("Launch: could not turn power throttling off (error %u)"), static_cast<uint32>(GetLastError()));
		}
#else
		UE_LOG(LogOpenSCCT, Warning, TEXT("Launch: this build's Windows SDK has no power throttling control"));
#endif
	}
#endif
}

void ScctLaunchOptions::Apply()
{
	const FScctLaunchParsed& Parsed = LaunchParsed();

	// The bridge reads both of these from the environment when it is configured, which comes after this.
	if (!Parsed.GameRoot.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("SVM_GAME_ROOT"), *Parsed.GameRoot);
		UE_LOG(LogOpenSCCT, Log, TEXT("Launch: the original game's folder is %s%s"), *Parsed.GameRoot, FPaths::DirectoryExists(Parsed.GameRoot) ? TEXT("") : TEXT(" (not found)"));
	}
	if (!Parsed.MapDownloads.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("OPENSCCT_PACKS"), *Parsed.MapDownloads);
		UE_LOG(LogOpenSCCT, Log, TEXT("Launch: downloaded maps and packs are kept in %s"), *Parsed.MapDownloads);
	}
	if (!Parsed.Content.IsEmpty())
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Launch: content folder %s (--svm-content)"), *Parsed.Content);
	}
	for (const FString& Folder : Parsed.Maps)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("Launch: maps are also looked for in %s (--svm-maps)"), *Folder);
	}

	// For this run only: FScctSettings reads a forced value in place of the stored one and never saves it.
	for (const TPair<FString, FString>& Setting : Parsed.Settings)
	{
		FScctSettingsForced::Force(FName(*Setting.Key), Setting.Value);
		UE_LOG(LogOpenSCCT, Log, TEXT("Launch: %s is %s for this run (--svm-setting, not saved)"), *Setting.Key, *Setting.Value);
	}

	FScctSettingDesc Desc;
	Desc.Key = LaunchThrottleStopKey;
	Desc.Page = TEXT("Performance");
	Desc.Label = LOCTEXT("ThrottleStop", "Stop Windows power throttling (restart)");
	Desc.Tooltip = LOCTEXT("ThrottleStopTip", "Keeps Windows from running the game on efficiency cores or at a low clock speed when its window is not in front. Read when the game starts.");
	Desc.Type = EScctSettingType::Bool;
	Desc.Default = TEXT("false");
	Desc.Order = 900;
	FScctSettings::Register(Desc);

#if PLATFORM_WINDOWS
	LaunchApplyProcess(Parsed);
	if (FScctSettings::GetBool(LaunchThrottleStopKey))
	{
		LaunchStopPowerThrottling();
	}
	else
	{
		UE_LOG(LogOpenSCCT, Verbose, TEXT("Launch: power throttling left to Windows (Performance.ThrottleStop is off)"));
	}
#endif
}

const FString& ScctLaunchOptions::ContentRoot()
{
	return LaunchParsed().Content;
}

const TArray<FString>& ScctLaunchOptions::MapRoots()
{
	return LaunchParsed().Maps;
}

#undef LOCTEXT_NAMESPACE
