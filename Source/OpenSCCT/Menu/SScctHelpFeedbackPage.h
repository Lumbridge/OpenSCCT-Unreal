#pragma once

#include "CoreMinimal.h"
#include "Menu/ScctMenuPage.h"

/** Project links and local game details, available from the front end and the in-match menu. */
namespace ScctHelpFeedback
{
	TSharedRef<SScctMenuPage> MakePage(const FScctMenuHost& Host);

	/** The session and display details shown on the page. */
	FString GameDetails();
}
