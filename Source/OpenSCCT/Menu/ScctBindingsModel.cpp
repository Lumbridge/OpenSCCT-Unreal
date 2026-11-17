#include "Menu/ScctBindingsModel.h"

#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OpenSCCT.h"
#include "Play/ScctControlSettings.h"
#include "Runtime/ScctBindingsApi.h"

namespace
{

	/** Reads one of the tables' texts, which are handed over as "copy what fits, return the full length". */
	template <typename TRead>
	FString ReadText(TRead&& Read)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(1024);
		const int32 Length = Read(Buffer.GetData(), Buffer.Num());
		if (Length >= Buffer.Num())
		{
			Buffer.SetNumZeroed(Length + 1);
			Read(Buffer.GetData(), Buffer.Num());
		}
		Buffer.Last() = TEXT('\0');
		return FString(Buffer.GetData());
	}

	const TCHAR* SideName(bool bMerc) { return bMerc ? TEXT("merc") : TEXT("spy"); }
}


// ---- The model -----------------------------------------------------------------------------------------------------

const FScctBindingsApi* ScctBindings::Api()
{
	// The player's files first: otherwise the draft would start from the defaults and a save write them over the file.
	UScctControlSettings::Get()->EnsureLoaded();
	return ScctFeature<FScctBindingsApi>();
}

TArray<FScctBindingRow> ScctBindings::Rows(bool bMerc)
{
	TArray<FScctBindingRow> Rows;
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		return Rows;
	}
	const FString Text = ReadText([Bindings, bMerc](TCHAR* Buffer, int32 Capacity) { return Bindings->GetRows(bMerc ? 1 : 0, Buffer, Capacity); });
	TArray<FString> Lines;
	Text.ParseIntoArray(Lines, TEXT("\n"));
	for (const FString& Line : Lines)
	{
		TArray<FString> Fields;
		Line.ParseIntoArray(Fields, TEXT("\t"), false);
		if (Fields.Num() < 5)
		{
			continue;
		}
		FScctBindingRow& Row = Rows.AddDefaulted_GetRef();
		Row.Id = Fields[0];
		Row.Category = Fields[1];
		Row.Label = Fields[2];
		Row.Description = Fields[3];
		Row.bDoubleTap = Fields[4] == TEXT("1");
		for (int32 Index = 5; Index < Fields.Num(); ++Index)
		{
			if (!Fields[Index].IsEmpty())
			{
				Row.Keys.Add(Fields[Index]);
			}
		}
	}
	return Rows;
}

FString ScctBindings::DisplayName(const FString& Key)
{
	static const TMap<FString, FString> Names = {
		{ TEXT("LeftMouse"), TEXT("Left Mouse") }, { TEXT("RightMouse"), TEXT("Right Mouse") }, { TEXT("MiddleMouse"), TEXT("Middle Mouse") },
		{ TEXT("MouseX1"), TEXT("Mouse 4") }, { TEXT("MouseX2"), TEXT("Mouse 5") },
		{ TEXT("MouseWheelUp"), TEXT("Wheel Up") }, { TEXT("MouseWheelDown"), TEXT("Wheel Down") },
		{ TEXT("PageUp"), TEXT("Page Up") }, { TEXT("PageDown"), TEXT("Page Down") }, { TEXT("CapsLock"), TEXT("Caps Lock") },
		{ TEXT("NumLock"), TEXT("Num Lock") }, { TEXT("ScrollLock"), TEXT("Scroll Lock") },
		{ TEXT("GreyStar"), TEXT("Num *") }, { TEXT("GreyPlus"), TEXT("Num +") }, { TEXT("GreyMinus"), TEXT("Num -") },
		{ TEXT("GreySlash"), TEXT("Num /") }, { TEXT("NumPadPeriod"), TEXT("Num .") },
		{ TEXT("Semicolon"), TEXT(";") }, { TEXT("EqualSign"), TEXT("=") }, { TEXT("Comma"), TEXT(",") }, { TEXT("Minus"), TEXT("-") },
		{ TEXT("Period"), TEXT(".") }, { TEXT("Slash"), TEXT("/") }, { TEXT("LeftBracket"), TEXT("[") }, { TEXT("Backslash"), TEXT("\\") },
		{ TEXT("RightBracket"), TEXT("]") }, { TEXT("SingleQuote"), TEXT("'") }, { TEXT("Tilde"), TEXT("~") },
	};
	TArray<FString> Parts;
	Key.ParseIntoArray(Parts, TEXT("+"));
	FString Text;
	bool bDouble = false;
	for (const FString& Part : Parts)
	{
		if (Part.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
		{
			bDouble = true;
			continue;
		}
		FString Shown = Part;
		if (const FString* Found = Names.Find(Part))
		{
			Shown = *Found;
		}
		else if (Part.StartsWith(TEXT("NumPad")) && Part.Len() == 7)
		{
			Shown = TEXT("Num ") + Part.Right(1);
		}
		Text += Text.IsEmpty() ? Shown : TEXT(" + ") + Shown;
	}
	return bDouble ? TEXT("Double-tap ") + Text : Text;
}

FString ScctBindings::Check(bool bMerc, const FString& ActionId, const FString& Key)
{
	const FScctBindingsApi* Bindings = Api();
	return Bindings == nullptr ? FString(TEXT("The bridge is not running."))
		: ReadText([&](TCHAR* Buffer, int32 Capacity) { return Bindings->Check(bMerc ? 1 : 0, *ActionId, *Key, Buffer, Capacity); });
}

FString ScctBindings::Conflict(bool bMerc, const FString& ActionId, const FString& OldKey, const FString& Key, bool& bOutAlreadyBound, bool& bOutCanSwap)
{
	bOutAlreadyBound = false;
	bOutCanSwap = false;
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		return FString();
	}
	int32 Flags = 0;
	const FString Text = ReadText([&](TCHAR* Buffer, int32 Capacity) { return Bindings->Conflict(bMerc ? 1 : 0, *ActionId, *OldKey, *Key, &Flags, Buffer, Capacity); });
	bOutAlreadyBound = (Flags & EScctBindingConflict::AlreadyBound) != 0;
	bOutCanSwap = (Flags & EScctBindingConflict::CanSwap) != 0;
	return Text;
}

