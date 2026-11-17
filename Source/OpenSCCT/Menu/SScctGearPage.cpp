#include "Menu/SScctGearPage.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Menu/ScctMenuKit.h"
#include "Menu/ScctMenuPage.h"
#include "Menu/ScctMenuStyle.h"
#include "OpenSCCT.h"
#include "Runtime/ScctLoadoutApi.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

// Short for the menus' own widgets (a using-directive would leak into the files built together with this one).
namespace Ui = ScctMenuStyle;

namespace
{
	const FName GearPageType(TEXT("SScctGearPage"));

	constexpr float GearPageWidth = 1240.0f;
	constexpr float GearKitWidth = 236.0f;
	constexpr float GearIntelWidth = 330.0f;
	constexpr float GearCardWidth = 170.0f;
	constexpr float GearCardHeight = 132.0f;

	/** Caution: a card locked by level or the host's rules says why in this colour. */
	FLinearColor GearWarning()
	{
		return ScctHudStyle::Srgb(239, 178, 125);
	}

	/** White boxes the button colours (as the menus' own buttons are made): kit slots and cards. */
	const FButtonStyle& GearTileStyle()
	{
		static const FButtonStyle Style = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 5.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), 1.0f))
			.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, 5.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.4f), 1.0f))
			.SetPressed(FSlateRoundedBoxBrush(FLinearColor::White, 5.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f), 1.0f))
			.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 0.4f), 5.0f))
			.SetNormalPadding(FMargin(0.0f))
			.SetPressedPadding(FMargin(0.0f));
		return Style;
	}

	FString GearText(const TCHAR* Text)
	{
		return FString(Text);
	}

	FString GearError(const FScctGearMenuApi* Api)
	{
		TCHAR Buffer[1024];
		Buffer[0] = 0;
		Api->LastError(Buffer, UE_ARRAY_COUNT(Buffer));
		FString Error(Buffer);
		int32 LineEnd = INDEX_NONE;
		if (Error.FindChar(TEXT('\n'), LineEnd))
		{
			Error.LeftInline(LineEnd);
		}
		return Error.TrimStartAndEnd();
	}

	/** The saved kit as its file holds it, on one line for the log. */
	FString GearSavedJson(const FScctGearMenuApi* Api)
	{
		TArray<TCHAR> Buffer;
		Buffer.SetNumZeroed(4096);
		Api->SavedJson(Buffer.GetData(), Buffer.Num());
		FString Json(Buffer.GetData());
		Json.ReplaceInline(TEXT("\r"), TEXT(""));
		Json.ReplaceInline(TEXT("\n"), TEXT(" "));
		while (Json.ReplaceInline(TEXT("  "), TEXT(" ")) > 0)
		{
		}
		return Json;
	}

	/** One side's part of a saved kit, to tell whether APPLY changed what the played side carries. */
	FString GearSideKit(const FScctLoadout& Kit, int32 Side)
	{
		FString Text;
		for (int32 Index = 0; Index < FScctLoadout::Slots; ++Index)
		{
			Text += FString(Side == 0 ? Kit.SpyGadgets[Index] : Kit.MercGadgets[Index]) + TEXT(",");
		}
		if (Side == 1)
		{
			Text += FString(Kit.MercWeapon) + TEXT(",") + Kit.MercSidearm;
		}
		return Text;
	}
}

TSharedPtr<SScctGearPage> SScctGearPage::Open(const TSharedRef<SOverlay>& Host, int32 Side, int32 Context, bool bTakeKeyboard,
	FSimpleDelegate OnApplied, FSimpleDelegate OnClosed)
{
	if (const TSharedPtr<SScctGearPage> Already = FindOpen(Host))
	{
		return Already;
	}
	const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>();
	if (Api == nullptr || Api->Open(Side, Context) == 0)
	{
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctGear: the gear picker cannot open: %s"), Api == nullptr ? TEXT("the bridge has no gear table") : *GearError(Api));
		return nullptr;
	}

	TSharedRef<SScctGearPage> Page = SNew(SScctGearPage)
		.Side(Side)
		.Context(Context)
		.bTakeKeyboard(bTakeKeyboard)
		.OnApplied(OnApplied)
		.OnClosed(OnClosed);
	Page->Host = Host;
	if (FSlateApplication::IsInitialized())
	{
		Page->FocusBefore = FSlateApplication::Get().GetKeyboardFocusedWidget();
	}
	Host->AddSlot()[ Page ];
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: the gear picker opened on the %s (%s)"), Page->State.Side == 1 ? TEXT("merc") : TEXT("spy"),
		Context == EScctGearMenuContext::Match ? TEXT("in a round") : Context == EScctGearMenuContext::Lobby ? TEXT("in the lobby") : TEXT("menus"));
	return Page;
}

