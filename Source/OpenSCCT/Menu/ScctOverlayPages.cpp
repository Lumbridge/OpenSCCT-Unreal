#include "Menu/ScctOverlayPages.h"

#include "Menu/SScctGearPage.h"
#include "Menu/SScctStrategicMapPage.h"
#include "Menu/ScctMenuKit.h"
#include "Online/ScctOnlineSubsystem.h"
#include "OpenSCCT.h"
#include "Runtime/ScctGearMenuApi.h"
#include "Runtime/ScctMapApi.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The front end's gear or map page on show, for FindFrontEndGear and FindFrontEndStrategicMap. */
	TWeakPtr<SScctGearPage> OverlayFrontEndGear;
	TWeakPtr<SScctStrategicMapPage> OverlayFrontEndMap;

	/** The side the lobby's player is on, 0 or 1 (the spy's while spectating). */
	int32 OverlayLobbySide(const TWeakObjectPtr<UScctOnlineSubsystem>& Online)
	{
		const int32 Team = Online.IsValid() ? Online->GetStatus().LocalTeam : 0;
		return Team == 1 ? 1 : 0;
	}

	/**
	 * A page of the front end's stack that shows the Equipment page or a map's strategic map over itself: each draws
	 * its own title, so the page has none. Closing the inner page closes this one (a tick later, so the host never
	 * pops a page while it is running).
	 */
	class SScctOverlayHostPage : public SScctMenuPage
	{
	public:
		SLATE_BEGIN_ARGS(SScctOverlayHostPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const FScctMenuHost& InHost, bool bInGear)
		{
			Host = InHost;
			bGear = bInGear;
			ChildSlot[ SAssignNew(Layer, SOverlay) ];
			if (bGear)
			{
				Reopen();
			}
		}

		virtual FString GetTitle() const override { return FString(); }

		virtual void ShowSub(const FString& Sub) override
		{
			if (bGear)
			{
				Side = Sub.Equals(TEXT("merc"), ESearchCase::IgnoreCase) ? 1 : 0;
			}
			else
			{
				Map = Sub;
			}
			Reopen();
		}

		virtual bool HandleBack() override
		{
			// The inner page goes back (and this page with it once it has closed); nothing open: the host goes back.
			return ScctOverlayPages::Back(Layer);
		}

		virtual TSharedPtr<SWidget> GetDefaultFocus() const override
		{
			FChildren* Children = Layer.IsValid() ? Layer->GetChildren() : nullptr;
			return Children != nullptr && Children->Num() > 0 ? TSharedPtr<SWidget>(Children->GetChildAt(Children->Num() - 1)) : TSharedPtr<SWidget>();
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SScctMenuPage::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			if (!bGear && !bOpened)
			{
				// "map" without a map: the lobby's own (there is none outside a lobby, and the page goes back).
				Reopen();
			}
			if (bBackPending && Host.Back)
			{
				bBackPending = false;
				Host.Back();
			}
		}

	private:
		void Reopen()
		{
			bOpened = true;
			// The page on show goes without closing this one.
			bReplacing = true;
			if (const TSharedPtr<SScctGearPage> Gear = SScctGearPage::FindOpen(Layer.ToSharedRef()))
			{
				Gear->Close();
			}
			if (const TSharedPtr<SScctStrategicMapPage> Old = SScctStrategicMapPage::FindOpen(Layer.ToSharedRef()))
			{
				Old->Close();
			}
			bReplacing = false;

			const TWeakPtr<SScctOverlayHostPage> Weak = SharedThis(this);
			const FSimpleDelegate Closed = FSimpleDelegate::CreateLambda([Weak]
			{
				if (const TSharedPtr<SScctOverlayHostPage> Page = Weak.Pin(); Page.IsValid() && !Page->bReplacing)
				{
					Page->bBackPending = true;
				}
			});
			if (bGear)
			{
				const TSharedPtr<SScctGearPage> Gear = SScctGearPage::Open(Layer.ToSharedRef(), Side, EScctGearMenuContext::Menus, true, FSimpleDelegate(), Closed);
				if (Gear.IsValid())
				{
					Gear->SetDialogs(Host.Dialogs);
				}
				OverlayFrontEndGear = Gear;
				bBackPending = !Gear.IsValid();
			}
			else
			{
				const TSharedPtr<SScctStrategicMapPage> Page = SScctStrategicMapPage::Open(Layer.ToSharedRef(), EScctMapContext::Lobby, Map, true, Closed);
				OverlayFrontEndMap = Page;
				bBackPending = !Page.IsValid();
			}
		}

		TSharedPtr<SOverlay> Layer;
		bool bGear = true;
		int32 Side = 0;
		FString Map;
		bool bOpened = false;
		bool bReplacing = false;
		bool bBackPending = false;
	};
}

