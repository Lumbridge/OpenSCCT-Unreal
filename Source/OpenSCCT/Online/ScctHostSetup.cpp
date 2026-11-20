#include "Online/ScctHostSetup.h"

#include "Menu/ScctMenuSubsystem.h"
#include "Misc/PackageName.h"
#include "Online/ScctOnlineSubsystem.h"
#include "Runtime/ScctFeatures.h"

namespace
{
	const FScctHostApi* SetupTable()
	{
		const FScctHostApi* Api = ScctFeature<FScctHostApi>();
		return Api != nullptr && Api->Version >= 2 && Api->SetupText != nullptr ? Api : nullptr;
	}

	TArray<FString> Lines(const FString& Text)
	{
		TArray<FString> Out;
		Text.ParseIntoArray(Out, TEXT("\n"), true);
		return Out;
	}

	/** A TAB separated line, empty fields kept. */
	TArray<FString> Fields(const FString& Line)
	{
		TArray<FString> Out;
		Line.ParseIntoArray(Out, TEXT("\t"), false);
		return Out;
	}

	int32 IntOf(const TArray<FString>& Parts, int32 Index)
	{
		return Parts.IsValidIndex(Index) ? FCString::Atoi(*Parts[Index]) : 0;
	}
}

FString FScctHostMap::DefaultMode() const
{
	const FString Story = TEXT("Story");
	if (Modes.Contains(Story))
	{
		// A Story settings entry can be present on a map with no Story objectives.
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		const FScctMenuMapInfo* CatalogMap = Menu != nullptr ? Menu->FindMap(Id) : nullptr;
		if (CatalogMap == nullptr || CatalogMap->Plays(0))
		{
			return Story;
		}
	}
	const FString* Fallback = Modes.FindByPredicate([&Story](const FString& Mode) { return Mode != Story; });
	return Fallback != nullptr ? *Fallback : Story;
}

bool ScctHostSetup::IsAvailable()
{
	return SetupTable() != nullptr;
}

FString ScctHostSetup::Text(EScctSetupText Kind, const FString& Options, const FString& Argument, int32 MaxPlayers)
{
	const FScctHostApi* Api = SetupTable();
	if (Api == nullptr)
	{
		return FString();
	}

	TCHAR Short[2048];
	const int32 Length = Api->SetupText(static_cast<int32>(Kind), *Options, *Argument, MaxPlayers, Short, UE_ARRAY_COUNT(Short));
	if (Length < UE_ARRAY_COUNT(Short))
	{
		return FString(Short);
	}

	TArray<TCHAR> Long;
	Long.SetNumZeroed(Length + 1);
	Api->SetupText(static_cast<int32>(Kind), *Options, *Argument, MaxPlayers, Long.GetData(), Long.Num());
	return FString(Long.GetData());
}

FString ScctHostSetup::MapDefaults(const FScctHostMap& Map, const FString& Options, int32 MaxPlayers)
{
	return Text(EScctSetupText::Defaults, Options, FString::Printf(TEXT("?MapName=%s?Mode=%s"), *Map.Id, *Map.DefaultMode()), MaxPlayers);
}

TArray<FScctHostMap> ScctHostSetup::Maps()
{
	TArray<FScctHostMap> Maps;
	for (const FString& Line : Lines(Text(EScctSetupText::Maps, FString())))
	{
		const TArray<FString> Parts = Fields(Line);
		// Only maps this game can draw (an imported level, or render files for the runtime level) can be shown here; a round on any other would sit the host out.
		if (Parts.Num() < 4 || !UScctOnlineSubsystem::IsPlayable(Parts[0]))
		{
			continue;
		}

		FScctHostMap& Map = Maps.AddDefaulted_GetRef();
		Map.Id = Parts[0];
		Map.Name = Parts[1].IsEmpty() ? Parts[0] : Parts[1];
		Map.Picture = Parts[2];
		Parts[3].ParseIntoArray(Map.Modes, TEXT(","), true);
	}
	return Maps;
}

