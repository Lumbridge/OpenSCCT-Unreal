#include "Menu/ScctFrontEndWidget.h"

#include "Menu/SScctFrontEnd.h"
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

void UScctFrontEndWidget::AskQuit()
{
	if (FrontEnd.IsValid())
	{
		FrontEnd->ShowPageNamed(TEXT("quit"));
	}
}