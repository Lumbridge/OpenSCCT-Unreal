#include "Menu/ScctMenuPage.h"

#include "Menu/ScctDashboardCards.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctOverlayPages.h"
#include "Menu/SScctTutorialsPage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedPtr<SScctMenuPage> ScctMenuPages::Make(const FString& Name, const FScctMenuHost& Host)
{
	if (Name.Equals(TEXT("home"), ESearchCase::IgnoreCase)) { return MakeHome(Host); }
	if (Name.Equals(TEXT("play"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("offline"), ESearchCase::IgnoreCase)) { return MakePlay(Host); }
	if (Name.Equals(TEXT("online"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("servers"), ESearchCase::IgnoreCase)) { return MakeOnline(Host, false); }
	if (Name.Equals(TEXT("address"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("join"), ESearchCase::IgnoreCase)) { return MakeOnline(Host, true); }
	if (Name.Equals(TEXT("browser"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("lan"), ESearchCase::IgnoreCase))
	{
		// The online page on its LAN and saved servers' browser.
		const TSharedRef<SScctMenuPage> Page = MakeOnline(Host, false);
		Page->ShowSub(TEXT("browser"));
		return Page;
	}
	if (Name.Equals(TEXT("host"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("create"), ESearchCase::IgnoreCase)) { return MakeHostGame(Host); }
	if (Name.Equals(TEXT("hub"), ESearchCase::IgnoreCase)) { return MakeHub(Host); }
	if (Name.Equals(TEXT("account"), ESearchCase::IgnoreCase)) { return MakeAccount(Host); }
	if (Name.Equals(TEXT("demos"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("replays"), ESearchCase::IgnoreCase)) { return MakeDemos(Host); }
	if (Name.Equals(TEXT("content"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("maps"), ESearchCase::IgnoreCase)) { return MakeContent(Host); }
	if (Name.Equals(TEXT("settings"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("options"), ESearchCase::IgnoreCase)) { return MakeSettings(Host); }
	// The main menu dashboard's What's new and Announcement pages (Menu/ScctDashboardCards.h).
	if (Name.Equals(TEXT("whatsnew"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("changelog"), ESearchCase::IgnoreCase)) { return ScctDashboard::MakeWhatsNewPage(Host); }
	if (Name.Equals(TEXT("announcement"), ESearchCase::IgnoreCase)) { return ScctDashboard::MakeAnnouncementPage(Host); }
	if (Name.Equals(TEXT("credits"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("about"), ESearchCase::IgnoreCase)) { return MakeCredits(Host); }
	// The Equipment page and a map's strategic map (Menu/ScctOverlayPages.h).
	if (Name.Equals(TEXT("gear"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("equipment"), ESearchCase::IgnoreCase)) { return MakeGear(Host); }
	if (Name.Equals(TEXT("map"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("strategicmap"), ESearchCase::IgnoreCase)) { return MakeStrategicMap(Host); }
	// The Tutorials hub and the pages it opens (Menu/SScctTutorialsPage.h).
	return ScctTutorialPages::Make(Name, Host);
}

namespace ScctUi
{
	FString GameText(const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback)
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		return Menu != nullptr ? Menu->Text(Section, Key, Fallback) : FString(Fallback);
	}

	FString GameLabel(const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback)
	{
		FString Label = GameText(Section, Key, Fallback).TrimStartAndEnd();
		for (const TCHAR* UnitSuffix : { TEXT(" (min)"), TEXT(" (sec)") })
		{
			Label.RemoveFromEnd(UnitSuffix, ESearchCase::IgnoreCase);
		}
		TArray<FString> Words;
		Label.ParseIntoArray(Words, TEXT(" "));
		bool bInBrackets = false;
		for (int32 Index = 0; Index < Words.Num(); ++Index)
		{
			FString& Word = Words[Index];
			bInBrackets |= Word.StartsWith(TEXT("("));
			// "Volume" becomes "volume"; "X", "PMC" and "(Spy)" are left as they are.
			const bool bCapitalised = Word.Len() > 1 && FChar::IsUpper(Word[0]) && FChar::IsLower(Word[1]);
			if (Index > 0 && !bInBrackets && bCapitalised)
			{
				Word[0] = FChar::ToLower(Word[0]);
			}
			bInBrackets &= !Word.EndsWith(TEXT(")"));
		}
		return FString::Join(Words, TEXT(" "));
	}

	FString GameTextItem(const TCHAR* Section, const TCHAR* Key, int32 Index, const TCHAR* Fallback)
	{
		// ("Spy","Mercenary"): the original's list syntax.
		const FString List = GameText(Section, Key, TEXT(""));
		TArray<FString> Items;
		int32 Start = INDEX_NONE;
		for (int32 At = 0; At < List.Len(); ++At)
		{
			if (List[At] == TEXT('"'))
			{
				if (Start == INDEX_NONE)
				{
					Start = At + 1;
				}
				else
				{
					Items.Add(List.Mid(Start, At - Start));
					Start = INDEX_NONE;
				}
			}
		}
		return Items.IsValidIndex(Index) && !Items[Index].IsEmpty() ? Items[Index] : FString(Fallback);
	}

	TSharedRef<SWidget> Card(const FString& Title, const TSharedRef<SWidget>& Content, const FMargin& Padding)
	{
		const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		if (!Title.IsEmpty())
		{
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ Label(Title, ScctHudStyle::TextDim(), 11.5f) ];
		}
		Box->AddSlot().FillHeight(1.0f)[ Content ];
		return Panel(Box, Padding, 0.7f);
	}

	TSharedRef<SWidget> DescriptionPane()
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Font(H2())
				.ColorAndOpacity(ScctHudStyle::Text())
				.AutoWrapText(true)
				.Text(TAttribute<FText>::CreateLambda([] { return FText::FromString(GetDescriptionTitle().ToString().ToUpper()); }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)[ Rule(0.12f) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(Body())
				.ColorAndOpacity(ScctHudStyle::TextDim())
				.AutoWrapText(true)
				.LineHeightPercentage(1.15f)
				.Text(TAttribute<FText>::CreateLambda([] { return GetDescriptionBody(); }))
			];
	}

	void Confirm(const TSharedPtr<SScctDialogLayer>& Dialogs, const FString& Title, const FString& Body, const FString& YesLabel, const FString& NoLabel, TFunction<void()> OnYes, bool bDanger)
	{
		if (!Dialogs.IsValid())
		{
			if (OnYes)
			{
				OnYes();
			}
			return;
		}
		FScctDialog Dialog;
		Dialog.Title = Title;
		Dialog.Body = Body;
		Dialog.Buttons.Add({ NoLabel, EScctButtonLook::Secondary, nullptr });
		Dialog.Buttons.Add({ YesLabel, bDanger ? EScctButtonLook::Danger : EScctButtonLook::Primary, [OnYes](const FString&) { if (OnYes) { OnYes(); } } });
		Dialog.FocusButton = 0;
		Dialog.CancelButton = 0;
		Dialogs->Show(Dialog);
	}

	FString BuildLabel()
	{
		UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
		const FString Version = Menu != nullptr ? Menu->GetGameVersion() : FString();
		return Version.IsEmpty() ? TEXT("OPENSCCT · UNREAL") : FString::Printf(TEXT("OPENSCCT %s · UNREAL"), *Version);
	}
}
