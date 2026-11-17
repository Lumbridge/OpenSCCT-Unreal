#include "Settings/ScctSettingsForced.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Settings/ScctSettings.h"

namespace
{
	TMap<FName, FString>& Forced()
	{
		static TMap<FName, FString> Values = []
		{
			TMap<FName, FString> Parsed;
			FString Sets;
			if (FParse::Value(FCommandLine::Get(), TEXT("ScctSettingSet="), Sets, false))
			{
				TArray<FString> Pairs;
				Sets.ParseIntoArray(Pairs, TEXT(";"));
				for (const FString& Pair : Pairs)
				{
					FString Key, Value;
					if (Pair.Split(TEXT("="), &Key, &Value) && !Key.TrimStartAndEnd().IsEmpty())
					{
						Parsed.Add(FName(*Key.TrimStartAndEnd()), Value.TrimStartAndEnd());
					}
					else
					{
						UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctSettingSet: '%s' is not Key=Value"), *Pair);
					}
				}
			}
			return Parsed;
		}();
		return Values;
	}
}

const FString* FScctSettingsForced::Find(FName Key)
{
	return Forced().Find(Key);
}

bool FScctSettingsForced::IsForced(FName Key)
{
	return Forced().Contains(Key);
}

void FScctSettingsForced::Force(FName Key, const FString& Value)
{
	Forced().Add(Key, Value);
}

bool FScctSettingsForced::Drop(FName Key)
{
	return Forced().Remove(Key) > 0;
}

void FScctSettingsForced::Report()
{
	for (const TPair<FName, FString>& Pair : Forced())
	{
		if (FScctSettings::Find(Pair.Key) == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctSettingSet: no option '%s' is registered (yet); it is kept in case one comes later"), *Pair.Key.ToString());
		}
		else
		{
			UE_LOG(LogOpenSCCT, Log, TEXT("-ScctSettingSet: %s is %s for this run (not saved)"), *Pair.Key.ToString(), *FScctSettings::GetString(Pair.Key));
		}
	}
}
