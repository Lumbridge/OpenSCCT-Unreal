// The Armoury page (the Godot client's ArmouryScreen and AppearanceSyncPanel over Social/ScctArmourySubsystem.h): the
// spy's and the mercenary's cosmetics by slot (suit or armour, light colour, pistol or each gun) with a picture per
// item (the content's flat picture of its camo or tint, a colour swatch where there is none), which of them the
// account unlocked and by what, a turning 3D preview of the slot (Menu/SScctArmouryPreview.h: the character for the
// suit and the light, the gun for a gun slot) wearing the kit, the kit equipped on each slot, the two cosmetics
// options, and the appearance sync's status with its two conflict choices. Pointing at an item (the mouse over it, or
// the focus on it) tries it on the preview without equipping it; pointing away shows the equipped look again. Picking
// an unlocked item equips it: the bridge saves it, sends it to the account and to the lobby or hub the player is in.

#include "Menu/ScctCareerPages.h"

#include "Menu/ScctMenuSubsystem.h"
#include "Menu/ScctMenuStyle.h"
#include "Menu/SScctArmouryPreview.h"
#include "Menu/SScctAttachmentPanel.h"
#include "Menu/SScctGearPage.h"
#include "Runtime/ScctLoadoutApi.h"
#include "Runtime/ScctArmouryPreviewApi.h"
#include "Settings/ScctSettings.h"
#include "Social/ScctArmourySubsystem.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
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

	/** An item's flat picture where the content has one, else its swatch. */
	TSharedRef<SWidget> ArmouryPicture(const FString& Id, int32 Rgb, int32 Rgb2, float Size)
	{
		if (const FSlateBrush* Brush = ScctArmouryThumbs::For(Id))
		{
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size)[ SNew(SImage).Image(Brush).DesiredSizeOverride(FVector2D(Size, Size)) ];
		}
		return ArmourySwatch(Rgb, Rgb2, Size);
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
					.Selected_Lambda([this, InSide] { return !bAttachments && Side == InSide; })
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
							+ SVerticalBox::Slot().AutoHeight()[
								SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
								.Text_Lambda([this] { return FText::FromString(bAttachments
									? TEXT("Choose a weapon to configure its attachments. The saved setup goes on when you equip that weapon.")
									: TEXT("Choose a slot. Point at a finish to preview it, then select an unlocked finish to equip it.")); })
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctUi::Accent()).AutoWrapText(true)
								.Text_Lambda([this] { return FText::FromString(StatusLine); })
								.Visibility_Lambda([this] { return bAttachments || StatusLine.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
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
								+ SWrapBox::Slot()[
									SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(TEXT("WEAPON ATTACHMENTS")))
									.Selected_Lambda([this] { return bAttachments; })
									.Description(FText::FromString(TEXT("Equip attachments and see unlock progress for each mercenary weapon.")))
									.OnPressed(FSimpleDelegate::CreateSP(this, &SScctArmouryPage::ShowAttachments))
								]
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
							SAssignNew(ListScroll, SScrollBox).ScrollWhenFocusChanges(EScrollWhenFocusChanges::AnimatedScroll)
							+ SScrollBox::Slot()[ SAssignNew(List, SBox) ]
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f)
						[
							SNew(SScrollBox).Visibility_Lambda([this] { return bAttachments ? EVisibility::Collapsed : EVisibility::Visible; })
							+ SScrollBox::Slot().Padding(0.0f, 0.0f, 0.0f, 10.0f)
							[
								ScctUi::Card(TEXT("Preview"),
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SBox).HeightOverride_Lambda([this]() -> FOptionalSize
										{
											return Slot == EScctCosmeticSlot::Body || Slot == EScctCosmeticSlot::Light ? 300.0f : 218.0f;
										})
										[
											SNew(SOverlay)
											+ SOverlay::Slot()[ SNew(SColorBlock).Color(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)) ]
											+ SOverlay::Slot()[ SAssignNew(Preview, SScctArmouryPreview) ]
											+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
											[
												// Without the imported models: the item's flat picture.
												SNew(SBox).WidthOverride(160.0f).HeightOverride(160.0f)
												.Visibility_Lambda([this] { return !Preview->HasModel() && ScctArmouryThumbs::For(PreviewItem) != nullptr ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
												[
													SNew(SImage).DesiredSizeOverride(FVector2D(160.0, 160.0))
													.Image_Lambda([this] { return ScctArmouryThumbs::For(PreviewItem); })
												]
											]
											+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(14.0f)
											[
												SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextFaint()).AutoWrapText(true).Justification(ETextJustify::Center)
												.Text(FText::FromString(TEXT("The 3D preview needs the imported character and gun models.")))
												.Visibility_Lambda([this] { return Preview->HasModel() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
											]
										]
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
									[
										SNew(STextBlock).Font(ScctUi::Small()).AutoWrapText(true)
										.ColorAndOpacity_Lambda([this] { return FSlateColor(bTrying ? ScctUi::Accent() : ScctHudStyle::TextDim()); })
										.Text_Lambda([this] { return FText::FromString(PreviewCaption); })
									],
									FMargin(20.0f, 14.0f))
							]
							+ SScrollBox::Slot()[ SAssignNew(Kit, SBox) ]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)[ Actions ]
				]
			];
			DefaultFocus = First;
			RebuildSide();
			RegisterActiveTimer(0.2f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctArmouryPage::Watch));
			RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctArmouryPage::WatchPointed));
			UpdatePreview();
		}

		virtual FString GetTitle() const override { return TEXT("Armoury"); }
		virtual FString GetKicker() const override { return TEXT("Equipment"); }
		virtual bool HasTabs() const override { return true; }
		virtual void StepTab(int32 Direction) override
		{
			if (bAttachments)
			{
				if (!Weapons.IsEmpty())
				{
					SelectWeapon((WeaponIndex + Direction + Weapons.Num()) % Weapons.Num());
				}
				return;
			}
			const int32 At = Slots.IndexOfByKey(Slot);
			if (Slots.Num() > 0)
			{
				SetSlot(Slots[(FMath::Max(At, 0) + Direction + Slots.Num()) % Slots.Num()]);
			}
		}

		/** "armoury/merc" opens on the mercenary's side. */
		virtual void ShowSub(const FString& Sub) override
		{
			if (Sub.StartsWith(TEXT("item/"), ESearchCase::IgnoreCase))
			{
				PendingPreview = Sub.Mid(5);
				OpenReward();
			}
			else if (Sub.Equals(TEXT("attachments"), ESearchCase::IgnoreCase))
			{
				ShowAttachments();
			}
			else if (Sub.StartsWith(TEXT("merc"), ESearchCase::IgnoreCase))
			{
				SetSide(1);
			}
		}

	private:
		void OpenReward()
		{
			const auto* Api = ScctFeature<FScctArmouryPreviewApi>();
			if (!Api || !bBuilt || PendingPreview.IsEmpty()) { return; }
			FScctArmouryPreviewItem Item = {};
			const FString Id = MoveTemp(PendingPreview);
			if (!Api->GetItem(*Id, &Item, nullptr, 0, nullptr, 0)) { return; }
			bAttachments = false;
			Side = Item.Side;
			Slot = Item.Slot;
			PinnedPreview = Id;
			RebuildSide();
			for (const auto& Entry : RowButtons)
			{
				if (Entry.Value == Id)
				{
					DefaultFocus = Entry.Key;
					ListScroll->ScrollDescendantIntoView(Entry.Key.Pin(), false, EDescendantScrollDestination::IntoView);
					break;
				}
			}
		}

		void ShowAttachments()
		{
			bAttachments = true;
			const FScctLoadoutApi* Loadout = ScctFeature<FScctLoadoutApi>();
			const FScctWeaponAttachmentsApi* Attachments = ScctFeature<FScctWeaponAttachmentsApi>();
			FScctGearRules Rules = {};
			TArray<FScctGearItem> Items;
			if (Loadout)
			{
				Items.SetNumZeroed(Loadout->ListGear(1, &Rules, nullptr, 0));
				Loadout->ListGear(1, &Rules, Items.GetData(), Items.Num());
			}
			const auto Chips = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 6));
			Weapons.Reset();
			int32 EquippedWeaponIndex = 0;
			for (const FScctGearItem& Item : Items)
			{
				if (Item.Kind != EScctGearKind::Weapon && Item.Kind != EScctGearKind::Sidearm) { continue; }
				FScctWeaponAttachmentState Info = {};
				if (!Attachments || !Attachments->GetState(Item.Path, &Info)) { continue; }
				const int32 Index = Weapons.Add(Item);
				if (Item.Kind == EScctGearKind::Weapon && (Item.Flags & EScctGearFlags::Equipped)) { EquippedWeaponIndex = Index; }
				Chips->AddSlot()[
					SNew(SScctButton).Look(EScctButtonLook::Chip).Text(FText::FromString(Item.Name))
					.Selected_Lambda([this, Index] { return WeaponIndex == Index; }).Colour(ScctHudStyle::Accent(1))
					.OnPressed(FSimpleDelegate::CreateSP(this, &SScctArmouryPage::SelectWeapon, Index))
				];
			}
			SlotChips->SetContent(Chips);
			SelectWeapon(EquippedWeaponIndex);
		}

		void SelectWeapon(int32 Index)
		{
			if (!Weapons.IsValidIndex(Index))
			{
				List->SetContent(ScctMenuStyle::Label(TEXT("Weapon progression is unavailable.")));
				return;
			}
			WeaponIndex = Index;
			const FString Weapon(Weapons[Index].Path);
			const auto Body = SNew(SHorizontalBox);
			Body->AddSlot().FillWidth(2.0f)
			[
				SNew(SScctAttachmentPanel).Weapon(Weapon).WeaponName(FString(Weapons[Index].Name))
				.OnChoose([Weapon](int32 Mask)
				{
					const FScctWeaponAttachmentsApi* Api = ScctFeature<FScctWeaponAttachmentsApi>();
					return Api && Api->SetEquipped(*Weapon, Mask) != 0;
				})
			];
			FScctGearIntel Intel = {};
			const FScctGearMenuApi* Gear = ScctFeature<FScctGearMenuApi>();
			if (Gear && Gear->GetIntel(*Weapon, &Intel))
			{
				const auto Details = SNew(SVerticalBox);
				if (const FSlateBrush* Icon = SScctGearPage::IconOf(Intel))
				{
					Details->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
					[
						SNew(SBox).HeightOverride(112.0f)
						[ SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(Icon)] ]
					];
				}
				if (Intel.Role[0]) { Details->AddSlot().AutoHeight()[ScctMenuStyle::Label(Intel.Role, ScctHudStyle::Accent(1))]; }
				Details->AddSlot().AutoHeight().Padding(0, 5, 0, 8)
				[
					ScctMenuStyle::Text(FText::FromString(Intel.Description), 13, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
				];
				for (int32 Stat = 0; Stat < FMath::Min(Intel.StatCount, FScctGearIntel::MaxStats); ++Stat)
				{
					if (FCString::Strcmp(Intel.StatCaption[Stat], TEXT("DAMAGE 0 M")) == 0)
					{
						Details->AddSlot().AutoHeight()[ScctMenuStyle::Label(FString::Printf(TEXT("Damage at 0 m   ·   %s"), Intel.StatValue[Stat]))];
						break;
					}
				}
				Body->AddSlot().FillWidth(1.0f).Padding(24, 0, 0, 0).VAlign(VAlign_Center)[Details];
			}
			List->SetContent(ScctUi::Card(TEXT("Weapon attachments"), Body));
		}

		void SetSide(int32 InSide)
		{
			if (InSide != Side || bAttachments)
			{
				bAttachments = false;
				PinnedPreview.Reset();
				Notice.Reset();
				Side = InSide;
				Slot = 0;
				RebuildSide();
			}
		}

		void SetSlot(int32 InSlot)
		{
			PinnedPreview.Reset();
			Notice.Reset();
			Slot = InSlot;
			RebuildList();
			ListScroll->ScrollToStart();
		}

		EActiveTimerReturnType Watch(double, float)
		{
			const UScctArmourySubsystem* Armoury = UScctArmourySubsystem::GetCurrent();
			if (bAttachments || Armoury == nullptr)
			{
				return EActiveTimerReturnType::Continue;
			}
			const FScctArmouryInfo Info = Armoury->GetInfo();
			if (!bBuilt && Armoury->IsReady())
			{
				RebuildSide();
				OpenReward();
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

		EActiveTimerReturnType WatchPointed(double, float)
		{
			if (!bAttachments) { UpdatePreview(); }
			return EActiveTimerReturnType::Continue;
		}

		/**
		 * What the preview wears: the equipped kit, with the item pointed at (the mouse over its row, else the focus on
		 * it) in place of the slot's own. The stage does nothing while that stays the same.
		 */
		void UpdatePreview()
		{
			if (!Preview.IsValid())
			{
				return;
			}
			bool bPointed = false;
			FString Pointed;
			for (int32 Pass = 0; Pass < 2 && !bPointed; ++Pass)
			{
				for (const TPair<TWeakPtr<SWidget>, FString>& Row : RowButtons)
				{
					const TSharedPtr<SWidget> Button = Row.Key.Pin();
					if (Button.IsValid() && (Pass == 0 ? Button->IsHovered() : Button->HasAnyUserFocus().IsSet()))
					{
						bPointed = true;
						Pointed = Row.Value;
						break;
					}
				}
			}

			const FString Chosen = Equipped.FindRef(Slot);
			const FString Worn = Chosen.IsEmpty() ? DefaultFinishId : Chosen;
			const FString Shown = bPointed ? (Pointed.IsEmpty() ? DefaultFinishId : Pointed) : !PinnedPreview.IsEmpty() ? PinnedPreview : Worn;
			const bool bCharacter = Slot == EScctCosmeticSlot::Body || Slot == EScctCosmeticSlot::Light;
			Preview->Show(Side, Slot,
				!bCharacter || Slot == EScctCosmeticSlot::Body ? Shown : Equipped.FindRef(EScctCosmeticSlot::Body),
				Slot == EScctCosmeticSlot::Light ? Shown : Equipped.FindRef(EScctCosmeticSlot::Light));

			if (Shown != PreviewItem || (Shown != Worn) != bTrying || PreviewCaption.IsEmpty())
			{
				PreviewItem = Shown;
				bTrying = Shown != Worn;
				const FScctArmouryRow* Row = Rows.IsValid() ? Rows->FindByPredicate([&Shown](const FScctArmouryRow& R) { return R.Id == Shown; }) : nullptr;
				const FString Name = Row ? Row->Name.ToUpper() : (!Shown.IsEmpty() && Shown == DefaultFinishId) ? DefaultFinishName.ToUpper() : FString(TEXT("RETAIL"));
				PreviewCaption = Name + (bTrying
					? (Row && !Row->IsUnlocked() ? TEXT("  ·  Locked") : TEXT("  ·  Available to equip"))
					: TEXT("  ·  Equipped"));
				if (Row && !Row->IsUnlocked()) { PreviewCaption += TEXT("\n") + Row->Tip; }
			}
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
			RowButtons.Reset();
			PreviewCaption.Reset();
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
			const auto Row = [this, &Box](const TSharedRef<SWidget>& Swatch, const TSharedRef<SWidget>& Button, const FString& Id)
			{
				RowButtons.Emplace(Button, Id);
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
				.Text_Lambda([this, NoneChosen]
				{
					const FString Base = DefaultFinishName.IsEmpty() ? TEXT("RETAIL") : TEXT("SHARED  ·  ") + DefaultFinishName.ToUpper();
					return FText::FromString(NoneChosen() ? Base + TEXT("  ·  EQUIPPED") : Base);
				})
				.Selected_Lambda(NoneChosen)
				.Description(FText::FromString(TEXT("Use the default finish for this slot.")))
				.OnPressed(FSimpleDelegate::CreateLambda([this, InSide, InSlot] { Pick(InSide, InSlot, FString()); })), FString());
			for (int32 Index = 0; Index < Shared->Num(); ++Index)
			{
				const FScctArmouryRow& Item = (*Shared)[Index];
				Row(ArmouryPicture(Item.Id, Item.Item.Rgb, Item.Item.Rgb2, 40.0f),
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
					.OnPressed(FSimpleDelegate::CreateLambda([this, InSide, InSlot, Id = Item.Id] { Pick(InSide, InSlot, Id); })), Item.Id);
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
				PinnedPreview = Id;
				Notice = Row != nullptr ? Row->Tip : FString(TEXT("That finish is locked."));
			}
			else
			{
				PinnedPreview.Reset();
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
				: FString::Printf(TEXT("%d / %d finishes available.%s%s"), Owned + 1, (Rows.IsValid() ? Rows->Num() : 0) + 1, Sync.IsEmpty() ? TEXT("") : TEXT("  "), *Sync);

			// The kit: what is equipped on every slot of the side.
			const TSharedRef<SVerticalBox> KitBox = SNew(SVerticalBox);
			Equipped.Reset();
			DefaultFinishId.Reset();
			DefaultFinishName.Reset();
			const FString SharedSlot = Armoury->GetText(EScctArmouryText::SharedFinishSlot, Side, Slot);
			const int32 Fallback = SharedSlot.IsEmpty() ? INDEX_NONE : FCString::Atoi(*SharedSlot);
			for (const int32 Each : Slots)
			{
				const TArray<FScctArmouryRow> Items = Armoury->ItemsOf(Side, Each);
				const FScctArmouryRow* Chosen = Items.FindByPredicate([](const FScctArmouryRow& R) { return R.IsChosen(); });
				Equipped.Add(Each, Chosen != nullptr ? Chosen->Id : FString());
				const FString Shared = Armoury->GetText(EScctArmouryText::SharedFinishSlot, Side, Each);
				const TArray<FScctArmouryRow> Defaults = !Chosen && !Shared.IsEmpty() ? Armoury->ItemsOf(Side, FCString::Atoi(*Shared)) : TArray<FScctArmouryRow>();
				const FScctArmouryRow* Worn = Chosen ? Chosen : Defaults.FindByPredicate([](const FScctArmouryRow& R) { return R.IsChosen(); });
				if (Each == Fallback && Chosen)
				{
					DefaultFinishId = Chosen->Id;
					DefaultFinishName = Chosen->Name;
				}
				KitBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						Worn ? ArmouryPicture(Worn->Id, Worn->Item.Rgb, Worn->Item.Rgb2, 26.0f) : ArmourySwatch(0x6f7a72, 0x3a423d, 26.0f)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						ScctCareerUi::StatRow(Armoury->GetText(EScctArmouryText::SlotName, Side, Each), Worn ? Worn->Name.ToUpper() : FString(TEXT("RETAIL")))
					]
				];
			}
			KitBox->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				ScctCareerUi::Line((Info.Flags & EScctArmouryFlags::HideMine) != 0
					? TEXT("Other players see your default appearance.")
					: TEXT("Private weapon finishes stay on this profile. Online matches use your account's finishes."), ScctUi::Small(), ScctHudStyle::TextDim())
			];
			Kit->SetContent(ScctUi::Card(Side == 0 ? TEXT("Your spy kit") : TEXT("Your mercenary kit"), KitBox, FMargin(20.0f, 14.0f)));
			PreviewCaption.Reset();
			UpdatePreview();
		}

		int32 Side = 0;
		bool bAttachments = false;
		int32 WeaponIndex = 0;
		TArray<FScctGearItem> Weapons;
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
		TSharedPtr<SScrollBox> ListScroll;
		TSharedPtr<SBox> Kit;
		TSharedPtr<SScctArmouryPreview> Preview;
		/** The buttons of the listed items with their ids (empty: retail), to see which one is pointed at. */
		TArray<TPair<TWeakPtr<SWidget>, FString>> RowButtons;
		/** The equipped item of each slot of the side (empty: retail). */
		TMap<int32, FString> Equipped;
		/** The item of the viewed slot the preview shows, whether that is a try-on, and the line under the preview. */
		FString PreviewItem;
		FString DefaultFinishId;
		FString DefaultFinishName;
		FString PinnedPreview;
		FString PendingPreview;
		bool bTrying = false;
		FString PreviewCaption;
	};
}

TSharedRef<SScctMenuPage> ScctCareerPages::MakeArmoury(const FScctMenuHost& Host)
{
	return SNew(SScctArmouryPage, Host);
}
