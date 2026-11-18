// The Armoury page (the Godot client's ArmouryScreen and AppearanceSyncPanel over Social/ScctArmourySubsystem.h): the
// spy's and the mercenary's cosmetics by slot (suit or armour, light colour, pistol or each gun) with a colour swatch
// per item, which of them the account unlocked and by what, the kit equipped on each slot, the two cosmetics options,
// and the appearance sync's status with its two conflict choices. Picking an unlocked item equips it: the bridge
// saves it, sends it to the account and to the lobby or hub the player is in.

#include "Menu/ScctCareerPages.h"

#include "Menu/ScctMenuSubsystem.h"
#include "Settings/ScctSettings.h"
#include "Social/ScctArmourySubsystem.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"

namespace
{
	const FName ArmouryPageHideMine(TEXT("Display.HideMyCosmetics"));
	const FName ArmouryPageHideOthers(TEXT("Display.HideOthersCosmetics"));

	TSharedRef<SWidget> ArmourySwatch(int32 Rgb, int32 Rgb2, float Size)
	{
		// A camo shows its darkest colour around its middle one; a tint or a light is one colour.
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SColorBlock).Color(UScctAccountSubsystem::ToColour(Rgb2)) ]
			+ SOverlay::Slot().Padding(Size * 0.22f)[ SNew(SColorBlock).Color(UScctAccountSubsystem::ToColour(Rgb)) ]
		];
	}

	class SScctArmouryPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctArmouryPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			if (UScctArmourySubsystem* Armoury = UScctArmourySubsystem::GetCurrent())
			{
				Armoury->Command(EScctArmouryCommand::Refresh);
			}

			const auto SideChip = [this](const TCHAR* Caption, int32 InSide)
			{
				return SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(Caption))
					.Selected_Lambda([this, InSide] { return Side == InSide; })
					.Description(FText::FromString(InSide == 0 ? TEXT("The spy's suit, goggle light and pistol.") : TEXT("The mercenary's armour, torch and lamp, and each gun.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this, InSide] { SetSide(InSide); }));
			};
			const auto Toggle = [](const TCHAR* Caption, const TCHAR* Description, FName Key)
			{
				return SNew(SScctButton).Look(EScctButtonLook::Chip)
					.Text_Lambda([Caption, Key] { return FText::FromString(FString(Caption) + (FScctSettings::GetBool(Key) ? TEXT(": ON") : TEXT(": OFF"))); })
					.Selected_Lambda([Key] { return FScctSettings::GetBool(Key); })
					.Description(FText::FromString(Description))
					.OnPressed(FSimpleDelegate::CreateLambda([Key]
					{
						FScctSettings::SetBool(Key, !FScctSettings::GetBool(Key));
						FScctSettings::Save();
					}));
			};

			const TSharedRef<SScctButton> First = SideChip(TEXT("SPY"), 0);
			const TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
			if (!Host.bInMatch)
			{
				Actions->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("ACHIEVEMENTS")))
					.Description(FText::FromString(TEXT("Every achievement with your progress and the cosmetics it unlocks.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(TEXT("achievements")); }))
				];
				Actions->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SScctButton).Look(EScctButtonLook::Secondary).Text(FText::FromString(TEXT("ACCOUNT")))
					.Description(FText::FromString(TEXT("Sign in or out, your name, rank, prestige and title.")))
					.OnPressed(FSimpleDelegate::CreateLambda([this] { Host.Open(TEXT("account")); }))
				];
			}

			ChildSlot
			[
				SNew(SBox).MaxDesiredWidth(1180.0f).HAlign(HAlign_Fill)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						ScctUi::Card(TEXT("Your collection"),
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[ ScctCareerUi::Line(TEXT("Select an equipment slot, then an item. Unlocked items are equipped when picked; servers check your look against your account when you join."), ScctUi::Small(), ScctHudStyle::TextDim()) ]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true)
								.Text_Lambda([this] { return FText::FromString(StatusLine); })
								.Visibility_Lambda([this] { return StatusLine.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(SHorizontalBox)
								.Visibility_Lambda([this] { return bConflict ? EVisibility::Visible : EVisibility::Collapsed; })
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
								[
									SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(TEXT("RELOAD ACCOUNT APPEARANCE")))
									.Description(FText::FromString(TEXT("Takes the look and title your account holds now; this PC's unsent choices are dropped.")))
									.OnPressed(FSimpleDelegate::CreateLambda([] { if (UScctArmourySubsystem* A = UScctArmourySubsystem::GetCurrent()) { A->Command(EScctArmouryCommand::Reload); } }))
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(TEXT("REAPPLY MY CHOICES")))
									.Description(FText::FromString(TEXT("Sends this PC's look and title to your account, over what it holds now.")))
									.OnPressed(FSimpleDelegate::CreateLambda([] { if (UScctArmourySubsystem* A = UScctArmourySubsystem::GetCurrent()) { A->Command(EScctArmouryCommand::Reapply); } }))
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
							[
								SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, 8.0f))
								+ SWrapBox::Slot()[ First ]
								+ SWrapBox::Slot()[ SideChip(TEXT("MERCENARY"), 1) ]
								+ SWrapBox::Slot()[ Toggle(TEXT("HIDE OTHERS' COSMETICS"), TEXT("Every other player is drawn in the retail look."), ArmouryPageHideOthers) ]
								+ SWrapBox::Slot()[ Toggle(TEXT("HIDE MY COSMETICS FROM OTHERS"), TEXT("Your look is shown on your own screen only: servers get the retail look."), ArmouryPageHideMine) ]
							],
							FMargin(20.0f, 14.0f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ SAssignNew(SlotChips, SBox) ]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.6f).Padding(0.0f, 0.0f, 16.0f, 0.0f)
						[
							SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(List, SBox) ]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SScrollBox) + SScrollBox::Slot()[ SAssignNew(Kit, SBox) ]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ Actions ]
				]
			];
			DefaultFocus = First;
			RebuildSide();
			RegisterActiveTimer(0.2f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctArmouryPage::Watch));
		}

		virtual FString GetTitle() const override { return TEXT("Armoury"); }
		virtual FString GetKicker() const override { return TEXT("Cosmetics"); }
		virtual bool HasTabs() const override { return true; }
		virtual void StepTab(int32 Direction) override
		{
			const int32 At = Slots.IndexOfByKey(Slot);
			if (Slots.Num() > 0)
			{
				SetSlot(Slots[(FMath::Max(At, 0) + Direction + Slots.Num()) % Slots.Num()]);
			}
		}

		/** "armoury/merc" opens on the mercenary's side. */
		virtual void ShowSub(const FString& Sub) override
		{
			if (Sub.StartsWith(TEXT("merc"), ESearchCase::IgnoreCase))
			{
				SetSide(1);
			}
		}

	private:
		void SetSide(int32 InSide)
		{
			if (InSide != Side)
			{
				Side = InSide;
				Slot = 0;
				RebuildSide();
			}
		}

		void SetSlot(int32 InSlot)
		{
			Slot = InSlot;
			RebuildList();
		}

		EActiveTimerReturnType Watch(double, float)
		{
			const UScctArmourySubsystem* Armoury = UScctArmourySubsystem::GetCurrent();
			if (Armoury == nullptr)
			{
				return EActiveTimerReturnType::Continue;
			}
			const FScctArmouryInfo Info = Armoury->GetInfo();
			if (!bBuilt && Armoury->IsReady())
			{
				RebuildSide();
			}
			else if (Info.Revision != ShownRevision)
			{
				const bool bRebuild = (Info.Flags & EScctArmouryFlags::AccountKnown) != (ShownFlags & EScctArmouryFlags::AccountKnown);
				ShownRevision = Info.Revision;
				ShownFlags = Info.Flags;
				if (bRebuild)
				{
					RebuildList();
				}
				else
				{
					Refresh();
				}
			}
			return EActiveTimerReturnType::Continue;
		}

		/** The slot chips of the side, then the list. */
		void RebuildSide()
		{
			const UScctArmourySubsystem* Armoury = UScctArmourySubsystem::GetCurrent();
			Slots = Armoury != nullptr ? Armoury->SlotsOf(Side) : TArray<int32>();
			bBuilt = Slots.Num() > 0;
			if (!Slots.Contains(Slot))
			{
				Slot = Slots.Num() > 0 ? Slots[0] : 0;
			}
			const TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6.0f, 6.0f));
			for (const int32 Each : Slots)
			{
				const FString Name = Armoury->GetText(EScctArmouryText::SlotName, Side, Each);
				Chips->AddSlot()
				[
					SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(Name.ToUpper()))
					.Selected_Lambda([this, Each] { return Slot == Each; })
					.Description(FText::FromString(FString::Printf(TEXT("The looks of the %s."), *Name.ToLower())))
					.OnPressed(FSimpleDelegate::CreateLambda([this, Each] { SetSlot(Each); }))
				];
			}
			SlotChips->SetContent(Chips);
			RebuildList();
		}

		/** The items of the slot: the retail look first, then the catalogue's. */
		void RebuildList()
		{
			UScctArmourySubsystem* Armoury = UScctArmourySubsystem::GetCurrent();
			const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
			Rows = MakeShared<TArray<FScctArmouryRow>>(Armoury != nullptr ? Armoury->ItemsOf(Side, Slot) : TArray<FScctArmouryRow>());
			if (Armoury == nullptr || !Armoury->IsReady())
			{
				Box->AddSlot().AutoHeight()[ ScctCareerUi::Line(TEXT("The Armoury is starting…"), ScctUi::Body(), ScctHudStyle::TextDim()) ];
				List->SetContent(Box);
				return;
			}
			const FScctArmouryInfo Info = Armoury->GetInfo();
			ShownRevision = Info.Revision;
			ShownFlags = Info.Flags;

			const TSharedPtr<TArray<FScctArmouryRow>> Shared = Rows;
			const int32 InSide = Side, InSlot = Slot;
			const auto Row = [&Box](const TSharedRef<SWidget>& Swatch, const TSharedRef<SWidget>& Button)
			{
				Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Swatch ]
					+ SHorizontalBox::Slot().FillWidth(1.0f)[ Button ]
				];
			};
			const auto NoneChosen = [Shared] { return !Shared->ContainsByPredicate([](const FScctArmouryRow& R) { return R.IsChosen(); }); };
			Row(ArmourySwatch(0x6f7a72, 0x3a423d, 40.0f),
				SNew(SScctButton).Look(EScctButtonLook::Secondary).FillWidth(true)
				.Text_Lambda([NoneChosen] { return FText::FromString(NoneChosen() ? TEXT("RETAIL  ·  EQUIPPED") : TEXT("RETAIL")); })
				.Selected_Lambda(NoneChosen)
				.Description(FText::FromString(TEXT("The original look.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this, InSide, InSlot] { Pick(InSide, InSlot, FString()); })));
			for (int32 Index = 0; Index < Shared->Num(); ++Index)
			{
				const FScctArmouryRow& Item = (*Shared)[Index];
				Row(ArmourySwatch(Item.Item.Rgb, Item.Item.Rgb2, 40.0f),
					SNew(SScctButton).Look(EScctButtonLook::Secondary).FillWidth(true)
					.Text_Lambda([Shared, Index]
					{
						const FScctArmouryRow& R = (*Shared)[Index];
						FString Text = R.Name.ToUpper();
						if (R.IsChosen()) { Text += TEXT("  ·  EQUIPPED"); }
						else if (!R.IsUnlocked())
						{
							Text += TEXT("  ·  LOCKED");
							if (R.Item.Target > 1) { Text += FString::Printf(TEXT("  %d/%d"), R.Item.Current, R.Item.Target); }
						}
						return FText::FromString(Text);
					})
					.Selected_Lambda([Shared, Index] { return (*Shared)[Index].IsChosen(); })
					.Description(FText::FromString(Item.Tip))
					.OnPressed(FSimpleDelegate::CreateLambda([this, InSide, InSlot, Id = Item.Id] { Pick(InSide, InSlot, Id); })));
			}
			List->SetContent(ScctUi::Card(Armoury->GetText(EScctArmouryText::SlotName, Side, Slot), Box, FMargin(20.0f, 14.0f)));
			Refresh();
		}

		void Pick(int32 InSide, int32 InSlot, const FString& Id)
		{
			UScctArmourySubsystem* Armoury = UScctArmourySubsystem::GetCurrent();
			if (Armoury == nullptr)
			{
				return;
			}
			if (!Armoury->Choose(InSide, InSlot, Id))
			{
				const FScctArmouryRow* Row = Rows.IsValid() ? Rows->FindByPredicate([&Id](const FScctArmouryRow& R) { return R.Id == Id; }) : nullptr;
				Notice = Row != nullptr ? Row->Name + TEXT(": ") + Row->Tip : FString(TEXT("That item is locked."));
			}
			else
			{
				Notice.Reset();
			}
			Refresh();
		}

		/** The states of the listed items, the status line and the kit, without remaking the list (the focus stays). */
		void Refresh()
		{
			const UScctArmourySubsystem* Armoury = UScctArmourySubsystem::GetCurrent();
			if (Armoury == nullptr || !Armoury->IsReady())
			{
				return;
			}
			const FScctArmouryInfo Info = Armoury->GetInfo();
			ShownRevision = Info.Revision;
			ShownFlags = Info.Flags;
			bConflict = (Info.Flags & EScctArmouryFlags::Conflict) != 0;
			if (Rows.IsValid())
			{
				const TArray<FScctArmouryRow> Now = Armoury->ItemsOf(Side, Slot);
				if (Now.Num() == Rows->Num())
				{
					*Rows = Now;
				}
			}

			int32 Owned = 0;
			if (Rows.IsValid())
			{
				for (const FScctArmouryRow& R : *Rows) { Owned += R.IsUnlocked() ? 1 : 0; }
			}
			const FString Sync = Armoury->GetText(EScctArmouryText::Status);
			StatusLine = !Notice.IsEmpty() ? Notice
				: (Info.Flags & EScctArmouryFlags::AccountKnown) == 0
					? ((Info.Flags & EScctArmouryFlags::HasMaster) != 0
						? TEXT("Sign in on the Account page and earn achievements on official servers to unlock these.")
						: TEXT("Cosmetics are unlocked by achievements on an official master server; none is set up."))
				: FString::Printf(TEXT("%d / %d looks available for this slot.%s%s"), Owned + 1, (Rows.IsValid() ? Rows->Num() : 0) + 1, Sync.IsEmpty() ? TEXT("") : TEXT("  "), *Sync);

			// The kit: what is equipped on every slot of the side.
			const TSharedRef<SVerticalBox> Equipped = SNew(SVerticalBox);
			for (const int32 Each : Slots)
			{
				const TArray<FScctArmouryRow> Items = Armoury->ItemsOf(Side, Each);
				const FScctArmouryRow* Chosen = Items.FindByPredicate([](const FScctArmouryRow& R) { return R.IsChosen(); });
				Equipped->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						Chosen != nullptr ? ArmourySwatch(Chosen->Item.Rgb, Chosen->Item.Rgb2, 26.0f) : ArmourySwatch(0x6f7a72, 0x3a423d, 26.0f)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						ScctCareerUi::StatRow(Armoury->GetText(EScctArmouryText::SlotName, Side, Each), Chosen != nullptr ? Chosen->Name.ToUpper() : FString(TEXT("RETAIL")))
					]
				];
			}
			Equipped->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				ScctCareerUi::Line((Info.Flags & EScctArmouryFlags::HideMine) != 0
					? TEXT("Hide my cosmetics is on: servers get the retail look, and this kit is drawn on your own screen only.")
					: TEXT("This kit is sent to the server when you join, and to the lobby or hub you are in when you change it."), ScctUi::Small(), ScctHudStyle::TextFaint())
			];
			Kit->SetContent(ScctUi::Card(Side == 0 ? TEXT("Your spy kit") : TEXT("Your mercenary kit"), Equipped, FMargin(20.0f, 14.0f)));
		}

		int32 Side = 0;
		int32 Slot = 0;
		TArray<int32> Slots;
		TSharedPtr<TArray<FScctArmouryRow>> Rows;
		bool bBuilt = false;
		bool bConflict = false;
		int32 ShownRevision = MIN_int32;
		int32 ShownFlags = 0;
		FString StatusLine;
		FString Notice;
		TSharedPtr<SBox> SlotChips;
		TSharedPtr<SBox> List;
		TSharedPtr<SBox> Kit;
	};
}

TSharedRef<SScctMenuPage> ScctCareerPages::MakeArmoury(const FScctMenuHost& Host)
{
	return SNew(SScctArmouryPage, Host);
}
