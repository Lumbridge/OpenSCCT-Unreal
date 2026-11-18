#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"
#include "Engine/GameInstance.h"
#include "Social/ScctAccountSubsystem.h"
#include "Social/ScctCareerSubsystem.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

/**
 * The career pages of the menus over Social/ScctCareerSubsystem.h (the Godot client's StatsScreen, the match history
 * of DemosScreen, AchievementsScreen and CareerPanels):
 *   - "stats": the career record of the signed-in account by category (Overview, Spy, Mercenary, Gadgets, Weapons,
 *     Maps, Progression) with the four headline numbers (Menu/SScctStatsPage.cpp);
 *   - "history" (or "matchhistory"): every round played on this PC joined with the account's history on the master,
 *     newest first, each with its scoreboard, its timeline from the master and its replay when one is saved
 *     (Menu/SScctMatchHistoryPage.cpp); "history/first" opens the newest round's scoreboard;
 *   - "achievements": every achievement by group with the account's progress, and the picked one in detail with what
 *     it unlocks (Menu/SScctAchievementsPage.cpp); "achievements/next" picks the dashboard's next unlock.
 */
namespace ScctCareerPages
{
	/** The page for Name, or null when Name is none of the career pages. */
	TSharedPtr<SScctMenuPage> Make(const FString& Name, const FScctMenuHost& Host);

	TSharedRef<SScctMenuPage> MakeStats(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeMatchHistory(const FScctMenuHost& Host);
	TSharedRef<SScctMenuPage> MakeAchievements(const FScctMenuHost& Host);

	/** "armoury" (or "cosmetics"): the cosmetics of each side by slot and the kit equipped (Menu/SScctArmouryPage.cpp); "armoury/merc" opens on the mercenary. */
	TSharedRef<SScctMenuPage> MakeArmoury(const FScctMenuHost& Host);
}

/** Small shared pieces of the career pages (the Godot CareerPanels). */
namespace ScctCareerUi
{
	inline UScctCareerSubsystem* Career() { return UScctCareerSubsystem::GetCurrent(); }

	/** The account subsystem of the same game instance (the name and rank). */
	inline UScctAccountSubsystem* Account()
	{
		const UScctCareerSubsystem* Subsystem = Career();
		const UGameInstance* Instance = Subsystem != nullptr ? Subsystem->GetGameInstance() : nullptr;
		return Instance != nullptr ? Instance->GetSubsystem<UScctAccountSubsystem>() : nullptr;
	}

	inline FString Str(EScctCareerString Kind, int32 Index = 0, int32 Sub = 0)
	{
		const UScctCareerSubsystem* Subsystem = Career();
		return Subsystem != nullptr ? Subsystem->GetString(Kind, Index, Sub) : FString();
	}

	inline bool Flag(int32 Flag)
	{
		const UScctCareerSubsystem* Subsystem = Career();
		return Subsystem != nullptr && Subsystem->HasFlag(Flag);
	}

	inline int32 Revision()
	{
		const UScctCareerSubsystem* Subsystem = Career();
		return Subsystem != nullptr ? Subsystem->GetInfo().Revision : -1;
	}

	inline TSharedRef<SWidget> Line(const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Colour, bool bWrap = true)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font).ColorAndOpacity(Colour).AutoWrapText(bWrap);
	}

	/** A stat row: the label, then the value at the right in the accent. */
	inline TSharedRef<SWidget> StatRow(const FString& Label, const FString& Value)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Line(Label, ScctUi::Body(), ScctHudStyle::TextDim()) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.0f, 0.0f, 0.0f, 0.0f)[ Line(Value, ScctUi::Caps(15.0f), ScctUi::Accent(), false) ];
	}
}