TSharedPtr<SScctGearPage> SScctGearPage::FindOpen(const TSharedRef<SOverlay>& Host)
{
	FChildren* Children = Host->GetChildren();
	for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
	{
		const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
		if (Child->GetType() == GearPageType)
		{
			return StaticCastSharedRef<SScctGearPage>(Child);
		}
	}
	return nullptr;
}

FString SScctGearPage::SummaryLine(int32 Side)
{
	const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>();
	if (Api == nullptr)
	{
		return FString();
	}
	TCHAR Buffer[512];
	Buffer[0] = 0;
	Api->Summary(Side, Buffer, UE_ARRAY_COUNT(Buffer));
	return FString(Buffer);
}

void SScctGearPage::Construct(const FArguments& InArgs)
{
	OnApplied = InArgs._OnApplied;
	OnClosed = InArgs._OnClosed;
	bTakeKeyboard = InArgs._bTakeKeyboard;
	Context = InArgs._Context;

	// Open has built the drafts; the side shown is the side played when the page was asked for "the side played".
	if (const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>())
	{
		Api->GetState(&State);
		PlayedSide = State.OwnSide != 0 ? State.Side : -1;
	}
	Category = State.Side == 1 ? EScctGearCategory::Weapon : EScctGearCategory::Gadget;

	const auto Tab = [this](const TCHAR* Text, int32 Side)
	{
		return Ui::Button(Ui::ToText(Text), FOnClicked::CreateLambda([this, Side] { ShowSide(Side); return FReply::Handled(); }),
			TAttribute<bool>::CreateLambda([this, Side] { return State.Side == Side; }), ScctHudStyle::Accent(Side), 120.0f);
	};

	static const FSlateColorBrush Dim(FLinearColor::White);
	ChildSlot
	[
		SNew(SOverlay)
		// What is behind is dimmed and, being covered, takes no clicks while the page is open.
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(&Dim)
			.ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24.0f)
		[
			SNew(SBox).MaxDesiredWidth(GearPageWidth)
			[
				Ui::Glass(
					SNew(SVerticalBox)
					// Title, the side tabs and the slot count.
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 28.0f, 0.0f)
						[
							Ui::Title(Ui::ToText(TEXT("EQUIPMENT")), 26.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[ Tab(TEXT("SPY"), 0) ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Tab(TEXT("MERCENARY"), 1) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(ScctHudStyle::Label(10.0f))
							.ColorAndOpacity(ScctHudStyle::TextDim())
							.Text(TAttribute<FText>::CreateLambda([this]
							{
								FString Text = FString::Printf(TEXT("%d GADGET SLOTS"), State.GadgetSlots);
								if (State.Unrestricted == 0)
								{
									Text += FString::Printf(TEXT("   /   LEVEL %d"), FMath::Max(1, State.CurrentLevel));
								}
								return Ui::ToText(Text);
							}))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 2.0f)
					[
						SNew(STextBlock)
						.Font(ScctHudStyle::Label(10.5f))
						.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(ScctHudStyle::Accent(State.Side)); }))
						.Text(TAttribute<FText>::CreateLambda([this]
						{
							return Ui::ToText(State.Side == 1 ? TEXT("MERCENARY  /  DEFENCE KIT") : TEXT("SPY  /  INFILTRATION KIT"));
						}))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
					[
						Ui::Text(Ui::ToText(TEXT("Choose a slot, compare equipment, then apply your loadout.")), 12.0f, FSlateColor(ScctHudStyle::TextDim()))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 16.0f)[ Ui::Rule() ]

					// Your kit, the catalogue, the intel.
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							SAssignNew(KitBox, SBox).WidthOverride(GearKitWidth)
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 18.0f, 0.0f)
						[
							// Three cards a row.
							SAssignNew(CatalogueBox, SBox).MinDesiredHeight(470.0f).MinDesiredWidth(3.0f * (GearCardWidth + 10.0f) + 16.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).WidthOverride(GearIntelWidth)
							[
								Ui::Glass(SAssignNew(IntelBox, SBox).MinDesiredHeight(440.0f), FMargin(18.0f), 0.5f)
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 12.0f)[ Ui::Rule() ]

					// When new gear goes on, and what the last action did.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(GearText(State.Rule)); }), 11.5f,
							FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						Ui::Text(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(StatusText); }), 11.5f,
							TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(bStatusError ? GearWarning() : ScctHudStyle::Good()); }),
							ScctHudStyle::EWeight::Regular, true)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							Ui::Button(Ui::ToText(TEXT("CANCEL")), FOnClicked::CreateLambda([this] { RequestBack(); return FReply::Handled(); }), false, Ui::Accent(), 150.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							Ui::Button(TAttribute<FText>::CreateLambda([this] { return Ui::ToText(State.Side == 1 ? TEXT("SWITCH TO SPY") : TEXT("SWITCH TO MERC")); }),
								FOnClicked::CreateLambda([this] { ShowSide(State.Side == 1 ? 0 : 1); return FReply::Handled(); }), false, Ui::Accent(), 170.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Ui::Button(Ui::ToText(TEXT("RESET LOADOUT")), FOnClicked::CreateLambda([this] { ResetSide(); return FReply::Handled(); }), false, Ui::Accent(), 170.0f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Right)
						[
							Ui::Button(Ui::ToText(TEXT("APPLY LOADOUT")), FOnClicked::CreateLambda([this] { ApplyKit(); return FReply::Handled(); }),
								TAttribute<bool>::CreateLambda([this] { return State.Changed != 0; }), ScctHudStyle::Good(), 190.0f)
						]
					], FMargin(30.0f, 26.0f), 0.82f)
			]
		]
	];
	Refresh();
}

