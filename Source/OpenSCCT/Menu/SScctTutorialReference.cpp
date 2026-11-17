// The Tutorials' reference pages: View HUD (and the keyboard pictures), Tips and Gadget Details
// (Menu/SScctTutorialsPage.h; the Godot client's HudLegendScreen, TipsScreen and GadgetDetailsScreen).

#include "Menu/SScctTutorialsPage.h"

#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/ScctHudTextures.h"
#include "Menu/ScctMenuSubsystem.h"
#include "Runtime/ScctTutorialApi.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FString TutorialRefCaption(const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback)
	{
		return ScctTutorialText::Lines(Section, Key, Fallback).Replace(TEXT("\n"), TEXT(" "));
	}

	void TutorialRefFocus(const TSharedPtr<SWidget>& Widget)
	{
		if (Widget.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetAllUserFocus(Widget, EFocusCause::SetDirectly);
		}
	}

	TSharedRef<SScctButton> TutorialRefButton(const FString& Caption, const FString& Description, TFunction<void()> OnPressed, EScctButtonLook Look = EScctButtonLook::Secondary)
	{
		return SNew(SScctButton)
			.Look(Look)
			.Text(FText::FromString(Caption.ToUpper()))
			.Description(FText::FromString(Description))
			.OnPressed(FSimpleDelegate::CreateLambda([OnPressed] { OnPressed(); }));
	}

	/** A picture that keeps its shape inside Width x Height; nothing when Brush is null. */
	TSharedRef<SWidget> TutorialRefPicture(const FSlateBrush* Brush, float Width, float Height)
	{
		return SNew(SBox).WidthOverride(Width).HeightOverride(Height)
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[ SNew(SImage).Image(Brush) ]
			];
	}

	TSharedRef<SWidget> TutorialRefText(const FString& Text, bool bBright)
	{
		return SNew(STextBlock).Font(ScctUi::Body()).ColorAndOpacity(bBright ? ScctHudStyle::Text() : ScctHudStyle::TextDim()).AutoWrapText(true)
			.LineHeightPercentage(1.15f).Text(FText::FromString(Text));
	}

	int32 TutorialRefTeamOf(const FString& Sub)
	{
		return Sub.Equals(TEXT("merc"), ESearchCase::IgnoreCase) || Sub.Equals(TEXT("sentry"), ESearchCase::IgnoreCase) ? 1 : 0;
	}

	// ---- View HUD -------------------------------------------------------------------------------------------------

	/** A label of a picture page: its caption key, fallback, alignment and place (fractions of the 640 x 480 page). */
	struct FTutorialSpot
	{
		const TCHAR* Control;
		const TCHAR* Fallback;
		ETextJustify::Type Align;
		float Top;
		float Left;
		float Width;
	};

	constexpr ETextJustify::Type SpotL = ETextJustify::Left;
	constexpr ETextJustify::Type SpotC = ETextJustify::Center;
	constexpr ETextJustify::Type SpotR = ETextJustify::Right;

	// GUI.View_Hud_Spy defaults: the labels' TextAlign, WinTop, WinLeft, WinWidth.
	const FTutorialSpot TutorialSpySpots[] = {
		{ TEXT("LBLSystemMessage"), TEXT("System Messages"), SpotL, 0.165f, 0.556f, 0.3f },
		{ TEXT("LBLInteractionMenu"), TEXT("Interaction|Menu"), SpotL, 0.378f, 0.78f, 0.3f },
		{ TEXT("LBLRadar"), TEXT("Tactical|Radar"), SpotR, 0.242f, 0.0f, 0.23f },
		{ TEXT("LBLGameMessage"), TEXT("Game Status|Messages"), SpotC, 0.773f, 0.292f, 0.3f },
		{ TEXT("LBLGadget"), TEXT("Current|Gadget"), SpotL, 0.775f, 0.675f, 0.3f },
		{ TEXT("LBLStealth"), TEXT("Enhanced Reality"), SpotC, 0.165f, 0.23f, 0.3f },
		{ TEXT("LBLLife"), TEXT("Life|Indicator"), SpotL, 0.483f, 0.78f, 0.3f },
		{ TEXT("LBLEnergy"), TEXT("Energy|Indicator"), SpotL, 0.585f, 0.78f, 0.3f },
		{ TEXT("LBLTimer"), TEXT("Timers"), SpotR, 0.485f, 0.0f, 0.23f },
		{ TEXT("LBLTime"), TEXT("Remaining|Time and|Objectives"), SpotL, 0.23f, 0.78f, 0.3f },
		{ TEXT("LBLAlarm"), TEXT("Coop Move|Indicator"), SpotR, 0.768f, 0.006f, 0.23f },
	};

	// GUI.View_Hud_Sentry defaults (the merc page's LocalMsg a little higher and ER a little lower, as Godot's modern look).
	const FTutorialSpot TutorialMercSpots[] = {
		{ TEXT("LBLReticule"), TEXT("Reticle"), SpotC, 0.77f, 0.459f, 0.3f },
		{ TEXT("LBLInteraction"), TEXT("Interaction|Menu"), SpotL, 0.673f, 0.77f, 0.2f },
		{ TEXT("LBLTimer"), TEXT("Timers"), SpotL, 0.5f, 0.78f, 0.2f },
		{ TEXT("LBLRObj"), TEXT("Remaining|Objectives"), SpotR, 0.206f, 0.026f, 0.2f },
		{ TEXT("LBLTime"), TEXT("Remaining|Time"), SpotR, 0.29f, 0.026f, 0.2f },
		{ TEXT("LBLLocalMsg"), TEXT("Local|Messages"), SpotR, 0.344583f, 0.05625f, 0.18f },
		{ TEXT("LBLGameMessage"), TEXT("Game Status|Messages"), SpotC, 0.77f, 0.258f, 0.3f },
		{ TEXT("LBLER"), TEXT("Enhanced|Reality"), SpotR, 0.422f, 0.026f, 0.2f },
		{ TEXT("LBLAmmo"), TEXT("Position|Indicator"), SpotR, 0.58f, 0.026f, 0.2f },
		{ TEXT("LBLAlarm"), TEXT("Alarm Indicator"), SpotC, 0.164f, 0.475f, 0.3f },
		{ TEXT("LBLLife"), TEXT("Life|Indicator"), SpotR, 0.495f, 0.026f, 0.2f },
		{ TEXT("LBLGadget"), TEXT("Current|Gadget"), SpotR, 0.683f, 0.026f, 0.2f },
		{ TEXT("LBLRadar"), TEXT("Tactical|Radar"), SpotL, 0.26f, 0.77f, 0.2f },
		{ TEXT("LBLSystemMessage"), TEXT("System Messages"), SpotC, 0.164f, 0.18f, 0.3f },
	};

	// GUI.View_KeyboardConfigOne defaults (Controls[0] is LBLUse).
	const FTutorialSpot TutorialKeyboardSpots[] = {
		{ TEXT("LBLUse"), TEXT("Use"), SpotC, 0.586f, 0.0f, 0.1f },
		{ TEXT("LBLBTW"), TEXT("Back to Wall"), SpotR, 0.66f, 0.0f, 0.2f },
		{ TEXT("LBLMove"), TEXT("Move"), SpotR, 0.7f, 0.0f, 0.2f },
		{ TEXT("LBLCrouch"), TEXT("Crouch"), SpotR, 0.74f, 0.06844f, 0.2f },
		{ TEXT("LBLCoop"), TEXT("Coop"), SpotR, 0.78f, 0.085936f, 0.2f },
		{ TEXT("LBLJump"), TEXT("Jump"), SpotR, 0.66f, 0.135f, 0.2f },
		{ TEXT("LBLReload"), TEXT("Reload"), SpotR, 0.7f, 0.178126f, 0.2f },
		{ TEXT("LBLZoom"), TEXT("Zoom"), SpotR, 0.74f, 0.2f, 0.2f },
		{ TEXT("LBLHack"), TEXT("Hack"), SpotR, 0.78f, 0.23f, 0.2f },
		{ TEXT("LBLLaser"), TEXT("Laser"), SpotR, 0.66f, 0.347f, 0.2f },
		{ TEXT("LBLPrevStickyCam"), TEXT("Previous Sticky Cam"), SpotC, 0.658335f, 0.565001f, 0.2f },
		{ TEXT("LBLAction1"), TEXT("Action 1"), SpotR, 0.41f, 0.0f, 0.13f },
		{ TEXT("LBLAction2"), TEXT("Action 2"), SpotR, 0.46f, 0.0f, 0.13f },
		{ TEXT("LBLGadget"), TEXT("Gadget Selection"), SpotC, 0.35f, 0.1f, 0.3f },
		{ TEXT("LBLMenu"), TEXT("In-Game Menu"), SpotL, 0.21f, 0.01f, 0.15f },
		{ TEXT("LBLSwitchVision"), TEXT("Switch Vision"), SpotL, 0.297079f, 0.04f, 0.3f },
		{ TEXT("LBLHelp"), TEXT("Help"), SpotC, 0.3f, 0.223438f, 0.2f },
		{ TEXT("LBLTalk"), TEXT("Talk"), SpotC, 0.279166f, 0.292188f, 0.2f },
		{ TEXT("LBLER"), TEXT("Enhanced Reality"), SpotR, 0.308333f, 0.373439f, 0.3f },
		{ TEXT("LBLStatus"), TEXT("Game Status"), SpotC, 0.341666f, 0.459376f, 0.2f },
		{ TEXT("LBLFire"), TEXT("Fire"), SpotR, 0.36f, 0.63f, 0.18f },
		{ TEXT("LBLUseGadget"), TEXT("Use Gadget"), SpotL, 0.36f, 0.83f, 0.17f },
		{ TEXT("LBLSlowMode"), TEXT("Click: Slow Mode|Roll: Zoom in/out"), SpotC, 0.25f, 0.7f, 0.3f },
	};

	enum class ETutorialLegend : uint8 { SpyHud, MercHud, Keyboard1, Keyboard2 };

	class SScctHudLegendPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctHudLegendPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			Rebuild();
		}

		virtual FString GetTitle() const override
		{
			switch (Page)
			{
			case ETutorialLegend::MercHud: return TutorialRefCaption(TEXT("View_Hud_Sentry"), TEXT("TitlePage"), TEXT("Mercenary HUD"));
			case ETutorialLegend::Keyboard1: return TutorialRefCaption(TEXT("View_KeyboardConfigOne"), TEXT("TitlePage"), TEXT("Keyboard Default Configuration 1"));
			case ETutorialLegend::Keyboard2: return TutorialRefCaption(TEXT("View_KeyboardConfigTwo"), TEXT("TitlePage"), TEXT("Keyboard Default Configuration 2"));
			default: return TutorialRefCaption(TEXT("View_Hud_Spy"), TEXT("TitlePage"), TEXT("Spy HUD"));
			}
		}

		virtual FString GetKicker() const override { return TEXT("Tutorials · reference"); }

		virtual void ShowSub(const FString& Sub) override
		{
			ETutorialLegend Wanted = ETutorialLegend::SpyHud;
			if (Sub.Equals(TEXT("merc"), ESearchCase::IgnoreCase)) { Wanted = ETutorialLegend::MercHud; }
			else if (Sub.Equals(TEXT("keyboard1"), ESearchCase::IgnoreCase) || Sub.Equals(TEXT("keyboard"), ESearchCase::IgnoreCase)) { Wanted = ETutorialLegend::Keyboard1; }
			else if (Sub.Equals(TEXT("keyboard2"), ESearchCase::IgnoreCase)) { Wanted = ETutorialLegend::Keyboard2; }
			Show(Wanted);
		}

	private:
		void Show(ETutorialLegend Wanted)
		{
			if (Wanted != Page)
			{
				Page = Wanted;
				Rebuild();
				TutorialRefFocus(DefaultFocus.Pin());
			}
		}

		void Rebuild()
		{
			static const FSlateColorBrush SpotBack(FLinearColor(0.0f, 0.0f, 0.0f, 100.0f / 255.0f));
			const bool bKeyboard = Page == ETutorialLegend::Keyboard1 || Page == ETutorialLegend::Keyboard2;
			const TCHAR* Section = Page == ETutorialLegend::MercHud ? TEXT("View_Hud_Sentry") : bKeyboard ? TEXT("View_KeyboardConfigOne") : TEXT("View_Hud_Spy");
			const TCHAR* Texture = Page == ETutorialLegend::MercHud ? TEXT("SENTRY_Tutorial2_User_Interface") : bKeyboard ? TEXT("clavier") : TEXT("SPY_Tutorial2_User_Interface");
			TArrayView<const FTutorialSpot> Spots = Page == ETutorialLegend::MercHud ? TArrayView<const FTutorialSpot>(TutorialMercSpots)
				: bKeyboard ? TArrayView<const FTutorialSpot>(TutorialKeyboardSpots) : TArrayView<const FTutorialSpot>(TutorialSpySpots);

			const UScctHudTextures::FEntry* Picture = UScctHudTextures::Find(TEXT("Tutorials_PC"), Texture);
			const TSharedRef<SCanvas> Labels = SNew(SCanvas);
			for (int32 Index = 0; Index < Spots.Num(); ++Index)
			{
				const FTutorialSpot& Spot = Spots[Index];
				FVector2D At(Spot.Left, Spot.Top);
				// View_KeyboardConfigTwo.InitComponent: Controls[0] (Use) moves to the page's middle.
				if (Page == ETutorialLegend::Keyboard2 && Index == 0)
				{
					At = FVector2D(0.5f, 0.5f);
				}
				// GUILabelPad's 55 high boxes; the keyboard's labels are packed tighter, as Godot's modern look has them.
				const float Height = bKeyboard ? (FCString::Strcmp(Spot.Control, TEXT("LBLSlowMode")) == 0 ? 40.0f : 22.0f) : 55.0f;
				const FString Caption = ScctTutorialText::Lines(Section, *(FString(Spot.Control) + TEXT(".Caption")), Spot.Fallback);
				Labels->AddSlot()
					.Position(FVector2D(At.X * 640.0f, At.Y * 480.0f))
					.Size(FVector2D(Spot.Width * 640.0f, Height))
					[
						SNew(SBorder)
						.BorderImage(&SpotBack)
						.Padding(FMargin(4.0f, 1.0f))
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Fill)
						[
							SNew(STextBlock)
							.Font(ScctHudStyle::Font(ScctHudStyle::EWeight::Regular, 10.0f))
							.ColorAndOpacity(ScctUi::Accent())
							.Justification(Spot.Align)
							.AutoWrapText(true)
							.Text(FText::FromString(Caption))
						]
					];
			}

			const TCHAR* NextFallback = Page == ETutorialLegend::MercHud ? TEXT("Spy HUD") : Page == ETutorialLegend::Keyboard1 ? TEXT("2nd Config") : Page == ETutorialLegend::Keyboard2 ? TEXT("1st Config") : TEXT("Merc HUD");
			const ETutorialLegend Next = Page == ETutorialLegend::MercHud ? ETutorialLegend::SpyHud : Page == ETutorialLegend::Keyboard1 ? ETutorialLegend::Keyboard2
				: Page == ETutorialLegend::Keyboard2 ? ETutorialLegend::Keyboard1 : ETutorialLegend::MercHud;
			const TSharedRef<SScctButton> NextButton = TutorialRefButton(TutorialRefCaption(Page == ETutorialLegend::Keyboard2 ? TEXT("View_KeyboardConfigTwo") : Section, TEXT("ButtonA"), NextFallback),
				TEXT("The other page of the pair."), [this, Next] { Show(Next); }, EScctButtonLook::Primary);
			DefaultFocus = NextButton;

			const TSharedRef<SWidget> Frame = Picture == nullptr
				? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(640.0f).HeightOverride(200.0f).VAlign(VAlign_Center)
					[ TutorialRefText(TEXT("Install the menu content (the ui conversion step) to view the HUD and keyboard reference pictures."), false) ])
				: StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(840.0f).HeightOverride(630.0f)
					[
						SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
						[
							SNew(SBox).WidthOverride(640.0f).HeightOverride(480.0f)
							[
								SNew(SOverlay)
								+ SOverlay::Slot()[ SNew(SImage).Image(&Picture->Brush) ]
								+ SOverlay::Slot()[ Labels ]
							]
						]
					]);

			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[ ScctUi::Panel(Frame, FMargin(12.0f)) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 16.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						TutorialRefButton(TEXT("Back"), TEXT("Back to the Tutorials page."), [this] { if (Host.Back) { Host.Back(); } })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						// The keyboard pages are not reachable in the original's menus; here this switches between the pairs.
						TutorialRefButton(bKeyboard ? TEXT("HUD") : TEXT("Keyboard"), bKeyboard ? TEXT("The HUD pictures.") : TEXT("The default keyboard layouts."),
							[this, bKeyboard] { Show(bKeyboard ? ETutorialLegend::SpyHud : ETutorialLegend::Keyboard1); })
					]
					+ SHorizontalBox::Slot().AutoWidth()[ NextButton ]
				]
			];
		}

		ETutorialLegend Page = ETutorialLegend::SpyHud;
	};

	// ---- Tips ---------------------------------------------------------------------------------------------------

	class SScctTipsPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctTipsPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			Rebuild();
		}

		virtual FString GetTitle() const override
		{
			const TArray<FString> Titles = TitlesOf(Team);
			if (Tip >= 0 && Titles.IsValidIndex(Tip))
			{
				return Titles[Tip];
			}
			return Team == 0 ? TutorialRefCaption(TEXT("Tutorials_List"), TEXT("TitreATT"), TEXT("Spy Tips")) : TutorialRefCaption(TEXT("Tutorials_List"), TEXT("TitreDEF"), TEXT("Mercenary Tips"));
		}

		virtual FString GetKicker() const override
		{
			return Team == 0 ? TEXT("Tutorials · spy tips") : TEXT("Tutorials · mercenary tips");
		}

		virtual void ShowSub(const FString& Sub) override
		{
			FString Side = Sub, Index;
			Sub.Split(TEXT("/"), &Side, &Index);
			Show(TutorialRefTeamOf(Side), Index.IsNumeric() ? FCString::Atoi(*Index) : -1);
		}

		virtual bool HandleBack() override
		{
			if (Tip >= 0)
			{
				Show(Team, -1);
				return true;
			}
			return false;
		}

		virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const override
		{
			if (Tip < 0)
			{
				OutKeyAndWhat.Add({ TEXT("T"), TutorialRefCaption(TEXT("Tutorials_List"), TEXT("ButtonY"), TEXT("Switch Team")) });
				OutPadKeys.Add(TEXT("Y"));
			}
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if (Tip < 0 && (Key == EKeys::T || Key == EKeys::Gamepad_FaceButton_Top))
			{
				Show(1 - Team, -1);
				return true;
			}
			return false;
		}

	private:
		/** The list of a side (Tutorials_List TitreExpliATT / TitreExpliDEF). */
		static TArray<FString> TitlesOf(int32 InTeam)
		{
			TArray<FString> Titles = ScctTutorialText::List(TEXT("Tutorials_List"), InTeam == 0 ? TEXT("TitreExpliATT") : TEXT("TitreExpliDEF"));
			for (FString& Title : Titles)
			{
				Title = Title.Replace(TEXT("|"), TEXT(" ")).TrimStartAndEnd();
			}
			return Titles;
		}

		void Show(int32 InTeam, int32 InTip)
		{
			Team = InTeam;
			Tip = InTip;
			Rebuild();
			TutorialRefFocus(DefaultFocus.Pin());
		}

		void Rebuild()
		{
			const TArray<FString> Titles = TitlesOf(Team);
			if (Tip >= Titles.Num())
			{
				Tip = -1;
			}
			if (Tip < 0)
			{
				const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
				TSharedPtr<SScctButton> First;
				for (int32 Index = 0; Index < Titles.Num(); ++Index)
				{
					const TSharedRef<SScctButton> Row = TutorialRefButton(Titles[Index], FString::Printf(TEXT("Tip %d of %d."), Index + 1, Titles.Num()),
						[this, Index] { Show(Team, Index); }, EScctButtonLook::Menu);
					List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)[ Row ];
					if (!First.IsValid())
					{
						First = Row;
					}
				}
				if (Titles.IsEmpty())
				{
					List->AddSlot().AutoHeight()[ TutorialRefText(TEXT("The tips need the converted menu texts (the ui conversion step)."), false) ];
				}
				const TSharedRef<SScctButton> Back = TutorialRefButton(TEXT("Back"), TEXT("Back to the Tutorials page."), [this] { if (Host.Back) { Host.Back(); } });
				DefaultFocus = First.IsValid() ? TSharedPtr<SWidget>(First) : TSharedPtr<SWidget>(Back);
				ChildSlot
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).WidthOverride(720.0f).HAlign(HAlign_Left)
						[
							ScctUi::Card(TEXT("Choose a tip"), SNew(SBox).MaxDesiredHeight(580.0f)[ SNew(SScrollBox) + SScrollBox::Slot().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ List ] ])
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 16.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Back ]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							TutorialRefButton(TutorialRefCaption(TEXT("Tutorials_List"), TEXT("ButtonY"), TEXT("Switch Team")), TEXT("The other side's tips."), [this] { Show(1 - Team, -1); })
						]
					]
				];
				return;
			}

			// Tutorials_Tuto.RefreshAll: the tip's picture, its legend and its explanation with the player's keys.
			const TCHAR* Side = Team == 0 ? TEXT("SPY") : TEXT("SENTRY");
			const UScctHudTextures::FEntry* Picture = UScctHudTextures::Find(TEXT("Tutorials_PC"), FString::Printf(TEXT("%s_Tuto%02d"), Side, Tip + 1));
			const FString Legend = ScctTutorialText::Item(TEXT("Tutorials_Tuto"), *(FString(Side) + TEXT("_TutoLegend")), Tip);
			const FString Explanation = ScctTutorialText::WithKeys(ScctTutorialText::Item(TEXT("Tutorials_Tuto"), *(FString(Side) + TEXT("_TutoExpli")), Tip), Team == 1);
			const int32 Count = Titles.Num();
			const TSharedRef<SScctButton> Next = TutorialRefButton(TutorialRefCaption(TEXT("Tutorials_Tuto"), TEXT("ButtonA"), TEXT("Next")), TEXT("The following tip."),
				[this, Count] { Show(Team, Tip + 1 >= Count ? 0 : Tip + 1); }, EScctButtonLook::Primary);
			DefaultFocus = Next;
			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 20.0f, 0.0f)
					[
						ScctUi::Panel(Picture != nullptr ? TutorialRefPicture(&Picture->Brush, 320.0f, 320.0f) : StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(320.0f).HeightOverride(320.0f)), FMargin(10.0f))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(620.0f)[ ScctUi::Card(TEXT("Key points"), TutorialRefText(Legend, true)) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
				[
					SNew(SBox).MaxDesiredWidth(980.0f).HAlign(HAlign_Left)[ ScctUi::Card(TEXT("How it works"), TutorialRefText(Explanation, false)) ]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 16.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ TutorialRefButton(TEXT("Back"), TEXT("Back to the list of tips."), [this] { Show(Team, -1); }) ]
					+ SHorizontalBox::Slot().AutoWidth()[ Next ]
				]
			];
		}

		int32 Team = 0;
		int32 Tip = -1;
	};

	// ---- Gadget Details -------------------------------------------------------------------------------------------

	class SScctGadgetsPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctGadgetsPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost)
		{
			Host = InHost;
			Rebuild();
		}

		virtual FString GetTitle() const override
		{
			const TArray<FString> Titles = TitlesOf(Team);
			if (Gadget >= 0 && Titles.IsValidIndex(Gadget))
			{
				return Titles[Gadget];
			}
			return Team == 0 ? TutorialRefCaption(TEXT("Tuto_Gadgets"), TEXT("TitlePageSpy"), TEXT("Spy Gadgets")) : TutorialRefCaption(TEXT("Tuto_Gadgets"), TEXT("TitlePageMerc"), TEXT("Mercenary Gadgets"));
		}

		virtual FString GetKicker() const override { return Team == 0 ? TEXT("Tutorials · spy gadgets") : TEXT("Tutorials · mercenary gadgets"); }

		virtual void ShowSub(const FString& Sub) override
		{
			FString Side = Sub, Index;
			Sub.Split(TEXT("/"), &Side, &Index);
			Show(TutorialRefTeamOf(Side), Index.IsNumeric() ? FCString::Atoi(*Index) : -1);
		}

		virtual bool HandleBack() override
		{
			if (Gadget >= 0)
			{
				Show(Team, -1);
				return true;
			}
			return false;
		}

		virtual void GetHints(TArray<TPair<FString, FString>>& OutKeyAndWhat, TArray<FString>& OutPadKeys) const override
		{
			if (Gadget < 0)
			{
				OutKeyAndWhat.Add({ TEXT("T"), TutorialRefCaption(TEXT("Tuto_Gadgets"), TEXT("ButtonY"), TEXT("Switch Team")) });
				OutPadKeys.Add(TEXT("Y"));
			}
		}

		virtual bool HandleShortcut(const FKey& Key) override
		{
			if (Gadget < 0 && (Key == EKeys::T || Key == EKeys::Gamepad_FaceButton_Top))
			{
				Show(1 - Team, -1);
				return true;
			}
			return false;
		}

	private:
		static TArray<FString> TitlesOf(int32 InTeam)
		{
			TArray<FString> Titles = ScctTutorialText::List(TEXT("Tuto_Gadgets"), InTeam == 0 ? TEXT("TitleGadgetSpy") : TEXT("TitleGadgetMerc"));
			for (FString& Title : Titles)
			{
				Title = Title.Replace(TEXT("|"), TEXT(" ")).TrimStartAndEnd();
			}
			return Titles;
		}

		/** The side's gadget at Index (its class and HUD icon), from the bridge. */
		static bool GadgetOf(int32 InTeam, int32 Index, FScctTutorialGadget& Out, int32* OutCount = nullptr)
		{
			const FScctTutorialApi* Tutorials = ScctFeature<FScctTutorialApi>();
			FMemory::Memzero(Out);
			const int32 Count = Tutorials != nullptr ? Tutorials->GetGadget(InTeam, Index, &Out) : 0;
			if (OutCount != nullptr)
			{
				*OutCount = Count;
			}
			return Index >= 0 && Index < Count;
		}

		/** The gadget's 64 x 64 tile of the HUD's gadget atlas (TeamInfo.ClassInventory[i] IconU / IconV), or null. */
		const FSlateBrush* IconOf(int32 Index)
		{
			const int32 Key = Team * 100 + Index;
			if (const TSharedPtr<FSlateBrush>* Known = Icons.Find(Key))
			{
				return Known->Get();
			}
			TSharedPtr<FSlateBrush> Brush;
			FScctTutorialGadget Info;
			UScctMenuSubsystem* Menu = UScctMenuSubsystem::GetCurrent();
			if (GadgetOf(Team, Index, Info) && Menu != nullptr)
			{
				if (const FSlateBrush* Atlas = Menu->GetPicture(FString(Info.Icon)))
				{
					Brush = MakeShared<FSlateBrush>(*Atlas);
					const FVector2D Size = Atlas->ImageSize;
					if (Info.Size > 0 && Size.X > 0 && Size.Y > 0)
					{
						const float W = static_cast<float>(Size.X), H = static_cast<float>(Size.Y);
						Brush->SetUVRegion(FBox2f(FVector2f(Info.U / W, Info.V / H), FVector2f((Info.U + Info.Size) / W, (Info.V + Info.Size) / H)));
						Brush->ImageSize = FVector2D(Info.Size, Info.Size);
					}
				}
			}
			Icons.Add(Key, Brush);
			return Brush.Get();
		}

		void Show(int32 InTeam, int32 InGadget)
		{
			Team = InTeam;
			Gadget = InGadget;
			Rebuild();
			TutorialRefFocus(DefaultFocus.Pin());
		}

		void Rebuild()
		{
			const TArray<FString> Titles = TitlesOf(Team);
			FScctTutorialGadget Info;
			int32 Count = 0;
			GadgetOf(Team, 0, Info, &Count);
			Count = Titles.Num() > 0 ? FMath::Min(Count > 0 ? Count : Titles.Num(), Titles.Num()) : Count;
			const auto IsRetired = [this](int32 Index) { FScctTutorialGadget Probe; return GadgetOf(Team, Index, Probe) && Probe.Retired != 0; };
			if (Gadget >= Count || (Gadget >= 0 && IsRetired(Gadget)))
			{
				Gadget = -1;
			}
			const auto TitleOf = [&Titles](int32 Index, const FScctTutorialGadget& Of)
			{
				if (Titles.IsValidIndex(Index))
				{
					return Titles[Index];
				}
				FString Name = FString(Of.Path);
				Name.Split(TEXT("."), nullptr, &Name);
				return Name;
			};

			if (Gadget < 0)
			{
				const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
				if (Team == 0)
				{
					List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						TutorialRefText(TEXT("Cloak is built into every spy's suit and uses no gadget slot. Bind Cloak and choose Hold or Toggle in Options, Spy bindings."), false)
					];
				}
				TSharedPtr<SScctButton> First;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					FScctTutorialGadget Of;
					GadgetOf(Team, Index, Of);
					if (Of.Retired != 0)
					{
						continue;
					}
					const TSharedRef<SScctButton> Row = TutorialRefButton(TitleOf(Index, Of), TEXT("Its specifications and how to use it."), [this, Index] { Show(Team, Index); }, EScctButtonLook::Menu);
					List->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)[ TutorialRefPicture(IconOf(Index), 44.0f, 44.0f) ]
						+ SHorizontalBox::Slot().FillWidth(1.0f)[ Row ]
					];
					if (!First.IsValid())
					{
						First = Row;
					}
				}
				if (Count == 0)
				{
					List->AddSlot().AutoHeight()[ TutorialRefText(TEXT("The gadget pages need the converted menu texts and the OpenSCCT bridge."), false) ];
				}
				const TSharedRef<SScctButton> Back = TutorialRefButton(TEXT("Back"), TEXT("Back to the Tutorials page."), [this] { if (Host.Back) { Host.Back(); } });
				DefaultFocus = First.IsValid() ? TSharedPtr<SWidget>(First) : TSharedPtr<SWidget>(Back);
				ChildSlot
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBox).WidthOverride(720.0f).HAlign(HAlign_Left)
						[
							ScctUi::Card(TEXT("Choose a gadget"), SNew(SBox).MaxDesiredHeight(580.0f)[ SNew(SScrollBox) + SScrollBox::Slot().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ List ] ])
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 16.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ Back ]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							TutorialRefButton(TutorialRefCaption(TEXT("Tuto_Gadgets"), TEXT("ButtonY"), TEXT("Switch Team")), TEXT("The other side's gadgets."), [this] { Show(1 - Team, -1); })
						]
					]
				];
				return;
			}

			// Tuto_OneGadget.RefreshAll: the icon, the legend (format, range, duration) and the explanation.
			const TCHAR* Side = Team == 0 ? TEXT("Spy") : TEXT("Merc");
			const FString Legend = ScctTutorialText::Item(TEXT("Tuto_OneGadget"), *(FString(Side) + TEXT("_TutorialLegend")), Gadget);
			const FString Explanation = ScctTutorialText::Item(TEXT("Tuto_OneGadget"), *(FString(Side) + TEXT("_TutorialText")), Gadget);
			int32 NextGadget = Gadget + 1;
			if (NextGadget < Count && IsRetired(NextGadget))
			{
				++NextGadget;
			}
			if (NextGadget >= Count)
			{
				NextGadget = IsRetired(0) ? 1 : 0;
			}
			const TSharedRef<SScctButton> Next = TutorialRefButton(TutorialRefCaption(TEXT("Tuto_OneGadget"), TEXT("ButtonA"), TEXT("Next")), TEXT("The following gadget."),
				[this, NextGadget] { Show(Team, NextGadget); }, EScctButtonLook::Primary);
			DefaultFocus = Next;
			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 20.0f, 0.0f)
					[
						ScctUi::Panel(TutorialRefPicture(IconOf(Gadget), 192.0f, 192.0f), FMargin(16.0f))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).MaxWidth(620.0f)[ ScctUi::Card(TEXT("Gadget specifications"), TutorialRefText(Legend, true)) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
				[
					SNew(SBox).MaxDesiredWidth(880.0f).HAlign(HAlign_Left)[ ScctUi::Card(TEXT("How to use"), TutorialRefText(Explanation, false)) ]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 16.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 10.0f, 0.0f)[ TutorialRefButton(TEXT("Back"), TEXT("Back to the list of gadgets."), [this] { Show(Team, -1); }) ]
					+ SHorizontalBox::Slot().AutoWidth()[ Next ]
				]
			];
		}

		int32 Team = 0;
		int32 Gadget = -1;
		TMap<int32, TSharedPtr<FSlateBrush>> Icons;
	};
}

TSharedRef<SScctMenuPage> ScctTutorialPages::MakeHudLegend(const FScctMenuHost& Host)
{
	return SNew(SScctHudLegendPage, Host);
}

TSharedRef<SScctMenuPage> ScctTutorialPages::MakeTips(const FScctMenuHost& Host)
{
	return SNew(SScctTipsPage, Host);
}

TSharedRef<SScctMenuPage> ScctTutorialPages::MakeGadgets(const FScctMenuHost& Host)
{
	return SNew(SScctGadgetsPage, Host);
}