TArray<FScctSetupRow> ScctHostSetup::Rows(const FString& Setup, int32 MaxPlayers, FString& OutVoiceHint, FString& OutBotHint)
{
	TArray<FScctSetupRow> Rows;
	for (const FString& Line : Lines(Text(EScctSetupText::Rows, Setup, FString(), MaxPlayers)))
	{
		const TArray<FString> Parts = Fields(Line);
		if (Parts.Num() >= 3 && Parts[0] == TEXT("hint"))
		{
			(Parts[1] == TEXT("Voice") ? OutVoiceHint : OutBotHint) = Parts[2];
			continue;
		}
		if (Parts.Num() < 8)
		{
			continue;
		}

		FScctSetupRow& Row = Rows.AddDefaulted_GetRef();
		Row.Row = Parts[0];
		Row.Kind = Parts[1] == TEXT("check") ? FScctSetupRow::EKind::Check : Parts[1] == TEXT("choice") ? FScctSetupRow::EKind::Choice : FScctSetupRow::EKind::Number;
		Row.Caption = Parts[2];
		Row.Shown = Parts[3];
		Row.Value = IntOf(Parts, 4);
		Row.Min = IntOf(Parts, 5);
		Row.Max = IntOf(Parts, 6);
		Row.bVisible = IntOf(Parts, 7) != 0;
		if (Parts.IsValidIndex(8))
		{
			Parts[8].ParseIntoArray(Row.Choices, TEXT("|"), true);
		}
	}
	return Rows;
}

TArray<FScctAlternateRow> ScctHostSetup::AlternateRows(const FString& Setup, int32 MaxPlayers)
{
	TArray<FScctAlternateRow> Rows;
	for (const FString& Line : Lines(Text(EScctSetupText::Alternate, Setup, FString(), MaxPlayers)))
	{
		const TArray<FString> Parts = Fields(Line);
		if (Parts.Num() < 4)
		{
			continue;
		}

		FScctAlternateRow& Row = Rows.AddDefaulted_GetRef();
		if (Parts[0] == TEXT("ability"))
		{
			Row.Kind = FScctAlternateRow::EKind::Ability;
			Row.Id = IntOf(Parts, 1);
			Row.Side = IntOf(Parts, 2);
			Row.Caption = Parts[3];
			Row.Value = IntOf(Parts, 4);
		}
		else if (Parts[0] == TEXT("gadget"))
		{
			Row.Kind = FScctAlternateRow::EKind::Gadget;
			Row.Side = IntOf(Parts, 1);
			Row.Id = IntOf(Parts, 2);
			Row.Caption = Parts[3];
			Row.Value = IntOf(Parts, 4);
		}
		else
		{
			Row.Kind = FScctAlternateRow::EKind::Slots;
			Row.Side = IntOf(Parts, 1);
			Row.Value = IntOf(Parts, 2);
			Row.Most = IntOf(Parts, 3);
		}
	}
	return Rows;
}

FString ScctHostSetup::Option(const FString& Line, const TCHAR* Key)
{
	TArray<FString> Pairs;
	Line.ParseIntoArray(Pairs, TEXT("?"), true);
	for (const FString& Pair : Pairs)
	{
		FString Name, Value;
		if (Pair.Split(TEXT("="), &Name, &Value) && Name.Equals(Key, ESearchCase::IgnoreCase))
		{
			return Value;
		}
	}
	return FString();
}

FString ScctHostSetup::ModeLabel(const FString& Key)
{
	if (Key.Equals(TEXT("DiskHunt"), ESearchCase::IgnoreCase)) { return TEXT("Disk Hunt"); }
	if (Key.Equals(TEXT("MercDeathMatch"), ESearchCase::IgnoreCase)) { return TEXT("Merc Deathmatch"); }
	return Key;
}

FString ScctHostSetup::ModeKeyOf(int32 GameMode)
{
	switch (GameMode)
	{
	case 1: return TEXT("Extraction");
	case 2: return TEXT("Sabotage");
	case 3: return TEXT("Neutralization");
	case 4: return TEXT("DiskHunt");
	case 6: return TEXT("Deathmatch");
	case 7: return TEXT("MercDeathMatch");
	default: return TEXT("Story");
	}
}

FString ScctHostSetup::FilterPassword(const FString& Text)
{
	static const FString Allowed = TEXT("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ._-1234567890");
	FString Out;
	for (const TCHAR Character : Text)
	{
		int32 Index = INDEX_NONE;
		if (Out.Len() < MaxPasswordLength && Allowed.FindChar(Character, Index))
		{
			Out.AppendChar(Character);
		}
	}
	return Out;
}