void SScctGearPage::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// The keyboard can only be given to a widget that is on screen, which the page is by its first tick.
	if (bTakeKeyboard && !bFocusTaken && FSlateApplication::IsInitialized())
	{
		bFocusTaken = true;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

FReply SScctGearPage::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		RequestBack();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SScctGearPage::RequestBack()
{
	const TSharedPtr<SOverlay> Overlay = Host.Pin();
	if (State.Changed == 0 || !Overlay.IsValid())
	{
		Close();
		return;
	}
	const TWeakPtr<SScctGearPage> Weak = SharedThis(this);
	const TSharedPtr<SScctDialogLayer> Layer = Dialogs.Pin();
	if (!Layer.IsValid())
	{
		// No question can be shown here: CANCEL keeps the page, as STAY would, so nothing is dropped unasked.
		SetStatus(TEXT("Apply the loadout or reset it before leaving."), true);
		return;
	}
	ScctUi::Confirm(Layer, TEXT("Leave the equipment page"), TEXT("Your gear changes have not been applied."), TEXT("Discard"), TEXT("Stay"),
		[Weak] { if (const TSharedPtr<SScctGearPage> Page = Weak.Pin()) { Page->Close(); } }, true);
}

void SScctGearPage::Close()
{
	if (bClosed)
	{
		return;
	}
	bClosed = true;
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: the gear picker closed"));

	// The page goes first, kept alive until this returns.
	const TSharedRef<SScctGearPage> Self = SharedThis(this);
	if (const TSharedPtr<SOverlay> Overlay = Host.Pin())
	{
		Overlay->RemoveSlot(Self);
	}
	if (const TSharedPtr<SWidget> Before = FocusBefore.Pin(); bTakeKeyboard && Before.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Before, EFocusCause::SetDirectly);
	}
	OnClosed.ExecuteIfBound();
}

void SScctGearPage::ShowSide(int32 Side)
{
	const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>();
	if (Api == nullptr || Side == State.Side)
	{
		return;
	}
	Api->SetSide(Side);
	Category = Side == 1 ? EScctGearCategory::Weapon : EScctGearCategory::Gadget;
	Slot = 0;
	Hovered.Reset();
	SetStatus(FString());
	Refresh();
}