bool ScctBindings::Commit(bool bMerc, const FScctBindingRow& Row, const FString& OldKey, const FString& Key, int32 Mode, const FString& ConflictWith, FString& OutError)
{
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		OutError = TEXT("The bridge is not running.");
		return false;
	}
	OutError = ReadText([&](TCHAR* Buffer, int32 Capacity) { return Bindings->Commit(bMerc ? 1 : 0, *Row.Id, *OldKey, *Key, Mode, Buffer, Capacity); });
	const bool bDone = OutError.IsEmpty();
	static const TCHAR* const ModeNames[] = { TEXT("share"), TEXT("replace"), TEXT("swap") };
	const FString How = ConflictWith.IsEmpty() ? FString(TEXT("the key was free"))
		: FString::Printf(TEXT("conflict with %s: %s"), *ConflictWith, ModeNames[FMath::Clamp(Mode, 0, 2)]);
	if (bDone)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s %s -> %s (%s%s)"), SideName(bMerc), *Key, *Row.Label, *How,
			OldKey.IsEmpty() ? TEXT(", added as another key") : *FString::Printf(TEXT(", in place of %s"), *OldKey));
	}
	else
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctBind: %s %s -> %s refused: %s"), SideName(bMerc), *Key, *Row.Label, *OutError);
	}
	return bDone;
}

void ScctBindings::Clear(bool bMerc, const FScctBindingRow& Row, const FString& Key)
{
	if (const FScctBindingsApi* Bindings = Api())
	{
		Bindings->Clear(bMerc ? 1 : 0, *Row.Id, *Key);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s %s cleared from %s"), SideName(bMerc), *Key, *Row.Label);
	}
}

void ScctBindings::ResetKeys(bool bMerc)
{
	if (const FScctBindingsApi* Bindings = Api())
	{
		Bindings->ResetKeys(bMerc ? 1 : 0);
		FString Sample;
		for (const FScctBindingRow& Row : Rows(bMerc))
		{
			if (Row.Id == TEXT("use") || Row.Id == TEXT("jump") || Row.Id == TEXT("fire"))
			{
				Sample += FString::Printf(TEXT("%s%s on %s"), Sample.IsEmpty() ? TEXT("") : TEXT(", "), *Row.Label, Row.Keys.Num() > 0 ? *FString::Join(Row.Keys, TEXT(" / ")) : TEXT("nothing"));
			}
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s keys reset to the defaults (%s)"), IsSplit() ? SideName(bMerc) : TEXT("shared"), *Sample);
	}
}

bool ScctBindings::IsSplit()
{
	const FScctBindingsApi* Bindings = Api();
	return Bindings == nullptr || Bindings->GetSplit() != 0;
}

void ScctBindings::SetSplit(bool bSplit)
{
	if (const FScctBindingsApi* Bindings = Api())
	{
		Bindings->SetSplit(bSplit ? 1 : 0);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s"), bSplit ? TEXT("the spy and the merc have a key layout each") : TEXT("both sides play the spy's key layout"));
	}
}

void ScctBindings::Apply()
{
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr || Bindings->HasEdits() == 0)
	{
		return;
	}
	Bindings->Apply();
	UScctControlSettings::Get()->NotifyRebound();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: key edits applied; a running match plays them from its next frame"));
}

void ScctBindings::Save()
{
	const FScctBindingsApi* Bindings = Api();
	if (Bindings != nullptr && Bindings->HasUnsaved() != 0)
	{
		UScctControlSettings::Get()->SaveBindings();
	}
}

