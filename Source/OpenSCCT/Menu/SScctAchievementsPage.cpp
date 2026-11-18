// The Achievements page (the Godot client's AchievementsScreen over Social/ScctCareerSubsystem.h): every achievement
// of OpenSCCT's shared catalogue by group, each with how far along the signed-in account is, and beside them the
// picked one in detail: what to do, its progress or the day it was earned, and the cosmetics it unlocks by name. The
// list is complete signed out too; progress and earned dates come from the master's account answer.

#include "Menu/ScctCareerPages.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SProgressBar.h"

namespace
{
	/** The achievement groups the bridge knows (AchievementCategory: General, Spy, Merc, Weapons, Gadgets, Map). */
	constexpr int32 ScctAchievementGroups = 6;

	struct FScctAchievementRow
	{
		int32 Index = 0;
		FScctCareerAchievement Data = {};
		FString Id;
		FString Title;
		float Fraction() const
		{
			return (Data.Flags & EScctCareerAchievementFlags::Earned) != 0 ? 1.0f : Data.Target > 0 ? FMath::Clamp(static_cast<float>(Data.Current) / Data.Target, 0.0f, 1.0f) : 0.0f;
		}
	};

	TSharedRef<SWidget> ScctAchievementBar(float Fraction, bool bDone, float Height = 4.0f)
	{
		return SNew(SBox).HeightOverride(Height)
			[
				SNew(SProgressBar).Percent(Fraction).FillColorAndOpacity(bDone ? ScctUi::Accent() : ScctUi::Accent() * FLinearColor(1.0f, 1.0f, 1.0f, 0.55f))
			];
	}

	class SScctAchievementsPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctAchievementsPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			if (UScctCareerSubsystem* Career = ScctCareerUi::Career())
			{
				Career->Command(EScctCareerCommand::RefreshAccount);
			}