void SScctGearPage::SelectSlot(int32 InCategory, int32 InSlot)
{
	Category = InCategory;
	Slot = InSlot;
	Hovered.Reset();
	Refresh();
}

bool SScctGearPage::Pick(const FString& Path)
{
	const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>();
	if (Api == nullptr)
	{
		return false;
	}
	if (Api->Choose(Category, Slot, *Path) == 0)
	{
		// Why: the intel's restriction ("Requires level 10.").
		FScctGearIntel Intel = {};
		Api->GetIntel(*Path, &Intel);
		const FString Why = Intel.Restriction[0] != 0 ? FString(Intel.Restriction) : FString(TEXT("This cannot go in that slot."));
		SetStatus(FString::Printf(TEXT("%s: %s"), Intel.Name, *Why), true);
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: %s refused for %s %d: %s"), *Path,
			Category == EScctGearCategory::Weapon ? TEXT("the weapon") : Category == EScctGearCategory::Sidearm ? TEXT("the sidearm") : TEXT("gadget slot"), Slot + 1, *Why);
		Refresh();
		return false;
	}
	SetStatus(FString());
	Refresh();
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: %s %d takes %s"),
		Category == EScctGearCategory::Weapon ? TEXT("weapon") : Category == EScctGearCategory::Sidearm ? TEXT("sidearm") : TEXT("gadget slot"), Slot + 1, *Path);
	return true;
}

void SScctGearPage::ResetSide()
{
	if (const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>())
	{
		Api->Reset();
		SetStatus(TEXT("The side's standard kit is back in your loadout. Apply it to keep it."));
		Refresh();
	}
}

void SScctGearPage::ApplyKit()
{
	const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>();
	const FScctLoadoutApi* Loadout = ScctFeature<FScctLoadoutApi>();
	if (Api == nullptr || Loadout == nullptr)
	{
		return;
	}

	FScctLoadout Before = {};
	Loadout->GetLoadout(&Before);
	if (Api->Apply() == 0)
	{
		const FString Error = GearError(Api);
		SetStatus(TEXT("The loadout could not be saved: ") + Error, true);
		UE_LOG(LogOpenSCCT, Warning, TEXT("ScctGear: APPLY failed: %s"), *Error);
		Refresh();
		return;
	}
	FScctLoadout After = {};
	Loadout->GetLoadout(&After);
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: APPLY saved the kit: %s"), *GearSavedJson(Api));

	// In a round the played side's new kit is asked for at once; the server answers with the HUD's message line.
	if (PlayedSide >= 0 && GearSideKit(Before, PlayedSide) != GearSideKit(After, PlayedSide))
	{
		const int32 Asked = Loadout->RequestGearChange();
		TCHAR Error[512];
		Error[0] = 0;
		if (Asked == 0)
		{
			Loadout->LastError(Error, UE_ARRAY_COUNT(Error));
		}
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: asked the server to put the %s kit on (ServerChangeGear): %s"),
			PlayedSide == 1 ? TEXT("merc") : TEXT("spy"), Asked != 0 ? TEXT("sent") : Error);
	}
	else if (PlayedSide >= 0)
	{
		UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: the played side's kit did not change; the server is not asked"));
	}

	// Kept alive through the callbacks, which may drop the page's host.
	const TSharedRef<SScctGearPage> Self = SharedThis(this);
	const FSimpleDelegate Applied = OnApplied;
	Close();
	Applied.ExecuteIfBound();
}

void SScctGearPage::SetStatus(const FString& Text, bool bError)
{
	StatusText = Text;
	bStatusError = bError;
}