void ScctBindings::Discard()
{
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		return;
	}
	const bool bChanged = Bindings->HasUnsaved() != 0;
	Bindings->Discard();
	if (bChanged)
	{
		UScctControlSettings::Get()->NotifyRebound();
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: key edits thrown away; the saved keys are in play again"));
	}
}

bool ScctBindings::HasEdits()
{
	const FScctBindingsApi* Bindings = ScctFeature<FScctBindingsApi>();
	return Bindings != nullptr && Bindings->HasEdits() != 0;
}

bool ScctBindings::HasUnsaved()
{
	const FScctBindingsApi* Bindings = ScctFeature<FScctBindingsApi>();
	return Bindings != nullptr && Bindings->HasUnsaved() != 0;
}

void ScctBindings::RunUnattendedTest()
{
	static bool bDone = false;
	FString Steps;
	if (bDone || !FParse::Value(FCommandLine::Get(), TEXT("ScctBindTest="), Steps, false))
	{
		return;
	}
	bDone = true;
	const FScctBindingsApi* Bindings = Api();
	if (Bindings == nullptr)
	{
		UE_LOG(LogOpenSCCT, Error, TEXT("-ScctBindTest: the bridge's bindings editor is missing"));
		return;
	}

	const FString File = UScctControlSettings::GetBindingsFile();
	const FDateTime Before = IFileManager::Get().GetTimeStamp(*File);
	Bindings->Begin();
	TArray<FString> Entries;
	Steps.ParseIntoArray(Entries, TEXT(";"));
	for (const FString& Entry : Entries)
	{
		TArray<FString> Parts;
		Entry.ParseIntoArray(Parts, TEXT(","));
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
		}
		const bool bMerc = Parts.Num() > 0 && Parts[0].Equals(TEXT("merc"), ESearchCase::IgnoreCase);
		if (Parts.Num() == 2 && Parts[1].Equals(TEXT("reset"), ESearchCase::IgnoreCase))
		{
			ResetKeys(bMerc);
			continue;
		}
		if (Parts.Num() < 3)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctBindTest: '%s' is not <side>,<key>,<action>[,replace|swap|share] or <side>,reset"), *Entry);
			continue;
		}

		const FString& Key = Parts[1];
		const FScctBindingRow* Row = nullptr;
		const TArray<FScctBindingRow> SideRows = Rows(bMerc);
		for (const FScctBindingRow& Each : SideRows)
		{
			if (Each.Id.Equals(Parts[2], ESearchCase::IgnoreCase) || Each.Label.Equals(Parts[2], ESearchCase::IgnoreCase))
			{
				Row = &Each;
				break;
			}
		}
		if (Row == nullptr)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("-ScctBindTest: the %s has no action '%s'"), SideName(bMerc), *Parts[2]);
			continue;
		}

		// As if the player clicked the action's primary key and pressed Key.
		const FString OldKey = Row->Keys.Num() > 0 ? Row->Keys[0] : FString();
		const FString Refused = Check(bMerc, Row->Id, Key);
		if (!Refused.IsEmpty())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctBind: %s %s -> %s refused at capture: %s"), SideName(bMerc), *Key, *Row->Label, *Refused);
			continue;
		}
		bool bAlready = false, bCanSwap = false;
		const FString With = Conflict(bMerc, Row->Id, OldKey, Key, bAlready, bCanSwap);
		int32 Mode = static_cast<int32>(EScctBindingCommit::Share);
		if (!With.IsEmpty())
		{
			const FString Answer = Parts.Num() > 3 ? Parts[3] : FString(TEXT("replace"));
			Mode = Answer.Equals(TEXT("swap"), ESearchCase::IgnoreCase) && bCanSwap ? static_cast<int32>(EScctBindingCommit::Swap)
				: Answer.Equals(TEXT("share"), ESearchCase::IgnoreCase) ? static_cast<int32>(EScctBindingCommit::Share)
				: static_cast<int32>(EScctBindingCommit::Replace);
		}
		FString Error;
		Commit(bMerc, *Row, OldKey, Key, Mode, With, Error);
		for (const FScctBindingRow& After : ScctBindings::Rows(bMerc))
		{
			if (After.Id == Row->Id)
			{
				UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: the %s's %s is now on %s"), SideName(bMerc), *After.Label, *FString::Join(After.Keys, TEXT(" / ")));
			}
		}
	}

	// Put in play and written, as the settings page does when it closes.
	Apply();
	Save();
	const FDateTime After = IFileManager::Get().GetTimeStamp(*File);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctBind: %s %s (was %s, now %s)"), *File, After != Before ? TEXT("written") : TEXT("unchanged"),
		Before == FDateTime::MinValue() ? TEXT("missing") : *Before.ToString(), After == FDateTime::MinValue() ? TEXT("missing") : *After.ToString());
}
