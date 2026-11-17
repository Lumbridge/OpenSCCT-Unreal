#include "Settings/ScctSettings.h"

#include "Algo/StableSort.h"
#include "Misc/ConfigCacheIni.h"
#include "Settings/ScctSettingsForced.h"

namespace
{
	const TCHAR* Section = TEXT("OpenSCCT.Settings");

	struct FRegistry
	{
		TArray<FScctSettingDesc> Options;
		TMap<FName, int32> Index;
		FScctSettings::FOnChanged Changed;
	};

	FRegistry& Registry()
	{
		static FRegistry Instance;
		return Instance;
	}

	const TArray<FName>& PageOrder()
	{
		static const TArray<FName> Order = {
			TEXT("Spy"), TEXT("Mercenary"), TEXT("Display"), TEXT("Graphics"), TEXT("Performance"),
			TEXT("Audio"), TEXT("Voice"), TEXT("Hud"), TEXT("Controller"), TEXT("Online") };
		return Order;
	}

	FString Clamp(const FScctSettingDesc& Desc, const FString& Value)
	{
		switch (Desc.Type)
		{
		case EScctSettingType::Bool:
			return Value.ToBool() ? TEXT("true") : TEXT("false");
		case EScctSettingType::Float:
		{
			float Number = FMath::Clamp(FCString::Atof(*Value), Desc.Min, Desc.Max);
			if (Desc.Step > 0.f)
			{
				Number = FMath::Clamp(Desc.Min + FMath::RoundToFloat((Number - Desc.Min) / Desc.Step) * Desc.Step, Desc.Min, Desc.Max);
			}
			return FString::SanitizeFloat(Number);
		}
		case EScctSettingType::Int:
			return FString::FromInt(FMath::Clamp(FCString::Atoi(*Value), FMath::RoundToInt(Desc.Min), FMath::RoundToInt(Desc.Max)));
		case EScctSettingType::Choice:
			return FString::FromInt(FMath::Clamp(FCString::Atoi(*Value), 0, FMath::Max(0, Desc.Choices.Num() - 1)));
		default:
			return Value;
		}
	}
}

void FScctSettings::Register(const FScctSettingDesc& Desc)
{
	FRegistry& R = Registry();
	if (const int32* Found = R.Index.Find(Desc.Key))
	{
		R.Options[*Found] = Desc;
		return;
	}
	R.Index.Add(Desc.Key, R.Options.Add(Desc));
}

const FScctSettingDesc* FScctSettings::Find(FName Key)
{
	FRegistry& R = Registry();
	const int32* Found = R.Index.Find(Key);
	return Found ? &R.Options[*Found] : nullptr;
}

TArray<const FScctSettingDesc*> FScctSettings::OptionsOf(FName Page)
{
	TArray<const FScctSettingDesc*> Result;
	for (const FScctSettingDesc& Desc : Registry().Options)
	{
		if (Desc.Page == Page && Desc.bVisible)
		{
			Result.Add(&Desc);
		}
	}
	// Stable: equal orders keep registration order.
	Algo::StableSortBy(Result, [](const FScctSettingDesc* Desc) { return Desc->Order; });
	return Result;
}

TArray<FName> FScctSettings::Pages()
{
	TArray<FName> Result;
	for (FName Page : PageOrder())
	{
		if (OptionsOf(Page).Num() > 0)
		{
			Result.Add(Page);
		}
	}
	for (const FScctSettingDesc& Desc : Registry().Options)
	{
		if (Desc.bVisible && !Result.Contains(Desc.Page))
		{
			Result.Add(Desc.Page);
		}
	}
	return Result;
}

FString FScctSettings::GetString(FName Key)
{
	const FScctSettingDesc* Desc = Find(Key);
	if (!Desc)
	{
		return FString();
	}
	if (const FString* Forced = FScctSettingsForced::Find(Key))
	{
		return Clamp(*Desc, *Forced);
	}
	FString Value;
	if (GConfig && GConfig->GetString(Section, *Key.ToString(), Value, GGameUserSettingsIni))
	{
		return Value;
	}
	return Desc->Default;
}

bool FScctSettings::GetBool(FName Key) { return GetString(Key).ToBool(); }
float FScctSettings::GetFloat(FName Key) { return FCString::Atof(*GetString(Key)); }
int32 FScctSettings::GetInt(FName Key) { return FCString::Atoi(*GetString(Key)); }

void FScctSettings::SetString(FName Key, const FString& Value)
{
	const FScctSettingDesc* Desc = Find(Key);
	if (!Desc || !GConfig)
	{
		return;
	}
	const FString Clamped = Clamp(*Desc, Value);
	const FString Before = GetString(Key);
	// Set by the player or the game: a value forced for the run gives way from now on.
	const bool bWasForced = FScctSettingsForced::Drop(Key);
	if (Clamped == Before && !bWasForced)
	{
		return;
	}
	GConfig->SetString(Section, *Key.ToString(), *Clamped, GGameUserSettingsIni);
	if (Clamped != Before)
	{
		Registry().Changed.Broadcast(Key);
	}
}

void FScctSettings::SetBool(FName Key, bool bValue) { SetString(Key, bValue ? TEXT("true") : TEXT("false")); }
void FScctSettings::SetFloat(FName Key, float Value) { SetString(Key, FString::SanitizeFloat(Value)); }
void FScctSettings::SetInt(FName Key, int32 Value) { SetString(Key, FString::FromInt(Value)); }

void FScctSettings::Reset(FName Key)
{
	const FScctSettingDesc* Desc = Find(Key);
	if (!Desc || !GConfig)
	{
		return;
	}
	const bool bChanged = GetString(Key) != Desc->Default;
	FScctSettingsForced::Drop(Key);
	GConfig->RemoveKey(Section, *Key.ToString(), GGameUserSettingsIni);
	if (bChanged)
	{
		Registry().Changed.Broadcast(Key);
	}
}

void FScctSettings::ResetPage(FName Page)
{
	for (const FScctSettingDesc& Desc : Registry().Options)
	{
		if (Desc.Page == Page)
		{
			Reset(Desc.Key);
		}
	}
}

void FScctSettings::Save()
{
	if (GConfig)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

FScctSettings::FOnChanged& FScctSettings::OnChanged()
{
	return Registry().Changed;
}