bool SScctGearPage::RunScript(const FString& Script)
{
	TArray<FString> Parts;
	Script.ParseIntoArray(Parts, TEXT(","));
	if (Parts.IsEmpty())
	{
		return false;
	}
	UE_LOG(LogOpenSCCT, Log, TEXT("ScctGear: -ScctGearApply=%s"), *Script);
	const FString SideName = Parts[0].TrimStartAndEnd();
	ShowSide(SideName.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1 : 0);

	bool bAll = true;
	int32 NextGadget = 0;
	for (int32 Index = 1; Index < Parts.Num(); ++Index)
	{
		FString Part = Parts[Index].TrimStartAndEnd();
		FString Key, Value;
		if (Part.Split(TEXT("="), &Key, &Value))
		{
			SelectSlot(Key.Equals(TEXT("sidearm"), ESearchCase::IgnoreCase) ? EScctGearCategory::Sidearm : EScctGearCategory::Weapon, 0);
			bAll &= Pick(Value.TrimStartAndEnd());
			continue;
		}
		if (NextGadget >= State.GadgetSlots)
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctGear: %s left out: the side has %d gadget slots"), *Part, State.GadgetSlots);
			bAll = false;
			continue;
		}
		SelectSlot(EScctGearCategory::Gadget, NextGadget++);
		bAll &= Pick(Part);
	}
	ApplyKit();
	return bAll;
}

FString SScctGearPage::SelectedPath() const
{
	switch (Category)
	{
	case EScctGearCategory::Weapon: return FString(State.Weapon);
	case EScctGearCategory::Sidearm: return FString(State.Sidearm);
	default: return FString(State.Gadgets[FMath::Clamp(Slot, 0, FScctGearMenuState::Slots - 1)]);
	}
}

void SScctGearPage::Refresh()
{
	if (const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>())
	{
		Api->GetState(&State);
	}
	// The spy's weapon is fixed: his catalogue is always a gadget slot's.
	if (State.Side == 0 && Category != EScctGearCategory::Gadget)
	{
		Category = EScctGearCategory::Gadget;
		Slot = 0;
	}
	if (Category == EScctGearCategory::Sidearm && State.SidearmsEnabled == 0)
	{
		Category = EScctGearCategory::Weapon;
	}
	if (Category == EScctGearCategory::Gadget && Slot >= State.GadgetSlots)
	{
		Slot = 0;
	}
	KitBox->SetContent(BuildKit());
	CatalogueBox->SetContent(BuildCatalogue());
	ShowIntel(Hovered.IsEmpty() ? SelectedPath() : Hovered);
}

void SScctGearPage::ShowIntel(const FString& Path)
{
	IntelBox->SetContent(BuildIntel(Path));
}

TSharedRef<SWidget> SScctGearPage::KitSlot(const FString& Caption, const FString& Name, int32 InCategory, int32 InSlot, bool bEnabled, const FString& Tooltip)
{
	const bool bActive = bEnabled && Category == InCategory && (InCategory != EScctGearCategory::Gadget || Slot == InSlot);
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);
	TSharedRef<SButton> Button = SNew(SButton)
		.ButtonStyle(&GearTileStyle())
		.IsFocusable(false)
		.IsEnabled(bEnabled)
		.ToolTipText(Ui::ToText(Tooltip))
		.OnClicked(FOnClicked::CreateLambda([this, InCategory, InSlot] { SelectSlot(InCategory, InSlot); return FReply::Handled(); }))
		[
			SNew(SBox).HeightOverride(58.0f).Padding(FMargin(12.0f, 8.0f)).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ Ui::Label(Caption, bActive ? Accent : ScctHudStyle::TextDim()) ]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Ui::Text(Ui::ToText(Name), 13.5f, FSlateColor(bEnabled ? ScctHudStyle::Text() : ScctHudStyle::TextFaint()))
				]
			]
		];
	const TWeakPtr<SButton> Weak = Button;
	Button->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([Weak, bActive, Accent]
	{
		const TSharedPtr<SButton> Pinned = Weak.Pin();
		const bool bHover = Pinned.IsValid() && Pinned->IsHovered();
		return FSlateColor(bActive ? ScctHudStyle::Fade(Accent, bHover ? 0.34f : 0.26f) : FLinearColor(1.0f, 1.0f, 1.0f, bHover ? 0.12f : 0.05f));
	}));
	return Button;
}

