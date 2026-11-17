#include "Menu/ScctFrontEndWidget.h"

#include "Menu/SScctFrontEnd.h"
#include "Menu/SScctGearPage.h"
#include "Menu/SScctStrategicMapPage.h"
#include "Menu/ScctOverlayPages.h"
#include "Online/ScctOnlineSubsystem.h"

TSharedRef<SWidget> UScctFrontEndWidget::RebuildWidget()
{
	return SAssignNew(FrontEnd, SScctFrontEnd)
		.Online(UScctOnlineSubsystem::Get(this));
}

void UScctFrontEndWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	FrontEnd.Reset();
}

void UScctFrontEndWidget::ShowPageNamed(const FString& Name)
{
	if (FrontEnd.IsValid())
	{
		FrontEnd->ShowPageNamed(Name);
	}
}


void UScctFrontEndWidget::ShowOptions(FName Page)
{
	if (FrontEnd.IsValid())
	{
		// The settings page of main's menus, on the tab of that name ("settings/audio").
		FrontEnd->ShowPageNamed(Page.IsNone() ? FString(TEXT("settings")) : TEXT("settings/") + Page.ToString().ToLower());
	}
}

TSharedPtr<SScctGearPage> UScctFrontEndWidget::ShowGear(int32 Side)
{
	if (!FrontEnd.IsValid())
	{
		return nullptr;
	}
	// The front end's "gear" page, over the main menu (Menu/ScctOverlayPages.h).
	FrontEnd->ShowPageNamed(Side == 1 ? TEXT("gear/merc") : TEXT("gear/spy"));
	return ScctOverlayPages::FindFrontEndGear();
}

TSharedPtr<SScctStrategicMapPage> UScctFrontEndWidget::ShowStrategicMap(const FString& Map)
{
	if (!FrontEnd.IsValid())
	{
		return nullptr;
	}
	// The front end's "map" page: a map's overview (the lobby's own map is the lobby's STRATEGIC MAP).
	FrontEnd->ShowPageNamed(TEXT("map/") + Map);
	return ScctOverlayPages::FindFrontEndStrategicMap();
}

void UScctFrontEndWidget::AskQuit()
{
	if (FrontEnd.IsValid())
	{
		FrontEnd->ShowPageNamed(TEXT("quit"));
	}
}