			const TSharedRef<SHorizontalBox> Filters = SNew(SHorizontalBox);
			for (int32 Filter = -1; Filter < ScctAchievementGroups; ++Filter)
			{
				const FString Name = Filter < 0 ? FString(TEXT("All")) : ScctCareerUi::Str(EScctCareerString::CategoryName, Filter);
				TSharedPtr<SScctButton> Chip;
				Filters->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SAssignNew(Chip, SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(Name.ToUpper()))
					.Selected_Lambda([this, Filter] { return Group == Filter; })
					.Description(FText::FromString(Filter < 0 ? TEXT("Every achievement.") : FString::Printf(TEXT("The %s achievements."), *Name.ToLower())))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Filter] { SetGroup(Filter); }))
				];
				if (Filter < 0)
				{
					DefaultFocus = Chip;
				}
			}

			ChildSlot
			[
				SNew(SBox).MaxDesiredWidth(1240.0f).HAlign(HAlign_Left)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						ScctUi::Card(TEXT("Your achievements"),
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true)
								.Text_Lambda([] { return FText::FromString(ScctCareerUi::Str(EScctCareerString::AchievementsSummary)); })
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SBox).WidthOverride(260.0f).HeightOverride(6.0f)
								.Visibility_Lambda([] { return ScctCareerUi::Flag(EScctCareerFlags::AccountKnown) ? EVisibility::Visible : EVisibility::Collapsed; })
								[
									SNew(SProgressBar).FillColorAndOpacity(ScctUi::Accent())
									.Percent_Lambda([]
									{
										const UScctCareerSubsystem* Career = ScctCareerUi::Career();
										const FScctCareerInfo Info = Career != nullptr ? Career->GetInfo() : FScctCareerInfo();
										return TOptional<float>(Info.Achievements > 0 ? static_cast<float>(Info.AchievementsEarned) / Info.Achievements : 0.0f);
									})
								]
							],
							FMargin(20.0f, 14.0f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)[ Filters ]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 20.0f, 0.0f)
						[
							SNew(SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
							+ SScrollBox::Slot()[ SAssignNew(List, SVerticalBox) ]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).WidthOverride(448.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()[ SAssignNew(Detail, SBox) ]
							]
						]
					]
				]
			];
			Rebuild();
			RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctAchievementsPage::Watch));
		}

		virtual FString GetTitle() const override { return TEXT("Achievements"); }
		virtual FString GetKicker() const override { return TEXT("Challenges and their unlocks"); }
		virtual bool HasTabs() const override { return true; }
		virtual void StepTab(int32 Direction) override { SetGroup((Group + 1 + Direction + ScctAchievementGroups + 1) % (ScctAchievementGroups + 1) - 1); }

		virtual void ShowSub(const FString& Sub) override
		{
			// "achievements/next": the dashboard's next unlock; "achievements/<id>": that one.
			if (Sub.Equals(TEXT("next"), ESearchCase::IgnoreCase))
			{
				const UScctCareerSubsystem* Career = ScctCareerUi::Career();
				const int32 Next = Career != nullptr ? Career->GetInfo().NextUnlock : -1;
				bPickNext = Next < 0;
				if (Next >= 0)
				{
					Selected = ScctCareerUi::Str(EScctCareerString::AchievementId, Next);
				}
			}
			else
			{
				Selected = Sub;
			}
			Rebuild();
		}

	private:
		void SetGroup(int32 InGroup)
		{
			Group = InGroup;
			Rebuild();
		}

		EActiveTimerReturnType Watch(double, float)
		{
			if (ScctCareerUi::Revision() != ShownRevision)
			{
				if (bPickNext)
				{
					const UScctCareerSubsystem* Career = ScctCareerUi::Career();
					if (Career != nullptr && Career->GetInfo().NextUnlock >= 0)
					{
						bPickNext = false;
						Selected = Career->GetString(EScctCareerString::AchievementId, Career->GetInfo().NextUnlock);
					}
				}
				Rebuild();
			}
			return EActiveTimerReturnType::Continue;
		}

		void Rebuild()
		{
			UScctCareerSubsystem* Career = ScctCareerUi::Career();
			ShownRevision = ScctCareerUi::Revision();
			List->ClearChildren();
			Rows.Reset();
			const int32 Count = Career != nullptr ? Career->GetInfo().Achievements : 0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FScctAchievementRow Row;
				Row.Index = Index;
				if (Career->GetAchievement(Index, Row.Data))
				{
					Row.Id = Career->GetString(EScctCareerString::AchievementId, Index);
					Row.Title = Career->GetString(EScctCareerString::AchievementTitle, Index);
					Rows.Add(Row);
				}
			}

			const FScctAchievementRow* Picked = nullptr;
			for (int32 Category = 0; Category < ScctAchievementGroups; ++Category)
			{
				if (Group >= 0 && Group != Category)
				{
					continue;
				}
				TArray<const FScctAchievementRow*> InGroup;
				int32 Earned = 0;
				for (const FScctAchievementRow& Row : Rows)
				{
					if (Row.Data.Category == Category)
					{
						InGroup.Add(&Row);
						Earned += (Row.Data.Flags & EScctCareerAchievementFlags::Earned) != 0 ? 1 : 0;
					}
				}
				if (InGroup.IsEmpty())
				{
					continue;
				}
				List->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 4.0f)
				[
					ScctCareerUi::Line(FString::Printf(TEXT("%s · %d / %d"), *ScctCareerUi::Str(EScctCareerString::CategoryName, Category).ToUpper(), Earned, InGroup.Num()), ScctUi::Caps(15.0f), ScctUi::Accent(), false)
				];
				const FString Note = ScctCareerUi::Str(EScctCareerString::CategoryNote, Category);
				if (!Note.IsEmpty())
				{
					List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[ ScctCareerUi::Line(Note, ScctUi::Small(), ScctHudStyle::TextDim()) ];
				}
				const TSharedRef<SWrapBox> Tiles = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, 8.0f));
				for (const FScctAchievementRow* Row : InGroup)
				{
					if (Picked == nullptr && (Selected.IsEmpty() || Row->Id == Selected))
					{
						Picked = Row;
					}
					Tiles->AddSlot()[ Tile(*Row) ];
				}
				List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[ Tiles ];
			}
			if (Picked != nullptr && Selected.IsEmpty())
			{
				Selected = Picked->Id;
			}
			Detail->SetContent(Picked != nullptr ? BuildDetail(*Picked) : SNullWidget::NullWidget);
		}

		/** The picked one's detail, without rebuilding the tiles (the focus stays on the tile). */
		void ShowDetail()
		{
			const FScctAchievementRow* Picked = Rows.FindByPredicate([this](const FScctAchievementRow& R) { return R.Id == Selected; });
			Detail->SetContent(Picked != nullptr ? BuildDetail(*Picked) : SNullWidget::NullWidget);
		}

		TSharedRef<SWidget> Tile(const FScctAchievementRow& Row)
		{
			const bool bDone = (Row.Data.Flags & EScctCareerAchievementFlags::Earned) != 0;
			const FString Id = Row.Id;
			const FString Status = bDone ? FString(TEXT("EARNED")) : ScctCareerUi::Str(EScctCareerString::AchievementStatus, Row.Index)
				+ ((Row.Data.Flags & EScctCareerAchievementFlags::NextUnlock) != 0 ? TEXT("  ·  NEXT UNLOCK") : TEXT(""));
			return SNew(SBox).WidthOverride(340.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SScctButton).Look(EScctButtonLook::Chip).FillWidth(true)
						.Text(FText::FromString(Row.Title))
						.SubText(FText::FromString(Status))
						.Colour(bDone ? ScctUi::Accent() : ScctHudStyle::TextDim())
						.Selected_Lambda([this, Id] { return Selected == Id; })
						.Description(FText::FromString(ScctCareerUi::Str(EScctCareerString::AchievementDescription, Row.Index)))
						.OnPressed(FSimpleDelegate::CreateLambda([this, Id] { Selected = Id; ShowDetail(); }))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 3.0f, 2.0f, 0.0f)[ ScctAchievementBar(Row.Fraction(), bDone, 3.0f) ]
				];
		}

		TSharedRef<SWidget> BuildDetail(const FScctAchievementRow& Row)
		{
			const bool bDone = (Row.Data.Flags & EScctCareerAchievementFlags::Earned) != 0;
			const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			Box->AddSlot().AutoHeight()[ ScctUi::Label(ScctCareerUi::Str(EScctCareerString::CategoryName, Row.Data.Category).ToUpper()) ];
			Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[ ScctCareerUi::Line(Row.Title, ScctUi::H1(), bDone ? ScctUi::Accent() : ScctHudStyle::Text()) ];
			Box->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[ ScctCareerUi::Line(ScctCareerUi::Str(EScctCareerString::AchievementDescription, Row.Index), ScctUi::Body(), ScctHudStyle::Text()) ];
			Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					ScctCareerUi::Line(ScctCareerUi::Str(EScctCareerString::AchievementStatusLong, Row.Index), ScctUi::Body(true), bDone ? ScctUi::Accent() : ScctHudStyle::Text(), false)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ ScctAchievementBar(Row.Fraction(), bDone, 6.0f) ]
			];
			if ((Row.Data.Flags & EScctCareerAchievementFlags::Reported) == 0 && !bDone && ScctCareerUi::Flag(EScctCareerFlags::AccountKnown))
			{
				Box->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					ScctCareerUi::Line(TEXT("The master gave no progress for this one: it is counted from your stats, feats from zero."), ScctUi::Small(), ScctHudStyle::TextFaint())
				];
			}
			Box->AddSlot().AutoHeight().Padding(0.0f, 18.0f, 0.0f, 6.0f)[ ScctUi::Label(bDone ? TEXT("UNLOCKED") : TEXT("UNLOCKS")) ];
			TArray<FString> Rewards;
			ScctCareerUi::Str(EScctCareerString::AchievementRewards, Row.Index).ParseIntoArrayLines(Rewards);
			for (const FString& Reward : Rewards)
			{
				Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
				[
					ScctCareerUi::Line((Row.Data.Flags & EScctCareerAchievementFlags::HasRewards) != 0 ? TEXT("•  ") + Reward : Reward, ScctUi::Body(),
						(Row.Data.Flags & EScctCareerAchievementFlags::HasRewards) != 0 ? ScctHudStyle::Text() : ScctHudStyle::TextDim())
				];
			}
			if ((Row.Data.Flags & EScctCareerAchievementFlags::HasRewards) != 0 && !bDone)
			{
				Box->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)[ ScctCareerUi::Line(TEXT("LOCKED until earned."), ScctUi::Caps(11.0f), ScctHudStyle::TextFaint()) ];
			}
			return ScctUi::Card(TEXT("Challenge details"), Box, FMargin(20.0f, 16.0f));
		}

		TArray<FScctAchievementRow> Rows;
		TSharedPtr<SVerticalBox> List;
		TSharedPtr<SBox> Detail;
		FString Selected;
		int32 Group = -1;
		int32 ShownRevision = -2;
		bool bPickNext = false;
	};
}

TSharedRef<SScctMenuPage> ScctCareerPages::MakeAchievements(const FScctMenuHost& Host)
{
	return SNew(SScctAchievementsPage, Host);
}