TSharedRef<SWidget> SScctGearPage::BuildKit()
{
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Ui::Label(TEXT("Your loadout")) ];
	const bool bMerc = State.Side == 1;
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
	[
		KitSlot(TEXT("Primary weapon"), GearText(State.WeaponName), EScctGearCategory::Weapon, 0, bMerc,
			bMerc ? GearText(State.WeaponName) : TEXT("The spy always carries the Shocker."))
	];
	if (bMerc)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			KitSlot(TEXT("Sidearm"), State.SidearmsEnabled != 0 ? GearText(State.SidearmName) : TEXT("Off"), EScctGearCategory::Sidearm, 0,
				State.SidearmsEnabled != 0, State.SidearmsEnabled != 0 ? GearText(State.SidearmName) : TEXT("Sidearms are disabled for this session."))
		];
	}
	else
	{
		Box->AddSlot().AutoHeight().Padding(2.0f, 2.0f, 0.0f, 8.0f)
		[
			Ui::Text(Ui::ToText(TEXT("Cloak is standard equipment. It uses no gadget slot.")), 11.0f, FSlateColor(ScctHudStyle::TextDim()), ScctHudStyle::EWeight::Regular, true)
		];
	}
	Box->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 12.0f)[ Ui::Rule() ];
	for (int32 Index = 0; Index < FScctGearMenuState::Slots; ++Index)
	{
		const bool bOpen = Index < State.GadgetSlots;
		FString Caption = FString::Printf(TEXT("Gadget %d"), Index + 1);
		if (State.QuickUse[Index] != 0)
		{
			Caption += FString::Printf(TEXT("  /  quick use %d"), Index + 1);
		}
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			KitSlot(Caption, bOpen ? GearText(State.GadgetNames[Index]) : TEXT("Unavailable"), EScctGearCategory::Gadget, Index, bOpen,
				bOpen ? GearText(State.GadgetNames[Index]) : TEXT("The host gives this side fewer gadget slots."))
		];
	}
	return Box;
}

TSharedRef<SWidget> SScctGearPage::Card(const FScctGearOption& Option)
{
	const FString Path(Option.Path);
	const bool bAllowed = (Option.Flags & EScctGearOptionFlags::Allowed) != 0;
	const bool bSelected = (Option.Flags & EScctGearOptionFlags::Selected) != 0;
	const bool bHeld = Category == EScctGearCategory::Weapon ? bSelected : (Option.Flags & EScctGearOptionFlags::Equipped) != 0;
	const FString Status = bSelected ? TEXT("Selected") : bHeld ? TEXT("Equipped") : !bAllowed ? TEXT("Locked")
		: (Option.Flags & EScctGearOptionFlags::Suppressed) != 0 ? TEXT("Suppressed") : Category == EScctGearCategory::Weapon ? TEXT("Primary") : TEXT("Available");
	const FLinearColor Accent = ScctHudStyle::Accent(State.Side);

	FScctGearIntel Intel = {};
	if (const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>())
	{
		Api->GetIntel(*Path, &Intel);
	}
	const FSlateBrush* Icon = IconOf(Intel);

	TSharedRef<SButton> Button = SNew(SButton)
		.ButtonStyle(&GearTileStyle())
		.IsFocusable(false)
		.ToolTipText(Ui::ToText(bAllowed ? FString(Option.Name) : FString(Intel.Restriction)))
		.OnClicked(FOnClicked::CreateLambda([this, Path] { Pick(Path); return FReply::Handled(); }))
		.OnHovered(FSimpleDelegate::CreateLambda([this, Path] { Hovered = Path; ShowIntel(Path); }))
		.OnUnhovered(FSimpleDelegate::CreateLambda([this, Path]
		{
			if (Hovered == Path)
			{
				Hovered.Reset();
				ShowIntel(SelectedPath());
			}
		}))
		[
			SNew(SBox).WidthOverride(GearCardWidth).HeightOverride(GearCardHeight).Padding(FMargin(12.0f, 10.0f))
			[
				SNew(SVerticalBox)
				.RenderOpacity(bAllowed ? 1.0f : 0.5f)
				+ SVerticalBox::Slot().AutoHeight()[ Ui::Label(Status, bSelected ? Accent : !bAllowed ? GearWarning() : ScctHudStyle::TextDim()) ]
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 6.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					Icon != nullptr
						? StaticCastSharedRef<SWidget>(SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[ SNew(SImage).Image(Icon) ])
						: SNullWidget::NullWidget
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Ui::Text(Ui::ToText(FString(Option.Name)), 13.5f, FSlateColor(bAllowed ? ScctHudStyle::Text() : ScctHudStyle::TextDim()))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
				[
					Ui::Label(FString(Option.Sub), !bAllowed ? GearWarning() : ScctHudStyle::TextFaint())
				]
			]
		];
	const TWeakPtr<SButton> Weak = Button;
	Button->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([Weak, bSelected, Accent]
	{
		const TSharedPtr<SButton> Pinned = Weak.Pin();
		const bool bHover = Pinned.IsValid() && Pinned->IsHovered();
		return FSlateColor(bSelected ? ScctHudStyle::Fade(Accent, bHover ? 0.34f : 0.26f) : FLinearColor(1.0f, 1.0f, 1.0f, bHover ? 0.12f : 0.05f));
	}));
	return Button;
}

