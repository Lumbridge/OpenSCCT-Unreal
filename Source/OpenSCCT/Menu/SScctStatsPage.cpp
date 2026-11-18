// The Stats page (the Godot client's StatsScreen and StatsModel over Social/ScctCareerSubsystem.h): the signed-in
// account's career record as the master last described it, with the four headline numbers, a row of categories and
// each category's counters grouped in cards. Signed out, or before the master answered, it says why there is nothing.

#include "Menu/ScctCareerPages.h"

#include "Menu/ScctMenuSubsystem.h"
#include "Social/ScctAccountSubsystem.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"

namespace
{
	struct FScctStatsRow
	{
		FString Category;
		FString Label;
		FString Value;
	};

	class SScctStatsPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctStatsPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			if (UScctCareerSubsystem* Career = ScctCareerUi::Career())
			{
				Career->Command(EScctCareerCommand::RefreshAccount);
				for (int32 Index = 0; Index < Career->GetInfo().StatCategories; ++Index)
				{
					Categories.Add(Career->GetString(EScctCareerString::StatCategoryName, Index));
				}
			}
			if (Categories.IsEmpty())
			{
				Categories = { TEXT("Overview") };
			}

			TSharedPtr<SScctButton> First;
			const TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
			const auto Action = [&Actions](const TSharedRef<SWidget>& Button) { Actions->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ Button ]; };
			Action(SAssignNew(First, SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("REFRESH")))
				.Description(FText::FromString(TEXT("Asks the master for your latest career totals.")))
				.OnPressed(FSimpleDelegate::CreateLambda([] { if (UScctCareerSubsystem* C = ScctCareerUi::Career()) { C->Command(EScctCareerCommand::RefreshAccountNow); } })));
			Action(SNew(SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("MATCH HISTORY")))
				.Description(FText::FromString(TEXT("Every round you played, with its scoreboard.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(TEXT("history")); })));
			Action(SNew(SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("ACHIEVEMENTS")))
				.Description(FText::FromString(TEXT("Every achievement with your progress and what it unlocks.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(TEXT("achievements")); })));
			Action(SNew(SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("ACCOUNT")))
				.Description(FText::FromString(TEXT("Sign in or out, your name, rank, prestige and title.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(TEXT("account")); })));
			DefaultFocus = First;

			const TSharedRef<SHorizontalBox> Chips = SNew(SHorizontalBox);
			for (int32 Index = 0; Index < Categories.Num(); ++Index)
			{
				Chips->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(Categories[Index].ToUpper()))
					.Selected_Lambda([this, Index] { return Category == Index; })
					.Description(FText::FromString(FString::Printf(TEXT("Your %s counters."), *Categories[Index].ToLower())))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Index] { SetCategory(Index); }))
				];
			}

			ChildSlot
			[
				SNew(SBox).MaxDesiredWidth(1180.0f).HAlign(HAlign_Left)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)[ SAssignNew(Header, SBox) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)[ SAssignNew(Summary, SBox) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)[ Chips ]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()[ SAssignNew(Body, SBox) ]
						+ SScrollBox::Slot().Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							ScctCareerUi::Line(TEXT("Career totals count official rounds played against a signed-in opponent. Totals update after the round is reported. ")
								TEXT("Categories can overlap. — means unavailable or no denominator."), ScctUi::Small(), ScctHudStyle::TextFaint())
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)[ Actions ]
				]
			];
			Rebuild();
			RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctStatsPage::Watch));
		}

		virtual FString GetTitle() const override { return TEXT("Player stats"); }
		virtual FString GetKicker() const override { return TEXT("Career record"); }
		virtual bool HasTabs() const override { return true; }
		virtual void StepTab(int32 Direction) override { SetCategory((Category + Direction + Categories.Num()) % Categories.Num()); }

		virtual void ShowSub(const FString& Sub) override
		{
			for (int32 Index = 0; Index < Categories.Num(); ++Index)
			{
				if (Categories[Index].Equals(Sub, ESearchCase::IgnoreCase))
				{
					SetCategory(Index);
				}
			}
		}

	private:
		void SetCategory(int32 Index)
		{
			Category = FMath::Clamp(Index, 0, Categories.Num() - 1);
			Rebuild();
		}

		EActiveTimerReturnType Watch(double, float)
		{
			const UScctAccountSubsystem* Account = ScctCareerUi::Account();
			const FString Key = FString::Printf(TEXT("%d|%d|%s"), ScctCareerUi::Revision(), Account != nullptr ? Account->GetRevision() : -1, *ScctCareerUi::Str(EScctCareerString::StatsStatus));
			if (Key != ShownKey)
			{
				Rebuild();
			}
			return EActiveTimerReturnType::Continue;
		}

		void Rebuild()
		{
			UScctCareerSubsystem* Career = ScctCareerUi::Career();
			const UScctAccountSubsystem* Account = ScctCareerUi::Account();
			ShownKey = FString::Printf(TEXT("%d|%d|%s"), ScctCareerUi::Revision(), Account != nullptr ? Account->GetRevision() : -1, *ScctCareerUi::Str(EScctCareerString::StatsStatus));

			TArray<FScctStatsRow> Rows;
			const int32 Count = Career != nullptr ? Career->GetInfo().Stats : 0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Rows.Add({ Career->GetString(EScctCareerString::StatCategory, Index), Career->GetString(EScctCareerString::StatLabel, Index), Career->GetString(EScctCareerString::StatValue, Index) });
			}
			const bool bKnown = ScctCareerUi::Flag(EScctCareerFlags::AccountKnown) && Rows.Num() > 0;

			// The name and rank, and where the numbers stand.
			FString Name = Account != nullptr ? Account->GetText(EScctAccountText::Name) : FString();
			const FString Rank = Account != nullptr && bKnown ? Account->GetText(EScctAccountText::RankTag) : FString();
			Header->SetContent(ScctUi::Card(TEXT("Career record"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ScctCareerUi::Line(bKnown && !Name.IsEmpty() ? Name : TEXT("Your career"), ScctUi::H2(), ScctHudStyle::Text(), false) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Font(ScctUi::Caps(13.0f)).Text(FText::FromString(Rank))
						.ColorAndOpacity(Account != nullptr ? UScctAccountSubsystem::ToColour(Account->GetInfo().RankColour) : ScctUi::Accent())
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)[ ScctCareerUi::Line(ScctCareerUi::Str(EScctCareerString::StatsStatus), ScctUi::Small(), ScctHudStyle::TextDim()) ],
				FMargin(20.0f, 14.0f)));

			const auto Value = [&Rows](const TCHAR* Label)
			{
				const FScctStatsRow* Row = Rows.FindByPredicate([Label](const FScctStatsRow& R) { return R.Label == Label; });
				return Row != nullptr ? Row->Value : FString(TEXT("—"));
			};
			if (bKnown)
			{
				const TSharedRef<SHorizontalBox> Tiles = SNew(SHorizontalBox);
				const TPair<const TCHAR*, const TCHAR*> Headline[] = {
					{ TEXT("Rounds"), TEXT("Rounds played") }, { TEXT("Wins"), TEXT("Wins") }, { TEXT("Win rate"), TEXT("Win rate") }, { TEXT("K / D"), TEXT("Kill / death ratio") } };
				for (const TPair<const TCHAR*, const TCHAR*>& Tile : Headline)
				{
					Tiles->AddSlot().FillWidth(1.0f).Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						ScctUi::Card(Tile.Key, ScctCareerUi::Line(Value(Tile.Value), ScctUi::Display(30.0f), ScctUi::Accent(), false), FMargin(18.0f, 12.0f))
					];
				}
				Summary->SetContent(Tiles);
			}
			else
			{
				Summary->SetContent(SNullWidget::NullWidget);
			}

			const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
			if (!bKnown)
			{
				const bool bSignedIn = ScctCareerUi::Flag(EScctCareerFlags::SignedIn);
				List->AddSlot().AutoHeight()
				[
					ScctUi::Card(TEXT("Your career starts here"), ScctCareerUi::Line(bSignedIn
						? TEXT("Your career stats are not available yet. Refresh to try again.")
						: ScctCareerUi::Flag(EScctCareerFlags::HasMaster)
							? TEXT("Sign in from Account to track your rounds, combat stats and progression. The rounds you play on this PC are listed in Match history either way.")
							: TEXT("No master server is set, so there is no account to keep stats for. The rounds you play on this PC are listed in Match history."),
						ScctUi::Body(), ScctHudStyle::Text()))
				];
				Body->SetContent(List);
				return;
			}

			const FString Current = Categories.IsValidIndex(Category) ? Categories[Category] : FString();
			TArray<FScctStatsRow> Shown = Rows.FilterByPredicate([&Current](const FScctStatsRow& R) { return R.Category == Current; });
			if (!ScctCareerUi::Flag(EScctCareerFlags::HasCareer))
			{
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ ScctCareerUi::Line(TEXT("This master does not provide detailed counters yet."), ScctUi::Small(), ScctHudStyle::TextDim()) ];
			}
			if (Current == TEXT("Maps"))
			{
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ ScctCareerUi::Line(TEXT("Base-map wins across variants. Custom-map wins are not tracked separately."), ScctUi::Small(), ScctHudStyle::TextDim()) ];
			}
			if (Value(TEXT("Rounds played")) == TEXT("0"))
			{
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[ ScctCareerUi::Line(TEXT("No counted rounds yet. Your career starts with your first eligible round."), ScctUi::Small(), ScctHudStyle::Text()) ];
			}

			// The groups of the category (StatsScreen.RenderStats).
			TArray<TPair<FString, TArray<FScctStatsRow>>> Groups;
			const auto AddGroup = [&Groups](const FString& Title, const TArray<FScctStatsRow>& GroupRows) { if (GroupRows.Num() > 0) { Groups.Add({ Title, GroupRows }); } };
			if (Current == TEXT("Gadgets") || Current == TEXT("Weapons") || Current == TEXT("Maps"))
			{
				for (const FScctStatsRow& Row : Shown)
				{
					FString Group, Label;
					if (!Row.Label.Split(TEXT(" · "), &Group, &Label))
					{
						Group = Current;
						Label = Row.Label;
					}
					TPair<FString, TArray<FScctStatsRow>>* Found = Groups.FindByPredicate([&Group](const TPair<FString, TArray<FScctStatsRow>>& G) { return G.Key == Group; });
					if (Found == nullptr)
					{
						Found = &Groups.Add_GetRef(TPair<FString, TArray<FScctStatsRow>>(Group, TArray<FScctStatsRow>()));
					}
					Found->Value.Add({ Row.Category, Label, Row.Value });
				}
			}
			else if (Current == TEXT("Overview"))
			{
				TArray<FScctStatsRow> Record, Combat;
				for (int32 Index = 0; Index < Shown.Num(); ++Index) { (Index < 4 ? Record : Combat).Add(Shown[Index]); }
				AddGroup(TEXT("Round record"), Record);
				AddGroup(TEXT("Combat performance"), Combat);
			}
			else if (Current == TEXT("Progression"))
			{
				TArray<FScctStatsRow> Unlocks, Rank2;
				for (const FScctStatsRow& Row : Shown)
				{
					const bool bUnlock = Row.Label == TEXT("Achievements earned") || Row.Label == TEXT("Cosmetics unlocked") || Row.Label == TEXT("Titles unlocked");
					(bUnlock ? Unlocks : Rank2).Add(Row);
				}
				AddGroup(TEXT("Unlocks"), Unlocks);
				AddGroup(TEXT("Rank & experience"), Rank2);
			}
			else
			{
				AddGroup(Current == TEXT("Spy") ? TEXT("Spy performance") : TEXT("Mercenary performance"), Shown);
			}

			const TSharedRef<SWrapBox> Cards = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.0f, 10.0f));
			for (const TPair<FString, TArray<FScctStatsRow>>& Group : Groups)
			{
				const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
				for (const FScctStatsRow& Row : Group.Value)
				{
					Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ ScctCareerUi::StatRow(Row.Label, Row.Value) ];
				}
				Cards->AddSlot()[ SNew(SBox).WidthOverride(560.0f)[ ScctUi::Card(Group.Key, Box, FMargin(20.0f, 14.0f)) ] ];
			}
			List->AddSlot().AutoHeight()[ Cards ];
			Body->SetContent(List);
		}

		TArray<FString> Categories;
		int32 Category = 0;
		FString ShownKey;
		TSharedPtr<SBox> Header;
		TSharedPtr<SBox> Summary;
		TSharedPtr<SBox> Body;
	};
}

TSharedRef<SScctMenuPage> ScctCareerPages::MakeStats(const FScctMenuHost& Host)
{
	return SNew(SScctStatsPage, Host);
}

TSharedPtr<SScctMenuPage> ScctCareerPages::Make(const FString& Name, const FScctMenuHost& Host)
{
	if (Name.Equals(TEXT("stats"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("career"), ESearchCase::IgnoreCase)) { return MakeStats(Host); }
	if (Name.Equals(TEXT("history"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("matchhistory"), ESearchCase::IgnoreCase)) { return MakeMatchHistory(Host); }
	if (Name.Equals(TEXT("achievements"), ESearchCase::IgnoreCase)) { return MakeAchievements(Host); }
	if (Name.Equals(TEXT("armoury"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("cosmetics"), ESearchCase::IgnoreCase)) { return MakeArmoury(Host); }
	return nullptr;
}