TSharedRef<SWidget> ScctOverlayPages::MakeRow(const TWeakObjectPtr<UScctOnlineSubsystem>& Online, TFunction<void()> OnGear, TFunction<void()> OnMap)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(ScctUi::Caps(11.0f))
				.ColorAndOpacity_Lambda([Online] { return FSlateColor(ScctHudStyle::Accent(OverlayLobbySide(Online))); })
				.Text_Lambda([Online] { return FText::FromString(OverlayLobbySide(Online) == 1 ? TEXT("YOUR GEAR · MERCENARY") : TEXT("YOUR GEAR · SPY")); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(ScctUi::Small())
				.ColorAndOpacity(ScctHudStyle::TextDim())
				.AutoWrapText(true)
				.Text_Lambda([Online] { return FText::FromString(SScctGearPage::SummaryLine(OverlayLobbySide(Online))); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SScctButton)
			.Text(FText::FromString(TEXT("STRATEGIC MAP")))
			.Description(FText::FromString(TEXT("The floors and the places of the lobby's map, before the round.")))
			.OnPressed(FSimpleDelegate::CreateLambda([OnMap] { if (OnMap) { OnMap(); } }))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SScctButton)
			.Text(FText::FromString(TEXT("EQUIPMENT")))
			.Description(FText::FromString(TEXT("Pick the gadgets, weapon and sidearm you take into the round, under this server's rules.")))
			.OnPressed(FSimpleDelegate::CreateLambda([OnGear] { if (OnGear) { OnGear(); } }))
		];
}

TSharedPtr<SScctGearPage> ScctOverlayPages::OpenGear(const TSharedPtr<SOverlay>& Layer, const FScctMenuHost& Host)
{
	if (!Layer.IsValid())
	{
		return nullptr;
	}
	const TSharedPtr<SScctGearPage> Gear = SScctGearPage::Open(Layer.ToSharedRef(), OverlayLobbySide(Host.Online), EScctGearMenuContext::Lobby, true);
	if (Gear.IsValid())
	{
		Gear->SetDialogs(Host.Dialogs);
	}
	return Gear;
}

TSharedPtr<SScctStrategicMapPage> ScctOverlayPages::OpenStrategicMap(const TSharedPtr<SOverlay>& Layer)
{
	return Layer.IsValid() ? SScctStrategicMapPage::Open(Layer.ToSharedRef(), EScctMapContext::Lobby, FString(), true) : nullptr;
}

bool ScctOverlayPages::Back(const TSharedPtr<SOverlay>& Layer)
{
	if (!Layer.IsValid())
	{
		return false;
	}
	if (const TSharedPtr<SScctGearPage> Gear = SScctGearPage::FindOpen(Layer.ToSharedRef()))
	{
		Gear->RequestBack();
		return true;
	}
	if (const TSharedPtr<SScctStrategicMapPage> Map = SScctStrategicMapPage::FindOpen(Layer.ToSharedRef()))
	{
		Map->Close();
		return true;
	}
	return false;
}

TSharedPtr<SScctGearPage> ScctOverlayPages::FindFrontEndGear()
{
	return OverlayFrontEndGear.Pin();
}

TSharedPtr<SScctStrategicMapPage> ScctOverlayPages::FindFrontEndStrategicMap()
{
	return OverlayFrontEndMap.Pin();
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeGear(const FScctMenuHost& Host)
{
	return SNew(SScctOverlayHostPage, Host, true);
}

TSharedRef<SScctMenuPage> ScctMenuPages::MakeStrategicMap(const FScctMenuHost& Host)
{
	return SNew(SScctOverlayHostPage, Host, false);
}