TSharedRef<SWidget> SScctGearPage::BuildCatalogue()
{
	const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>();
	TArray<FScctGearOption> Options;
	Options.SetNumZeroed(48);
	const int32 Count = Api != nullptr ? FMath::Min(Api->ListOptions(Category, Slot, Options.GetData(), Options.Num()), Options.Num()) : 0;

	const FString Caption = Category == EScctGearCategory::Weapon ? FString(TEXT("Primary weapons"))
		: Category == EScctGearCategory::Sidearm ? FString(TEXT("Sidearms")) : FString::Printf(TEXT("Gadgets  /  slot %d"), Slot + 1);
	const FString Hint = Category == EScctGearCategory::Weapon ? TEXT("Select a weapon. Use your Scope binding to aim down its sights.")
		: Category == EScctGearCategory::Sidearm ? TEXT("Your sidearm has its own slot.") : TEXT("Equipped gadgets swap slots when selected again.");

	const TSharedRef<SWrapBox> Cards = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.0f, 10.0f));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Cards->AddSlot()[ Card(Options[Index]) ];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ Ui::Label(Caption, ScctHudStyle::Accent(State.Side)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			Ui::Text(Ui::ToText(Hint), 11.5f, FSlateColor(ScctHudStyle::TextDim()))
		]
		+ SVerticalBox::Slot().AutoHeight().MaxHeight(440.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Count > 0
					? StaticCastSharedRef<SWidget>(Cards)
					: Ui::Text(Ui::ToText(TEXT("No equipment is available for this slot.")), 13.0f, FSlateColor(ScctHudStyle::TextDim()))
			]
		];
}

TSharedRef<SWidget> SScctGearPage::BuildIntel(const FString& Path)
{
	FScctGearIntel Intel = {};
	if (const FScctGearMenuApi* Api = ScctFeature<FScctGearMenuApi>())
	{
		Api->GetIntel(*Path, &Intel);
	}
	const FSlateBrush* Icon = IconOf(Intel);

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[ Ui::Label(TEXT("Equipment intel")) ];
	if (Icon != nullptr)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f).HAlign(HAlign_Center)
		[
			SNew(SBox).HeightOverride(96.0f).MaxDesiredWidth(260.0f)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[ SNew(SImage).Image(Icon) ]
			]
		];
	}
	Box->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(Ui::ToText(FString(Intel.Name)))
		.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Light, 21.0f, 60))
		.ColorAndOpacity(ScctHudStyle::Text())
		.AutoWrapText(true)
	];
	if (Intel.Role[0] != 0)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)[ Ui::Label(FString(Intel.Role), ScctHudStyle::Accent(State.Side)) ];
	}
	Box->AddSlot().AutoHeight().Padding(0.0f, 10.0f)[ Ui::Rule() ];
	Box->AddSlot().AutoHeight()
	[
		Ui::Text(Ui::ToText(FString(Intel.Description)), 12.0f, FSlateColor(ScctHudStyle::Text()), ScctHudStyle::EWeight::Regular, true)
	];
	if (Intel.Restriction[0] != 0)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			Ui::Text(Ui::ToText(FString(Intel.Restriction)), 12.0f, FSlateColor(GearWarning()), ScctHudStyle::EWeight::Regular, true)
		];
	}
	if (Intel.StatCount > 0)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 10.0f)[ Ui::Rule() ];
		for (int32 Index = 0; Index < FMath::Min(Intel.StatCount, FScctGearIntel::MaxStats); ++Index)
		{
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Ui::Label(FString(Intel.StatCaption[Index])) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Ui::Text(Ui::ToText(FString(Intel.StatValue[Index])), 12.0f)
				]
			];
		}
	}
	if (Intel.Note[0] != 0)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			Ui::Text(Ui::ToText(FString(Intel.Note)), 10.5f, FSlateColor(ScctHudStyle::TextFaint()), ScctHudStyle::EWeight::Regular, true)
		];
	}
	return Box;
}

