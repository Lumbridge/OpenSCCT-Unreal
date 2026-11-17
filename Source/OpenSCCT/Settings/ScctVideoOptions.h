#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"

// What the Display, Graphics and Performance settings (Settings/ScctDisplaySettings.h and its two siblings) share:
// a short way to describe an option, and setting a console variable as a game setting.
namespace ScctVideoOptions
{
	inline FScctSettingDesc Option(const TCHAR* Key, const TCHAR* Page, const TCHAR* Group, const TCHAR* Label, const TCHAR* Tooltip,
		EScctSettingType Type, const FString& Default, int32 Order)
	{
		FScctSettingDesc Desc;
		Desc.Key = Key;
		Desc.Page = Page;
		Desc.Group = FText::FromString(Group);
		Desc.Label = FText::FromString(Label);
		Desc.Tooltip = FText::FromString(Tooltip);
		Desc.Type = Type;
		Desc.Default = Default;
		Desc.Order = Order;
		return Desc;
	}

	inline TArray<FText> Texts(std::initializer_list<const TCHAR*> Labels)
	{
		TArray<FText> Result;
		for (const TCHAR* Label : Labels)
		{
			Result.Add(FText::FromString(Label));
		}
		return Result;
	}

	/**
	 * Sets a console variable at the game setting priority: above the scalability groups and the project's ini, below
	 * a console command. A variable this build does not have is skipped (logged once per name).
	 */
	inline void SetCVar(const TCHAR* Name, const FString& Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(*Value, ECVF_SetByGameSetting);
			return;
		}
		static TSet<FString> Missing;
		if (!Missing.Contains(Name))
		{
			Missing.Add(Name);
			UE_LOG(LogOpenSCCT, Warning, TEXT("Settings: this build has no console variable %s"), Name);
		}
	}

	inline void SetCVar(const TCHAR* Name, int32 Value) { SetCVar(Name, FString::FromInt(Value)); }
	inline void SetCVar(const TCHAR* Name, float Value) { SetCVar(Name, FString::SanitizeFloat(Value)); }

	inline FString GetCVar(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable != nullptr ? Variable->GetString() : FString(TEXT("?"));
	}
}
