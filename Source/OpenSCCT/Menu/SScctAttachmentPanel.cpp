#include "Menu/SScctAttachmentPanel.h"

#include "Menu/ScctMenuKit.h"
#include "Menu/ScctMenuStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr const TCHAR* AttachmentNames[] = {TEXT("Laser"), TEXT("Red dot sight"), TEXT("Silencer"), TEXT("30-round magazine"), TEXT("ACOG")};
}

void SScctAttachmentPanel::Construct(const FArguments& Args)
{
	Weapon = Args._Weapon;
	OnChoose = Args._OnChoose;
	const FScctWeaponAttachmentsApi* Api = ScctFeature<FScctWeaponAttachmentsApi>();
	if (!Api || !Api->GetState(*Weapon, &State))
	{
		ChildSlot[ScctMenuStyle::Label(TEXT("No attachments available for this weapon"))];
		return;
	}
	bDraft = Args._DraftMask.IsSet();
	Mask = Args._DraftMask.Get(State.Equipped);
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	Rows->AddSlot().AutoHeight()[ScctMenuStyle::Title(FText::FromString(Args._WeaponName), 22)];
	Rows->AddSlot().AutoHeight().Padding(0, 4, 0, 9)
	[
		SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(ScctHudStyle::Accent(1))
		.Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("%d weapon kills   ·   %d / %d points available"),
			State.Kills, State.Budget - PointsUsed(Mask), State.Budget)); })
	];
	FString StandardEquipment = FString::Printf(TEXT("Standard equipment   ·   Torch and %d-round magazine"), State.BaseMagazine);
	if (!(State.Supported & EScctWeaponAttachment::Suppressor)) { StandardEquipment += TEXT("   ·   Integral silencer"); }
	if (State.NativeOpticPower > 1.0f) { StandardEquipment += FString::Printf(TEXT("   ·   %.0f× scope"), State.NativeOpticPower); }
	Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
	[
		ScctMenuStyle::Text(FText::FromString(StandardEquipment), 12, FSlateColor(ScctHudStyle::TextDim()))
	];
	TArray<int32, TInlineAllocator<UE_ARRAY_COUNT(AttachmentNames)>> SortedSlots;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(AttachmentNames); ++Index)
	{
		if (State.Supported & (1 << Index)) { SortedSlots.Add(Index); }
	}
	SortedSlots.Sort([this](int32 A, int32 B) { return State.KillsRequired[A] < State.KillsRequired[B]; });
	for (const int32 Index : SortedSlots)
	{
		const TAttribute<FText> Caption = TAttribute<FText>::CreateLambda([this, Index] { return FText::FromString(RowLabel(Index)); });
		Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
		[
			SNew(SWidgetSwitcher).WidgetIndex_Lambda([this, Index] { return CanToggle(Index) ? 1 : 0; })
			+ SWidgetSwitcher::Slot()
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(FLinearColor(0.25f, 0.31f, 0.36f, 0.13f))
				.Padding(FMargin(24.0f, 13.0f)).HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(Caption).Font(ScctUi::Caps(13)).ColorAndOpacity(ScctHudStyle::Text())
				]
			]
			+ SWidgetSwitcher::Slot()
			[
				SNew(SScctButton).Look(EScctButtonLook::Secondary).FillWidth(true).Text(Caption)
				.Selected_Lambda([this, Index] { return (Mask & (1 << Index)) != 0; })
				.Colour(ScctHudStyle::Accent(1))
				.OnPressed(FSimpleDelegate::CreateSP(this, &SScctAttachmentPanel::Toggle, Index))
			]
		];
	}
	Rows->AddSlot().AutoHeight()
	[
		SNew(STextBlock).Font(ScctUi::Small()).ColorAndOpacity(ScctHudStyle::TextDim()).AutoWrapText(true)
		.Text_Lambda([this] { return FText::FromString(Notice); })
	];
	ChildSlot[Rows];
	RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SScctAttachmentPanel::WatchProgress));
}

EActiveTimerReturnType SScctAttachmentPanel::WatchProgress(double, float)
{
	const FScctWeaponAttachmentsApi* Api = ScctFeature<FScctWeaponAttachmentsApi>();
	FScctWeaponAttachmentState Current = {};
	if (Api && Api->GetState(*Weapon, &Current))
	{
		State = Current;
		if (!bDraft) { Mask = Current.Equipped; }
	}
	return EActiveTimerReturnType::Continue;
}

int32 SScctAttachmentPanel::PointsUsed(int32 Selection) const
{
	int32 Used = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(AttachmentNames); ++Index)
	{
		if (Selection & (1 << Index)) { Used += State.Costs[Index]; }
	}
	return Used;
}

int32 SScctAttachmentPanel::SelectionAfterToggle(int32 Index) const
{
	const int32 Bit = 1 << Index;
	if (Mask & Bit) { return Mask & ~Bit; }
	return ((Bit & EScctWeaponAttachment::Optics) ? (Mask & ~EScctWeaponAttachment::Optics) : Mask) | Bit;
}

FString SScctAttachmentPanel::RowLabel(int32 Index) const
{
	const int32 Bit = 1 << Index;
	const int32 Shortfall = PointsUsed(SelectionAfterToggle(Index)) - State.Budget;
	const bool bSwapsOptic = (Bit & EScctWeaponAttachment::Optics) && (Mask & EScctWeaponAttachment::Optics);
	const FString Status = (Mask & Bit) ? TEXT("Equipped")
		: !(State.Available & Bit) ? FString::Printf(TEXT("Locked  %d / %d kills"), State.Kills, State.KillsRequired[Index])
		: Shortfall > 0 ? FString::Printf(TEXT("Needs %d more %s"), Shortfall, Shortfall == 1 ? TEXT("point") : TEXT("points"))
		: bSwapsOptic ? (Bit == EScctWeaponAttachment::Acog ? TEXT("Replace red dot") : TEXT("Replace ACOG")) : TEXT("Equip");
	return FString::Printf(TEXT("%s   ·   %d %s   ·   %s"), AttachmentNames[Index], State.Costs[Index],
		State.Costs[Index] == 1 ? TEXT("pt") : TEXT("pts"), *Status);
}

bool SScctAttachmentPanel::CanToggle(int32 Index) const
{
	const int32 Bit = 1 << Index;
	return (Mask & Bit) || ((State.Available & Bit) && PointsUsed(SelectionAfterToggle(Index)) <= State.Budget);
}

void SScctAttachmentPanel::Toggle(int32 Index)
{
	if (!CanToggle(Index)) { return; }
	const int32 Requested = SelectionAfterToggle(Index);
	if (OnChoose && OnChoose(Requested))
	{
		Mask = Requested;
		Notice.Reset();
	}
	else
	{
		Notice = TEXT("The attachment choice could not be saved.");
	}
}