const FSlateBrush* SScctGearPage::IconOf(const FScctGearIntel& Intel)
{
	// The pictures are decoded once a run and kept until the game ends (never released: Slate may be gone by the
	// time statics are destroyed). The atlas is one image; each item is a brush over its tile.
	static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>>& Images = *new TMap<FString, TSharedPtr<FSlateDynamicImageBrush>>();
	static TMap<FString, FSlateBrush*>& Tiles = *new TMap<FString, FSlateBrush*>();

	const FString File(Intel.IconFile);
	if (File.IsEmpty())
	{
		return nullptr;
	}
	const FString Key = FString::Printf(TEXT("%s|%d|%d|%d"), *File, Intel.IconU, Intel.IconV, Intel.IconSize);
	if (FSlateBrush* const* Found = Tiles.Find(Key))
	{
		return *Found;
	}

	if (!Images.Contains(File))
	{
		TSharedPtr<FSlateDynamicImageBrush> Made;
		FImage Loaded;
		if (FImageUtils::LoadImage(*File, Loaded))
		{
			// Slate takes 8-bit BGRA. The content's PNGs decode to that (8-bit colour), to grey or to 16 bits a
			// channel; those two are turned into it here.
			const int64 Pixels = static_cast<int64>(Loaded.SizeX) * Loaded.SizeY;
			TArray<uint8> Bytes;
			if (Loaded.Format == ERawImageFormat::BGRA8)
			{
				Bytes = TArray<uint8>(Loaded.RawData.GetData(), static_cast<int32>(Loaded.RawData.Num()));
			}
			else if (Loaded.Format == ERawImageFormat::G8 || Loaded.Format == ERawImageFormat::RGBA16)
			{
				Bytes.SetNumUninitialized(static_cast<int32>(Pixels * 4));
				const uint16* Wide = reinterpret_cast<const uint16*>(Loaded.RawData.GetData());
				for (int64 Index = 0; Index < Pixels; ++Index)
				{
					uint8* Out = &Bytes[static_cast<int32>(Index * 4)];
					if (Loaded.Format == ERawImageFormat::G8)
					{
						Out[0] = Out[1] = Out[2] = Loaded.RawData[Index];
						Out[3] = 255;
					}
					else
					{
						Out[0] = static_cast<uint8>(Wide[Index * 4 + 2] >> 8);
						Out[1] = static_cast<uint8>(Wide[Index * 4 + 1] >> 8);
						Out[2] = static_cast<uint8>(Wide[Index * 4 + 0] >> 8);
						Out[3] = static_cast<uint8>(Wide[Index * 4 + 3] >> 8);
					}
				}
			}
			if (Bytes.Num() == Pixels * 4 && Pixels > 0)
			{
				Made = FSlateDynamicImageBrush::CreateWithImageData(FName(*(TEXT("ScctGear:") + File)), FVector2D(Loaded.SizeX, Loaded.SizeY), Bytes);
			}
		}
		if (!Made.IsValid())
		{
			UE_LOG(LogOpenSCCT, Warning, TEXT("ScctGear: the picture %s could not be read"), *File);
		}
		Images.Add(File, Made);
	}
	const TSharedPtr<FSlateDynamicImageBrush> Image = Images.FindRef(File);

	FSlateBrush* Tile = nullptr;
	if (Image.IsValid())
	{
		Tile = new FSlateBrush(*Image);
		const FVector2D Size = Image->GetImageSize();
		if (Intel.IconSize > 0 && Size.X > 0 && Size.Y > 0)
		{
			Tile->SetUVRegion(FBox2f(FVector2f(Intel.IconU / Size.X, Intel.IconV / Size.Y),
				FVector2f((Intel.IconU + Intel.IconSize) / Size.X, (Intel.IconV + Intel.IconSize) / Size.Y)));
			Tile->ImageSize = FVector2D(Intel.IconSize, Intel.IconSize);
		}
	}
	Tiles.Add(Key, Tile);
	return Tile;
}
